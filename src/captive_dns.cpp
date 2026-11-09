#include "captive_dns.h"
#include "app_log.h"

bool CaptiveDnsServer::start(uint16_t port, const IPAddress& address) {
    for (size_t i = 0; i < 4; ++i) address_[i] = address[i];
    // DNS redirection is only needed on the hotspot address.
    started_ = udp_.begin(address, port) == 1;
    if (!started_) udp_.stop();
    return started_;
}

void CaptiveDnsServer::dropped(int length) {
    ++dropped_;
    const uint32_t now = millis();
    if (dropped_ == 1 || (uint32_t)(now - last_drop_log_) >= 30000) {
        last_drop_log_ = now;
        AppLog.printf("[DNS] rejected malformed/unsupported packet bytes=%d total=%lu\n",
            length, (unsigned long)dropped_);
    }
}

void CaptiveDnsServer::processNextRequest() {
    if (!started_) return;
    const int length = udp_.parsePacket();
    if (length <= 0) return;
    if ((size_t)length > sizeof(packet_)) {
        udp_.flush();
        dropped(length);
        return;
    }
    const int received = udp_.read(packet_, length);
    udp_.flush(); // discard any unread remainder before the next datagram
    if (received != length) { dropped(length); return; }
    const size_t size = captive_dns::reply(packet_, length, sizeof(packet_), address_);
    if (!size) { dropped(length); return; }
    if (!udp_.beginPacket(udp_.remoteIP(), udp_.remotePort())) return;
    if (udp_.write(packet_, size) == size) udp_.endPacket();
}
