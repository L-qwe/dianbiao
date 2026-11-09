/**
 * @file ntp_time.h
 * @brief NTP 时间同步
 */

#ifndef NTP_TIME_H
#define NTP_TIME_H

#include <Arduino.h>

/** 同步 NTP 时间, 超时 10s */
void ntp_sync(void);

/** 获取格式化时间字符串 "YYYY-MM-DD HH:MM:SS" */
String ntp_time_str(void);

/** NTP 是否已同步 */
bool ntp_ok(void);

/** 格式化任意 time_t 为 "YYYY-MM-DD HH:MM:SS", t==0 时返回 "---" */
String ntp_format_time(time_t t);

/** 毫秒级时间字符串 "YYYY-MM-DD HH:MM:SS.mmm" */
String ntp_time_str_ms(void);

/** 根据 alarm 发生时的 millis() 反推真实时间 (毫秒级) */
String ntp_time_for_millis(unsigned long alarm_millis);

#endif
