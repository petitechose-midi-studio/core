#pragma once

#include "state/sequencer/SequencerUiState.hpp"

namespace core::state::sequencer {

inline contextual::ContextActionSpec buildSequencerPresetLibraryActionSpec(
    const SequencerPresetLibrarySessionState& picker
) {
    const bool saveMode =
        picker.mode.get() == SequencerPresetLibraryMode::SAVE;
    const bool selectedNewAsset = picker.selectedItemIsNewAsset();
    const bool focusedAsset = picker.selectedItemIsExistingAsset();

    if (picker.libraryKind.get() == SequencerPresetLibraryKind::CHORD) {
        return buildSequencerChordPresetActionSpec(
            saveMode,
            selectedNewAsset,
            focusedAsset,
            picker.chord().target,
            picker.chord().descriptor
        );
    }
    return buildSequencerStepPresetActionSpec(
        saveMode,
        selectedNewAsset,
        focusedAsset,
        picker.step().target,
        picker.step().descriptor
    );
}

}  // namespace core::state::sequencer
