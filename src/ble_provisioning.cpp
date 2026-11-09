/**
 * @file ble_provisioning.cpp
 * @brief 与验针机一致的 BLE 配网协议，增加安东运行状态特征 a05
 */

#include "app_log.h"
#include <Arduino.h>
#include <WiFi.h>
#include <NimBLEDevice.h>

#include "ble_provisioning.h"
#include "device_info.h"
#include "web_portal.h"

static const char* SERVICE_UUID          = "8e7f1a01-2b3c-4d5e-9f01-aabbccddeeff";
static const char* CHAR_WIFI_CRED_UUID   = "8e7f1a02-2b3c-4d5e-9f01-aabbccddeeff";
static const char* CHAR_STATUS_UUID      = "8e7f1a03-2b3c-4d5e-9f01-aabbccddeeff";
static const char* CHAR_DEVICE_INFO_UUID = "8e7f1a04-2b3c-4d5e-9f01-aabbccddeeff";
static const char* CHAR_ANDON_STATE_UUID = "8e7f1a05-2b3c-4d5e-9f01-aabbccddeeff";

static const unsigned long CONNECT_TIMEOUT_MS = 30000UL;

static NimBLEServer* _server = nullptr;
static NimBLECharacteristic* _status_char = nullptr;
static String _current_status = "IDLE";
static String _pending_status;
static volatile bool _notify_pending = false;
static volatile wl_status_t _last_wifi_status = WL_IDLE_STATUS;
static volatile unsigned long _connecting_since = 0;
static unsigned _status_repeats = 0;
static uint32_t _next_status_notify = 0;

bool g_andon_provisioning_active = false;

static void _notify_status(const String& status) {
    _current_status = status;
    if (!_status_char) return;
    _status_char->setValue(std::string(status.c_str()));
    // Repeat from loop without holding HTTP service for 3 * 100 ms.
    _status_repeats = 3;
    _next_status_notify = millis();
    AppLog.printf("[BLE] status -> %s\n", status.c_str());
}

class WifiCredentialsCallback : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* characteristic, NimBLEConnInfo& connInfo) override {
        std::string value = characteristic->getValue();
        if (value.empty()) return;

        String payload(value.c_str());
        int separator = payload.indexOf('|');
        if (separator < 0) separator = payload.indexOf(',');
        String ssid = separator < 0 ? payload : payload.substring(0, separator);
        String pass = separator < 0 ? String("") : payload.substring(separator + 1);
        ssid.trim();
        pass.trim();

        if (web_portal_set_wifi_credentials_from_ble(ssid, pass)) {
            _pending_status = "CONNECTING";
            _notify_pending = true;
            _connecting_since = millis();
            g_andon_provisioning_active = true;
            _last_wifi_status = WL_IDLE_STATUS;
        } else {
            _pending_status = "FAILED,EMPTY_SSID";
            _notify_pending = true;
            _connecting_since = 0;
        }
    }
};

class CommonInfoCallback : public NimBLECharacteristicCallbacks {
    void onRead(NimBLECharacteristic* characteristic, NimBLEConnInfo& connInfo) override {
        const String json = andon_common_info_json();
        characteristic->setValue(std::string(json.c_str()));
        AppLog.printf("[BLE] common info read (%u bytes)\n", (unsigned)json.length());
    }
};

class AndonStateCallback : public NimBLECharacteristicCallbacks {
    void onRead(NimBLECharacteristic* characteristic, NimBLEConnInfo& connInfo) override {
        const String json = andon_runtime_info_json();
        characteristic->setValue(std::string(json.c_str()));
        AppLog.printf("[BLE] runtime info read (%u bytes)\n", (unsigned)json.length());
    }
};

void andon_ble_init(void) {
    const String name = andon_ble_name();
    AppLog.printf("[BLE] Starting service: %s\n", name.c_str());

    NimBLEDevice::init(name.c_str());
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);
    _server = NimBLEDevice::createServer();
    NimBLEService* service = _server->createService(SERVICE_UUID);

    NimBLECharacteristic* credentials = service->createCharacteristic(
        CHAR_WIFI_CRED_UUID, NIMBLE_PROPERTY::WRITE);
    credentials->setCallbacks(new WifiCredentialsCallback());

    _status_char = service->createCharacteristic(
        CHAR_STATUS_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
    _status_char->setValue("IDLE");

    NimBLECharacteristic* common_info = service->createCharacteristic(
        CHAR_DEVICE_INFO_UUID, NIMBLE_PROPERTY::READ);
    common_info->setCallbacks(new CommonInfoCallback());
    common_info->setValue(std::string(andon_common_info_json().c_str()));

    NimBLECharacteristic* andon_state = service->createCharacteristic(
        CHAR_ANDON_STATE_UUID, NIMBLE_PROPERTY::READ);
    andon_state->setCallbacks(new AndonStateCallback());
    andon_state->setValue(std::string(andon_runtime_info_json().c_str()));

    // 先把完整的服务表注册到 GATT，再开始广播。只启动广播名称并不能保证
    // 手机连接后一定能发现自定义服务，因此必须检查 server/service 启动结果。
    if (!_server->start() || !service->isStarted()) {
        AppLog.println(F("[BLE] ERROR: provisioning GATT service failed to start"));
        return;
    }
    AppLog.printf("[BLE] GATT service ready: %s\n", SERVICE_UUID);

    NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
    advertising->setName(name.c_str());
    advertising->addServiceUUID(SERVICE_UUID);
    advertising->enableScanResponse(true);
    advertising->setMinInterval(80);
    advertising->setMaxInterval(160);
    advertising->setPreferredParams(0x06, 0x40);
    const bool advertising_started = advertising->start();
    AppLog.printf("[BLE] Advertising start: %s\n",
                  advertising_started ? "OK" : "FAILED");
}

void andon_ble_loop(void) {
    if (_notify_pending) {
        _notify_pending = false;
        _notify_status(_pending_status);
    }
    if (_status_char && _status_repeats && int32_t(millis() - _next_status_notify) >= 0) {
        _status_char->notify(true);
        --_status_repeats;
        _next_status_notify = millis() + 100;
    }

    const wl_status_t status = WiFi.status();
    const char* phase = web_portal_wifi_connection_state();
    if (g_andon_provisioning_active) {
        // The previous association may still be connected while a queued switch
        // waits to start. Only the shared job can confirm the new credentials.
        if (!strcmp(phase,"connected") && status == WL_CONNECTED) {
            _notify_status(String("CONNECTED,") + WiFi.localIP().toString());
            _connecting_since = 0;
            g_andon_provisioning_active = false;
        } else if (!strcmp(phase,"failed")) {
            _notify_status(status == WL_NO_SSID_AVAIL ? "FAILED,SSID_NOT_FOUND" : "FAILED,CONNECT_FAILED");
            _connecting_since = 0;
            g_andon_provisioning_active = false;
        }
        _last_wifi_status = status;
    } else if (status != _last_wifi_status) {
        if (status == WL_CONNECTED) {
            _notify_status(String("CONNECTED,") + WiFi.localIP().toString());
            _connecting_since = 0;
            g_andon_provisioning_active = false;
        } else if (status == WL_NO_SSID_AVAIL) {
            _notify_status("FAILED,SSID_NOT_FOUND");
            _connecting_since = 0;
            g_andon_provisioning_active = false;
        } else if (status == WL_CONNECT_FAILED) {
            _notify_status("FAILED,CONNECT_FAILED");
            _connecting_since = 0;
            g_andon_provisioning_active = false;
        }
        _last_wifi_status = status;
    }

    if (_connecting_since > 0 && millis() - _connecting_since > CONNECT_TIMEOUT_MS) {
        _notify_status("FAILED,TIMEOUT");
        _connecting_since = 0;
        g_andon_provisioning_active = false;
    }

    static unsigned long last_advertising_check = 0;
    if (millis() - last_advertising_check >= 5000UL) {
        last_advertising_check = millis();
        NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
        if (!advertising->isAdvertising()) {
            AppLog.println(F("[BLE] Advertising stopped, restarting"));
            const bool restarted = advertising->start();
            AppLog.printf("[BLE] Advertising restart: %s\n",
                          restarted ? "OK" : "FAILED");
        }
    }
}

String andon_ble_status(void) {
    return _current_status;
}
