#pragma once

#if defined(MS_UX_RECORDER)

#include "state/contextual/GuardedActionState.hpp"
#include "state/contextual/OperationFeedbackState.hpp"

namespace oc::core::input {
struct InputBindingTraceEvent;
}

namespace core::state::sequencer {
struct SequencerPresetLibrarySessionState;
}

namespace core::validation::ux {
struct SemanticUxContext;
}

namespace core::context::standalone::ux {

const char* sequencerUxContextActionReasonName(
    core::state::contextual::ContextActionReason reason
);
const char* sequencerUxOperationOutcomeName(
    core::state::contextual::OperationFeedbackStatus status
);
const char* sequencerUxGuardedActionOutcomeName(
    core::state::contextual::GuardedActionPhase phase
);

}  // namespace core::context::standalone::ux

#endif
