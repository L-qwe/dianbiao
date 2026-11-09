#pragma once

#include <Arduino.h>
#include <cmath>
#include <cstdio>
#include <cstdint>

// Round once to 0.001 kWh, then print integer digits. ESP32 String(double, 3)
// uses dtostrf(), whose digit extraction can lose the last digit at a tie.
// This formats a snapshot only; never round stored energy or pulse increments.
inline String format_energy_kwh(double kwh) {
    if (!std::isfinite(kwh) || kwh < 0) return "null";
    const double rounded = std::round(kwh * 1000.0);
    char text[336]; // Enough for a finite double in fixed notation.
    if (rounded < static_cast<double>(UINT64_MAX)) {
        const uint64_t milli = static_cast<uint64_t>(rounded);
        snprintf(text, sizeof(text), "%llu.%03u",
                 static_cast<unsigned long long>(milli / 1000),
                 static_cast<unsigned>(milli % 1000));
    } else {
        // At this magnitude double has no fractional-kWh resolution. Avoid an
        // overflowing integer conversion and retain a valid JSON number.
        snprintf(text, sizeof(text), "%.3f", kwh);
    }
    return String(text);
}
