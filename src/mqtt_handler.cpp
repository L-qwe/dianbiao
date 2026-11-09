/**
 * @file mqtt_handler.cpp
 * @brief MQTT 连接/心跳/电量上报/指令处理实现
 */
#include "app_log.h"
#include "mqtt_handler.h"
#include "app_config.h"
#include "meter_pulse.h"
#include "ntp_time.h"
#include "ota_handler.h"
#include "network_timing.h"
#include <atomic>
#include <ArduinoJson.h>

class BoundedMqttTransport:public WiFiClient {
public:
    using WiFiClient::connect;
    int connect(const char* host,uint16_t port)override{return WiFiClient::connect(host,port,1000);}
    int connect(IPAddress ip,uint16_t port)override{return WiFiClient::connect(ip,port,1000);}
};
static BoundedMqttTransport s_wifi_client;
enum class ConnectState:uint8_t {idle,running,succeeded,failed};
static std::atomic<ConnectState> s_connect_state{ConnectState::idle};
static std::atomic<bool> s_online{false};
static TaskHandle_t s_connect_task=nullptr;
static String s_broker,s_connect_cid,s_connect_user,s_connect_pass;
static int s_port=1883;
static bool s_first_attempt=true;

static PubSubClient mqtt(s_wifi_client);

String g_dev_mac    = "";
String g_dev_mac_nc = "";
String g_topic_pub  = "";
String g_topic_sub  = "";

String g_ota_url  = "";
String g_ota_md5  = "";
long   g_ota_size = 0;

static bool          s_setup_done   = false;
static uint32_t s_last_attempt = 0;
static unsigned long s_last_hb      = 0;
static unsigned long s_last_report  = 0;
static uint32_t      s_report_index = 0;

// ── JSON 字符串转义 ─────────────────────────────────────────────
static String json_escape(const String& value) {
    String out;
    out.reserve(value.length() + 8);
    for (size_t i = 0; i < value.length(); i++) {
        const char c = value[i];
        switch (c) {
            case '\"': out += F("\\\""); break;
            case '\\': out += F("\\\\"); break;
            case '\b': out += F("\\b"); break;
            case '\f': out += F("\\f"); break;
            case '\n': out += F("\\n"); break;
            case '\r': out += F("\\r"); break;
            case '\t': out += F("\\t"); break;
            default:   out += c; break;
        }
    }
    return out;
}

// ── topic {MAC} 展开 ────────────────────────────────────────────
static String expand_topic(const String& pattern, const String& def_prefix) {
    String t = pattern;
    t.replace("{MAC}", g_dev_mac_nc);
    if (t.length() == 0) t = def_prefix + g_dev_mac_nc;
    return t;
}

// Parse explicit commands only. Publish and subscribe topics may be identical;
// reflected telemetry and command words inside names/URLs must not execute.
static void handle_command(const char* topic, const uint8_t* payload, unsigned int len) {
    if (topic && String(topic) != g_topic_sub) return;
    StaticJsonDocument<1024> doc;
    if (deserializeJson(doc, payload, len) || !doc.is<JsonObject>()) return;
    const char* command = doc["cmd"];
    if (!command) command = doc["msg_type"];
    if (!command) return;
    const bool ota = !strcmp(command, "ota") || !strcmp(command, "upgrade");
    if (!ota && strcmp(command, "report_now") && strcmp(command, "get_config") && strcmp(command, "reboot")) return;
    AppLog.printf("[MQTT] cmd: %s\n", command);
    if (ota) {
        const char* rawUrl = doc["file_url"];
        if (!rawUrl) rawUrl = doc["url"];
        if (!rawUrl) return;
        String url = rawUrl;
        if (!url.startsWith("http://") || url.length() > 512) return;
        if (doc.containsKey("file_md5") && !doc["file_md5"].is<const char*>()) return;
        if (doc.containsKey("file_size") && !doc["file_size"].is<long>()) return;
        const long size = doc["file_size"] | 0L;
        if (size < 0) return;
        g_ota_url = url; g_ota_md5 = doc["file_md5"] | ""; g_ota_size = size;
        AppLog.printf("[MQTT] OTA queued: %s size=%ld\n", url.c_str(), size);
    } else if (!strcmp(command, "report_now")) {
        mqtt_heartbeat();
        mqtt_meter_report();
    } else if (!strcmp(command, "get_config")) {
        mqtt_meter_config();
    } else if (!strcmp(command, "reboot")) {
        AppLog.println(F("[MQTT] 收到重启指令"));
        delay(300);
        meter_pulse_flush();
        ESP.restart();
    }
}

static void mqtt_on_message(char* topic, uint8_t* payload, unsigned int len) {
    handle_command(topic, payload, len);
}

// Only this worker uses PubSubClient during a handshake. All other access is
// gated by s_connect_state; its release/acquire handoff returns ownership to loopTask.
static bool mqtt_connect_now(void) {
    AppLog.printf("[MQTT] connecting %s:%d\n",s_broker.c_str(),s_port);
    bool ok=s_connect_user.length()
        ? mqtt.connect(s_connect_cid.c_str(),s_connect_user.c_str(),s_connect_pass.c_str())
        : mqtt.connect(s_connect_cid.c_str());
    if(ok && mqtt.subscribe(g_topic_sub.c_str())) return true;
    mqtt.disconnect();s_wifi_client.stop();return false;
}
static void mqtt_connect_worker(void*) {
    for(;;){
        ulTaskNotifyTake(pdTRUE,portMAX_DELAY);
        bool ok=mqtt_connect_now();
        s_connect_state.store(ok?ConnectState::succeeded:ConnectState::failed);
    }
}
static void start_connect(uint32_t now) {
    s_last_attempt=now;s_first_attempt=false;
    if(!s_connect_task && xTaskCreatePinnedToCore(mqtt_connect_worker,"mqtt_connect",8192,nullptr,1,&s_connect_task,0)!=pdPASS){
        AppLog.println("[MQTT] connect worker unavailable; retry in 10s");return;
    }
    s_connect_cid=g_cfg_mqtt_client_id.length()?g_cfg_mqtt_client_id:String("DianBiao_")+g_dev_mac_nc;
    s_connect_user=g_cfg_mqtt_username;s_connect_pass=g_cfg_mqtt_password;
    s_online.store(false);s_connect_state.store(ConnectState::running);xTaskNotifyGive(s_connect_task);
}

void mqtt_setup(void) {
    if (s_setup_done) return;
    s_setup_done = true;

    g_dev_mac    = WiFi.macAddress();
    g_dev_mac_nc = g_dev_mac; g_dev_mac_nc.replace(":", "");
    g_topic_pub  = expand_topic(g_cfg_mqtt_pub_topic, F("dianbiao/meter/"));
    g_topic_sub  = expand_topic(g_cfg_mqtt_sub_topic, F("dianbiao/meter/"));

    s_broker=g_cfg_mqtt_broker;s_port=g_cfg_mqtt_port;
    mqtt.setServer(s_broker.length()?s_broker.c_str():"0.0.0.0",s_port);
    if (!mqtt.setBufferSize(MQTT_BUFFER_BYTES)) AppLog.println("[MQTT] buffer allocation failed; publish will retry");
    AppLog.printf("[MQTT] buffer=%u bytes\n", (unsigned)mqtt.getBufferSize());
    mqtt.setKeepAlive(60);
    mqtt.setSocketTimeout(2);
    mqtt.setCallback(mqtt_on_message);

    // First handshake is scheduled by maintain(), never inside HTTP/Wi-Fi setup.
}

void mqtt_maintain(void) {
    if(!s_setup_done)return;
    ConnectState job=s_connect_state.load();
    if(job==ConnectState::running)return;
    if(job==ConnectState::succeeded || job==ConnectState::failed){
        s_connect_state.store(ConnectState::idle);
        s_online.store(job==ConnectState::succeeded && WiFi.status()==WL_CONNECTED);
        AppLog.println(s_online.load()?"[MQTT] connected":"[MQTT] connect failed; retry in 10s");
        if(s_online.load())mqtt_heartbeat();
    }
    if(!s_broker.length() || WiFi.status()!=WL_CONNECTED){
        s_wifi_client.stop();s_online.store(false);return;
    }
    s_online.store(mqtt.connected());
    if(!s_online.load()){
        uint32_t now=millis();
        if(network_timing::reconnect_due(now,s_last_attempt,s_first_attempt))start_connect(now);
        return;
    }
    mqtt.loop();s_online.store(mqtt.connected());
    uint32_t now=millis();
    if(uint32_t(now-s_last_hb)>=g_cfg_heartbeat_interval_ms)mqtt_heartbeat();
    if(g_cfg_report_interval_s && uint32_t(now-s_last_report)>=g_cfg_report_interval_s*1000UL)mqtt_meter_report();
}

bool mqtt_connected(void) {
    return s_connect_state.load()==ConnectState::idle && s_online.load();
}

bool mqtt_pub(const String& payload) {
    if (!mqtt_connected() || payload.length() == 0) return false;
    const size_t needed = 5U + 2U + g_topic_pub.length() + payload.length();
    if (needed > 65535U) { AppLog.println("[MQTT] packet exceeds client limit"); return false; }
    if (needed > mqtt.getBufferSize()) {
        const uint16_t capacity = needed > MQTT_BUFFER_BYTES ? uint16_t(needed) : MQTT_BUFFER_BYTES;
        if (!mqtt.setBufferSize(capacity)) {
            AppLog.printf("[MQTT] buffer allocation failed (need=%u bytes)\n", (unsigned)needed);
            return false;
        }
    }
    const bool ok = mqtt.publish(g_topic_pub.c_str(), reinterpret_cast<const uint8_t*>(payload.c_str()), payload.length());
    if (!ok) AppLog.printf("[MQTT] publish failed (%u bytes)\n", (unsigned)payload.length());
    return ok;
}

// Heartbeats and periodic energy reports share the same compact totals.
static void append_meter_totals(String& body) {
    body += F(",\"meters\":[");
    for (int i = 0; i < METER_CHANNELS; i++) {
        if (i) body += ',';
        body += F("{\"ch\":");
        body += String(i + 1);
        body += F(",\"name\":\"");
        body += json_escape(g_meters[i].name);
        body += F("\",\"energy_kwh\":");
        body += meter_energy_kwh_text(i);
        body += '}';
    }
    body += ']';
}

void mqtt_heartbeat(void) {
    s_last_hb = millis();
    if (!mqtt_connected()) return;

    String body;
    body.reserve(1600);
    body += F("{\"");
    body += g_dev_mac_nc;
    body += F("_online\":true,\"msg_type\":\"heartbeat\",\"dev_mac\":\"");
    body += g_dev_mac;
    body += F("\",\"device_name\":\"");
    body += json_escape(g_cfg_device_name);
    body += F("\",\"version\":\"");
    body += FIRMWARE_VERSION;
    body += F("\",\"ip\":\"");
    body += WiFi.localIP().toString();
    body += F("\",\"rssi\":");
    body += String(WiFi.RSSI());
    body += F(",\"time\":\"");
    body += ntp_time_str();
    body += F("\",\"ntp_ok\":");
    body += ntp_ok() ? F("true") : F("false");
    body += F(",\"uptime_s\":");
    body += String((unsigned long)(millis() / 1000UL));
    body += F(",\"free_heap\":");
    body += String((unsigned)ESP.getFreeHeap());
    body += F(",\"report_interval_s\":");
    body += String((unsigned long)g_cfg_report_interval_s);
    append_meter_totals(body);
    body += '}';
    mqtt_pub(body);
}

void mqtt_meter_report(void) {
    s_last_report = millis();
    if (!mqtt_connected()) return;

    String body;
    body.reserve(1600);
    body += F("{\"msg_type\":\"meter_energy\",\"dev_mac\":\"");
    body += g_dev_mac;
    body += F("\",\"time\":\"");
    body += ntp_time_str();
    body += F("\",\"report_index\":");
    body += String((unsigned)++s_report_index);
    append_meter_totals(body);
    body += '}';
    mqtt_pub(body);
}

void mqtt_meter_config(void) {
    if (!mqtt_connected()) return;

    String body;
    body.reserve(2000);
    body += F("{\"msg_type\":\"meter_config\",\"dev_mac\":\"");
    body += g_dev_mac;
    body += F("\",\"time\":\"");
    body += ntp_time_str();
    body += F("\",\"meters\":[");

    for (int i = 0; i < METER_CHANNELS; i++) {
        const MeterChannelConfig& m = g_meters[i];
        if (i) body += ',';
        body += F("{\"ch\":");
        body += String(i + 1);
        body += F(",\"gpio\":");
        body += String(METER_GPIO[i]);
        body += F(",\"name\":\"");
        body += json_escape(m.name);
        body += F("\",\"enabled\":");
        body += m.enabled ? F("true") : F("false");
        body += F(",\"imp_kwh\":");
        body += String(m.imp_per_kwh);
        body += F(",\"initial_kwh\":");
        body += String(m.initial_kwh, 3);
        body += '}';
    }
    body += F("]}");
    mqtt_pub(body);
}
