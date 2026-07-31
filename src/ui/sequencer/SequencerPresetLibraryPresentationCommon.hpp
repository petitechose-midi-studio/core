#pragma once

#include <cstdint>

#include "ui/sequencer/SequencerPresetLibraryPresentation.hpp"

namespace core::ui::sequencer::preset_library_presentation_common {

struct ListConfig {
    const char* kindLabel = "Preset";
    const char* loadedFeedback = "Loaded";
    const char* queuedFeedback = "Queued";
    const char* warningFeedback = "Check impact";
    const char* compatibility = "";
    const char* idleMeta = "";
};

uint32_t mixRevision(uint32_t seed, uint32_t value);

const char* shortOperationLabel(
    const core::state::contextual::OperationFeedbackState& feedback
);

void formatList(
    SequencerPresetLibraryPresentation& data,
    const core::state::sequencer::SequencerPresetLibrarySessionState& picker,
    bool saveMode,
    const ListConfig& config
);

uint32_t baseRevision(
    const core::state::sequencer::SequencerPresetLibrarySessionState& picker
);

}  // namespace core::ui::sequencer::preset_library_presentation_common
