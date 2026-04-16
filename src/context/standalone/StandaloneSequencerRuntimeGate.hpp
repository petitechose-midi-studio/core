#pragma once

#include <cstdint>

namespace core::context::standalone {

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
    bool wasStandaloneActive
) {
    if (isStandaloneActive) {
        return {
            StandaloneSequencerRuntimeAction::UPDATE,
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
