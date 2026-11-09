/**
 * @file device_info.h
 * @brief BLE/Web 共用的设备信息 JSON
 */
#ifndef DEVICE_INFO_H
#define DEVICE_INFO_H

#include <Arduino.h>

String andon_ble_name(void);
String andon_common_info_json(void);
String andon_runtime_info_json(void);
String andon_device_info_json(void);

#endif