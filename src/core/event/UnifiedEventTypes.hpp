#pragma once

#include <stdint.h>

using EventType = uint16_t;
using EventCategoryType = uint8_t;

namespace EventCategory {
constexpr EventCategoryType System = 0;
constexpr EventCategoryType Input = 1;
constexpr EventCategoryType MIDI = 2;
constexpr EventCategoryType UI = 3;
constexpr EventCategoryType Integration = 4;
}  // namespace EventCategory

namespace SystemEvent {
constexpr EventType ViewChange = 4000;
constexpr EventType ModeChange = 4001;
constexpr EventType Error = 4002;
constexpr EventType BootComplete = 4003;

constexpr EventType PluginRegistered = 4004;
constexpr EventType PluginActivated = 4005;
constexpr EventType PluginDeactivated = 4006;
constexpr EventType PluginError = 4007;
}  // namespace SystemEvent

namespace InputEvent {
constexpr EventType EncoderChanged = 100;
constexpr EventType ButtonPress = 101;
constexpr EventType ButtonRelease = 102;
constexpr EventType ButtonLongPress = 5;
constexpr EventType ButtonCombo = 6;
constexpr EventType ButtonDoublePress = 7;
}  // namespace InputEvent

namespace MidiEvent {
constexpr EventType CC = 2002;
constexpr EventType NoteOn = 2000;
constexpr EventType NoteOff = 2001;
constexpr EventType ProgramChange = 2003;
constexpr EventType PitchBend = 2004;
constexpr EventType Mapping = 2005;
constexpr EventType SysEx = 2006;
}  // namespace MidiEvent
