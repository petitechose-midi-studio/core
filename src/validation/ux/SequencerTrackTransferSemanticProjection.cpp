#include "validation/ux/SequencerTrackTransferSemanticProjection.hpp"

#include <config/PlatformCompat.hpp>

namespace core::validation::ux {

FLASHMEM const char* sequencerTrackTransferSemanticReason(
    core::state::contextual::ContextActionReason reason
) {
    using Reason = core::state::contextual::ContextActionReason;
    switch (reason) {
        case Reason::EMPTY_CLIPBOARD: return "clipboard_empty";
        case Reason::WRONG_PAYLOAD: return "wrong_payload";
        case Reason::INVALID_PAYLOAD: return "invalid_payload";
        case Reason::SAME_SOURCE_TARGET: return "same_track";
        case Reason::OUT_OF_RANGE: return "out_of_range";
        case Reason::CAPACITY: return "capacity";
        case Reason::PENDING: return "paste_pending";
        case Reason::NO_ROUTE: return "no_route";
        case Reason::HISTORY_UNAVAILABLE: return "history_unavailable";
        case Reason::ALLOCATION_UNAVAILABLE: return "allocation_unavailable";
        case Reason::NONE: return nullptr;
        default: return "blocked";
    }
}

}  // namespace core::validation::ux
