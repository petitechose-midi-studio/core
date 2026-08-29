#include "ui/sequencer/SequencerQuickControlVisuals.hpp"

#include <cstdio>

#include "ui/font/StandaloneIcons.hpp"
#include "ui/theme/StandaloneTheme.hpp"

namespace core::ui::sequencer::visual {

namespace theme = standalone::theme;
using QuickItem = core::state::sequencer::PatternQuickControlItem;

const char* quickControlIconGlyph(QuickItem item) {
    switch (item) {
        case QuickItem::LENGTH:
            return standalone::icons::LENGTH;
        case QuickItem::DIVISION:
            return standalone::icons::DIVISION;
        case QuickItem::SWING:
            return standalone::icons::SWING;
        case QuickItem::NUDGE:
            return standalone::icons::NOTE_PROP_NUDGE;
        case QuickItem::OFFSET:
        default:
            return standalone::icons::OFFSET;
    }
}

uint32_t quickControlColor(QuickItem item) {
    switch (item) {
        case QuickItem::LENGTH:
            return theme::color::STEP_LENGTH;
        case QuickItem::DIVISION:
            return theme::color::STEP_DIVISION;
        case QuickItem::SWING:
            return theme::color::STEP_SWING;
        case QuickItem::NUDGE:
            return theme::color::STEP_PATTERN_NUDGE;
        case QuickItem::OFFSET:
        default:
            return theme::color::STEP_OFFSET;
    }
}

const char* launcherQuickActionIconGlyph(
    core::state::sequencer::ClipWorkspaceQuickAction action
) {
    using Action = core::state::sequencer::ClipWorkspaceQuickAction;
    switch (action) {
        case Action::LENGTH:
            return standalone::icons::LENGTH;
        case Action::FOLLOW:
            return standalone::icons::ACTION_PLACE_TARGET;
        case Action::QUANTIZE:
            return standalone::icons::CLOCK_SYNC;
        case Action::EDIT:
        case Action::COUNT:
        default:
            return standalone::icons::CLIP;
    }
}

void formatLauncherFollowChoice(
    char* buffer,
    size_t size,
    core::state::sequencer::SequencerLauncherFollowChoice choice,
    bool scene
) {
    using Choice = core::state::sequencer::SequencerLauncherFollowChoice;
    if (buffer == nullptr || size == 0U) return;
    switch (choice) {
        case Choice::NONE:
            std::snprintf(buffer, size, "None");
            return;
        case Choice::NEXT:
            std::snprintf(buffer, size, "Next");
            return;
        case Choice::PREVIOUS:
            std::snprintf(buffer, size, "Previous");
            return;
        case Choice::FIRST:
            std::snprintf(buffer, size, "First");
            return;
        case Choice::RANDOM_OTHER:
            std::snprintf(buffer, size, "Random other");
            return;
        case Choice::RANDOM_ANY:
            std::snprintf(buffer, size, "Random any");
            return;
        default:
            break;
    }
    const uint8_t slot = core::state::sequencer::
        sequencerLauncherFollowTargetSlot(choice);
    if (slot >= core::state::sequencer::SequencerClipGridState::SLOT_COUNT) {
        std::snprintf(buffer, size, "None");
        return;
    }
    std::snprintf(
        buffer,
        size,
        "%s %u",
        scene ? "Scene" : "Clip",
        static_cast<unsigned>(slot + 1U)
    );
}

}  // namespace core::ui::sequencer::visual
