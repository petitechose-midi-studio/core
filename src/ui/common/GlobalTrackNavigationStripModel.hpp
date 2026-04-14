#pragma once

#include "state/CoreState.hpp"
#include "state/shared/StructureSlotOps.hpp"
#include "ui/common/TrackNavigationStrip.hpp"

namespace core::ui {

TrackNavigationStripProps buildGlobalTrackNavigationStripProps(const core::state::CoreState& state);
bool globalTrackNavigationStripPropsEqual(
    const TrackNavigationStripProps& lhs,
    const TrackNavigationStripProps& rhs
);

}  // namespace core::ui
