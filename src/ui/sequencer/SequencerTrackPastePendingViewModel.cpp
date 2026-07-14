#include "ui/sequencer/SequencerTrackPastePendingViewModel.hpp"

#include <config/PlatformCompat.hpp>

namespace core::ui::sequencer {

FLASHMEM SequencerTrackPastePendingViewModel
buildSequencerTrackPastePendingViewModel(
    const core::state::ClipboardTransferPlan& plan
) {
    if (plan.reason != core::state::ClipboardTransferReason::PASTE_PENDING) {
        return {};
    }
    return {
        .visible = true,
        .label = "Paste pending",
    };
}

}  // namespace core::ui::sequencer
