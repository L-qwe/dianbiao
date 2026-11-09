/**
 * @file app_config.cpp
 * @brief 可配置参数默认值 + NVS 持久化
 */
#include "app_log.h"
#include "app_config.h"
#include <Preferences.h>

const char* FIRMWARE_VERSION = "v1.0.8";
const char* FIRMWARE_TYPE    = "DianBiao 电表脉冲采集";

// ── 默认值 ──────────────────────────────────────────────────────
String g_cfg_device_name = "12路电表采集";

String g_cfg_wifi_ssid = "";
String g_cfg_wifi_pass = "";

String g_cfg_ap_ssid     = "";    // 空 = 自动生成
String g_cfg_ap_password = "";    // 空 = 无密码

String g_cfg_static_ip      = "";
String g_cfg_static_gateway = "";
String g_cfg_static_subnet  = "";
String g_cfg_static_dns     = "";

int g_cfg_web_port = 80;

String  g_cfg_mqtt_broker    = "";                  // 空 = 不启用 MQTT
int     g_cfg_mqtt_port      = 1883;
String  g_cfg_mqtt_client_id = "";                  // 空 = 自动 "DianBiao_<MAC>"
String  g_cfg_mqtt_username  = "";
String  g_cfg_mqtt_password  = "";
String  g_cfg_mqtt_pub_topic = "dianbiao/meter/{MAC}";
String  g_cfg_mqtt_sub_topic = "dianbiao/meter/{MAC}";

String g_cfg_ntp_server1 = "ntp.aliyun.com";
String g_cfg_ntp_server2 = "ntp.ntsc.ac.cn";
String g_cfg_tz_info     = "CST-8";    // UTC+8

unsigned long g_cfg_heartbeat_interval_ms = 60000;  // 心跳 60s
unsigned long g_cfg_report_interval_s    = 60;      // 电量上报 60s

// ── 12 路表计默认配置 ───────────────────────────────────────────
MeterChannelConfig g_meters[METER_CHANNELS];

// ── NVS 命名空间 ────────────────────────────────────────────────
static const char* NVS_NS = "dianbiao";

void config_load(void) {
    // 恢复通道默认值 (NVS 里有的 key 会覆盖)
    for (int i = 0; i < METER_CHANNELS; i++) {
        MeterChannelConfig& m = g_meters[i];
        m.enabled      = true;
        m.initial_kwh  = 0.0;
        m.imp_per_kwh  = 1600;   // 12 路默认脉冲常数, Web 页可改
        snprintf(m.name, METER_NAME_LEN, "电表%02d", i + 1);
    }

    Preferences p;
    p.begin(NVS_NS, false);
    if (!p.isKey("fw_ver")) { p.end(); return; }  // 首次启动用默认值

    g_cfg_device_name = p.getString("dev_name", g_cfg_device_name);
    g_cfg_wifi_ssid   = p.getString("wf_ssid",  g_cfg_wifi_ssid);
    g_cfg_wifi_pass   = p.getString("wf_pass",  g_cfg_wifi_pass);
    g_cfg_ap_ssid     = p.getString("ap_ssid",  g_cfg_ap_ssid);
    g_cfg_ap_password = p.getString("ap_pass",  g_cfg_ap_password);
    g_cfg_static_ip      = p.getString("sta_ip", g_cfg_static_ip);
    g_cfg_static_gateway = p.getString("sta_gw", g_cfg_static_gateway);
    g_cfg_static_subnet  = p.getString("sta_sn", g_cfg_static_subnet);
    g_cfg_static_dns     = p.getString("sta_dns", g_cfg_static_dns);
    g_cfg_web_port = p.getInt("web_port", g_cfg_web_port);

    g_cfg_mqtt_broker    = p.getString("mq_brk", g_cfg_mqtt_broker);
    g_cfg_mqtt_port      = p.getInt("mq_port",   g_cfg_mqtt_port);
    g_cfg_mqtt_client_id = p.getString("mq_cid", g_cfg_mqtt_client_id);
    g_cfg_mqtt_username  = p.getString("mq_user", g_cfg_mqtt_username);
    g_cfg_mqtt_password  = p.getString("mq_pass", g_cfg_mqtt_password);
    g_cfg_mqtt_pub_topic = p.getString("mq_pub", g_cfg_mqtt_pub_topic);
    g_cfg_mqtt_sub_topic = p.getString("mq_sub", g_cfg_mqtt_sub_topic);

    g_cfg_ntp_server1 = p.getString("ntp1", g_cfg_ntp_server1);
    g_cfg_ntp_server2 = p.getString("ntp2", g_cfg_ntp_server2);
    g_cfg_tz_info     = p.getString("tz",   g_cfg_tz_info);

    g_cfg_heartbeat_interval_ms = p.getULong("hb_ms", g_cfg_heartbeat_interval_ms);
    g_cfg_report_interval_s     = p.getULong("rep_s", g_cfg_report_interval_s);

    for (int i = 0; i < METER_CHANNELS; i++) {
        char k[16];
        MeterChannelConfig& m = g_meters[i];
        snprintf(k, sizeof(k), "en%d",  i); m.enabled       = p.getBool(k, m.enabled);
        snprintf(k, sizeof(k), "nm%d",  i); m.name[0] = 0;
            String nm = p.getString(k, String(m.name));
            strncpy(m.name, nm.c_str(), METER_NAME_LEN - 1);
            m.name[METER_NAME_LEN - 1] = 0;
        snprintf(k, sizeof(k), "ini%d", i); m.initial_kwh   = p.getDouble(k, m.initial_kwh);
        snprintf(k, sizeof(k), "imp%d", i); m.imp_per_kwh   = p.getULong(k, m.imp_per_kwh);
        if (m.imp_per_kwh == 0) m.imp_per_kwh = 1600;
    }
    p.end();
    AppLog.println(F("[CFG] 配置已从 NVS 加载"));
}

void config_save(void) {
    Preferences p;
    p.begin(NVS_NS, false);
    p.putString("fw_ver",   FIRMWARE_VERSION);
    p.putString("dev_name", g_cfg_device_name);
    p.putString("wf_ssid",  g_cfg_wifi_ssid);
    p.putString("wf_pass",  g_cfg_wifi_pass);
    p.putString("ap_ssid",  g_cfg_ap_ssid);
    p.putString("ap_pass",  g_cfg_ap_password);
    p.putString("sta_ip",   g_cfg_static_ip);
    p.putString("sta_gw",   g_cfg_static_gateway);
    p.putString("sta_sn",   g_cfg_static_subnet);
    p.putString("sta_dns",  g_cfg_static_dns);
    p.putInt("web_port",    g_cfg_web_port);
    p.putString("mq_brk",   g_cfg_mqtt_broker);
    p.putInt("mq_port",     g_cfg_mqtt_port);
    p.putString("mq_cid",   g_cfg_mqtt_client_id);
    p.putString("mq_user",  g_cfg_mqtt_username);
    p.putString("mq_pass",  g_cfg_mqtt_password);
    p.putString("mq_pub",   g_cfg_mqtt_pub_topic);
    p.putString("mq_sub",   g_cfg_mqtt_sub_topic);
    p.putString("ntp1",     g_cfg_ntp_server1);
    p.putString("ntp2",     g_cfg_ntp_server2);
    p.putString("tz",       g_cfg_tz_info);
    p.putULong("hb_ms",     g_cfg_heartbeat_interval_ms);
    p.putULong("rep_s",     g_cfg_report_interval_s);

    for (int i = 0; i < METER_CHANNELS; i++) {
        char k[16];
        const MeterChannelConfig& m = g_meters[i];
        snprintf(k, sizeof(k), "en%d",  i); p.putBool(k, m.enabled);
        snprintf(k, sizeof(k), "nm%d",  i); p.putString(k, m.name);
        snprintf(k, sizeof(k), "ini%d", i); p.putDouble(k, m.initial_kwh);
        snprintf(k, sizeof(k), "imp%d", i); p.putULong(k, m.imp_per_kwh);
    }
    p.end();
    AppLog.println(F("[CFG] 配置已保存到 NVS"));
}
