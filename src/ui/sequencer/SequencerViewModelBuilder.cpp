#include "ui/sequencer/SequencerViewModelBuilder.hpp"

#include <config/PlatformCompat.hpp>
#include <oc/diagnostics/Performance.hpp>

#include "ui/sequencer/SequencerBottomActionStripViewModelBuilder.hpp"
#include "ui/sequencer/SequencerHeaderViewModelBuilder.hpp"
#include "ui/sequencer/SequencerLeftActionStripViewModelBuilder.hpp"
#include "ui/sequencer/SequencerPropertyOverlayViewModelBuilder.hpp"
#include "ui/sequencer/SequencerStepGridViewModelBuilder.hpp"

namespace core::ui::sequencer {

FLASHMEM SequencerHeaderBarProps buildHeaderBarProps(const SequencerViewModelSource& source) {
    OC_PERF_SCOPE(perfProjection, "ui.sequencer.projection.header");
    return buildSequencerHeaderBarProps(source);
}

FLASHMEM StepPropertySelectionOverlayProps buildPropertySelectionOverlayProps(
    const SequencerViewModelSource& source
) {
    OC_PERF_SCOPE(perfProjection, "ui.sequencer.projection.selector");
    return buildSequencerPropertySelectionOverlayProps(source);
}

FLASHMEM ContextActionStripProps buildLeftActionStripProps(const SequencerViewModelSource& source) {
    OC_PERF_SCOPE(perfProjection, "ui.sequencer.projection.left-actions");
    return buildSequencerLeftActionStripProps(source);
}

FLASHMEM ContextActionStripProps buildBottomActionStripProps(const SequencerViewModelSource& source) {
    OC_PERF_SCOPE(perfProjection, "ui.sequencer.projection.bottom-actions");
    return buildSequencerBottomActionStripProps(source);
}

FLASHMEM grid::StepGridFrameState buildStepGridProps(const SequencerViewModelSource& source) {
    OC_PERF_SCOPE(perfProjection, "ui.sequencer.projection.step-grid");
    return buildSequencerStepGridProps(source);
}

}  // namespace core::ui::sequencer
