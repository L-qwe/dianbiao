#pragma once
#include "app_config.h"
#include <ArduinoJson.h>
#include <cmath>

namespace portal {
struct Setting { const char* key; String* value; size_t limit; };
inline Setting* settings() {
    static Setting values[] = {
        {"dev_name",&g_cfg_device_name,64},{"mq_brk",&g_cfg_mqtt_broker,128},
        {"mq_cid",&g_cfg_mqtt_client_id,96},{"mq_user",&g_cfg_mqtt_username,64},
        {"mq_pass",&g_cfg_mqtt_password,128},{"mq_pub",&g_cfg_mqtt_pub_topic,160},
        {"mq_sub",&g_cfg_mqtt_sub_topic,160},{"ntp1",&g_cfg_ntp_server1,128},
        {"ntp2",&g_cfg_ntp_server2,128},{"tz",&g_cfg_tz_info,64},
        {"sta_ip",&g_cfg_static_ip,15},{"sta_gw",&g_cfg_static_gateway,15},
        {"sta_sn",&g_cfg_static_subnet,15},{"sta_dns",&g_cfg_static_dns,15},
        {"ap_ssid",&g_cfg_ap_ssid,32},{"ap_pass",&g_cfg_ap_password,63},
        {nullptr,nullptr,0}
    };
    return values;
}
inline const char* validate_settings(JsonObjectConst o) {
    for (auto* s=settings();s->key;++s) if(o.containsKey(s->key)) {
        if(!o[s->key].is<const char*>()) return "invalid_settings";
        if(strlen(o[s->key].as<const char*>())>s->limit) return "field_too_long";
    }
    if(o.containsKey("dev_name") && !strlen(o["dev_name"].as<const char*>())) return "invalid_device_name";
    if(o.containsKey("ap_pass")) {
        size_t n=strlen(o["ap_pass"].as<const char*>());
        if(n && n<8) return "invalid_ap_password";
    }
    if(o.containsKey("mq_port") && (!o["mq_port"].is<uint32_t>() || o["mq_port"].as<uint32_t>()<1 || o["mq_port"].as<uint32_t>()>65535)) return "invalid_mqtt_port";
    if(o.containsKey("hb_s") && (!o["hb_s"].is<uint32_t>() || o["hb_s"].as<uint32_t>()<5 || o["hb_s"].as<uint32_t>()>4294967)) return "invalid_heartbeat";
    if(o.containsKey("rep_s") && (!o["rep_s"].is<uint32_t>() || o["rep_s"].as<uint32_t>()>4294967)) return "invalid_report_interval";
    return nullptr;
}
inline void apply_settings(JsonObjectConst o) {
    for(auto* s=settings();s->key;++s) if(o.containsKey(s->key)) *s->value=o[s->key].as<const char*>();
    if(o.containsKey("mq_port")) g_cfg_mqtt_port=o["mq_port"].as<unsigned>();
    if(o.containsKey("hb_s")) g_cfg_heartbeat_interval_ms=o["hb_s"].as<unsigned long>()*1000UL;
    if(o.containsKey("rep_s")) g_cfg_report_interval_s=o["rep_s"].as<unsigned long>();
}
inline const char* parse_meters(JsonObjectConst o,MeterChannelConfig (&next)[METER_CHANNELS],int& zero) {
    memcpy(next,g_meters,sizeof(next)); zero=-1;
    if(o.containsKey("zero_channel")) {
        if(!o["zero_channel"].is<unsigned>() || o["zero_channel"].as<unsigned>()<1 || o["zero_channel"].as<unsigned>()>METER_CHANNELS) return "invalid_channel";
        zero=o["zero_channel"].as<unsigned>()-1;
    }
    if(!o.containsKey("meters")) return zero>=0 ? nullptr : "invalid_meters";
    JsonArrayConst list=o["meters"].as<JsonArrayConst>();
    if(list.size()!=METER_CHANNELS) return "invalid_meters";
    uint16_t seen=0;
    for(JsonObjectConst m:list) {
        if(!m["ch"].is<unsigned>()) return "invalid_channel";
        unsigned ch=m["ch"].as<unsigned>();
        if(ch<1 || ch>METER_CHANNELS || (seen&(1U<<(ch-1)))) return "invalid_channel";
        seen|=1U<<(ch-1);
        if(!m["enabled"].is<bool>() || !m["name"].is<const char*>() || !m["initial_kwh"].is<double>() || !m["imp_kwh"].is<uint32_t>()) return "invalid_meters";
        const char* name=m["name"];
        if(!*name || strlen(name)>=METER_NAME_LEN) return "invalid_meter_name";
        double initial=m["initial_kwh"];
        if(!std::isfinite(initial) || initial<0 || !m["imp_kwh"].as<uint32_t>()) return "invalid_meter_value";
        auto& target=next[ch-1];target.enabled=m["enabled"];strcpy(target.name,name);
        target.initial_kwh=initial;target.imp_per_kwh=m["imp_kwh"];
    }
    return nullptr;
}
}
