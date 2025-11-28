#pragma once

#include <Arduino.h>

#ifdef DEBUG_LOGS

/**
 * @brief Wait for Serial USB connection before logging (Teensy)
 * On Teensy, Serial USB is native - no begin() needed, baudrate ignored.
 * @param timeoutMs Maximum time to wait (default 2000ms)
 * @return true if Serial connected, false if timeout
 */
inline bool waitForSerial(unsigned long timeoutMs = 2000)
{
    unsigned long startMillis = millis();
    while (!Serial && (millis() - startMillis < timeoutMs)) {
        yield();
    }
    return Serial;
}

#define LOG(msg)           \
    do {                   \
        Serial.print(msg); \
    } while (0)
#define LOGF(...)                   \
    do {                            \
        Serial.printf(__VA_ARGS__); \
    } while (0)
#define LOGLN(msg)           \
    do {                     \
        Serial.println(msg); \
    } while (0)
#else
inline bool waitForSerial(unsigned long = 2000)
{
    return false;
}
#define LOG(msg) ((void)0)
#define LOGF(...) ((void)0)
#define LOGLN(msg) ((void)0)
#endif
