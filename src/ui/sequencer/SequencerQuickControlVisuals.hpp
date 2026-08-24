#pragma once

#include <cstddef>
#include <cstdint>

#include "state/sequencer/SequencerClipGridState.hpp"
#include "state/sequencer/SequencerUiState.hpp"

namespace core::ui::sequencer::visual {

const char* quickControlIconGlyph(
    core::state::sequencer::PatternQuickControlItem item
);

uint32_t quickControlColor(
    core::state::sequencer::PatternQuickControlItem item
);

void formatLauncherFollowChoice(
    char* buffer,
    size_t size,
    core::state::sequencer::SequencerLauncherFollowChoice choice,
    bool scene
);

}  // namespace core::ui::sequencer::visual
