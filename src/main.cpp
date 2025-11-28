#include <Arduino.h>
#include "log/Macros.hpp"

#include "app/MidiStudioApp.hpp"

MidiStudioApp app(nullptr);

void setup() {
    app.setup();
    LOGLN("=== MIDI Studio ===");
    LOGLN("===   Core Dev  ===");
    LOG("Version: ");
    LOGLN(Core::VERSION);
}

void loop() {
    app.update();
}
