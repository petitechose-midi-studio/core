#pragma once

#include "state/CoreState.hpp"
#include "ui/sequencer/PatternQuickControls.hpp"
#include "ui/sequencer/SequencerHeaderBar.hpp"
#include "ui/sequencer/StepGridRenderTypes.hpp"
#include "ui/sequencer/StepPropertyStrip.hpp"
#include "ui/strip/ContextActionStrip.hpp"

namespace core::ui::sequencer {

SequencerHeaderBarProps buildHeaderBarProps(const core::state::CoreState& coreState);
PatternQuickControlsProps buildPatternQuickControlsProps(const core::state::CoreState& coreState);
StepPropertyStripProps buildStepPropertyStripProps(const core::state::CoreState& coreState);
ContextActionStripProps buildLeftActionStripProps(const core::state::CoreState& coreState);
ContextActionStripProps buildBottomActionStripProps(const core::state::CoreState& coreState);
grid::StepGridFrameState buildStepGridProps(const core::state::CoreState& coreState);

}  // namespace core::ui::sequencer
