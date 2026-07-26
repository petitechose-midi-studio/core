#pragma once

#include <ms/ui/widget/KeyValueSparkline.hpp>

#include "state/modulation/ProjectControlState.hpp"

namespace core::ui::modulation::sparkline {

/** Graphical authored signature; excludes name, routing, trigger and runtime. */
[[nodiscard]] uint32_t sourceGeometryRevision(
    const core::state::modulation::ProjectControlState& control,
    const core::state::modulation::ModulatorSourceState& source
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
