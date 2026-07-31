#pragma once

#include "ui/sequencer/SequencerPresetLibraryPresentation.hpp"

namespace core::ui::sequencer {

SequencerPresetLibraryPresentation
buildSequencerChordPresetPresentation(
    const core::state::sequencer::SequencerState& sequencer
);

}  // namespace core::ui::sequencer
