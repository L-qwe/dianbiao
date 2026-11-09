#pragma once
#include <cstdint>
struct FakeGpioRegisters {
    uint32_t in = 0;
    struct { uint32_t data = 0; } in1;
};
inline FakeGpioRegisters GPIO;
