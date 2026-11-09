#pragma once
#include <WiFiUdp.h>
#include "captive_dns_packet.h"

// Owned and polled by loopTask only.
class CaptiveDnsServer {
public:
    bool start(uint16_t port, const IPAddress& address);
    void processNextRequest();
private:
    WiFiUDP udp_;
    bool started_ = false;
    uint8_t address_[4] = {};
    uint8_t packet_[captive_dns::PACKET_CAPACITY] = {};
    uint32_t dropped_ = 0, last_drop_log_ = 0;
    void dropped(int length);
};
