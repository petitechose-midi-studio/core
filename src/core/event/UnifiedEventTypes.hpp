#pragma once

#include <stdint.h>

using EventType = uint16_t;
using EventCategoryType = uint8_t;

namespace EventCategory {
constexpr EventCategoryType SYSTEM = 0;
constexpr EventCategoryType USER_INPUT = 1;  // Note: "INPUT" collides with Arduino macro
constexpr EventCategoryType MIDI = 2;
constexpr EventCategoryType UI = 3;
constexpr EventCategoryType INTEGRATION = 4;
}  // namespace EventCategory

namespace SystemEvent {
constexpr EventType MODE_CHANGE = 4001;
constexpr EventType ERROR = 4002;
constexpr EventType BOOT_COMPLETE = 4003;

constexpr EventType PLUGIN_REGISTERED = 4004;
constexpr EventType PLUGIN_ACTIVATED = 4005;
constexpr EventType PLUGIN_DEACTIVATED = 4006;
constexpr EventType PLUGIN_ERROR = 4007;
}  // namespace SystemEvent

namespace InputEvent {
constexpr EventType ENCODER_CHANGED = 100;
constexpr EventType BUTTON_PRESS = 101;
constexpr EventType BUTTON_RELEASE = 102;
constexpr EventType BUTTON_LONG_PRESS = 5;
constexpr EventType BUTTON_COMBO = 6;
constexpr EventType BUTTON_DOUBLE_PRESS = 7;
}  // namespace InputEvent

namespace MidiEvent {
constexpr EventType CC = 2002;
constexpr EventType NOTE_ON = 2000;
constexpr EventType NOTE_OFF = 2001;
constexpr EventType PROGRAM_CHANGE = 2003;
constexpr EventType PITCH_BEND = 2004;
constexpr EventType MAPPING = 2005;
constexpr EventType SYSEX = 2006;
}  // namespace MidiEvent
