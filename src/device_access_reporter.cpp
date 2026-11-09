#include "device_access_reporter.h"
#include "device_info.h"
#include "app_log.h"
#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <atomic>

namespace {
// Same endpoint and refresh cadence as Yanzhenji2.0.
constexpr const char* REPORT_URL = "http://10.0.100.65:8080/anton/device-access/report";
constexpr uint32_t REPORT_INTERVAL_MS = 60000;
constexpr uint32_t REPORT_RETRY_MS = 15000;
std::atomic<bool> running{false}, succeeded{false};
bool wasConnected = false, pendingConnection = false;
uint32_t lastAttempt = 0;
// Main loop creates a snapshot. Only the worker reads it while running is true.
String payload;

void report_task(void*) {
    HTTPClient http;
    http.setConnectTimeout(3000);
    http.setTimeout(5000);
    http.setReuse(false);
    bool ok = false;
    if (http.begin(REPORT_URL)) {
        http.addHeader("Content-Type", "application/json; charset=utf-8");
        http.addHeader("Accept", "application/json");
        const int code = http.POST(payload);
        ok = code >= 200 && code < 300;
        // Do not download an unbounded response body from the platform.
        AppLog.printf("[DEVICE-ACCESS] report %s, HTTP %d\n", ok ? "OK" : "failed", code);
        http.end();
    } else {
        AppLog.println(F("[DEVICE-ACCESS] HTTP init failed"));
    }
    succeeded.store(ok);
    running.store(false);
    vTaskDelete(nullptr);
}
}

void device_access_report_loop() {
    if (WiFi.status() != WL_CONNECTED) {
        wasConnected = false;
        succeeded.store(false);
        return;
    }
    if (!wasConnected) pendingConnection = true;
    wasConnected = true;
    if (running.load()) return;
    const uint32_t now = millis();
    const uint32_t interval = succeeded.load() ? REPORT_INTERVAL_MS : REPORT_RETRY_MS;
    if (!pendingConnection && uint32_t(now - lastAttempt) < interval) return;

    payload = andon_device_access_report_json();
    if (!payload.length()) return;
    pendingConnection = false;
    lastAttempt = now;
    running.store(true);
    if (xTaskCreate(report_task, "device_access", 8192, nullptr, 1, nullptr) != pdPASS) {
        running.store(false);
        succeeded.store(false);
        AppLog.println(F("[DEVICE-ACCESS] failed to create report task"));
    }
}
