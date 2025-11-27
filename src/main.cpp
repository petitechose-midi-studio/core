#include <Arduino.h>
#include "log/Macros.hpp"

#include "app/MidiStudioApp.hpp"

MidiStudioApp app(nullptr);

void setup() {
    LOGLN("=== MIDI Studio ===");
    LOGLN("===   Core Dev  ===");
    LOG("Version: ");
    LOGLN(Core::VERSION);

    app.setup();
}

void loop() {
    app.update();
}
