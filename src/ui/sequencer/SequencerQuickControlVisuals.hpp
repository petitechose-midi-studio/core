#pragma once

#include <cstdint>

#include "state/sequencer/SequencerUiState.hpp"

namespace core::ui::sequencer::visual {

const char* quickControlIconGlyph(
    core::state::sequencer::PatternQuickControlItem item
);

uint32_t quickControlColor(
    core::state::sequencer::PatternQuickControlItem item
);

}  // namespace core::ui::sequencer::visual
