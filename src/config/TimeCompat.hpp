#pragma once

#include <cstdint>

#ifdef ARDUINO
    #include <Arduino.h>
#else
    #include <chrono>
#endif

#include <oc/time/Time.hpp>

namespace core::time_compat {

inline uint32_t platformMillis() {
#ifdef ARDUINO
    return ::millis();
#else
    using namespace std::chrono;
    return static_cast<uint32_t>(
        duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count()
    );
#endif
}

inline uint32_t millis() {
    return oc::time::isConfigured() ? oc::time::millis() : platformMillis();
}

inline uint32_t platformMicros() {
#ifdef ARDUINO
    return ::micros();
#else
    using namespace std::chrono;
    return static_cast<uint32_t>(
        duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count()
    );
#endif
}

inline uint32_t micros() {
    return oc::time::isMicrosConfigured() ? oc::time::micros32() : platformMicros();
}

}  // namespace core::time_compat
