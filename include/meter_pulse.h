/**
 * @file meter_pulse.h
 * @brief 12 路脉冲采集与电量换算
 *
 * 硬件链路: 端子 CN1~CN12 → H11L1M 光耦 → SN74LVC14A 整形 → ESP32 GPIO
 * 有效脉冲: 稳定低电平后变高，高电平达最小宽度并回到低电平时计一次。
 * 脉冲累计值周期性写入 NVS, 掉电最多丢失最近一个保存周期的计数。
 */
#ifndef METER_PULSE_H
#define METER_PULSE_H

#include <Arduino.h>
#include "app_config.h"

// CN1~CN12 → ESP32-S3 GPIO 映射 (依据检测板实际走线, 非 GPIO 顺序)
extern const uint8_t METER_GPIO[METER_CHANNELS];

// 两脉冲最小间隔 (微秒): 滤除接触抖动, 10ms 间隔允许单路最高 ~100Hz 脉冲
#ifndef PULSE_MIN_INTERVAL_US
#define PULSE_MIN_INTERVAL_US  10000
#endif

// 毛刺过滤: 高、低电平各至少 1ms。需小于实际电表的最短高/低脉宽。
#ifndef PULSE_MIN_HIGH_US
#define PULSE_MIN_HIGH_US  1000
#endif
#ifndef PULSE_MIN_LOW_US
#define PULSE_MIN_LOW_US   1000
#endif

/** 初始化 GPIO + 中断 + 从 NVS 恢复累计脉冲 (setup 阶段调用) */
void meter_pulse_init(void);

/** 主循环维护: 收集中断增量、周期性写 NVS */
void meter_pulse_loop(void);

/** 通道累计脉冲数 (已含中断暂存增量) */
uint64_t meter_total_pulses(int ch);

/** 通道累计电量 kWh = 初始电量 + 脉冲数 / imp_per_kWh */
double meter_energy_kwh(int ch);

/** 同一精度的总电量 JSON 数值文本，四舍五入到 0.001kWh，供 Web/MQTT/BLE 共用 */
String meter_energy_kwh_text(int ch);

/** 清零某通道累计脉冲 (立即写 NVS) */
void meter_pulse_zero(int ch);

/** 恢复后强制把已收集计数写 NVS (配置保存后调用) */
void meter_pulse_flush(void);

#endif
