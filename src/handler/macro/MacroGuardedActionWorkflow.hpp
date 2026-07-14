#pragma once

#include <cstdint>

#include "state/MacroEditState.hpp"

namespace core::handler::macro {

/** Shared tap-to-hold lifecycle for Macro contextual actions. */
class MacroGuardedActionWorkflow final {
public:
    static bool begin(
        core::state::MacroEditState& state,
        core::state::MacroContextButton button,
        core::state::contextual::ContextActionId holdAction,
        core::state::contextual::ContextEntityRef source,
        core::state::contextual::ContextEntityRef target,
        uint32_t nowMs,
        uint16_t durationMs
    );
    static bool update(core::state::MacroEditState& state, uint32_t nowMs);
    static core::state::contextual::GuardedActionRelease release(
        core::state::MacroEditState& state,
        core::state::MacroContextButton button,
        uint32_t nowMs
    );
    static void complete(
        core::state::MacroEditState& state,
        bool applied,
        uint32_t nowMs
    );
    static void cancel(core::state::MacroEditState& state, uint32_t nowMs);
};

}  // namespace core::handler::macro
