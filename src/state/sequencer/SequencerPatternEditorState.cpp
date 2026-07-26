#include "state/sequencer/SequencerPatternEditorState.hpp"

#include <config/PlatformCompat.hpp>

namespace core::state::sequencer {

FLASHMEM void SequencerPatternEditorState::bump() {
    ++revision;
    // Reuse the lifecycle signal as the retained-state invalidation edge;
    // framework notifications are coalesced by owner/slot.
    active.notify();
}

FLASHMEM void SequencerPatternEditorState::open(
    uint8_t track,
    uint8_t firstStep
) {
    ownerTrack = track;
    windowStart = static_cast<uint8_t>((firstStep / 8U) * 8U);
    focusedField = SequencerPatternEditorField::LENGTH;
    focusedLayer = SequencerPatternEditorLayer::NOTES;
    navigationMode = SequencerPatternEditorNavigationMode::FIELDS;
    active.set(true);
    bump();
}

FLASHMEM void SequencerPatternEditorState::close() {
    if (!active.get() &&
        navigationMode == SequencerPatternEditorNavigationMode::FIELDS) {
        return;
    }
    active.set(false);
    navigationMode = SequencerPatternEditorNavigationMode::FIELDS;
    bump();
}

FLASHMEM void SequencerPatternEditorState::reset() {
    active.set(false);
    focusedField = SequencerPatternEditorField::LENGTH;
    focusedLayer = SequencerPatternEditorLayer::NOTES;
    navigationMode = SequencerPatternEditorNavigationMode::FIELDS;
    windowStart = 0;
    ownerTrack = 0;
    bump();
}

}  // namespace core::state::sequencer
