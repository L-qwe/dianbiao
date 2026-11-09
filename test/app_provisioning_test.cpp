#include "Arduino.h"
#include "app_config.h"
#include <ArduinoJson.h>
#include <atomic>
#include <cassert>
#include <functional>
#include <iostream>
#include <map>
#include <vector>

using wl_status_t = int;
enum { WL_IDLE_STATUS, WL_NO_SSID_AVAIL, WL_SCAN_COMPLETED, WL_CONNECTED,
       WL_CONNECT_FAILED, WL_CONNECTION_LOST, WL_DISCONNECTED };
struct Address { String toString() const { return "192.168.1.66"; } };
struct {
    int current = WL_DISCONNECTED;
    String ssid = "";
    int status() { return current; }
    String SSID() { return ssid; }
    String macAddress() { return "AA:BB:CC:11:22:33"; }
    Address localIP() { return {}; }
} WiFi;
struct {
    template<class... T> void printf(const char*, T...) {}
    void println(const char*) {}
} AppLog;
const char* FIRMWARE_VERSION = "v1.0.9";
const char* FIRMWARE_TYPE = "DianBiao 电表脉冲采集";
String g_cfg_ap_ssid;
int g_cfg_web_port = 80;
MeterChannelConfig g_meters[METER_CHANNELS] = {};
bool mqtt_connected() { return false; }
String ntp_time_str() { return "1970-01-01 00:00:00"; }
uint64_t meter_total_pulses(int) { return 4; }
String meter_energy_kwh_text(int) { return "85858.003"; }
// DEVICE_INFO_SOURCE

struct NimBLEConnInfo {};
struct NimBLECharacteristic;
struct NimBLECharacteristicCallbacks {
    virtual void onWrite(NimBLECharacteristic*, NimBLEConnInfo&) {}
    virtual void onRead(NimBLECharacteristic*, NimBLEConnInfo&) {}
};
namespace NIMBLE_PROPERTY { enum { READ=1, WRITE=2, WRITE_NR=4, NOTIFY=8 }; }
struct NimBLECharacteristic {
    std::string value;
    unsigned properties;
    NimBLECharacteristicCallbacks* callbacks = nullptr;
    void setCallbacks(NimBLECharacteristicCallbacks* cb) { callbacks=cb; }
    void setValue(const std::string& v) { value=v; }
    std::string getValue() { return value; }
    void notify(bool) {}
};
struct NimBLEService {
    std::map<std::string, NimBLECharacteristic> chars;
    NimBLECharacteristic* createCharacteristic(const char* id,unsigned p) { auto& c=chars[id];c.properties=p;return &c; }
    bool isStarted() { return true; }
};
struct NimBLEServer {
    NimBLEService service;
    NimBLEService* createService(const char*) { return &service; }
    bool start() { return true; }
};
struct NimBLEAdvertising {
    void setName(const char*) {} void addServiceUUID(const char*) {}
    void enableScanResponse(bool) {} void setMinInterval(int) {} void setMaxInterval(int) {}
    void setPreferredParams(int,int) {} bool start() { return true; } bool isAdvertising() { return true; }
};
constexpr int ESP_PWR_LVL_P9=9;
struct NimBLEDevice {
    static void init(const char*) {} static void setPower(int) {}
    static NimBLEServer* createServer() { static NimBLEServer s;return &s; }
    static NimBLEAdvertising* getAdvertising() { static NimBLEAdvertising a;return &a; }
};
String receivedSsid, receivedPass;
bool acceptCredentials = true;
bool web_portal_set_wifi_credentials_from_ble(const String& ssid,const String& pass) {
    receivedSsid=ssid;receivedPass=pass;
    return acceptCredentials && ssid.length()>0 && ssid.length()<=32 && pass.length()<=64;
}
const char* web_portal_wifi_connection_state() { return "queued"; }
// BLE_SOURCE

int httpCode=200, taskCreates=0, posts=0;
bool taskCreationOk=true;
String postedPayload, postedUrl;
void (*pendingTask)(void*)=nullptr;
constexpr int pdPASS=1;
int xTaskCreate(void (*fn)(void*),const char*,unsigned,void*,int,void*) {
    ++taskCreates;
    if (!taskCreationOk) return 0;
    pendingTask=fn;return pdPASS;
}
void vTaskDelete(void*) {}
struct HTTPClient {
    void setConnectTimeout(int n) { assert(n==3000); }
    void setTimeout(int n) { assert(n==5000); }
    void setReuse(bool n) { assert(!n); }
    bool begin(const char* url) { postedUrl=url;return true; }
    void addHeader(const char*,const char*) {}
    int POST(const String& body) { postedPayload=body;++posts;return httpCode; }
    void end() {}
};
// REPORT_SOURCE

constexpr int HTTP_GET=0;
struct Router {
    std::function<void()> handler;
    int code=0;
    String contentType, response, cache;
    void on(const char* path,int method,std::function<void()> fn) {
        assert(String(path)=="/api/device-info" && method==HTTP_GET);handler=fn;
    }
    void sendHeader(const char* name,const char* value) { assert(String(name)=="Cache-Control");cache=value; }
    void send(int status,const char* type,const String& body) { code=status;contentType=type;response=body; }
} router;
Router* server=&router;
void register_routes() {
// HTTP_ROUTE_SOURCE
}

DynamicJsonDocument decode(const String& text) {
    DynamicJsonDocument doc(8192);
    assert(!deserializeJson(doc,text.c_str()));
    return doc;
}
void finish_request() { assert(pendingTask);auto fn=pendingTask;pendingTask=nullptr;fn(nullptr); }
int main() {
    register_routes();router.handler();
    auto offline=decode(router.response);
    assert(router.code==200 && router.contentType=="application/json; charset=utf-8" && router.cache=="no-store");
    assert(offline["mac_address"]=="AA:BB:CC:11:22:33");
    assert(offline["wifi_port"]==80 && offline["wifi_status"]=="disconnected");
    assert(offline["wifi_connected"]==false && offline["wifi_ip"]=="" && offline["wifi_ssid"]=="");
    assert(offline["channels"].size()==12);
    assert(std::abs(offline["channels"][0]["kwh"].as<double>()-85858.003)<0.000001);
    assert(andon_device_access_report_json().isEmpty());
    device_access_report_loop();assert(taskCreates==0);

    WiFi.current=WL_CONNECTED;WiFi.ssid="车间\"A\\B\n\x01";g_cfg_web_port=8080;
    andon_ble_init();
    auto& chars=_server->service.chars;
    auto& info=chars[CHAR_DEVICE_INFO_UUID];NimBLEConnInfo conn;
    info.callbacks->onRead(&info,conn);
    assert(info.value.size()<=512);
    auto common=decode(info.value);
    router.handler();auto online=decode(router.response);
    for(const char* key:{"mac_address","firmware_type","firmware_version","ble_name","wifi_ip","wifi_ssid","wifi_status"})
        assert(common[key].as<String>()==online[key].as<String>());
    assert(common["ble_name"]=="DianBiao_AABBCC112233");
    assert(common["wifi_connected"]==true && common["wifi_status"]=="connected" && common["wifi_port"]==8080);
    assert(common["wifi_ssid"].as<String>()==WiFi.ssid);

    auto& cred=chars[CHAR_WIFI_CRED_UUID];
    assert((cred.properties & NIMBLE_PROPERTY::WRITE) && (cred.properties & NIMBLE_PROPERTY::WRITE_NR));
    cred.value=" Factory | pass,word| ";cred.callbacks->onWrite(&cred,conn);
    assert(receivedSsid==" Factory " && receivedPass==" pass,word| ");
    cred.value="OpenNetwork|";cred.callbacks->onWrite(&cred,conn);assert(receivedPass.isEmpty());
    cred.value="Legacy,password";cred.callbacks->onWrite(&cred,conn);assert(receivedPass=="password");
    cred.value="|bad";cred.callbacks->onWrite(&cred,conn);assert(_pending_status=="FAILED,EMPTY_SSID");

    fakeMillis=100;device_access_report_loop();assert(taskCreates==1 && posts==0);
    device_access_report_loop();assert(taskCreates==1); // no parallel requests or blocking HTTP in loop
    finish_request();assert(posts==1 && succeeded.load());
    assert(postedUrl=="http://10.0.100.65:8080/anton/device-access/report");
    assert(decode(postedPayload)["wifi_port"]==8080);
    fakeMillis=60099;device_access_report_loop();assert(taskCreates==1);
    fakeMillis=60100;device_access_report_loop();assert(taskCreates==2);
    httpCode=503;finish_request();assert(!succeeded.load());
    fakeMillis=75099;device_access_report_loop();assert(taskCreates==2);
    fakeMillis=75100;device_access_report_loop();assert(taskCreates==3);
    // Reconnect while an older request is still running must trigger a new report afterward.
    WiFi.current=WL_DISCONNECTED;device_access_report_loop();WiFi.current=WL_CONNECTED;
    device_access_report_loop();assert(taskCreates==3);finish_request();
    device_access_report_loop();assert(taskCreates==4);httpCode=200;finish_request();
    WiFi.current=WL_DISCONNECTED;device_access_report_loop();WiFi.current=WL_CONNECTED;
    taskCreationOk=false;device_access_report_loop();assert(taskCreates==5 && !running.load());
    taskCreationOk=true;fakeMillis+=14999;device_access_report_loop();assert(taskCreates==5);
    fakeMillis+=1;device_access_report_loop();assert(taskCreates==6);finish_request();
    for(int i=0;i<7;++i) { WiFi.current=i;assert(decode(andon_common_info_json())["wifi_status"]!="unknown"); }
    std::cout << "PASS APP protocol: HTTP AP/LAN discovery, BLE JSON and Write/WriteNR credentials, exact passwords, background registration cadence/retry/reconnect/task failure\n";
}
