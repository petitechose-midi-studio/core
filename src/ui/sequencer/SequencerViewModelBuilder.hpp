#pragma once

#include <oc/state/Signal.hpp>

#include "state/StructureSelectionState.hpp"
#include "state/StatusBarState.hpp"
#include "state/StructureClipboardState.hpp"
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
    const oc::state::Signal<
        core::state::StructureNavigationFocus,
        core::state::kStructureNavigationFocusMaxSubscribers>& navigationFocus;
    const core::state::StructureClipboardState& structureClipboard;
    const core::state::StatusBarState& statusBar;
};

SequencerHeaderBarProps buildHeaderBarProps(const SequencerViewModelSource& source);
TrackNavigationStripProps buildTrackNavigationStripProps(const SequencerViewModelSource& source);
SequencerBottomControlsProps buildBottomControlsProps(const SequencerViewModelSource& source);
StepPropertyStripProps buildStepPropertyStripProps(const SequencerViewModelSource& source);
ContextActionStripProps buildLeftActionStripProps(const SequencerViewModelSource& source);
ContextActionStripProps buildBottomActionStripProps(const SequencerViewModelSource& source);
grid::StepGridFrameState buildStepGridProps(const SequencerViewModelSource& source);

}  // namespace core::ui::sequencer
