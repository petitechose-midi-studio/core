#include <Arduino.h>
#include "log/Macros.hpp"

#include "app/MidiStudioApp.hpp"

MidiStudioApp app(nullptr);

void setup() {
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
