#pragma once

#include "state/StatusBarState.hpp"
#include "state/StructureSelectionState.hpp"
#include "state/TrackNavigationState.hpp"
#include "ui/common/TrackNavigationStripProps.hpp"

namespace core::ui {

/**
 * Focused read source for the global track strip projection.
 *
 * This keeps the model independent from the global app state while preserving
 * the exact slices needed to render active, preview, selection, and activity
 * props.
 */
struct GlobalTrackNavigationStripSource {
    const core::state::TrackNavigationState& trackNavigation;
    core::state::StructureNavigationFocus structureNavigationFocus =
        core::state::StructureNavigationFocus::PAGE;
    uint16_t sharedTrackEnabledMask = 0x0001;
    uint8_t sharedTrackActive = 0;
    const core::state::StatusBarState& statusBar;
};

TrackNavigationStripProps buildGlobalTrackNavigationStripProps(
    const GlobalTrackNavigationStripSource& source
);
bool globalTrackNavigationStripPropsEqual(
    const TrackNavigationStripProps& lhs,
    const TrackNavigationStripProps& rhs
);

}  // namespace core::ui
