/**
 * @file web_portal.h
 * @brief WiFi AP+STA 配网 + Web 门户 (实时总览 / 电量采集配置)
 */
#ifndef WEB_PORTAL_H
#define WEB_PORTAL_H

#include <Arduino.h>

/** 启动热点 + Web 服务器 + STA 连接 (setup 阶段调用) */
void web_portal_init(void);

/** 主循环维护 (DNS + WiFi 状态迁移 + 延迟重启, 每帧调用) */
void web_portal_loop(void);

/** 获取设备 STA 已连 WiFi 的 IP (空串 = 未连接) */
String web_portal_sta_ip(void);

/** BLE 配网写入 WiFi 凭证, 与 Web /connect 使用相同的连接流程。 */
bool web_portal_set_wifi_credentials_from_ble(const String& ssid, const String& pass);
const char* web_portal_wifi_connection_state();

#endif
