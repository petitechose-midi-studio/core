#pragma once

#include <oc/state/Signal.hpp>

#include "state/StructureNavigationState.hpp"
#include "state/StatusBarState.hpp"
#include "state/StructureClipboardState.hpp"
#include "state/TrackNavigationState.hpp"
#include "state/project/ProjectNavigationState.hpp"
#include "state/project/ProjectTrackState.hpp"
#include "state/sequencer/SequencerState.hpp"
#include "state/sequencer/SequencerTrackActivationQueue.hpp"
#include "state/sequencer/SequencerTrackBankState.hpp"
#include "ui/common/TrackNavigationStrip.hpp"
#include "ui/sequencer/SequencerHeaderBar.hpp"
#include "ui/sequencer/StepPropertySelectionOverlay.hpp"
#include "ui/sequencer/StepGridRenderTypes.hpp"
#include "ui/strip/ContextActionStrip.hpp"

namespace core::ui::sequencer {

/**
 * Projects sequencer domain state into immutable view props.
 *
 * Builders read SequencerState, track navigation, clipboard, and status signals
 * to produce header, control, action strip, and step-grid props without mutating
 * state or touching LVGL objects.
 */
struct SequencerViewModelSource {
    const core::state::sequencer::SequencerState& sequencer;
    const core::state::sequencer::SequencerTrackBankState& tracks;
    const core::state::project::ProjectTrackState& projectTracks;
    const core::state::TrackNavigationState& trackNavigation;
    const oc::state::Signal<
        core::state::StructureNavigationFocus,
        core::state::kStructureNavigationFocusMaxSubscribers>& navigationFocus;
    const oc::state::Signal<uint8_t, 8>& sharedTrackActive;
    const oc::state::Signal<uint16_t, 16>& sharedTrackEnabledMask;
    const core::state::StructureClipboardState& structureClipboard;
    const core::state::StatusBarState& statusBar;
    const core::state::project::ProjectNavigationState& projectNavigation;
    const core::state::sequencer::SequencerTrackActivationQueue& trackActivations;
};

inline bool sequencerPreviewingEmptyTrack(
    const SequencerViewModelSource& source
) {
    return source.navigationFocus.get() ==
               core::state::StructureNavigationFocus::TRACK &&
           source.trackNavigation.previewAddSlot.get();
}

SequencerHeaderBarProps buildHeaderBarProps(const SequencerViewModelSource& source);
StepPropertySelectionOverlayProps buildPropertySelectionOverlayProps(
    const SequencerViewModelSource& source
);
ContextActionStripProps buildLeftActionStripProps(const SequencerViewModelSource& source);
ContextActionStripProps buildBottomActionStripProps(const SequencerViewModelSource& source);
grid::StepGridFrameState buildStepGridProps(const SequencerViewModelSource& source);

}  // namespace core::ui::sequencer
