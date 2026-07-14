#pragma once

#include "state/StructureClipboardPastePlan.hpp"

namespace core::ui::sequencer {

struct SequencerTrackPastePendingViewModel {
    bool visible = false;
    const char* label = nullptr;
};

SequencerTrackPastePendingViewModel buildSequencerTrackPastePendingViewModel(
    const core::state::ClipboardTransferPlan& plan
);

}  // namespace core::ui::sequencer
