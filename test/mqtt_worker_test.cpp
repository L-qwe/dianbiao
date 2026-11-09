
#include "mqtt_handler.h"
#include "meter_pulse.h"
#include "energy_format.h"
#include <iostream>
// CONFIG_GLOBALS
const uint8_t METER_GPIO[METER_CHANNELS]={7,4,5,6,8,15,9,10,11,12,13,14};
uint64_t meter_total_pulses(int){return 1234;}
double energyValues[METER_CHANNELS];
unsigned energyReads=0;
double meter_energy_kwh(int ch){return energyValues[ch];}
String meter_energy_kwh_text(int ch){++energyReads;return format_energy_kwh(meter_energy_kwh(ch));}
unsigned flushCount=0;uint32_t flushedAt=0;
void meter_pulse_flush(){++flushCount;flushedAt=millis();}
String ntp_time_str(){return "2026-11-07 08:03:00";}
bool ntp_ok(){return true;}
// MQTT_SOURCE
void waitFor(auto predicate){for(int i=0;i<3000&&!predicate();i++)std::this_thread::sleep_for(std::chrono::milliseconds(1));assert(predicate());}
int main(){
 for(int ch=0;ch<METER_CHANNELS;ch++)energyValues[ch]=123.456+ch;
 for(auto&m:g_meters){strcpy(m.name,"Meter");m.imp_per_kwh=1600;}
 strcpy(g_meters[0].name,"Meter \"A\"\\B\n");g_meters[3].enabled=false;
 g_cfg_device_name="Device \"A\"\\B\n";
 g_cfg_mqtt_broker="192.168.1.10";g_cfg_mqtt_client_id="original-client";fakeMillis=1000;
 mqtt_setup();assert(connectCalls==0 && mqtt.getBufferSize()==4096);mqtt_maintain();assert(connectCalls==0 && s_connect_state.load()==ConnectState::idle);
 assert(g_topic_pub=="dianbiao/meter/AA:BB:CC:11:22:33" && g_topic_sub==g_topic_pub);
 assert(expand_topic("test/MAC","fallback/")=="test/AABBCC112233");
 assert(expand_topic("test/{MAC}/MAC/{MAC}/MAC","fallback/")=="test/AA:BB:CC:11:22:33/AABBCC112233/AA:BB:CC:11:22:33/AABBCC112233");
 assert(expand_topic("literal/topic","fallback/")=="literal/topic");
 assert(expand_topic("test/mac","fallback/")=="test/mac");
 assert(expand_topic("","fallback/")=="fallback/AABBCC112233");
 failTaskCreation=false;fakeMillis=10999;mqtt_maintain();assert(connectCalls==0);
 fakeMillis=11000;mqtt_maintain();waitFor([]{return connectCalls.load()==1;});
 auto started=std::chrono::steady_clock::now();
 for(int i=0;i<100;i++){mqtt_maintain();assert(!mqtt_connected());assert(!mqtt_pub("test"));mqtt_meter_config();mqtt_meter_report();mqtt_heartbeat();}
 assert(std::chrono::steady_clock::now()-started<std::chrono::milliseconds(200));assert(concurrentAccess==0);
 g_cfg_mqtt_client_id="changed-after-start";assert(mqtt.cid=="original-client");
 releaseHandshake=true;waitFor([]{return s_connect_state.load()==ConnectState::failed;});mqtt_maintain();assert(!mqtt_connected());
 fakeMillis=20999;mqtt_maintain();assert(connectCalls==1);
 releaseHandshake=false;connectResult=true;fakeMillis=21000;mqtt_maintain();waitFor([]{return connectCalls.load()==2;});
 assert(mqtt.cid=="changed-after-start");assert(!mqtt_connected());releaseHandshake=true;
 waitFor([]{return s_connect_state.load()==ConnectState::succeeded;});mqtt_maintain();assert(mqtt_connected());assert(publishCalls>0&&mqtt.payload.find("_online")!=std::string::npos);
 auto checkTotals=[](){
  DynamicJsonDocument doc(8192);assert(!deserializeJson(doc,mqtt.payload.c_str()));
  JsonArray meters=doc["meters"].as<JsonArray>();assert(meters.size()==METER_CHANNELS);
  for(int ch=0;ch<METER_CHANNELS;ch++){
   JsonObject m=meters[ch];assert(m.size()==3 && m["ch"].as<int>()==ch+1);
   assert(!strcmp(m["name"].as<const char*>(),g_meters[ch].name));
   double error=m["energy_kwh"].as<double>()-meter_energy_kwh(ch);assert(error>-0.0001 && error<0.0001);
  }
 };
 assert(energyReads==0 && mqtt.publishedTopic==g_topic_pub && mqtt.subscribedTopic==g_topic_sub);
 {DynamicJsonDocument doc(8192);assert(!deserializeJson(doc,mqtt.payload.c_str()));
  assert(doc["msg_type"]=="heartbeat" && doc["dev_mac"]==g_dev_mac.c_str());
  assert(doc["device_name"]==g_cfg_device_name.c_str() && doc["version"]==FIRMWARE_VERSION);
  assert(doc["uptime_s"].is<unsigned long>() && doc["ip"].is<const char*>());
  assert(!doc.containsKey("meters"));
 }
 auto send=[](const std::string& body,const char* topic=nullptr){
  handle_command(topic?topic:g_topic_sub.c_str(),reinterpret_cast<const uint8_t*>(body.data()),body.size());
 };
 auto published=publishCalls.load();
 String echo=mqtt.payload;send(echo); // Default pub==sub, reflected heartbeat must do nothing.
 send("{\"msg_type\":\"meter_config\",\"meters\":[{\"name\":\"reboot report_now get_config\"}]}");
 send("{\"name\":\"reboot\"}");send("{\"cmd\":\"not_reboot\"}");send("{\"cmd\":[\"reboot\"]}");send("broken");
 send("{\"cmd\":\"reboot\"}","wrong/topic");
 assert(restartCount==0 && flushCount==0 && publishCalls==published && g_ota_url.empty());
 mqtt_meter_report();assert(publishCalls==published+1 && mqtt.payload.find("meter_energy")!=std::string::npos);checkTotals();
 echo=mqtt.payload;send(echo);assert(publishCalls==published+1 && restartCount==0);
 mqtt_meter_config();assert(publishCalls==published+2 && mqtt.payload.length()>1000);
 const double regressionValues[]={85858+4.0/1600,85858+12.0/1600,1.9995,85858,0,85858+1.0/1600,85858+2.0/1600,85858+3.0/1600,85858+20.0/1600,85858+1599.0/1600,85859,1e18};
 const char* regressionText[]={"85858.003","85858.008","2.000","85858.000","0.000","85858.001","85858.001","85858.002","85858.013","85858.999","85859.000","1000000000000000000.000"};
 std::copy(std::begin(regressionValues),std::end(regressionValues),energyValues);
 const unsigned beforeHeartbeatReads=energyReads;
 mqtt_heartbeat();assert(energyReads==beforeHeartbeatReads);
 {DynamicJsonDocument heartbeat(2048);assert(!deserializeJson(heartbeat,mqtt.payload.c_str()));
  assert(heartbeat["msg_type"]=="heartbeat" && !heartbeat.containsKey("meters"));}
 {
  mqtt_meter_report();
  DynamicJsonDocument doc(8192);assert(!deserializeJson(doc,mqtt.payload.c_str()));
  for(int ch=0;ch<METER_CHANNELS;ch++){
   StaticJsonDocument<128> web;web["energy_kwh"]=serialized(meter_energy_kwh_text(ch));
   std::string webJson;serializeJson(web,webJson);
   const std::string token=std::string("\"energy_kwh\":")+regressionText[ch];
   assert(webJson=="{"+token+"}");
   assert(mqtt.payload.find(token)!=std::string::npos);
   assert(doc["meters"][ch]["energy_kwh"].is<double>());
   // Parsers can differ by one binary ULP; published decimal digits and the
   // resulting three-decimal display must agree, not the binary bit pattern.
   assert(format_energy_kwh(doc["meters"][ch]["energy_kwh"].as<double>())==regressionText[ch]);
  }
 }
 std::cout<<"PASS energy report/Web JSON numeric energy uses identical rounded digits at half ties, trailing-zero and carry boundaries, including 85858+4/1600 -> 85858.003\n";
 assert(mqtt.setBufferSize(256));assert(mqtt_pub(String(1519,'x')) && mqtt.getBufferSize()==4096); // Mirrors the reported failure size.
 assert(mqtt.setBufferSize(256));failBufferAllocation=true;
 published=publishCalls.load();assert(!mqtt_pub(String(1519,'x')) && publishCalls==published);
 failBufferAllocation=false;assert(mqtt_pub(String(1519,'x')) && mqtt.getBufferSize()==4096);
 published=publishCalls.load();send("{\"cmd\":\"report_now\"}");assert(publishCalls==published+2);
 {DynamicJsonDocument heartbeat(2048),energy(8192);
  assert(!deserializeJson(heartbeat,mqtt.messages[mqtt.messages.size()-2].c_str()));
  assert(!deserializeJson(energy,mqtt.messages.back().c_str()));
  assert(heartbeat["msg_type"]=="heartbeat" && !heartbeat.containsKey("meters"));
  assert(energy["msg_type"]=="meter_energy" && energy["meters"].size()==METER_CHANNELS);}
 send("{\"msg_type\":\"get_config\"}");assert(publishCalls==published+3 && mqtt.payload.find("meter_config")!=std::string::npos);
 send("{\"msg_type\":\"upgrade\",\"file_url\":\"http://example/reboot.bin\",\"file_md5\":\"0123456789abcdef0123456789abcdef\",\"file_size\":1234}");
 assert(g_ota_url=="http://example/reboot.bin" && g_ota_size==1234 && g_ota_md5.length()==32 && restartCount==0);
 g_ota_url="";send("{\"cmd\":\"ota\",\"url\":\"http://example/a.bin\",\"file_size\":\"bad\"}");assert(g_ota_url.empty());
 std::cout<<"PASS MQTT: 4KB buffer despite library 256-byte default, 12-meter/config and 1519-byte publish, allocation failure/retry, reflected telemetry ignored, exact commands, OTA URL cannot trigger reboot\n";
 std::cout<<"PASS compact telemetry: heartbeat has no meters or energy reads, energy reports retain 12 totals, report_now emits separate heartbeat/energy messages\n";
 std::cout<<"PASS Topic placeholders: braced MAC has colons, bare MAC has none, mixed/repeated/literal/default topics and actual publish/subscribe match\n";
 WiFi.current=0;mqtt_maintain();assert(!mqtt_connected());assert(concurrentAccess==0);
 const char reboot[]="{\"cmd\":\"reboot\"}";auto beforeReboot=millis();
 handle_command(g_topic_sub.c_str(),reinterpret_cast<const uint8_t*>(reboot),sizeof(reboot)-1);
 assert(flushCount==1 && flushedAt==beforeReboot+300 && restartCount==1 && restartedAt==flushedAt);
 std::cout<<"PASS MQTT reboot flushes pulse counters after its delay and immediately before restart\n";
 std::cout<<"PASS actual MQTT source: task-allocation failure has no blocking fallback, 10s cooldown, blocked handshake leaves loop responsive, exclusive client ownership, frozen credentials, successful handoff, Wi-Fi loss\n"<<std::flush;
 std::_Exit(0); // Stop the detached, intentionally persistent worker with the test process.
}
