
#include "web_settings.h"
#include "network_timing.h"
#include <cassert>
#include <iostream>
// CONFIG_GLOBALS
void defaults(){for(int i=0;i<12;i++){g_meters[i].enabled=true;strcpy(g_meters[i].name,"Meter");g_meters[i].initial_kwh=10;g_meters[i].imp_per_kwh=1600;}}
DynamicJsonDocument valid_meters(){DynamicJsonDocument d(8192);auto list=d.createNestedArray("meters");for(int i=0;i<12;i++){auto m=list.createNestedObject();m["ch"]=i+1;m["enabled"]=true;m["name"]="车间总表";m["initial_kwh"]=123.456;m["imp_kwh"]=3200;}return d;}
int main(){
 defaults();
 DynamicJsonDocument settings(4096);
 assert(!deserializeJson(settings,R"({"dev_name":"车间 \"A\" & <表>","mq_port":1884,"hb_s":5,"rep_s":0})"));
 assert(!portal::validate_settings(settings.as<JsonObjectConst>()));
 String original=g_cfg_mqtt_broker;portal::apply_settings(settings.as<JsonObjectConst>());
 assert(g_cfg_device_name=="车间 \"A\" & <表>" && g_cfg_mqtt_port==1884 && g_cfg_heartbeat_interval_ms==5000 && g_cfg_report_interval_s==0 && g_cfg_mqtt_broker==original);
 for(const char* bad:{R"({"mq_port":0})",R"({"mq_port":65536})",R"({"mq_port":1.5})",R"({"mq_port":"1883"})",R"({"hb_s":4})",R"({"hb_s":4294968})",R"({"rep_s":-1})",R"({"rep_s":4294968})",R"({"ap_pass":"short"})",R"({"dev_name":""})",R"({"mq_pass":null})"}){
  settings.clear();assert(!deserializeJson(settings,bad));assert(portal::validate_settings(settings.as<JsonObjectConst>()));assert(g_cfg_mqtt_port==1884);
 }
 settings.clear();settings["ap_pass"]="";assert(!portal::validate_settings(settings.as<JsonObjectConst>()));
 auto d=valid_meters();MeterChannelConfig next[12];int zero=-1;
 assert(!portal::parse_meters(d.as<JsonObjectConst>(),next,zero));assert(zero==-1 && next[11].imp_per_kwh==3200 && g_meters[11].imp_per_kwh==1600);
 auto before=std::string(reinterpret_cast<char*>(g_meters),sizeof(g_meters));
 d["meters"][11]["ch"]=1;assert(portal::parse_meters(d.as<JsonObjectConst>(),next,zero));
 d=valid_meters();d["meters"][11]["initial_kwh"]=-1;assert(portal::parse_meters(d.as<JsonObjectConst>(),next,zero));
 d=valid_meters();d["meters"][11]["imp_kwh"]=0;assert(portal::parse_meters(d.as<JsonObjectConst>(),next,zero));
 d=valid_meters();d["meters"][11]["name"]="一二三四五六七";assert(portal::parse_meters(d.as<JsonObjectConst>(),next,zero));
 d=valid_meters();d["meters"][11]["enabled"]=1;assert(portal::parse_meters(d.as<JsonObjectConst>(),next,zero));
 d=valid_meters();d["meters"].as<JsonArray>().remove(11);assert(portal::parse_meters(d.as<JsonObjectConst>(),next,zero));
 assert(std::string(reinterpret_cast<char*>(g_meters),sizeof(g_meters))==before);
 d.clear();d["zero_channel"]=12;assert(!portal::parse_meters(d.as<JsonObjectConst>(),next,zero)&&zero==11);
 for(int ch:{-1,0,13}){d["zero_channel"]=ch;assert(portal::parse_meters(d.as<JsonObjectConst>(),next,zero));}
 assert(network_timing::reconnect_due(0,0,true));assert(!network_timing::reconnect_due(10999,1000,false));assert(network_timing::reconnect_due(11000,1000,false));
 assert(!network_timing::reconnect_due(500,0xfffffff0U,false));assert(network_timing::reconnect_due(10000,0xfffffff0U,false));
 std::cout<<"PASS settings types/ranges/partial updates, 12-channel validation and atomic staging, UTF-8 byte limits, clear-channel bounds, reconnect cooldown and millis rollover\n";
}
