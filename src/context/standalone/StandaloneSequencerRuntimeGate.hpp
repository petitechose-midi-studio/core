#pragma once

#include <cstdint>

namespace core::context::standalone {

/**
 * Pure decision gate for the standalone runtime owner in `main.cpp`.
 *
 * The pre-context hook calls this on each loop. It updates while standalone is
 * active and its control-plane deadline is due, stops once when leaving
 * standalone, and otherwise stays idle. Keeping this as a header-level pure
 * function makes both cadence admission and lifecycle testable without
 * constructing the runtime service.
 */
enum class StandaloneSequencerRuntimeAction : uint8_t {
    NONE = 0,
    UPDATE,
    STOP,
};

struct StandaloneSequencerRuntimeDecision {
    StandaloneSequencerRuntimeAction action = StandaloneSequencerRuntimeAction::NONE;
    bool nextWasStandaloneActive = false;
};

constexpr StandaloneSequencerRuntimeDecision decideStandaloneSequencerRuntimeAction(
    bool isStandaloneActive,
    bool wasStandaloneActive,
    bool updateDue = true
) {
    if (isStandaloneActive) {
        return {
            updateDue
                ? StandaloneSequencerRuntimeAction::UPDATE
                : StandaloneSequencerRuntimeAction::NONE,
            true,
        };
    }

    if (wasStandaloneActive) {
        return {
            StandaloneSequencerRuntimeAction::STOP,
            false,
        };
    }

    return {
        StandaloneSequencerRuntimeAction::NONE,
        false,
    };
}

}  // namespace core::context::standalone
