#pragma once

#include <cstdint>

#include "state/macro/MacroWorkflow.hpp"
#include "state/modulation/ProjectControlDomainState.hpp"
#include "state/modulation/ProjectModulationDomainOps.hpp"

namespace core::state {
struct CoreState;
}

namespace core::handler::recorded_shape_capture_detail {

void markProjectMutatedFromCoreState(void* context);

bool sameSource(
    const core::state::modulation::ModulatorSourceState& lhs,
    const core::state::modulation::ModulatorSourceState& rhs
);
bool sameCurve(
    const core::state::modulation::ProjectCurveRecord& lhs,
    const core::state::modulation::ProjectCurveRecord& rhs
);
bool sameActivationPlan(
    const core::state::macro::MacroDestinationActivationPlan& lhs,
    const core::state::macro::MacroDestinationActivationPlan& rhs
);
core::state::modulation::ProjectCurveSpec curveSpec(
    const core::state::modulation::ProjectCurveRecord& record
);
const core::state::modulation::ProjectPackedCurvePoint* curvePoints(
    const core::state::modulation::ProjectCurveArena& arena,
    const core::state::modulation::ProjectCurveRecord& record
);
bool pointHash(
    const core::state::modulation::ProjectPackedCurvePoint* points,
    uint16_t count,
    uint64_t& out
);
core::state::modulation::ProjectModulationResult resultWith(
    core::state::modulation::ProjectModulationStatus status
);
float validTempo(float value);

}  // namespace core::handler::recorded_shape_capture_detail
