#pragma once

namespace core::state {

class CoreState;

namespace diagnostics {

/** Installs opt-in signal labels used by the diagnostics firmware. */
void configureDebugLabels(CoreState& state);

}  // namespace diagnostics
}  // namespace core::state
