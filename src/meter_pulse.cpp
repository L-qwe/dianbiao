/**
 * @file meter_pulse.cpp
 * @brief 12 路脉冲采集与电量换算实现
 */
#include "app_log.h"
#include "meter_pulse.h"
#include "energy_format.h"
#include <Preferences.h>
#include <soc/gpio_struct.h>

#if !CONFIG_ARDUINO_ISR_IRAM
#error "Pulse acquisition requires CONFIG_ARDUINO_ISR_IRAM=1 for Flash-safe interrupts"
#endif

// CN1~CN12 → GPIO (原理图走线: 通道号与 GPIO 顺序错位)
// CN1=7 CN2=4 CN3=5 CN4=6 CN5=8 CN6=15 CN7=9 CN8=10 CN9=11 CN10=12 CN11=13 CN12=14
const uint8_t METER_GPIO[METER_CHANNELS] = {7, 4, 5, 6, 8, 15, 9, 10, 11, 12, 13, 14};

static const char*      NVS_NS     = "dianbiao";
static const uint32_t   SAVE_MS    = 30000;   // 有新增脉冲时每 30s 落盘

// ISR 暂存 (volatile uint32_t, 32 位单字访问在 Xtensa 上原子)
static DRAM_ATTR volatile uint32_t s_delta[METER_CHANNELS];
struct PulseEdgeState {
    uint32_t edge_us;
    uint32_t accepted_rise_us;
    uint32_t low_us;
    uint32_t accepted_high_us;
    uint32_t accepted_low_us;
    bool high;
    bool candidate;
    bool counted;
};
static DRAM_ATTR volatile PulseEdgeState s_edge_state[METER_CHANNELS];
struct PulseIsrContext { uint8_t ch; uint8_t gpio; };
// Every callback receives a non-null pointer to internal RAM, including CN1.
// Arduino 2.x dispatches null arguments through its no-argument callback path.
static DRAM_ATTR PulseIsrContext s_isr_context[METER_CHANNELS];

// Read MMIO directly: digitalRead() can transitively call Flash-resident IDF
// code even when its Arduino wrapper is in IRAM. Support both S3 GPIO banks.
static inline __attribute__((always_inline)) bool pulse_level(uint8_t gpio) {
    return gpio < 32 ? ((GPIO.in >> gpio) & 1U)
                     : ((GPIO.in1.data >> (gpio - 32)) & 1U);
}

static void reset_edge_state(int ch) {
    auto& state = s_edge_state[ch];
    state.high = pulse_level(METER_GPIO[ch]);
    state.edge_us = micros();
    state.accepted_rise_us = 0;
    state.low_us = 0;
    state.accepted_high_us = 0;
    state.accepted_low_us = 0;
    state.candidate = false; // A high already present at boot/zero is not a pulse.
    state.counted = false;
}

// 主循环汇总后的累计值
static uint64_t s_total[METER_CHANNELS];
static uint64_t s_saved_total[METER_CHANNELS];

static unsigned long s_last_save_ms = 0;
static bool          s_dirty        = false;
// Main-loop diagnostics: aggregate counts and print at most once/second/channel.
// No logging, allocation or string operations run in the ISR.
static uint32_t s_log_pending[METER_CHANNELS];
static uint32_t s_log_high_us[METER_CHANNELS];
static uint32_t s_log_low_us[METER_CHANNELS];
static uint32_t s_log_at_us[METER_CHANNELS];
static uint32_t s_log_ms[METER_CHANNELS];

void IRAM_ATTR meter_isr(void* arg) {
    const auto& ctx = *static_cast<const PulseIsrContext*>(arg);
    const int ch = ctx.ch;
    auto& state = s_edge_state[ch];
    const bool high = pulse_level(ctx.gpio);
    if (high == state.high) return; // A repeated IRQ is not another edge.
    const uint32_t now = micros();
    const uint32_t started_us = state.edge_us;
    const uint32_t duration = now - started_us;
    state.edge_us = now;
    state.high = high;
    if (high) {
        state.low_us = duration;
        state.candidate = duration >= PULSE_MIN_LOW_US;
        return;
    }
    const bool candidate = state.candidate;
    state.candidate = false;
    if (candidate && duration >= PULSE_MIN_HIGH_US &&
        (!state.counted || uint32_t(started_us - state.accepted_rise_us) >= PULSE_MIN_INTERVAL_US)) {
        state.accepted_rise_us = started_us;
        state.accepted_high_us = duration;
        state.accepted_low_us = state.low_us;
        state.counted = true;
        s_delta[ch]++;
    }
}

static uint64_t load_total(int ch) {
    Preferences p;
    p.begin(NVS_NS, true);
    char k[16];
    snprintf(k, sizeof(k), "pulse%d", ch);
    const uint64_t v = p.getULong64(k, 0);
    const bool on = p.getBool("pulse_ok", false);
    p.end();
    return on ? v : 0;
}

void meter_pulse_init(void) {
    for (int ch = 0; ch < METER_CHANNELS; ch++) {
        s_delta[ch]   = 0;
        s_total[ch]  = load_total(ch);
        s_saved_total[ch] = s_total[ch];
        s_log_pending[ch] = 0;
        s_log_ms[ch] = millis();
        // SN74LVC14A drives push-pull. A weak pull-down also defines the idle
        // level if the header loses contact; it cannot suppress an active driver.
        pinMode(METER_GPIO[ch], INPUT_PULLDOWN);
        reset_edge_state(ch);
        s_isr_context[ch] = {uint8_t(ch), METER_GPIO[ch]};
        attachInterruptArg(digitalPinToInterrupt(METER_GPIO[ch]),
                           meter_isr, &s_isr_context[ch], CHANGE);
    }
    s_last_save_ms = millis();
    AppLog.printf("[PULSE] IRAM CHANGE: interval>=%luus high>=%luus low>=%luus\n",
                  (unsigned long)PULSE_MIN_INTERVAL_US,
                  (unsigned long)PULSE_MIN_HIGH_US, (unsigned long)PULSE_MIN_LOW_US);
    AppLog.printf("[PULSE] 恢复累计脉冲: %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu\n",
                  (unsigned long long)s_total[0],  (unsigned long long)s_total[1],
                  (unsigned long long)s_total[2],  (unsigned long long)s_total[3],
                  (unsigned long long)s_total[4],  (unsigned long long)s_total[5],
                  (unsigned long long)s_total[6],  (unsigned long long)s_total[7],
                  (unsigned long long)s_total[8],  (unsigned long long)s_total[9],
                  (unsigned long long)s_total[10], (unsigned long long)s_total[11]);
}

static void collect_deltas(void) {
    for (int ch = 0; ch < METER_CHANNELS; ch++) {
        noInterrupts();
        const uint32_t d = s_delta[ch];
        if (d) {
            s_log_high_us[ch] = s_edge_state[ch].accepted_high_us;
            s_log_low_us[ch] = s_edge_state[ch].accepted_low_us;
            s_log_at_us[ch] = s_edge_state[ch].accepted_rise_us;
        }
        s_delta[ch] = 0;
        interrupts();
        if (d) {
            s_total[ch] += d;
            s_log_pending[ch] += d;
            s_dirty = true;
        }
        const uint32_t now = millis();
        if (s_log_pending[ch] && uint32_t(now - s_log_ms[ch]) >= 1000) {
            AppLog.printf("[PULSE] CH=%d GPIO=%u accepted=%lu total=%llu high_us=%lu low_us=%lu at_us=%lu level=%u\n",
                          ch + 1, (unsigned)METER_GPIO[ch], (unsigned long)s_log_pending[ch],
                          (unsigned long long)meter_total_pulses(ch),
                          (unsigned long)s_log_high_us[ch], (unsigned long)s_log_low_us[ch],
                          (unsigned long)s_log_at_us[ch], (unsigned)pulse_level(METER_GPIO[ch]));
            s_log_pending[ch] = 0;
            s_log_ms[ch] = now;
        }
    }
}

void meter_pulse_flush(void) {
    collect_deltas();
    if(!s_dirty)return;
    s_last_save_ms=millis(); // Also back off after a storage failure.
    Preferences p;
    if(!p.begin(NVS_NS,false)){AppLog.println("[PULSE] NVS open failed; counters retained in RAM");return;}
    bool initialized=p.getBool("pulse_ok",false),ok=true;
    // One namespace session per flush. Reuse existing pulseN/pulse_ok keys;
    // first initialization must write even zero channels before setting pulse_ok.
    for(int ch=0;ch<METER_CHANNELS;++ch){
        if(initialized && s_saved_total[ch]==s_total[ch])continue;
        char key[16];snprintf(key,sizeof(key),"pulse%d",ch);
        if(p.putULong64(key,s_total[ch])==sizeof(uint64_t))s_saved_total[ch]=s_total[ch];
        else ok=false;
    }
    if(ok && !initialized)ok=p.putBool("pulse_ok",true)==1;
    p.end();
    if(ok){s_dirty=false;AppLog.println("[PULSE] changed counters saved");}
    else AppLog.println("[PULSE] NVS write failed; pending counters retained for retry");
}

void meter_pulse_loop(void) {
    collect_deltas();
    const uint32_t now = millis();
    if (s_dirty && (uint32_t)(now - s_last_save_ms) >= SAVE_MS) {
        meter_pulse_flush();
    }
}

uint64_t meter_total_pulses(int ch) {
    if (ch < 0 || ch >= METER_CHANNELS) return 0;
    noInterrupts();
    const uint64_t total = s_total[ch] + s_delta[ch];
    interrupts();
    return total;
}

double meter_energy_kwh(int ch) {
    if (ch < 0 || ch >= METER_CHANNELS) return 0;
    const MeterChannelConfig& m = g_meters[ch];
    if (m.imp_per_kwh == 0) return m.initial_kwh;
    return m.initial_kwh + (double)meter_total_pulses(ch) / (double)m.imp_per_kwh;
}

String meter_energy_kwh_text(int ch) {
    return format_energy_kwh(meter_energy_kwh(ch));
}

void meter_pulse_zero(int ch) {
    if (ch < 0 || ch >= METER_CHANNELS) return;
    collect_deltas();
    noInterrupts();
    s_delta[ch] = 0;
    s_total[ch] = 0;
    s_log_pending[ch] = 0;
    s_log_ms[ch] = millis();
    reset_edge_state(ch);
    interrupts();
    s_dirty = true;
    meter_pulse_flush();
    AppLog.printf("[PULSE] 通道 %d 累计脉冲已清零\n", ch + 1);
}
