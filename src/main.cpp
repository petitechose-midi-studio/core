#include <Arduino.h>

#include "app/MidiStudioApp.hpp"
#include "log/Macros.hpp"

MidiStudioApp app(nullptr);

void setup() {
    delay(1000);
    LOGLN("=======================================");
    LOGLN("======== MIDI Studio - Core Dev =======");
    LOGLN("=======================================");
    LOG("Version : ");
    LOGLN(Core::VERSION);
    LOGLN("=======================================");
    LOGLN("============= System Boot =============");
    LOGLN("=======================================");
}

void loop() {
    app.update();
}
