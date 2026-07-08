#include "ui/sequencer/SequencerViewModelBuilder.hpp"

#include <config/PlatformCompat.hpp>

#include "ui/sequencer/SequencerBottomActionStripViewModelBuilder.hpp"
#include "ui/sequencer/SequencerHeaderViewModelBuilder.hpp"
#include "ui/sequencer/SequencerLeftActionStripViewModelBuilder.hpp"
#include "ui/sequencer/SequencerPropertyOverlayViewModelBuilder.hpp"
#include "ui/sequencer/SequencerStepGridViewModelBuilder.hpp"

namespace core::ui::sequencer {

FLASHMEM SequencerHeaderBarProps buildHeaderBarProps(const SequencerViewModelSource& source) {
    return buildSequencerHeaderBarProps(source);
}

FLASHMEM StepPropertySelectionOverlayProps buildPropertySelectionOverlayProps(
    const SequencerViewModelSource& source
) {
    return buildSequencerPropertySelectionOverlayProps(source);
}

FLASHMEM ContextActionStripProps buildLeftActionStripProps(const SequencerViewModelSource& source) {
    return buildSequencerLeftActionStripProps(source);
}

FLASHMEM ContextActionStripProps buildBottomActionStripProps(const SequencerViewModelSource& source) {
    return buildSequencerBottomActionStripProps(source);
}

FLASHMEM grid::StepGridFrameState buildStepGridProps(const SequencerViewModelSource& source) {
    return buildSequencerStepGridProps(source);
}

}  // namespace core::ui::sequencer
