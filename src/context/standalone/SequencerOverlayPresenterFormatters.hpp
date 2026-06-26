#pragma once

#include "context/standalone/SequencerOverlayPresenterTypes.hpp"

namespace core::context::standalone::sequencer_overlay_presenter {

StepEditRenderData buildStepEditRenderData(const Source& source);
core::ui::ContextActionStripProps buildStepEditActionStripProps(const ActionSource& source);
StepPresetPickerRenderData buildStepPresetPickerRenderData(
    const Source& source
);
core::ui::ContextActionStripProps buildStepPresetActionStripProps(
    const Source& source
);

}  // namespace core::context::standalone::sequencer_overlay_presenter
