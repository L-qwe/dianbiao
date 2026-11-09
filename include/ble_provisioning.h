/**
 * @file ble_provisioning.h
 * @brief 电表 BLE 配网与设备信息服务
 *
 * Service: 8e7f1a01-2b3c-4d5e-9f01-aabbccddeeff
 * a02 Write/WriteNR SSID|PASSWORD
 * a03 Read/Notify IDLE / CONNECTING / CONNECTED,<ip> / FAILED,<reason>
 * a04 Read        公共设备信息 JSON
 * a05 Read        电表通道运行状态 JSON
 */
#ifndef BLE_PROVISIONING_H
#define BLE_PROVISIONING_H

#include <Arduino.h>

void andon_ble_init(void);
void andon_ble_loop(void);
String andon_ble_status(void);

extern bool g_andon_provisioning_active;

#endif
