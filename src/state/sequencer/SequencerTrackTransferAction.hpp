#pragma once

#include <cstdint>

#include "state/StructureClipboardPastePlan.hpp"
#include "state/contextual/ContextActionSpec.hpp"

namespace core::state::sequencer {

/**
 * Projects the Track copy/paste control from the same immutable transfer plan
 * used by the transaction. Presenters and handlers must not infer paste
 * availability or impact independently from this model.
 */
core::state::contextual::ContextActionSpec buildSequencerTrackTransferActionSpec(
    const core::state::ClipboardTransferPlan& plan,
    uint8_t focusedTrack,
    bool copyAvailable,
    uint16_t guardDurationMs
);

core::state::contextual::ContextActionReason contextualReasonForTrackTransfer(
    core::state::ClipboardTransferReason reason
);

}  // namespace core::state::sequencer
