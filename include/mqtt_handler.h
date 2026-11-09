/**
 * @file mqtt_handler.h
 * @brief MQTT 通信 — 设备心跳 / 12 路电量上报 / OTA 与维护指令订阅
 *
 * 心跳 (pub topic):
 *   {"<MAC>_online":true,"version":"v1.x","ip":"192.168.1.23","rssi":-55,
 *    "time":"2026-10-30 12:00:00","ntp_ok":true,"uptime_s":1234,
 *    "free_heap":200000,"report_interval_s":60,"msg_type":"heartbeat",
 *    "dev_mac":"AA:BB:...","device_name":"12路电表采集"}
 *   心跳不包含 meters；电量通过下面的 meter_energy 单独上报。
 *
 * 电量上报 (pub topic):
 *   {"msg_type":"meter_energy","dev_mac":"AA:BB:...","time":"...","report_index":42,
 *    "meters":[{"ch":1,"name":"电表01","energy_kwh":203.38},...]}
 *
 * 配置回执 (pub topic, 收到 get_config 或 Web 保存后触发):
 *   {"msg_type":"meter_config","dev_mac":"...","time":"...","meters":[...]}
 *
 * 订阅指令 (sub topic):
 *   {"msg_type":"upgrade","file_url":"http://...","file_md5":"...","file_size":1234}
 *   {"cmd":"ota","url":"http://..."}
 *   {"msg_type":"report_now"} / {"msg_type":"get_config"} / {"msg_type":"reboot"}
 */
#ifndef MQTT_HANDLER_H
#define MQTT_HANDLER_H

#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFi.h>

// Do not use MQTT_MAX_PACKET_SIZE here: PubSubClient.h already defines it as
// 256 by default. Resize explicitly using an application-owned constant.
constexpr uint16_t MQTT_BUFFER_BYTES = 4096;

// ── 设备 MAC & topic ────────────────────────────────────────────
extern String g_dev_mac;      // 带冒号大写 AA:BB:CC:DD:EE:FF
extern String g_dev_mac_nc;   // 不带冒号大写 AABBCCDDEEFF
extern String g_topic_pub;
extern String g_topic_sub;

// ── OTA 待处理参数 (ota_handler 使用) ───────────────────────────
extern String g_ota_url;
extern String g_ota_md5;
extern long   g_ota_size;

// ── 公开接口 ────────────────────────────────────────────────────

/** 基于配置初始化连接参数；握手由维护函数调度到专用任务。 */
void mqtt_setup(void);

/** 维护连接/心跳/上报 (loop 中调用) */
void mqtt_maintain(void);

/** 读取缓存的连接状态；不会在其他任务握手时访问 MQTT 客户端。 */
bool mqtt_connected(void);

/** 发布设备心跳 */
void mqtt_heartbeat(void);

/** 发布 12 路电量报文 */
void mqtt_meter_report(void);

/** 发布当前表计配置回执 */
void mqtt_meter_config(void);

/** 发布任意 payload 到 pub topic */
bool mqtt_pub(const String& payload);

#endif
