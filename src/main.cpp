#include <Arduino.h>

#include "app/MidiStudioApp.hpp"
#include "log/Macros.hpp"

MidiStudioApp app(nullptr);

void setup() {
    LOGLN("=== MIDI Studio ===");
    LOGLN("===   Core Dev  ===");
    LOG("Version: ");
    LOGLN(Core::VERSION);
    app.setup();
}

void loop() { app.update(); }
