#pragma once

#include "ui/sequencer/SequencerPresetLibraryPresentation.hpp"

namespace core::ui::sequencer {

SequencerPresetLibraryPresentation
buildSequencerPatternPresetPresentation(
    const core::state::sequencer::SequencerState& sequencer
);

}  // namespace core::ui::sequencer
