#pragma once

#include "ui/sequencer/SequencerCcLaneGrid.hpp"
#include "ui/sequencer/SequencerViewModelBuilder.hpp"

namespace core::ui::sequencer {

SequencerCcLaneGridProps buildSequencerCcLaneGridProps(
    const SequencerViewModelSource& source
);

}  // namespace core::ui::sequencer
