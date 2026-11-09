#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// Bounded, in-place DNS responder for the configuration hotspot.
// No allocations, label copies or dereferencing DNS compression pointers.
namespace captive_dns {
constexpr size_t PACKET_CAPACITY = 512;

inline uint16_t u16(const uint8_t* p) {
    return (uint16_t(p[0]) << 8) | p[1];
}

// Returns zero for malformed/unsupported packets, without changing the input.
// Only one ordinary question and an optional EDNS0 OPT record are accepted.
inline size_t reply(uint8_t* p, size_t length, size_t capacity, const uint8_t ip[4]) {
    if (!p || !ip || length < 17 || length > capacity || length > PACKET_CAPACITY)
        return 0;
    if ((p[2] & 0xfa) || (p[3] & 0x0f) || u16(p + 4) != 1 ||
        u16(p + 6) != 0 || u16(p + 8) != 0 || u16(p + 10) > 1)
        return 0; // response, non-query opcode, truncated query, RCODE or extra sections

    size_t pos = 12;
    for (;;) {
        if (pos >= length) return 0;
        const size_t label = p[pos++];
        if (label > 63 || label > length - pos) return 0;
        pos += label;
        // Include the terminating zero in the 255-byte wire-format name limit.
        if (pos - 12 > 255) return 0;
        if (!label) break;
    }
    if (length - pos < 4) return 0;
    const uint16_t type = u16(p + pos), cls = u16(p + pos + 2);
    const size_t question_end = pos + 4;
    if (u16(p + 10) == 0) {
        if (question_end != length) return 0;
    } else {
        // OPT: root owner, type 41, payload size, extended RCODE/version/flags,
        // RDLENGTH and bounded options. We omit OPT in our small legacy response.
        const size_t remain = length - question_end;
        const uint8_t* opt = p + question_end;
        if (remain < 11 || opt[0] != 0 || u16(opt + 1) != 41 ||
            opt[5] != 0 || opt[6] != 0 || u16(opt + 9) != remain - 11)
            return 0;
        size_t option = question_end + 11;
        while (option < length) {
            if (length - option < 4) return 0;
            size_t bytes = u16(p + option + 2);
            option += 4;
            if (bytes > length - option) return 0;
            option += bytes;
        }
    }
    // AAAA and other types get a valid empty answer, not an incorrect A record.
    const bool answer = cls == 1 && (type == 1 || type == 255);
    const size_t output_length = question_end + (answer ? 16 : 0);
    if (output_length > capacity || output_length > PACKET_CAPACITY) return 0;

    p[2] = 0x84 | (p[2] & 1); // QR, AA, preserve RD; recursion is unavailable
    p[3] = 0;
    p[6] = 0; p[7] = answer ? 1 : 0;
    p[8] = p[9] = p[10] = p[11] = 0;
    if (answer) {
        const uint8_t record[12] = {
            0xc0, 0x0c, 0, 1, 0, 1, 0, 0, 0, 60, 0, 4
        };
        memcpy(p + question_end, record, sizeof(record));
        memcpy(p + question_end + sizeof(record), ip, 4);
    }
    return output_length;
}
}
