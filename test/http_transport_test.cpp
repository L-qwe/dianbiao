#include "Arduino.h"
#include <cstdarg>
struct RecordingLog{
 String text;
 void printf(const char* fmt,...){char buffer[512];va_list args;va_start(args,fmt);vsnprintf(buffer,sizeof(buffer),fmt,args);va_end(args);text+=buffer;}
}AppLog;
#include <cassert>
#include <deque>
#include <memory>
#include <vector>
#include <iostream>
enum HTTPClientStatus { HC_NONE,HC_WAIT_READ,HC_WAIT_CLOSE };
int g_cfg_web_port=80;
struct FakeAddress { String toString() const { return "192.168.4.1"; } };
struct ClientBytes { std::vector<uint8_t> data; size_t offset=0; bool stopped=false; };
class WiFiClient {
public:
    std::shared_ptr<ClientBytes> bytes;
    WiFiClient()=default;
    explicit WiFiClient(std::shared_ptr<ClientBytes> b):bytes(std::move(b)) {}
    explicit operator bool() const { return bytes && !bytes->stopped; }
    bool connected() const { return bool(*this); }
    size_t available() const { return connected() ? bytes->data.size()-bytes->offset : 0; }
    int peek() const { return available() ? bytes->data[bytes->offset] : -1; }
    int read(uint8_t* out,size_t size) {
        size=std::min(size,available());memcpy(out,bytes->data.data()+bytes->offset,size);bytes->offset+=size;return int(size);
    }
    void stop() { if(bytes)bytes->stopped=true; }
    FakeAddress localIP() const { return {}; }
};
class WiFiServer {
public:
    std::deque<WiFiClient> queue;
    WiFiClient available() { if(queue.empty())return {};auto c=queue.front();queue.pop_front();return c; }
};
class WebServer {
public:
    explicit WebServer(int) {}
    virtual void handleClient() {
        ++baseCalls;
        if(_currentClient.available()) {
            ++parses;parsed.assign(_currentClient.bytes->data.begin()+_currentClient.bytes->offset,_currentClient.bytes->data.end());
            _currentClient.stop();_currentClient={};_currentStatus=HC_NONE;
        }
    }
    std::shared_ptr<ClientBytes> enqueue(std::vector<uint8_t> data) {
        auto state=std::make_shared<ClientBytes>();state->data=std::move(data);_server.queue.emplace_back(state);return state;
    }
    unsigned baseCalls=0,parses=0;
    std::string parsed;
protected:
    WiFiServer _server;
    WiFiClient _currentClient;
    HTTPClientStatus _currentStatus=HC_NONE;
    unsigned long _statusChange=0;
    bool _nullDelay=true;
    std::unique_ptr<int> _currentUpload,_currentRaw;
};
// PRODUCTION_HTTP_TRANSPORT
int main() {
    PlainHttpWebServer server(80);
    auto tls=server.enqueue({0x16,0x03,0x01,0x00,0xa0,0x01});server.handleClient();
    assert(tls->stopped && server.parses==0 && server.baseCalls==0);
    assert(AppLog.text.find("TLS/HTTPS on HTTP port")!=std::string::npos);
    assert(AppLog.text.find("http://192.168.4.1:80/")!=std::string::npos);
    const std::string request="GET /api/status HTTP/1.1\r\nHost: 192.168.4.1\r\n\r\n";
    server.enqueue({request.begin(),request.end()});server.handleClient();
    assert(server.parses==1 && server.parsed==request);
    auto log=AppLog.text;
    for(unsigned i=0;i<100;++i) { server.enqueue({0x16,0x03,0x03});server.handleClient(); }
    assert(AppLog.text==log && server.parses==1);
    fakeMillis+=5000;server.enqueue({0xff,0x00,0x01});server.handleClient();
    assert(AppLog.text.find("non-HTTP bytes")!=std::string::npos && server.parses==1);
    auto slow=server.enqueue({});server.handleClient();assert(!slow->stopped);
    slow->data.assign(request.begin(),request.end());server.handleClient();assert(server.parses==2 && server.parsed==request);
    std::cout<<"PASS HTTP transport: TLS/binary rejected before parser, URL diagnosis/log throttling, valid/fragmented HTTP and post-rejection recovery\n";
}
