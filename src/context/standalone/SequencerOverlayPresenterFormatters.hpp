#pragma once

#include "context/standalone/SequencerOverlayPresenterTypes.hpp"

namespace core::context::standalone::sequencer_overlay_presenter {

// `data` owns the character buffers referenced by its overlay props. Keeping
// it address-stable is part of the rendering contract.
void buildStepEditRenderData(const Source& source, StepEditRenderData& data);
core::ui::ContextActionStripProps buildStepEditActionStripProps(const ActionSource& source);
PresetLibraryRenderData buildPresetLibraryRenderData(
    const Source& source
);
core::ui::ContextActionStripProps buildPresetLibraryActionStripProps(
    const Source& source
);

}  // namespace core::context::standalone::sequencer_overlay_presenter
