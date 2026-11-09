/**
 * @file ota_handler.h
 * @brief OTA 远程固件升级 — HTTP GET + MD5/大小校验 + 分块刷写
 */
#ifndef OTA_HANDLER_H
#define OTA_HANDLER_H
#include <Arduino.h>
extern String g_ota_state;

/** 从 URL 下载固件并刷写 (支持 MD5 + 大小校验)
 *  @param url    固件二进制 URL
 *  @param md5    期望 MD5 (空="不校验")
 *  @param fsize  期望大小 (0=不校验)
 */
void ota_go(const String& url, const String& md5="", long fsize=0);

/** 检查是否有待处理的 OTA (MQTT 或 Web 触发), 有则执行 */
void ota_check(void);

#endif
