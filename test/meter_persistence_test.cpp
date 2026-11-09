
#include "Arduino.h"
#include "app_config.h"
#include "Preferences.h"
#include <soc/gpio_struct.h>
#include <cassert>
#include <iostream>
#define IRAM_ATTR
#define DRAM_ATTR
#define CONFIG_ARDUINO_ISR_IRAM 1
#define INPUT_PULLDOWN 0x09
#define CHANGE 0x03
struct PulseInput { int mode=-1,edge=-1; void(*handler)(void*)=nullptr; void* arg=nullptr; };
PulseInput inputs[49];
void pinMode(int pin,int mode){inputs[pin].mode=mode;}
void attachInterruptArg(int pin,void(*handler)(void*),void* arg,int edge){
 assert(arg!=nullptr); // In Arduino 2.x, null selects the no-argument callback path.
 inputs[pin].handler=handler;inputs[pin].arg=arg;inputs[pin].edge=edge;
}
int digitalPinToInterrupt(int pin){return pin;}
void noInterrupts(){}void interrupts(){}
uint32_t subMicros=0;
uint32_t micros(){return fakeMillis*1000U+subMicros;}
void advanceUs(uint32_t us){uint64_t t=uint64_t(fakeMillis)*1000+subMicros+us;fakeMillis=uint32_t(t/1000);subMicros=uint32_t(t%1000);}
// CONFIG_GLOBALS
// METER_SOURCE
void resetStats(){opens=0;writes.clear();}
void edge(int ch){auto& input=inputs[METER_GPIO[ch]];assert(input.handler);input.handler(input.arg);}
void level(int ch,bool high){
 const uint32_t mask=1U<<METER_GPIO[ch];const bool old=(GPIO.in&mask)!=0;
 if(high)GPIO.in|=mask;else GPIO.in&=~mask;
 if(old!=high)edge(ch);
}
void levels(bool high){for(int ch=0;ch<12;ch++)level(ch,high);}
void pulse(int ch){advanceUs(10000);level(ch,true);advanceUs(1000);level(ch,false);}
int main(){
 for(auto&m:g_meters){m.initial_kwh=100;m.imp_per_kwh=1600;}
 for(int i=0;i<12;i++)stored["pulse"+std::to_string(i)]=999; // Stale keys with no initialization marker.
 fakeMillis=100;meter_pulse_init();assert(meter_total_pulses(0)==0);resetStats();
 pulse(0);edge(0); // A repeated interrupt at the same level cannot double-count.
 assert(meter_total_pulses(0)==1 && meter_energy_kwh(0)==100+1.0/1600); // Pending ISR count is visible before collection.
 meter_pulse_flush();assert(opens==1 && writes.size()==13 && pulseValid);assert(stored["pulse0"]==1);
 for(int i=1;i<12;i++)assert(stored["pulse"+std::to_string(i)]==0);
 resetStats();pulse(1);meter_pulse_flush();assert(opens==1 && writes==std::vector<std::string>{"pulse1"});
 assert(meter_total_pulses(0)==1 && meter_total_pulses(1)==1);
 resetStats();meter_pulse_flush();assert(opens==0 && writes.empty());
 meter_pulse_init();assert(meter_total_pulses(0)==1 && meter_total_pulses(1)==1);
 resetStats();meter_pulse_zero(1);assert(stored["pulse0"]==1 && stored["pulse1"]==0 && writes==std::vector<std::string>{"pulse1"});
 assert(meter_energy_kwh(1)==100 && meter_energy_kwh(0)>100);
 pulse(0);failKey="pulse0";resetStats();meter_pulse_flush();assert(s_dirty && stored["pulse0"]==1 && meter_total_pulses(0)==2);
 resetStats();fakeMillis++;meter_pulse_loop();assert(opens==0);failKey="";fakeMillis+=30000;meter_pulse_loop();assert(!s_dirty && stored["pulse0"]==2);
 resetStats();pulse(0);writeHook=[](){pulse(2);};meter_pulse_flush();meter_pulse_loop();assert(meter_total_pulses(2)==1 && s_dirty);
 meter_pulse_flush();assert(stored["pulse2"]==1 && stored["pulse0"]==3 && !s_dirty);
 resetStats();pulse(3);openFailure=true;meter_pulse_flush();assert(s_dirty && meter_total_pulses(3)==1);openFailure=false;meter_pulse_flush();assert(!s_dirty && stored["pulse3"]==1);
 std::cout<<"PASS real meter persistence: one NVS session, changed keys only, first initialization clears stale keys, reboot restore, channel zero isolation, write/open failure retry, pulses arriving during save retained\n";
 const int expected[12]={7,4,5,6,8,15,9,10,11,12,13,14};
 auto fresh=[](uint32_t startMs=100){
  stored.clear();pulseValid=false;fakeMillis=startMs;subMicros=0;GPIO.in=0;GPIO.in1.data=0;meter_pulse_init();
 };
 for(int hz: {1,10,50,100,200}){
  fresh();
  for(int ch=0;ch<12;ch++){
   assert(METER_GPIO[ch]==expected[ch]);const auto& input=inputs[expected[ch]];
   assert(input.mode==INPUT_PULLDOWN && input.edge==CHANGE && input.arg!=nullptr);
  }
  for(int n=0;n<100;n++){
   advanceUs(1000000/hz-2000);levels(true);advanceUs(2000);levels(false);meter_pulse_loop();
  }
  for(int ch=0;ch<12;ch++)assert(meter_total_pulses(ch)==(hz<=100?100:50));
 }
 // Idle inputs must not drift, even while housekeeping and save timers run.
 fresh();for(int n=0;n<120;n++){advanceUs(1000000);meter_pulse_loop();}
 for(int ch=0;ch<12;ch++)assert(meter_total_pulses(ch)==0);
 // Noise spaced further apart than the old 10ms filter is still rejected.
 fresh();for(uint32_t width: {1U,100U,999U}){
  for(int n=0;n<100;n++){advanceUs(20000);levels(true);advanceUs(width);levels(false);meter_pulse_loop();}
 }
 for(int ch=0;ch<12;ch++)assert(meter_total_pulses(ch)==0);
 advanceUs(20000);levels(true);
 for(int ch=0;ch<12;ch++)assert(meter_total_pulses(ch)==0); // Rising alone is not a completed pulse.
 advanceUs(1000);levels(false);
 for(int ch=0;ch<12;ch++)assert(meter_total_pulses(ch)==1);
 // Qualify low duration independently of the interval filter.
 fresh();advanceUs(999);levels(true);advanceUs(2000);levels(false);
 for(int ch=0;ch<12;ch++)assert(meter_total_pulses(ch)==0);
 advanceUs(1000);levels(true);advanceUs(1000);levels(false);
 for(int ch=0;ch<12;ch++)assert(meter_total_pulses(ch)==1);
 for(uint32_t interval: {9000U,10000U}){
  fresh();advanceUs(2000);levels(true);advanceUs(1000);levels(false);
  advanceUs(interval-1000);levels(true);advanceUs(1000);levels(false);
  for(int ch=0;ch<12;ch++)assert(meter_total_pulses(ch)==(interval==9000?1:2));
 }
 // A pre-existing high at boot cannot turn into a phantom pulse on release.
 fresh();for(int pin:expected)GPIO.in|=1U<<pin;meter_pulse_init();
 advanceUs(100000);levels(false);
 for(int ch=0;ch<12;ch++)assert(meter_total_pulses(ch)==0);
 advanceUs(1000);levels(true);advanceUs(1000);levels(false);
 for(int ch=0;ch<12;ch++)assert(meter_total_pulses(ch)==1);
 // A stuck high does not repeatedly count, and clear cancels in-flight pulses.
 fresh();advanceUs(2000);levels(true);advanceUs(120000000);meter_pulse_loop();
 for(int ch=0;ch<12;ch++)assert(meter_total_pulses(ch)==0);
 meter_pulse_zero(2);meter_pulse_zero(3);levels(false);
 assert(meter_total_pulses(2)==0 && meter_total_pulses(3)==0);
 pulse(2);pulse(3);assert(meter_total_pulses(2)==1 && meter_total_pulses(3)==1);
 fresh(4294966U);advanceUs(1000);levels(true);advanceUs(1000);levels(false);
 for(int ch=0;ch<12;ch++)assert(meter_total_pulses(ch)==1); // High straddles micros() wrap.
 fresh();pulse(0);writeHook=[](){for(int n=0;n<10;n++){advanceUs(18000);levels(true);advanceUs(2000);levels(false);}};
 meter_pulse_flush();meter_pulse_loop();
 assert(meter_total_pulses(0)==11);for(int ch=1;ch<12;ch++)assert(meter_total_pulses(ch)==10);
 meter_pulse_flush();for(int ch=0;ch<12;ch++)assert(stored["pulse"+std::to_string(ch)]==meter_total_pulses(ch));
 // Accepted-event diagnostics are emitted by housekeeping, not the interrupt.
 fresh();AppLog.lines.clear();pulse(0);assert(AppLog.lines.empty());meter_pulse_loop();assert(AppLog.lines.empty());
 advanceUs(1000000);meter_pulse_loop();
 auto diagnostic=[](const std::string& line){return line.find("CH=1 GPIO=7 accepted=1 total=1 high_us=1000 low_us=10000")!=std::string::npos;};
 assert(std::count_if(AppLog.lines.begin(),AppLog.lines.end(),diagnostic)==1);
 auto logged=AppLog.lines.size();for(int n=0;n<10;n++){pulse(0);meter_pulse_loop();}assert(AppLog.lines.size()==logged);
 advanceUs(1000000);meter_pulse_loop();assert(AppLog.lines.back().find("accepted=10 total=11")!=std::string::npos);
 std::cout<<"PASS accepted-pulse diagnostics: GPIO, count, total, high/low duration, ISR has no logging, main-loop aggregation/throttle\n";
 std::cout<<"PASS 12 pulse inputs: GPIO/CHANGE/pull-down, 1/10/50/100Hz, 200Hz filtering, 120s idle, 1/100/999us glitches, 1ms high/low boundary, complete pulses only, 9/10ms interval, boot/stuck high, in-flight clear, micros wrap, pulses during save\n";
 fresh();g_meters[0].initial_kwh=85858;g_meters[0].imp_per_kwh=1600;
 assert(meter_energy_kwh_text(0)=="85858.000");
 for(int n=1;n<=1600;n++){
  pulse(0);
  if(n==4){
   assert(meter_energy_kwh_text(0)=="85858.003");
   for(int i=0;i<100;i++)assert(meter_energy_kwh_text(0)=="85858.003");
   assert(meter_total_pulses(0)==4 && meter_energy_kwh(0)==85858+4.0/1600);
  }
  if(n==12)assert(meter_energy_kwh_text(0)=="85858.008");
  if(n==20)assert(meter_energy_kwh_text(0)=="85858.013");
  if(n==1599)assert(meter_energy_kwh_text(0)=="85858.999");
 }
 assert(meter_energy_kwh_text(0)=="85859.000");meter_pulse_flush();meter_pulse_init();
 assert(meter_total_pulses(0)==1600 && meter_energy_kwh_text(0)=="85859.000");
 assert(format_energy_kwh(0.000499)=="0.000" && format_energy_kwh(0.0005)=="0.001");
 assert(format_energy_kwh(1.9995)=="2.000");
 assert(format_energy_kwh(1e18)=="1000000000000000000.000");
 std::cout<<"PASS energy rounding: initial 85858 / 1600 imp/kWh with 4, 12, 20, 1599, 1600 actual pulses; carry, zero, large value, repeated formatting does not change raw energy or stored counts\n";
}
