#pragma once

#include "ui/sequencer/SequencerViewModelBuilder.hpp"

namespace core::ui::sequencer {

StepPropertySelectionOverlayProps buildSequencerPropertySelectionOverlayProps(
    const SequencerViewModelSource& source
);

}  // namespace core::ui::sequencer
