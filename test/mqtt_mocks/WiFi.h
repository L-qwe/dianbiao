
#pragma once
#include <Arduino.h>
#include <atomic>
struct IPAddress{String toString()const{return "192.168.1.66";}};
inline constexpr int WL_CONNECTED=3;
struct FakeWiFi{int current=WL_CONNECTED;int status(){return current;}String macAddress(){return "AA:BB:CC:11:22:33";}IPAddress localIP(){return {};}int RSSI(){return -45;}};
inline FakeWiFi WiFi;
inline unsigned restartCount=0;inline uint32_t restartedAt=0;
struct FakeESP{unsigned getFreeHeap(){return 200000;}void restart(){++restartCount;restartedAt=millis();}};
inline FakeESP ESP;
class WiFiClient{
public:
 std::atomic<bool> online{false};
 virtual int connect(const char*,uint16_t){online=true;return 1;}
 virtual int connect(IPAddress,uint16_t){online=true;return 1;}
 int connect(const char*,uint16_t,int32_t timeout){if(timeout!=1000)abort();online=true;return 1;}
 int connect(IPAddress,uint16_t,int32_t timeout){if(timeout!=1000)abort();online=true;return 1;}
 void stop(){online=false;}
};
