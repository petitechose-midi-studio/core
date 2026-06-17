#pragma once

#include <array>
#include <cstddef>

#include "state/sequencer/SequencerUiState.hpp"

namespace core::state::sequencer {

/**
 * Presentation order for sequencer pattern quick controls.
 *
 * The enum values are stable domain identifiers; this table defines display
 * order and labels without coupling UI code to enum ordinal order.
 */
struct PatternQuickControlSpec {
    PatternQuickControlItem item;
    const char* label;
};

using PatternQuickControlSpecs = std::array<PatternQuickControlSpec, 5>;

inline constexpr PatternQuickControlSpecs QUICK_CONTROL_VISUAL_ORDER = {
    PatternQuickControlSpec{PatternQuickControlItem::LENGTH, "Length"},
    PatternQuickControlSpec{PatternQuickControlItem::DIVISION, "Division"},
    PatternQuickControlSpec{PatternQuickControlItem::OFFSET, "Offset"},
    PatternQuickControlSpec{PatternQuickControlItem::SWING, "Swing"},
    PatternQuickControlSpec{PatternQuickControlItem::NUDGE, "Nudge"},
};

inline constexpr const char* quickControlLabel(PatternQuickControlItem item) {
    for (const auto& spec : QUICK_CONTROL_VISUAL_ORDER) {
        if (spec.item == item) {
            return spec.label;
        }
    }
    return QUICK_CONTROL_VISUAL_ORDER.front().label;
}

inline constexpr std::size_t quickControlOrderIndex(PatternQuickControlItem item) {
    for (std::size_t i = 0; i < QUICK_CONTROL_VISUAL_ORDER.size(); ++i) {
        if (QUICK_CONTROL_VISUAL_ORDER[i].item == item) {
            return i;
        }
    }
    return 0;
}

inline constexpr PatternQuickControlItem quickControlAtOrderIndex(std::size_t index) {
    return QUICK_CONTROL_VISUAL_ORDER[index < QUICK_CONTROL_VISUAL_ORDER.size() ? index : 0]
        .item;
}

}  // namespace core::state::sequencer
