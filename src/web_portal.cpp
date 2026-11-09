// Static portal + JSON endpoints, owned by loopTask, matching QC_ESP32.
#include "web_portal.h"
#include "web_assets.h"
#include "web_settings.h"
#include "device_info.h"
#include "app_log.h"
#include "meter_pulse.h"
#include "mqtt_handler.h"
#include "ntp_time.h"
#include "ota_handler.h"
#include "captive_dns.h"
#include <WebServer.h>
#include <esp_wifi.h>
#include <esp_ota_ops.h>
#include <atomic>
#include <memory>

class PlainHttpWebServer:public WebServer {
public:
    using WebServer::WebServer;
    void handleClient() override {
        if(_currentStatus==HC_NONE) {
            _currentClient=_server.available();
            if(!_currentClient) { if(_nullDelay) delay(1); return; }
            _currentStatus=HC_WAIT_READ; _statusChange=millis();
        }
        if(_currentStatus==HC_WAIT_READ && _currentClient.connected() && _currentClient.available()) {
            int first=_currentClient.peek();
            if(first>=0 && (first<'A' || first>'Z')) {
                uint8_t prefix[3]={};
                size_t count=_currentClient.available(); if(count>sizeof(prefix)) count=sizeof(prefix);
                int received=_currentClient.read(prefix,count);
                bool tls=received>=2 && prefix[0]==0x16 && prefix[1]==0x03;
                if(!logged_ || uint32_t(millis()-lastLog_)>=5000) {
                    AppLog.printf("[WEB] rejected %s; use http://%s:%u/ (prefix=%02X %02X %02X)\n",
                        tls ? "TLS/HTTPS on HTTP port" : "non-HTTP bytes",
                        _currentClient.localIP().toString().c_str(),unsigned(g_cfg_web_port),prefix[0],prefix[1],prefix[2]);
                    logged_=true; lastLog_=millis();
                }
                _currentClient.stop(); _currentClient=WiFiClient(); _currentStatus=HC_NONE;
                _currentUpload.reset(); _currentRaw.reset(); return;
            }
        }
        WebServer::handleClient();
    }
private:
    bool logged_=false;
    uint32_t lastLog_=0;
};

namespace {
std::unique_ptr<WebServer> server;
CaptiveDnsServer dns;
uint32_t rebootAt=0,lastConnect=0,backoff=15000,wifiStarted=0,wifiBeginAt=0;
bool wifiWasConnected=false,disconnectedObserved=false,scanRunning=false;
std::atomic<bool> wifiBusy{false},acceptJobs{true};
const char* wifiState="idle";
const char* wifiError="";
String wifiSsid,scanCache="{\"scanning\":false,\"networks\":[]}";
struct WifiJob {char ssid[33];char pass[65];};
QueueHandle_t wifiQueue=nullptr;

String ap_name() {
    if(g_cfg_ap_ssid.length()) return g_cfg_ap_ssid;
    String mac=WiFi.macAddress();mac.replace(":","");return "DianBiao_"+mac.substring(6);
}
String uptime_text() {
    unsigned long s=millis()/1000;char b[32];
    snprintf(b,sizeof(b),"%lud %02lu:%02lu:%02lu",s/86400,(s/3600)%24,(s/60)%60,s%60);return b;
}
void json(int code,JsonDocument& d) {
    if(d.overflowed()) {server->send(503,"application/json","{\"success\":false,\"error\":\"no_memory\"}");return;}
    String body;serializeJson(d,body);server->sendHeader("Cache-Control","no-store");
    server->send(code,"application/json; charset=utf-8",body);
}
void result(int code,const char* error=nullptr) {
    StaticJsonDocument<192> d;d["success"]=error==nullptr;if(error)d["error"]=error;json(code,d);
}
bool body(JsonDocument& d) {
    if(server->arg("plain").length()>8192) {result(413,"body_too_large");return false;}
    if(deserializeJson(d,server->arg("plain")) || !d.is<JsonObject>()) {result(400,"invalid_json");return false;}
    return true;
}
bool maintenance() {return rebootAt || g_ota_state=="queued" || g_ota_state=="running";}
// Reserve the same slot used by BLE enqueue, so Flash writes and radio jobs
// cannot pass independent busy checks at the same time.
class Mutation {
public:
    bool ok=false;
    Mutation(){bool expected=false;ok=!maintenance() && wifiBusy.compare_exchange_strong(expected,true);}
    ~Mutation(){if(ok)wifiBusy.store(false);}
};
void meter_config(JsonArray list) {
    for(int i=0;i<METER_CHANNELS;++i) {
        JsonObject m=list.createNestedObject();const auto& cfg=g_meters[i];
        m["ch"]=i+1;m["gpio"]=METER_GPIO[i];m["name"]=cfg.name;m["enabled"]=cfg.enabled;
        m["initial_kwh"]=cfg.initial_kwh;m["imp_kwh"]=cfg.imp_per_kwh;
    }
}
void send_config() {
    DynamicJsonDocument d(8192);
    for(auto* s=portal::settings();s->key;++s)d[s->key]=*s->value;
    d["mq_port"]=g_cfg_mqtt_port;d["hb_s"]=g_cfg_heartbeat_interval_ms/1000UL;d["rep_s"]=g_cfg_report_interval_s;
    meter_config(d.createNestedArray("meters"));json(200,d);
}
void send_wifi_status() {
    StaticJsonDocument<512> d;
    d["state"]=web_portal_wifi_connection_state();
    d["error"]=wifiError;d["requested_ssid"]=wifiSsid;
    d["connected"]=WiFi.status()==WL_CONNECTED;d["st"]=int(WiFi.status());
    d["ssid"]=WiFi.status()==WL_CONNECTED?WiFi.SSID():"";
    d["ip"]=WiFi.status()==WL_CONNECTED?WiFi.localIP().toString():"";json(200,d);
}
void send_status() {
    DynamicJsonDocument d(8192);
    d["dev_mac"]=WiFi.macAddress();d["firmware"]=FIRMWARE_VERSION;d["device_name"]=g_cfg_device_name;
    d["uptime"]=uptime_text();d["uptime_ms"]=millis();d["free_heap"]=ESP.getFreeHeap();d["ota_state"]=g_ota_state;
    JsonObject ap=d.createNestedObject("ap");ap["ssid"]=ap_name();ap["clients"]=WiFi.softAPgetStationNum();
    JsonObject w=d.createNestedObject("wifi");bool online=WiFi.status()==WL_CONNECTED;
    w["connected"]=online;w["ssid"]=online?WiFi.SSID():"";w["ip"]=online?WiFi.localIP().toString():"";
    w["rssi"]=online?WiFi.RSSI():0;w["state"]=wifiState;
    JsonObject m=d.createNestedObject("mqtt");m["configured"]=bool(g_cfg_mqtt_broker.length());
    m["connected"]=mqtt_connected();m["broker"]=g_cfg_mqtt_broker;
    JsonObject n=d.createNestedObject("ntp");n["ok"]=ntp_ok();n["time"]=ntp_time_str();
    JsonArray list=d.createNestedArray("meters");
    for(int i=0;i<METER_CHANNELS;++i) {
        JsonObject item=list.createNestedObject();const auto& cfg=g_meters[i];
        item["ch"]=i+1;item["gpio"]=METER_GPIO[i];item["name"]=cfg.name;item["enabled"]=cfg.enabled;
        item["pulses"]=meter_total_pulses(i);item["imp_kwh"]=cfg.imp_per_kwh;
        item["energy_kwh"]=serialized(meter_energy_kwh_text(i));
    }
    json(200,d);
}
void stop_scan() {esp_wifi_scan_stop();WiFi.scanDelete();scanRunning=false;}
void scan_status(bool start) {
    if(maintenance() || wifiBusy.load()) {result(409,"device_busy");return;}
    if(start && !scanRunning) {
        WiFi.scanDelete();
        int rc=WiFi.scanNetworks(true,false,false,120);
        if(rc==WIFI_SCAN_FAILED) {result(503,"wifi_scan_failed");return;}
        scanRunning=true;
    }
    if(scanRunning) {
        int count=WiFi.scanComplete();
        if(count==WIFI_SCAN_RUNNING) {StaticJsonDocument<96>d;d["scanning"]=true;json(200,d);return;}
        if(count<0) {scanRunning=false;result(503,"wifi_scan_failed");return;}
        DynamicJsonDocument d(4096);d["scanning"]=false;JsonArray list=d.createNestedArray("networks");
        for(int i=0;i<count && list.size()<20;++i) {
            String ssid=WiFi.SSID(i);if(!ssid.length())continue;
            bool duplicate=false;for(int j=0;j<i;++j)if(WiFi.SSID(j)==ssid){duplicate=true;break;}
            if(duplicate)continue;
            JsonObject entry=list.createNestedObject();entry["ssid"]=ssid;entry["rssi"]=WiFi.RSSI(i);
            entry["secure"]=WiFi.encryptionType(i)!=WIFI_AUTH_OPEN;
            entry["connected"]=WiFi.status()==WL_CONNECTED && WiFi.SSID()==ssid;
        }
        WiFi.scanDelete();scanRunning=false;
        if(d.overflowed()){result(503,"no_memory");return;}
        scanCache="";serializeJson(d,scanCache);
    }
    server->sendHeader("Cache-Control","no-store");server->send(200,"application/json; charset=utf-8",scanCache);
}
void connect_wifi() {
    IPAddress ip,gw,sn,ns;
    if(g_cfg_static_ip.length() && ip.fromString(g_cfg_static_ip) && gw.fromString(g_cfg_static_gateway) && sn.fromString(g_cfg_static_subnet) && ns.fromString(g_cfg_static_dns))
        WiFi.config(ip,gw,sn,ns);
    else WiFi.config(INADDR_NONE,INADDR_NONE,INADDR_NONE,INADDR_NONE);
    WiFi.begin(g_cfg_wifi_ssid.c_str(),g_cfg_wifi_pass.length()?g_cfg_wifi_pass.c_str():nullptr);
    lastConnect=millis();
}
void save_meters(JsonObjectConst o,bool legacy=false) {
    Mutation mutation;if(!mutation.ok){result(409,"device_busy");return;}
    MeterChannelConfig next[METER_CHANNELS];int zero;
    const char* error=portal::parse_meters(o,next,zero);
    if(error){result(400,error);return;}
    memcpy(g_meters,next,sizeof(next));
    if(zero>=0)meter_pulse_zero(zero);
    meter_pulse_flush();config_save();mqtt_meter_config();
    if(legacy){server->sendHeader("Location","/#config");server->send(303);}
    else result(200);
}
void save_settings(JsonObjectConst o) {
    Mutation mutation;if(!mutation.ok){result(409,"device_busy");return;}
    const char* error=portal::validate_settings(o);
    if(error){result(400,error);return;}
    portal::apply_settings(o);meter_pulse_flush();config_save();
    acceptJobs.store(false);rebootAt=millis()+1500;result(200);
}
const char* queue_ota(const String& url,const String& md5,long size) {
    Mutation mutation;if(!mutation.ok)return "device_busy";
    if(!url.startsWith("http://") || url.length()>512 || url.indexOf(' ')>=0)return "invalid_ota_url";
    if(md5.length()) {
        if(md5.length()!=32)return "invalid_md5";
        for(unsigned i=0;i<md5.length();++i)if(!isxdigit(static_cast<unsigned char>(md5[i])))return "invalid_md5";
    }
    const esp_partition_t* part=esp_ota_get_next_update_partition(nullptr);
    if(size<0 || (size>0 && (!part || uint32_t(size)>part->size)))return "invalid_file_size";
    if(WiFi.status()!=WL_CONNECTED)return "wifi_not_connected";
    g_ota_url=url;g_ota_md5=md5;g_ota_size=size;g_ota_state="queued";acceptJobs.store(false);return nullptr;
}
class PortalFallbackHandler:public RequestHandler {
public:
    bool canHandle(HTTPMethod,String)override{return true;}
    bool handle(WebServer& web,HTTPMethod method,String uri)override {
        web.sendHeader("Cache-Control","no-store");
        if(uri=="/favicon.ico" || uri=="/apple-touch-icon.png" || uri=="/apple-touch-icon-precomposed.png")web.send(204,"text/plain","");
        else if(method==HTTP_HEAD && uri=="/")web.send(200,"text/html; charset=utf-8","");
        else if(uri.startsWith("/api/"))web.send(404,"application/json","{\"success\":false,\"error\":\"not_found\"}");
        else if(method==HTTP_GET){web.sendHeader("Location","http://"+WiFi.softAPIP().toString()+":"+String(g_cfg_web_port)+"/");web.send(302,"text/plain","");}
        else web.send(404,"text/plain","Not Found");
        return true;
    }
};
}

bool web_portal_set_wifi_credentials_from_ble(const String& ssid,const String& pass) {
    // Both BLE and HTTP only enqueue. Configuration/radio work has one owner.
    if(!acceptJobs.load() || !wifiQueue || !ssid.length() || ssid.length()>32 || pass.length()>64)return false;
    if(wifiBusy.exchange(true))return false;
    if(!acceptJobs.load()){wifiBusy.store(false);return false;}
    WifiJob job={};strlcpy(job.ssid,ssid.c_str(),sizeof(job.ssid));strlcpy(job.pass,pass.c_str(),sizeof(job.pass));
    if(xQueueSend(wifiQueue,&job,0)!=pdTRUE){wifiBusy.store(false);return false;}
    return true;
}
const char* web_portal_wifi_connection_state() {
    if(wifiQueue && uxQueueMessagesWaiting(wifiQueue))return "queued";
    return wifiState;
}
void web_portal_init() {
    wifiQueue=xQueueCreate(1,sizeof(WifiJob));
    WiFi.persistent(false);WiFi.mode(WIFI_AP_STA);WiFi.setAutoReconnect(false);
    WiFi.softAP(ap_name().c_str(),g_cfg_ap_password.length()?g_cfg_ap_password.c_str():nullptr);
    dns.start(53,WiFi.softAPIP());
    server.reset(new PlainHttpWebServer(g_cfg_web_port));
    auto page=[](){server->sendHeader("Cache-Control","no-store");server->send_P(200,"text/html; charset=utf-8",WEB_PAGE);};
    for(const char* path:{"/","/config","/mqtt","/wifi","/pw","/connecting","/ota_web"})server->on(path,HTTP_GET,page);
    server->on("/api/status",HTTP_GET,send_status);
    // APP uses this route for both the device hotspot and its LAN address.
    server->on("/api/device-info",HTTP_GET,[]{
        server->sendHeader("Cache-Control","no-store");
        server->send(200,"application/json; charset=utf-8",andon_device_info_json());
    });
    server->on("/api/config",HTTP_GET,send_config);
    server->on("/api/config",HTTP_POST,[]{DynamicJsonDocument d(8192);if(body(d))save_settings(d.as<JsonObjectConst>());});
    server->on("/api/meters",HTTP_POST,[]{DynamicJsonDocument d(8192);if(body(d))save_meters(d.as<JsonObjectConst>());});
    server->on("/api/wifi/scan",HTTP_GET,[]{scan_status(server->arg("start")=="1");});
    server->on("/api/wifi/status",HTTP_GET,send_wifi_status);
    server->on("/api/wifi/connect",HTTP_POST,[]{
        StaticJsonDocument<512>d;if(!body(d))return;
        if(maintenance()){result(409,"device_busy");return;}
        if(!d["ssid"].is<const char*>() || !d["password"].is<const char*>()){result(400,"invalid_wifi_credentials");return;}
        String ssid=d["ssid"].as<String>(),pass=d["password"].as<String>();
        if(!ssid.length() || ssid.length()>32 || pass.length()>64){result(400,"invalid_wifi_credentials");return;}
        if(!web_portal_set_wifi_credentials_from_ble(ssid,pass)){result(409,"device_busy");return;}
        StaticJsonDocument<192>out;out["success"]=true;out["state"]="queued";json(202,out);
    });
    server->on("/api/reboot",HTTP_POST,[]{Mutation mutation;if(!mutation.ok){result(409,"device_busy");return;}meter_pulse_flush();rebootAt=millis()+1500;acceptJobs.store(false);result(200);});
    server->on("/api/ota",HTTP_POST,[]{
        StaticJsonDocument<1024>d;if(!body(d))return;
        if(!d["url"].is<const char*>() || (d.containsKey("md5")&&!d["md5"].is<const char*>()) || (d.containsKey("size")&&!d["size"].is<long>())){result(400,"invalid_ota_url");return;}
        const char* error=queue_ota(d["url"].as<String>(),d["md5"]|"",d["size"]|0L);result(error?400:202,error);
    });
    // Legacy links/form clients remain usable; the browser portal uses JSON only.
    server->on("/wifi_status",HTTP_GET,send_wifi_status);
    server->on("/scan_result",HTTP_GET,[]{scan_status(false);});
    server->on("/connect",HTTP_GET,[]{
        bool ok=!maintenance() && web_portal_set_wifi_credentials_from_ble(server->arg("ssid"),server->arg("pass"));
        server->send(ok?200:409,"text/plain; charset=utf-8",ok?"OK: 正在连接 WiFi":"ERROR: 参数无效或设备忙");
    });
    server->on("/webconnect",HTTP_GET,[]{
        if(maintenance() || !web_portal_set_wifi_credentials_from_ble(server->arg("ssid"),server->arg("pass"))){result(409,"device_busy");return;}
        server->sendHeader("Location","/#wifi");server->send(303);
    });
    server->on("/save_mqtt",HTTP_POST,[]{
        DynamicJsonDocument d(4096);JsonObject o=d.to<JsonObject>();
        for(auto* s=portal::settings();s->key;++s)if(server->hasArg(s->key))o[s->key]=server->arg(s->key);
        for(const char* key:{"mq_port","hb_s","rep_s"})if(server->hasArg(key))o[key]=server->arg(key).toInt();
        save_settings(o);
    });
    server->on("/save_meters",HTTP_POST,[]{
        DynamicJsonDocument d(8192);JsonArray list=d.createNestedArray("meters");
        for(int i=0;i<METER_CHANNELS;++i){
            String suffix=String(i);JsonObject m=list.createNestedObject();m["ch"]=i+1;
            m["enabled"]=server->arg("m_en"+suffix)=="1";
            m["name"]=server->hasArg("m_name"+suffix)?server->arg("m_name"+suffix):String(g_meters[i].name);
            m["initial_kwh"]=server->hasArg("m_init"+suffix)?server->arg("m_init"+suffix).toDouble():g_meters[i].initial_kwh;
            m["imp_kwh"]=server->hasArg("m_imp"+suffix)?server->arg("m_imp"+suffix).toInt():long(g_meters[i].imp_per_kwh);
            if(server->arg("m_zero"+suffix)=="1")d["zero_channel"]=i+1;
        }
        save_meters(d.as<JsonObjectConst>(),true);
    });
    server->on("/ota",HTTP_GET,[]{const char* e=queue_ota(server->arg("url"),server->arg("md5"),server->arg("size").toInt());result(e?400:202,e);});
    server->on("/reboot",HTTP_GET,[]{Mutation mutation;if(!mutation.ok){result(409,"device_busy");return;}meter_pulse_flush();rebootAt=millis()+1500;acceptJobs.store(false);result(200);});
    server->addHandler(new PortalFallbackHandler());server->begin();
    if(g_cfg_wifi_ssid.length())connect_wifi();
    AppLog.printf("[WEB] static portal at http://%s:%d/\n",WiFi.softAPIP().toString().c_str(),g_cfg_web_port);
}
void web_portal_loop() {
    dns.processNextRequest();server->handleClient();
    if(rebootAt && int32_t(millis()-rebootAt)>=0){meter_pulse_flush();ESP.restart();}
    acceptJobs.store(!maintenance());
    WifiJob job;
    if(wifiQueue && xQueueReceive(wifiQueue,&job,0)==pdTRUE){
        if(maintenance()){wifiState="failed";wifiError="device_busy";wifiBusy.store(false);}
        else {
            wifiSsid=job.ssid;wifiError="";wifiState="queued";
            g_cfg_wifi_ssid=job.ssid;g_cfg_wifi_pass=job.pass;config_save();
            wifiBeginAt=millis()+150;disconnectedObserved=false;
        }
    }
    if(!strcmp(wifiState,"queued") && int32_t(millis()-wifiBeginAt)>=0){
        stop_scan();WiFi.disconnect(false,false);wifiWasConnected=false;
        wifiState="disconnecting";wifiStarted=millis();
    }
    bool online=WiFi.status()==WL_CONNECTED;
    if(!strcmp(wifiState,"disconnecting")){
        if(!online){disconnectedObserved=true;connect_wifi();wifiState="connecting";}
        else if(uint32_t(millis()-wifiStarted)>=3000){wifiState="failed";wifiError="wifi_disconnect_failed";wifiBusy.store(false);lastConnect=millis();}
    }
    if(!strcmp(wifiState,"connecting")){
        if(online && disconnectedObserved && WiFi.SSID()==wifiSsid){wifiState="connected";wifiError="";wifiBusy.store(false);}
        else if(uint32_t(millis()-wifiStarted)>=30000){
            wifiState="failed";wifiBusy.store(false);lastConnect=millis();
            wifiError=WiFi.status()==WL_NO_SSID_AVAIL?"wifi_ssid_not_found":WiFi.status()==WL_CONNECT_FAILED?"wifi_auth_failed":"wifi_connection_timeout";
        }
    }
    if(online && !wifiWasConnected){backoff=15000;mqtt_setup();ntp_sync();}
    wifiWasConnected=online;
    if(!online && !wifiBusy.load() && !scanRunning && !maintenance() && g_cfg_wifi_ssid.length() && uint32_t(millis()-lastConnect)>=backoff){
        WiFi.disconnect(false,false);connect_wifi();backoff=backoff<60000?backoff*2:60000;
    }
}
String web_portal_sta_ip(){return WiFi.status()==WL_CONNECTED?WiFi.localIP().toString():String("");}
