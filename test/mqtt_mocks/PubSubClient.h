
#pragma once
#include <WiFi.h>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <cassert>
#define MQTT_MAX_PACKET_SIZE 256
inline std::atomic<bool> releaseHandshake{false},handshakeActive{false},connectResult{false};
inline std::atomic<unsigned> connectCalls{0},concurrentAccess{0},publishCalls{0};
inline std::thread::id handshakeThread;
inline bool failBufferAllocation=false;
class PubSubClient {
 WiFiClient& transport;
 void check(){if(handshakeActive.load()&&std::this_thread::get_id()!=handshakeThread)++concurrentAccess;}
public:
 unsigned timeout=0,capacity=MQTT_MAX_PACKET_SIZE;String cid,payload;
 PubSubClient(WiFiClient& t):transport(t){}
 void setServer(const char*,int){}bool setBufferSize(uint16_t n){check();if(failBufferAllocation)return false;capacity=n;return true;}
 uint16_t getBufferSize(){check();return capacity;}
 void setKeepAlive(int){}void setSocketTimeout(int n){timeout=n;}
 void setCallback(void(*)(char*,uint8_t*,unsigned)){}
 bool connect(const char* id,const char* =nullptr,const char* =nullptr){
  handshakeThread=std::this_thread::get_id();cid=id;handshakeActive=true;++connectCalls;
  assert(timeout==2);transport.connect("192.168.1.10",1883);
  while(!releaseHandshake.load())std::this_thread::sleep_for(std::chrono::milliseconds(1));
  bool result=connectResult.load();transport.online=result;handshakeActive=false;return result;
 }
 bool subscribe(const char*){check();return transport.online.load();}
 void disconnect(){check();transport.stop();}
 bool connected(){check();return transport.online.load();}
 void loop(){check();}
 bool publish(const char* topic,const uint8_t* data,unsigned length){
  check();if(5+2+strlen(topic)+length>capacity)return false;
  payload.assign(reinterpret_cast<const char*>(data),length);++publishCalls;return transport.online.load();
 }
};
using TaskHandle_t=void*;
inline constexpr int pdPASS=1,pdTRUE=1;inline constexpr unsigned portMAX_DELAY=0xffffffffU;
inline std::mutex notifyMutex;inline std::condition_variable notifyCv;inline unsigned notifications=0;
inline bool failTaskCreation=true,workerStarted=false;inline void(*workerFunction)(void*)=nullptr;
inline int xTaskCreatePinnedToCore(void(*fn)(void*),const char*,unsigned,void*,unsigned,TaskHandle_t* handle,int){
 if(failTaskCreation)return 0;workerFunction=fn;*handle=reinterpret_cast<void*>(1);return pdPASS;
}
inline void xTaskNotifyGive(TaskHandle_t){
 if(!workerStarted){workerStarted=true;std::thread([]{workerFunction(nullptr);}).detach();}
 {std::lock_guard<std::mutex>lock(notifyMutex);++notifications;}notifyCv.notify_one();
}
inline unsigned ulTaskNotifyTake(int,unsigned){std::unique_lock<std::mutex>lock(notifyMutex);notifyCv.wait(lock,[]{return notifications>0;});unsigned count=notifications;notifications=0;return count;}
