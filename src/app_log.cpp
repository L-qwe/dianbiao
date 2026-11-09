/**
 * @file app_log.cpp
 * @brief 精简版日志: 串口输出, 每行加运行毫秒前缀 (无 RAM 环形缓冲)
 */
#include "app_log.h"

AppLogPrint AppLog;

static bool s_line_start = true;

void app_log_init() {
    s_line_start = true;
}

size_t AppLogPrint::write(uint8_t value) {
    return write(&value, 1);
}

size_t AppLogPrint::write(const uint8_t* data, size_t length) {
    if (!data || !length) return 0;
    size_t out = 0;
    for (size_t i = 0; i < length; i++) {
        if (s_line_start) {
            char prefix[32];
            const int n = snprintf(prefix, sizeof(prefix), "[%lu ms] ", (unsigned long)millis());
            Serial.write(reinterpret_cast<const uint8_t*>(prefix), n);
            s_line_start = false;
        }
        Serial.write(data[i]);
        out++;
        if (data[i] == '\n') s_line_start = true;
    }
    return out;
}

bool app_log_snapshot(String& output) {
    output = String("[LOG] uptime_ms=") + String((unsigned long)millis());
    return true;
}