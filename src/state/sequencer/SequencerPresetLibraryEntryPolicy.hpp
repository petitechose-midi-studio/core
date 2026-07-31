#pragma once

#include <cstdint>

#include "state/sequencer/SequencerState.hpp"

namespace core::state::sequencer::preset_library_entry_policy {

enum class EntryKind : uint8_t {
    NONE = 0,
    STEP,
    CHORD,
};

inline EntryKind entryKind(const SequencerState& sequencer) {
    if (!sequencer.stepEdit.visible.get() ||
        sequencer.stepContentDraft.exitPromptVisible.get()) {
        return EntryKind::NONE;
    }

    const auto& chordEditor = sequencer.stepEdit.chordEditor;
    if (chordEditor.active.get()) {
        const auto subEditor = chordEditor.subEditor.get();
        return sequencer.stepContentDraft.active.get() &&
                       !subEditor.formulaEditorActive &&
                       !subEditor.sourceSelectorActive
            ? EntryKind::CHORD
            : EntryKind::NONE;
    }

    return !sequencer.stepContentDraft.active.get()
        ? EntryKind::STEP
        : EntryKind::NONE;
}

inline bool canOpenStepPresets(const SequencerState& sequencer) {
    return entryKind(sequencer) == EntryKind::STEP;
}

inline bool canOpenChordPresets(const SequencerState& sequencer) {
    return entryKind(sequencer) == EntryKind::CHORD;
}

}  // namespace core::state::sequencer::preset_library_entry_policy
