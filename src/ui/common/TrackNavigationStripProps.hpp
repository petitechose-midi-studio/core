#pragma once

#include <array>
#include <cstdint>

#include "state/StatusBarState.hpp"

namespace core::ui {

/**
 * Projection data for the shared 16-track navigation strip.
 *
 * Kept separate from the LVGL widget so state-to-props builders and native
 * tests can validate projection behavior without pulling renderer dependencies.
 */
struct TrackNavigationStripProps {
    static constexpr uint8_t TRACK_COUNT = core::state::StatusBarState::TRACK_COUNT;

    uint8_t activeTrack = 0;
    uint8_t previewTrack = 0;
    uint8_t addTrackIndex = TRACK_COUNT;
    uint16_t enabledMask = 0x0001;
    uint16_t mutedMask = 0;
    uint16_t selectedMask = 0;
    bool focusingTrack = false;
    bool selectingTrack = false;
    std::array<uint8_t, TRACK_COUNT> activity{};
};

}  // namespace core::ui
