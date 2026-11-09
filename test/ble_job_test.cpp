
#include "Arduino.h"
#include "WiFi.h"
#include "app_log.h"
#include <cassert>
#include <vector>
#include <iostream>
using wl_status_t=int;
constexpr int WL_IDLE_STATUS=0,WL_NO_SSID_AVAIL=1,WL_CONNECT_FAILED=4;
struct NimBLEServer{};
struct NimBLECharacteristic{std::string value;std::vector<std::string>notifies;void setValue(const std::string& s){value=s;}void notify(bool){notifies.push_back(value);}};
struct NimBLEAdvertising{bool isAdvertising(){return true;}bool start(){return true;}};
struct NimBLEDevice{static NimBLEAdvertising* getAdvertising(){static NimBLEAdvertising a;return &a;}};
const char* fakePhase="queued";
const char* web_portal_wifi_connection_state(){return fakePhase;}
// NOTIFY_SOURCE
// LOOP_SOURCE
int main(){
 NimBLECharacteristic characteristic;_status_char=&characteristic;
 fakeMillis=100;WiFi.current=WL_CONNECTED;_last_wifi_status=WL_IDLE_STATUS;
 _pending_status="CONNECTING";_notify_pending=true;_connecting_since=100;g_andon_provisioning_active=true;
 andon_ble_loop();assert(_current_status=="CONNECTING" && g_andon_provisioning_active && fakeMillis==100);
 andon_ble_loop();assert(characteristic.notifies.size()==1 && fakeMillis==100);
 fakeMillis=199;andon_ble_loop();assert(characteristic.notifies.size()==1);
 fakeMillis=200;andon_ble_loop();fakeMillis=300;andon_ble_loop();assert(characteristic.notifies.size()==3 && fakeMillis==300);
 fakePhase="disconnecting";andon_ble_loop();assert(_current_status=="CONNECTING" && g_andon_provisioning_active);
 fakePhase="connecting";WiFi.current=0;andon_ble_loop();assert(_current_status=="CONNECTING");
 fakePhase="connected";WiFi.current=WL_CONNECTED;andon_ble_loop();assert(_current_status=="CONNECTED,192.168.1.66" && !g_andon_provisioning_active);
 fakePhase="queued";WiFi.current=WL_NO_SSID_AVAIL;g_andon_provisioning_active=true;_connecting_since=fakeMillis;_notify_status("CONNECTING");andon_ble_loop();assert(_current_status=="CONNECTING");
 fakePhase="failed";andon_ble_loop();assert(_current_status=="FAILED,SSID_NOT_FOUND" && !g_andon_provisioning_active);
 fakePhase="connecting";WiFi.current=0;g_andon_provisioning_active=true;_connecting_since=100;fakeMillis=30101;andon_ble_loop();assert(_current_status=="FAILED,TIMEOUT" && !g_andon_provisioning_active && fakeMillis==30101);
 std::cout<<"PASS actual BLE loop: notifications repeat without blocking, old Wi-Fi association cannot complete a queued job, phase success/failure/timeout preserved\n";
}
