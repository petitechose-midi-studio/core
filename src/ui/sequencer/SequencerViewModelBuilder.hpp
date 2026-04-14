#pragma once

#include <oc/state/Signal.hpp>

#include "state/StructureSelectionState.hpp"
#include "state/StatusBarState.hpp"
#include "state/StructureClipboardState.hpp"
#include "state/TrackNavigationState.hpp"
#include "state/sequencer/SequencerState.hpp"
#include "state/sequencer/SequencerTrackBankState.hpp"
#include "ui/common/TrackNavigationStrip.hpp"
#include "ui/sequencer/SequencerBottomControls.hpp"
#include "ui/sequencer/SequencerHeaderBar.hpp"
#include "ui/sequencer/StepPropertyStrip.hpp"
#include "ui/sequencer/StepGridRenderTypes.hpp"
#include "ui/strip/ContextActionStrip.hpp"

namespace core::ui::sequencer {

struct SequencerViewModelSource {
    const core::state::sequencer::SequencerState& sequencer;
    const core::state::sequencer::SequencerTrackBankState& tracks;
    const core::state::TrackNavigationState& trackNavigation;
    const oc::state::Signal<
        core::state::StructureNavigationFocus,
        core::state::kStructureNavigationFocusMaxSubscribers>& navigationFocus;
    const oc::state::Signal<uint8_t, 8>& sharedTrackActive;
    const oc::state::Signal<uint16_t, 16>& sharedTrackEnabledMask;
    const core::state::StructureClipboardState& structureClipboard;
    const core::state::StatusBarState& statusBar;
};

SequencerHeaderBarProps buildHeaderBarProps(const SequencerViewModelSource& source);
SequencerBottomControlsProps buildBottomControlsProps(const SequencerViewModelSource& source);
StepPropertyStripProps buildStepPropertyStripProps(const SequencerViewModelSource& source);
ContextActionStripProps buildLeftActionStripProps(const SequencerViewModelSource& source);
ContextActionStripProps buildBottomActionStripProps(const SequencerViewModelSource& source);
grid::StepGridFrameState buildStepGridProps(const SequencerViewModelSource& source);

}  // namespace core::ui::sequencer
