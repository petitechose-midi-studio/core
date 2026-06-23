#include "context/standalone/SequencerOverlayPresenter.hpp"

#include <config/PlatformCompat.hpp>

#include "context/standalone/SequencerOverlayPresenterFormatters.hpp"
#include "ui/sequencer/SequencerStepEditOverlay.hpp"

namespace core::context::standalone {

SequencerOverlayPresenter::SequencerOverlayPresenter(
    StateRefs stateRefs,
    core::ui::SequencerStepEditOverlay& stepEditOverlay,
    core::ui::ContextActionStrip& stepEditActionStrip
)
    : state_refs_(stateRefs)
    , step_edit_overlay_(stepEditOverlay)
    , step_edit_action_strip_(stepEditActionStrip) {}

FLASHMEM void SequencerOverlayPresenter::bind() {
    step_edit_watcher_.watchAll(
        [this]() { renderStepEdit(); },
        state_refs_.sequencer.stepEdit.visible,
        state_refs_.sequencer.stepEdit.stepIndex,
        state_refs_.sequencer.stepEdit.focusedRow,
        state_refs_.sequencer.stepEdit.localVariationEditActive,
        state_refs_.sequencer.stepEdit.chordEditor.active,
        state_refs_.sequencer.stepEdit.chordEditor.focusedField,
        state_refs_.sequencer.pattern.enabledMask,
        state_refs_.sequencer.pattern.stepDataRevision,
        state_refs_.sequencer.pattern.patternScaleRevision,
        state_refs_.sequencer.pattern.graphRevision,
        state_refs_.sequencer.contentView.revision,
        state_refs_.tracks.projectScaleRevisionSignal()
    );
    step_edit_action_watcher_.watchAll(
        [this]() { renderStepEditActionStrip(); },
        state_refs_.structureClipboard.revision,
        state_refs_.sequencer.stepEdit.contextHold.action,
        state_refs_.sequencer.stepEdit.contextHold.startedAtMs
    );
}

FLASHMEM void SequencerOverlayPresenter::renderStepEdit() {
    auto data = core::context::standalone::sequencer_overlay_presenter::buildStepEditRenderData({
        state_refs_.sequencer,
        state_refs_.tracks,
    });
    if (!data.visible) {
        step_edit_overlay_.render({.visible = false});
        step_edit_action_strip_.render({.visible = false});
        return;
    }

    step_edit_overlay_.render(data.overlayProps);
    renderStepEditActionStrip();
}

FLASHMEM void SequencerOverlayPresenter::renderStepEditActionStrip() {
    if (!state_refs_.sequencer.stepEdit.visible.get()) {
        step_edit_action_strip_.render({.visible = false});
        return;
    }

    step_edit_action_strip_.render(
        core::context::standalone::sequencer_overlay_presenter::buildStepEditActionStripProps({
            state_refs_.sequencer,
            state_refs_.tracks,
            state_refs_.structureClipboard,
        })
    );
}

}  // namespace core::context::standalone
