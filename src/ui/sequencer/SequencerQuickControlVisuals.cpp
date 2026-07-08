#include "ui/sequencer/SequencerQuickControlVisuals.hpp"

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

}  // namespace core::ui::sequencer::visual
