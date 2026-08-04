#pragma once

#include <cstddef>
#include <cstdint>

#include <config/PlatformCompat.hpp>

#include "state/sequencer/SequencerChordUiOps.hpp"
#include "state/sequencer/SequencerUiState.hpp"
#include "ui/sequencer/SequencerStepEditOverlay.hpp"

namespace core::context::standalone::sequencer_chord_field_presentation {

FLASHMEM const char* modeLabel(
    oc::note::sequencer::StepSequencerChordMode mode
);
FLASHMEM const char* shapeLabel(
    oc::note::sequencer::StepSequencerChordHarmony harmony
);
FLASHMEM const char* sourceLabel(
    core::state::sequencer::SequencerChordSourceChoice choice
);

FLASHMEM const char* label(
    core::state::sequencer::SequencerChordEditField field
);
FLASHMEM const char* icon(
    core::state::sequencer::SequencerChordEditField field
);
FLASHMEM uint32_t color(
    core::state::sequencer::SequencerChordEditField field
);
FLASHMEM core::ui::SequencerStepEditVisualSlot visualSlot(
    core::state::sequencer::SequencerChordEditField field
);

FLASHMEM void formatValue(
    char* out,
    size_t outSize,
    core::state::sequencer::SequencerChordEditField field,
    const core::state::sequencer::SequencerStepChordUiState& chord
);
FLASHMEM void formatFormula(
    char* out,
    size_t outSize,
    const core::state::sequencer::SequencerStepChordUiState& chord
);
FLASHMEM void formatContext(
    char* out,
    size_t outSize,
    const core::state::sequencer::SequencerStepChordUiState& chord
);
FLASHMEM void formatSource(
    char* out,
    size_t outSize,
    const core::state::sequencer::SequencerStepChordUiState& chord
);
FLASHMEM void formatFormulaVoice(
    char* out,
    size_t outSize,
    uint8_t voiceIndex,
    const core::state::sequencer::SequencerStepChordUiState& chord
);
FLASHMEM void formatFormulaVoiceInterval(
    char* out,
    size_t outSize,
    uint8_t voiceIndex,
    const core::state::sequencer::SequencerStepChordUiState& chord
);
FLASHMEM bool formulaVoiceActive(
    uint8_t voiceIndex,
    const core::state::sequencer::SequencerStepChordUiState& chord
);

}  // namespace core::context::standalone::sequencer_chord_field_presentation
