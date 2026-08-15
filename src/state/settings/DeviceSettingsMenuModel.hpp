#pragma once

#include <array>
#include <cstdint>

#include "state/DeviceSettingsState.hpp"
#include "state/MidiNoteDisplayState.hpp"
#include "state/MidiSyncState.hpp"

namespace core::state::settings {

struct DeviceSettingsMenuRow {
    const char* label = "";
    const char* value = "";
    bool enabled = true;
};

struct DeviceSettingsMenuContext {
    core::state::MidiSyncMode mode = core::state::MidiSyncMode::AUTO;
    bool followTransport = true;
    uint16_t autoFallbackMs = 500;
    uint8_t autoLockClockCount = 6;
    core::midi::NoteOctaveConvention noteOctaveConvention =
        core::midi::DEFAULT_NOTE_OCTAVE_CONVENTION;
    core::state::ClockSourceActive activeSource = core::state::ClockSourceActive::INTERNAL;
    bool externalClockPresent = false;
};

struct DeviceSettingsMenuPage {
    static constexpr uint8_t MAX_ROWS = DeviceSettingsState::ROW_COUNT;

    const char* title = "Device Settings";
    std::array<char, 24> meta{};
    std::array<std::array<char, 16>, 2> valueBuffers{};
    std::array<DeviceSettingsMenuRow, MAX_ROWS> rows{};
    uint8_t rowCount = 0;
    uint8_t selectedIndex = 0;
    uint32_t dataRevision = 0;
};

constexpr uint8_t deviceSettingsRowCount();
const char* deviceSettingsRowLabel(uint8_t row);
DeviceSettingsMenuPage buildDeviceSettingsMenuPage(
    const core::state::DeviceSettingsState& state,
    const DeviceSettingsMenuContext& context
);

}  // namespace core::state::settings
