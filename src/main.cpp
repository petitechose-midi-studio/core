
#include <Arduino.h>

#include "app/MidiStudioApp.hpp"
#include "log/Macros.hpp"

MidiStudioApp app;

void setup() {
    LOGLN("=== MIDI Studio - Starting ===");
    LOGF("Version: %d.%d.%d\n", 0, 9, 0);
    LOGLN("=== System Ready ===");
}

void loop() {
    app.update();
}