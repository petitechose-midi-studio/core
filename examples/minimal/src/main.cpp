#include <Arduino.h>

#include "app/MidiStudioApp.hpp"
#include "log/Macros.hpp"

// Core framework test without any plugins
MidiStudioApp app(nullptr);

void setup() {
    LOGLN("=== MIDI Studio Core - Minimal Example ===");
    LOGLN("Version: " Core::VERSION);
    LOGLN("No plugins loaded - testing core framework only");
    LOGLN("=== System Ready ===");
}

void loop() {
    app.update();
}
