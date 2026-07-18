#pragma once

#include <ms/ui/widget/VirtualListKeyValueOverlay.hpp>

#include "state/modulation/ProjectControlState.hpp"

namespace core::ui::modulation::sparkline {

/** Current source value from the compiled Project runtime, or zero if absent. */
[[nodiscard]] float liveValue(
    const core::state::modulation::ProjectControlState& control,
    core::state::modulation::ModulatorId sourceId
);

/**
 * Allocation-free compact source preview shared by Macro and Project lists.
 *
 * Shape comes from authored authority, the marker from runtime authority and
 * Recorded Shape polarity from its Project curve domain.
 */
[[nodiscard]] ms::ui::KeyValueSparkline buildSource(
    const core::state::modulation::ProjectControlState& control,
    const core::state::modulation::ModulatorSourceState& source
);

}  // namespace core::ui::modulation::sparkline
