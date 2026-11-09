
#pragma once
#include <Arduino.h>
#include <map>
#include <vector>
#include <functional>
inline std::map<std::string,uint64_t> stored;
inline bool pulseValid=false,openFailure=false;
inline std::string failKey;
inline unsigned opens=0;
inline std::vector<std::string>writes;
inline std::function<void()>writeHook;
class Preferences {
public:
 bool begin(const char*,bool=false){++opens;return !openFailure;}
 void end(){}
 uint64_t getULong64(const char* key,uint64_t fallback){auto at=stored.find(key);return at==stored.end()?fallback:at->second;}
 bool getBool(const char*,bool){return pulseValid;}
 size_t putULong64(const char* key,uint64_t value){writes.push_back(key);if(writeHook){auto fn=writeHook;writeHook={};fn();}if(failKey==key)return 0;stored[key]=value;return 8;}
 size_t putBool(const char*,bool value){writes.push_back("pulse_ok");if(failKey=="pulse_ok")return 0;pulseValid=value;return 1;}
};
