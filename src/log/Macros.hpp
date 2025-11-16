#pragma once

#include <Arduino.h>

#ifdef DEBUG_LOGS
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
#define LOG(msg) ((void)0)
#define LOGF(...) ((void)0)
#define LOGLN(msg) ((void)0)
#endif
