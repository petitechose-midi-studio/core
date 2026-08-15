#pragma once

#include "state/sequencer/DrumPatternState.hpp"
#include "ui/font/StandaloneIcons.hpp"

namespace core::ui::sequencer {

inline const char* drumLaneIconGlyph(
    core::state::sequencer::DrumLaneIcon icon
) {
    using Icon = core::state::sequencer::DrumLaneIcon;
    switch (icon) {
        case Icon::KICK: return standalone::icons::DRUM_KICK;
        case Icon::SNARE: return standalone::icons::DRUM_SNARE;
        case Icon::CLOSED_HAT: return standalone::icons::DRUM_CLOSED_HAT;
        case Icon::OPEN_HAT: return standalone::icons::DRUM_OPEN_HAT;
        case Icon::CLAP: return standalone::icons::DRUM_CLAP;
        case Icon::TOM: return standalone::icons::DRUM_TOM;
        case Icon::PERCUSSION: return standalone::icons::DRUM_PERCUSSION;
        case Icon::GENERIC:
        case Icon::COUNT:
        default: return standalone::icons::DRUM_GENERIC;
    }
}

}  // namespace core::ui::sequencer
