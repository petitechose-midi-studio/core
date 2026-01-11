#pragma once

/**
 * @file PlatformCompat.hpp
 * @brief Platform compatibility layer for Arduino/Native builds
 *
 * Provides no-op definitions for Arduino-specific memory attributes
 * when building for desktop (SDL) platforms.
 */

#ifdef ARDUINO
    #include <Arduino.h>
#else
    // Memory attributes - no-op on desktop
    #ifndef PROGMEM
        #define PROGMEM
    #endif
    #ifndef DMAMEM
        #define DMAMEM
    #endif
    #ifndef EXTMEM
        #define EXTMEM
    #endif

    // Standard types for desktop
    #include <cstdint>
    #include <cstddef>
#endif
