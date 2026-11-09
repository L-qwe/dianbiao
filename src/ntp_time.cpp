/**
 * @file ntp_time.cpp
 * @brief NTP 时间同步实现
 */
#include "app_log.h"
#include "ntp_time.h"
#include "app_config.h"
#include <WiFi.h>

void ntp_sync(void) {
    if (WiFi.status()!=WL_CONNECTED) {
        AppLog.println(F("[NTP] WiFi 未连接, 跳过同步"));
        return;
    }
    AppLog.printf("[NTP] 同步中... srv=%s / %s  tz=%s\n",
                  g_cfg_ntp_server1.c_str(), g_cfg_ntp_server2.c_str(), g_cfg_tz_info.c_str());
    setenv("TZ", g_cfg_tz_info.c_str(), 1);   // 显式设置时区, 覆盖 SNTP 缓存的旧时区 (对齐参考项目)
    tzset();
    // configTzTime 在后台异步启动 SNTP 同步; 不在此阻塞等待.
    // (原 getLocalTime(&ti, 10000) 会卡主循环长达 10 秒, 期间 Web 页面完全无响应)
    configTzTime(g_cfg_tz_info.c_str(), g_cfg_ntp_server1.c_str(), g_cfg_ntp_server2.c_str());

    struct tm ti;
    if (getLocalTime(&ti, 0)) {   // 立即返回, 不阻塞
        AppLog.print(F("[NTP] 已同步: "));
        AppLog.println(ntp_time_str());
    } else {
        AppLog.println(F("[NTP] SNTP 后台同步中 (异步, 不阻塞主循环)"));
    }
}

String ntp_time_str(void) {
    struct tm ti;
    // 心跳主循环不得因 NTP 尚未同步而等待默认的 5 秒超时。
    if (getLocalTime(&ti, 0)) {
        char b[20]; strftime(b,sizeof(b),"%Y-%m-%d %H:%M:%S",&ti); return String(b);
    }
    return "1970-01-01 00:00:00";
}

bool ntp_ok(void) {
    time_t now; time(&now);
    return now > 1577836800;  // 2020-01-01
}

String ntp_format_time(time_t t) {
    if (t == 0) return "---";
    struct tm ti;
    localtime_r(&t, &ti);
    char b[20];
    strftime(b, sizeof(b), "%Y-%m-%d %H:%M:%S", &ti);
    return String(b);
}

String ntp_time_str_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    if (tv.tv_sec <= 1577836800) return "1970-01-01 00:00:00.000";  // 2020-01-01 前视为未同步
    struct tm ti;
    localtime_r(&tv.tv_sec, &ti);
    char b[24];
    strftime(b, sizeof(b), "%Y-%m-%d %H:%M:%S", &ti);
    sprintf(b + 19, ".%03d", (int)(tv.tv_usec / 1000));
    return String(b);
}

String ntp_time_for_millis(unsigned long alarm_millis) {
    if (!ntp_ok()) return "1970-01-01 00:00:00.000";
    time_t now;
    time(&now);
    unsigned long elapsed_ms = millis() - alarm_millis;
    time_t alarm_time = now - (elapsed_ms / 1000);
    struct tm ti;
    localtime_r(&alarm_time, &ti);
    char b[24];
    strftime(b, sizeof(b), "%Y-%m-%d %H:%M:%S", &ti);
    sprintf(b + 19, ".%03d", (int)(elapsed_ms % 1000));
    return String(b);
}
