#pragma once

#include "ui/sequencer/SequencerViewModelBuilder.hpp"

namespace core::ui::sequencer {

grid::StepGridFrameState buildSequencerStepGridProps(
    const SequencerViewModelSource& source
);

}  // namespace core::ui::sequencer
