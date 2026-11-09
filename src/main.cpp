/**
 * @file main.cpp
 * @brief 12 路电表脉冲采集系统 — ESP32-S3-N16R8 主程序
 *
 * 功能:
 *   1. 12 路脉冲输入 (CN1~CN12 → H11L1M 光耦 → SN74LVC14A → GPIO) 采集
 *   2. 电量换算: kWh = 初始电量 + 累计脉冲 / imp_kWh (每路 Web 可配)
 *   3. WiFi (AP+STA) 配网 + Web 门户 (实时总览 + 采集配置)
 *   4. BLE 配网 (Service 8e7f1a01: a02 写 SSID|密码)
 *   5. MQTT: NTP 在线心跳 + 12 路电量 JSON 上报 + OTA/维护指令订阅
 *   6. NTP 对时 + HTTP OTA (Web/MQTT 两种触发)
 */
#include <Arduino.h>
#include <WiFi.h>
#include "app_log.h"
#include "app_config.h"
#include "meter_pulse.h"
#include "web_portal.h"
#include "ble_provisioning.h"
#include "device_access_reporter.h"
#include "mqtt_handler.h"
#include "ntp_time.h"
#include "ota_handler.h"

void setup() {
    Serial.begin(115200);
    app_log_init();
    delay(400);
    AppLog.printf("\n[BOOT] %s %s build=%s %s\n",
                  FIRMWARE_TYPE, FIRMWARE_VERSION, __DATE__, __TIME__);

    config_load();          // 系统 + 12 路表计配置
    meter_pulse_init();     // GPIO 中断 + 恢复累计脉冲
    web_portal_init();      // AP+STA + Web 门户
    andon_ble_init();       // BLE 配网

    if (WiFi.status() == WL_CONNECTED) mqtt_setup();

    AppLog.println(F("[BOOT] Ready → http://192.168.4.1/ (AP) 或 STA 地址:/"));
}

void loop() {
    web_portal_loop();      // DNS + WiFi 状态迁移 + 延迟重启
    andon_ble_loop();       // BLE 状态通知/广播保活
    device_access_report_loop(); // 后台向 APP 平台登记/刷新设备信息
    meter_pulse_loop();     // 收集脉冲增量 + 周期写 NVS
    mqtt_maintain();        // 连接/心跳/电量上报/指令
    if (WiFi.status() == WL_CONNECTED) ota_check();
    delay(2);
}
