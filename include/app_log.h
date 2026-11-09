#pragma once
#include <Arduino.h>

// Application output is mirrored to Serial and a bounded RAM buffer.
class AppLogPrint : public Print {
public:
    using Print::write;
    size_t write(uint8_t value) override;
    size_t write(const uint8_t* data, size_t length) override;
};
extern AppLogPrint AppLog;
void app_log_init();
// false means allocation/lock failure; callers should return a retryable error.
bool app_log_snapshot(String& output);
