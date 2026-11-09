/**
 * @file app_config.h
 * @brief 全局可配置参数 (Web 页面可修改, NVS 持久化)
 *
 * 分两类:
 *  1. 系统配置: WiFi/AP/MQTT/NTP/上报周期/设备名称
 *  2. 表计配置: 12 路通道使能/名称/初始电量/脉冲常数 (imp/kWh)
 */
#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include <Arduino.h>

// ── 通道数 ──────────────────────────────────────────────────────
#define METER_CHANNELS   12
#define METER_NAME_LEN   20   // 通道路名称最大字节 (UTF-8)

// ── 固件版本 ────────────────────────────────────────────────────
extern const char* FIRMWARE_VERSION;
extern const char* FIRMWARE_TYPE;

// ── 设备名称 (Web 页面标题) ─────────────────────────────────────
extern String g_cfg_device_name;

// ── WiFi STA (连接外网做 MQTT/NTP) ───────────────────────────────
extern String g_cfg_wifi_ssid;
extern String g_cfg_wifi_pass;

// ── WiFi AP (设备热点, 配网入口) ─────────────────────────────────
extern String g_cfg_ap_ssid;        // 空 = 自动生成 "DianBiao_XXXXXX"
extern String g_cfg_ap_password;    // 空 = 无密码

// ── WiFi STA 静态 IP (留空 = DHCP) ──────────────────────────────
extern String g_cfg_static_ip;
extern String g_cfg_static_gateway;
extern String g_cfg_static_subnet;
extern String g_cfg_static_dns;

// ── Web 服务端口 ────────────────────────────────────────────────
extern int g_cfg_web_port;

// ── MQTT ────────────────────────────────────────────────────────
extern String  g_cfg_mqtt_broker;
extern int     g_cfg_mqtt_port;
extern String  g_cfg_mqtt_client_id;
extern String  g_cfg_mqtt_username;
extern String  g_cfg_mqtt_password;
extern String  g_cfg_mqtt_pub_topic;   // 支持 "{MAC}" 占位符
extern String  g_cfg_mqtt_sub_topic;

// ── NTP ─────────────────────────────────────────────────────────
extern String  g_cfg_ntp_server1;
extern String  g_cfg_ntp_server2;
extern String  g_cfg_tz_info;

// ── 心跳 / 上报周期 (Web 可配) ──────────────────────────────────
extern unsigned long g_cfg_heartbeat_interval_ms;  // 设备在线心跳间隔
extern unsigned long g_cfg_report_interval_s;      // 电量 MQTT 上报间隔 (秒, 0=关闭)

// ── 单通道表计配置 ──────────────────────────────────────────────
struct MeterChannelConfig {
    bool     enabled;        // 是否启用采集上送
    char     name[METER_NAME_LEN];  // 通道名称 (如 "1号厂房总表")
    double   initial_kwh;    // 初始电量 (kWh, 已用电量基数)
    uint32_t imp_per_kwh;    // 脉冲常数 (imp/kWh, 电能表铭牌参数, 最小 1)
};

extern MeterChannelConfig g_meters[METER_CHANNELS];

// ── NVS 存取 ────────────────────────────────────────────────────
void config_load(void);
void config_save(void);

#endif