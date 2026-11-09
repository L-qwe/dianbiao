#pragma once
#include <stdint.h>
namespace network_timing {
inline bool reconnect_due(uint32_t now,uint32_t last,bool first) {
    return first || uint32_t(now-last)>=10000U;
}
}
