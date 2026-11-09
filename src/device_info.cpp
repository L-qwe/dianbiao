/**
 * @file device_info.cpp
 * @brief 设备公共信息与电表运行状态 JSON (BLE 特征值, 注意 ≤512 字节)
 */
#include "app_log.h"
#include "device_info.h"
#include "app_config.h"
#include "meter_pulse.h"
#include "mqtt_handler.h"
#include "ntp_time.h"
#include <WiFi.h>

static String json_escape(const String& value) {
    String out;
    out.reserve(value.length() + 8);
    for (size_t i = 0; i < value.length(); i++) {
        const char c = value[i];
        switch (c) {
            case '\"': out += F("\\\""); break;
            case '\\': out += F("\\\\"); break;
            case '\n': out += F("\\n"); break;
            case '\r': out += F("\\r"); break;
            case '\t': out += F("\\t"); break;
            default:   out += c; break;
        }
    }
    return out;
}

String andon_ble_name(void) {
    String mac = WiFi.macAddress();
    mac.replace(":", "");
    return String("DianBiao_") + mac;
}

String andon_common_info_json(void) {
    const bool wifi_conn = WiFi.status() == WL_CONNECTED;
    String j;
    j.reserve(360);
    j += F("{\"mac_address\":\"");
    j += WiFi.macAddress();
    j += F("\",\"firmware_version\":\"");
    j += FIRMWARE_VERSION;
    j += F("\",\"firmware_type\":\"");
    j += FIRMWARE_TYPE;
    j += F("\",\"ble_name\":\"");
    j += andon_ble_name();
    j += F("\",\"wifi_ssid\":\"");
    if (wifi_conn) j += json_escape(WiFi.SSID());
    j += F("\",\"wifi_ip\":\"");
    if (wifi_conn) j += WiFi.localIP().toString();
    j += F("\",\"wifi_connected\":");
    j += wifi_conn ? F("true") : F("false");
    j += F(",\"mqtt_connected\":");
    j += mqtt_connected() ? F("true") : F("false");
    j += F(",\"ap_ssid\":\"");
    String mac = WiFi.macAddress();
    mac.replace(":", "");
    j += json_escape(g_cfg_ap_ssid.length() ? g_cfg_ap_ssid
                    : String(F("DianBiao_")) + mac.substring(6));
    j += F("\"}");
    return j;
}

String andon_runtime_info_json(void) {
    // 12 路脉冲与电量, 紧凑格式保证 BLE 特征值 <512 字节
    String j;
    j.reserve(480);
    j += F("{\"uptime_s\":");
    j += String((unsigned long)(millis() / 1000UL));
    j += F(",\"ntp_time\":\"");
    j += ntp_time_str();
    j += F("\",\"channels\":[");
    for (int i = 0; i < METER_CHANNELS; i++) {
        if (i) j += ',';
        j += F("{\"ch\":");
        j += String(i + 1);
        j += F(",\"en\":");
        j += g_meters[i].enabled ? '1' : '0';
        j += F(",\"p\":");
        j += String((unsigned long long)meter_total_pulses(i));
        j += F(",\"kwh\":");
        j += meter_energy_kwh_text(i);
        j += '}';
    }
    j += F("]}");
    return j;
}

String andon_device_info_json(void) {
    String common = andon_common_info_json();
    String runtime = andon_runtime_info_json();
    if (common.endsWith("}")) common.remove(common.length() - 1);
    if (runtime.startsWith("{")) runtime.remove(0, 1);
    common += ',';
    common += runtime;
    return common;
}
