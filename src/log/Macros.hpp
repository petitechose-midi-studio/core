#pragma once

#include <Arduino.h>

#ifdef DEBUG_LOGS

// Initialiser Serial avant les constructeurs globaux
// S'exécute automatiquement dès que ce header est inclus
namespace
{
    __attribute__((constructor(101))) void initSerialForLogging()
    {
        // Attendre que Serial soit prêt (timeout 3 secondes)
        unsigned long startMillis = millis();
        while (!Serial && (millis() - startMillis < 5000))
        {
            // Attendre la connexion du moniteur série
        }
    }
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
#define LOG(msg) ((void)0)
#define LOGF(...) ((void)0)
#define LOGLN(msg) ((void)0)
#endif
