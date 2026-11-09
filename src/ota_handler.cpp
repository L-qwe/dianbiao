/**
 * @file ota_handler.cpp
 * @brief OTA 远程升级 — HTTP GET + MD5/大小校验 + 分块刷写
 */
#include "app_log.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <Update.h>
#include <MD5Builder.h>
#include "ota_handler.h"
#include "mqtt_handler.h"
#include "app_config.h"
#include "meter_pulse.h"
String g_ota_state="idle";

namespace {
// A failed download must release Update's active state so a later attempt can
// call begin() again. Do not activate an incomplete image.
class OtaAttempt {
public:
    ~OtaAttempt() { if (!committed) Update.abort(); }
    bool committed = false;
};
constexpr uint32_t OTA_IDLE_TIMEOUT_MS = 30000;
}

void ota_go(const String& url, const String& md5, long fsize) {
    AppLog.printf("[OTA] === START ===\n[OTA] URL: %s\n",url.c_str());
    if(md5.length()>0) AppLog.printf("[OTA] MD5: %s\n",md5.c_str());
    if(fsize>0)        AppLog.printf("[OTA] Size: %ld\n",fsize);

    WiFiClient cl;
    HTTPClient http;
    http.setConnectTimeout(10000);
    http.setTimeout(30000);
    if(!http.begin(cl,url)){AppLog.println("[OTA] HTTP begin failed");return;}
    int code=http.GET();
    if(code!=200){AppLog.printf("[OTA] HTTP %d\n",code);http.end();return;}

    int len=http.getSize();
    if(len<=0){AppLog.println("[OTA] No Content-Length");http.end();return;}
    if(fsize>0&&len!=fsize){AppLog.printf("[OTA] Size mismatch: want %ld got %d\n",fsize,len);http.end();return;}
    AppLog.printf("[OTA] %d bytes (%.1f KB)\n",len,len/1024.0);

    if(Update.isRunning()) {
        AppLog.println("[OTA] Clearing unfinished previous attempt");
        Update.abort();
    }
    if(!Update.begin(len)){AppLog.printf("[OTA] begin fail: %s\n",Update.errorString());http.end();return;}
    OtaAttempt attempt;

    // ── 分块下载 + 同时计算 MD5 ────────────────────────────
    WiFiClient* s=http.getStreamPtr();
    uint8_t b[1024];size_t wr=0;int prev=-1;
    MD5Builder md5c;md5c.begin();
    bool read_err=false;
    uint32_t last_data = millis();
    while(wr<(size_t)len){
        if((uint32_t)(millis()-last_data)>=OTA_IDLE_TIMEOUT_MS){
            AppLog.printf("[OTA] download timeout: received=%u expected=%d\n",(unsigned)wr,len);
            read_err=true;break;
        }
        int available = s->available();
        if(available<=0){
            if(!http.connected()) break;
            delay(1);
            continue;
        }
        size_t want = (size_t)len-wr;
        if(want>sizeof(b)) want=sizeof(b);
        if(want>(size_t)available) want=(size_t)available;
        int n=s->read(b,want);
        if(n>0){
            if(Update.write(b,(size_t)n)!=(size_t)n){
                AppLog.printf("[OTA] Flash write failed at %u: %s\n",(unsigned)wr,Update.errorString());
                read_err=true;break;
            }
            md5c.add(b,n);wr+=n;
            last_data=millis();
            int pct=wr*100/len;if(pct/10>prev/10){AppLog.printf("[OTA] %d%%\n",pct);prev=pct;}
        } else if(n<0){
            AppLog.println("[OTA] stream read error");read_err=true;break;   // 防死循环
        }
        delay(1); // Let other tasks run during both streaming and stalled reads.
    }
    http.end();
    if(read_err) return;

    // 大小校验 (下载字节数必须等于 Content-Length)
    if(wr!=(size_t)len){AppLog.printf("[OTA] size mismatch: got %d want %d\n",(int)wr,len);return;}

    // ── MD5 校验 ────────────────────────────────────────────
    md5c.calculate();String mstr=md5c.toString();
    AppLog.printf("[OTA] MD5 computed: %s\n",mstr.c_str());
    if(md5.length()>0&&!String(mstr).equalsIgnoreCase(md5)){AppLog.printf("[OTA] MD5 MISMATCH! want=%s got=%s\n",md5.c_str(),mstr.c_str());return;}
    if(md5.length()>0)AppLog.println("[OTA] MD5 PASS");

    if(!Update.end()||!Update.isFinished()){AppLog.printf("[OTA] end fail: %s\n",Update.errorString());return;}
    attempt.committed = true;
    AppLog.println("[OTA] SUCCESS! Rebooting in 2s...");delay(2000);
    meter_pulse_flush(); // Include pulses received during download, Flash writes and the delay.
    ESP.restart();
}

void ota_check(void) {
    if(g_ota_url.length()>5){
        String u=g_ota_url,m=g_ota_md5;long s=g_ota_size;
        g_ota_url="";g_ota_md5="";g_ota_size=0;
        meter_pulse_flush();
        g_ota_state="running";
        ota_go(u,m,s);
        g_ota_state="failed"; // Successful upgrades restart; a return means failure.
    }
}
