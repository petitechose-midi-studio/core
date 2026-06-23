#pragma once

#include <cstddef>
#include <cstdint>

#include <config/PlatformCompat.hpp>

#include "context/standalone/SequencerOverlayPresenterTypes.hpp"
#include "state/sequencer/SequencerChordUiOps.hpp"
#include "state/sequencer/SequencerUiState.hpp"

namespace core::context::standalone::sequencer_overlay_presenter {

FLASHMEM uint32_t chordColor();

FLASHMEM void formatChordValue(
    char* out,
    size_t outSize,
    const core::state::sequencer::SequencerStepChordUiState& chord
);

FLASHMEM void formatChordPreviewNotes(
    char* out,
    size_t outSize,
    const core::state::sequencer::SequencerChordPreview& preview
);

FLASHMEM void populateChordDetailOverlay(
    StepEditRenderData& data,
    const core::state::sequencer::SequencerStepChordUiState& chord,
    core::state::sequencer::SequencerChordEditField focusedField,
    bool enabled
);

}  // namespace core::context::standalone::sequencer_overlay_presenter
