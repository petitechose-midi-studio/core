#pragma once

#include "ui/sequencer/SequencerStructureWorkspace.hpp"
#include "ui/sequencer/SequencerViewModelBuilder.hpp"

namespace core::ui::sequencer {

SequencerStructureWorkspaceProps buildSequencerStructureWorkspaceProps(
    const SequencerViewModelSource& source
);

}  // namespace core::ui::sequencer
