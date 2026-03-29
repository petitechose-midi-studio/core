#pragma once

#include <cstdint>

#ifdef ARDUINO
    #include <Arduino.h>
#else
    #include <chrono>
#endif

namespace core::time_compat {

inline uint32_t millis() {
#ifdef ARDUINO
    return ::millis();
#else
    using namespace std::chrono;
    return static_cast<uint32_t>(
        duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count()
    );
#endif
}

inline uint32_t micros() {
#ifdef ARDUINO
    return ::micros();
#else
    using namespace std::chrono;
    return static_cast<uint32_t>(
        duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count()
    );
#endif
}

}  // namespace core::time_compat
