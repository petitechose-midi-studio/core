#include "context/standalone/SequencerOverlayPresenter.hpp"

#include <config/PlatformCompat.hpp>
#include <ms/ui/widget/VirtualListSelectorOverlay.hpp>

#include "context/standalone/SequencerOverlayPresenterFormatters.hpp"
#include "ui/sequencer/SequencerStepEditOverlay.hpp"

namespace core::context::standalone {

FLASHMEM SequencerOverlayPresenter::~SequencerOverlayPresenter() {}

FLASHMEM SequencerOverlayPresenter::SequencerOverlayPresenter(
    StateRefs stateRefs,
    core::ui::SequencerStepEditOverlay& stepEditOverlay,
    core::ui::ContextActionStrip& stepEditActionStrip,
    ms::ui::VirtualListSelectorOverlay& stepPresetOverlay,
    core::ui::ContextActionStrip& stepPresetActionStrip
)
    : state_refs_(stateRefs)
    , step_edit_overlay_(stepEditOverlay)
    , step_edit_action_strip_(stepEditActionStrip)
    , step_preset_overlay_(stepPresetOverlay)
    , step_preset_action_strip_(stepPresetActionStrip)
    , render_scheduler_(
          core::ui::renderSchedulerDebugLabel("SequencerOverlay"),
          &SequencerOverlayPresenter::drainRenderQueue,
          this
      ) {}

FLASHMEM bool SequencerOverlayPresenter::bind() {
    bool bound = render_scheduler_.valid();
    step_edit_watcher_.bind<&SequencerOverlayPresenter::requestStepEditRender>(
        *this, 0, "SequencerOverlay.stepEdit"
    );
    bound = step_edit_watcher_.watchAll(
        state_refs_.sequencer.stepEdit.visible,
        state_refs_.sequencer.stepEdit.stepIndex,
        state_refs_.sequencer.stepEdit.focusedRow,
        state_refs_.sequencer.stepEdit.localVariationEditActive,
        state_refs_.sequencer.stepEdit.chordEditor.active,
        state_refs_.sequencer.stepEdit.chordEditor.focusedField,
        state_refs_.sequencer.stepPresetPicker.visible,
        state_refs_.sequencer.pattern.enabledMask,
        state_refs_.sequencer.pattern.stepDataRevision,
        state_refs_.sequencer.pattern.patternScaleRevision,
        state_refs_.sequencer.pattern.graphRevision,
        state_refs_.sequencer.contentView.revision,
        state_refs_.tracks.projectScaleRevisionSignal()
    ) && bound;
    step_edit_action_watcher_.bind<&SequencerOverlayPresenter::requestStepEditActionsRender>(
        *this, 1, "SequencerOverlay.stepEditActions"
    );
    bound = step_edit_action_watcher_.watchAll(
        state_refs_.structureClipboard.revision,
        state_refs_.sequencer.stepEdit.contextHold.action,
        state_refs_.sequencer.stepEdit.contextHold.startedAtMs
    ) && bound;
    step_preset_watcher_.bind<&SequencerOverlayPresenter::requestStepPresetRender>(
        *this, 2, "SequencerOverlay.stepPreset"
    );
    bound = step_preset_watcher_.watchAll(
        state_refs_.sequencer.stepPresetPicker.visible,
        state_refs_.sequencer.stepPresetPicker.mode,
        state_refs_.sequencer.stepPresetPicker.selectedIndex,
        state_refs_.sequencer.stepPresetPicker.entryCount,
        state_refs_.sequencer.stepPresetPicker.truncated,
        state_refs_.sequencer.stepPresetPicker.hasPreviousPage,
        state_refs_.sequencer.stepPresetPicker.hasNextPage,
        state_refs_.sequencer.stepPresetPicker.totalEntryCount,
        state_refs_.sequencer.stepPresetPicker.detailVisible,
        state_refs_.sequencer.stepPresetPicker.detailFocus,
        state_refs_.sequencer.stepPresetPicker.feedback,
        state_refs_.sequencer.stepPresetPicker.operationFeedback,
        state_refs_.sequencer.stepPresetPicker.actionGuard,
        state_refs_.sequencer.stepPresetPicker.revision,
        state_refs_.sequencer.stepEdit.stepIndex
    ) && bound;
    step_preset_action_watcher_.bind<&SequencerOverlayPresenter::requestStepPresetActionsRender>(
        *this, 3, "SequencerOverlay.stepPresetActions"
    );
    bound = step_preset_action_watcher_.watchAll(
        state_refs_.sequencer.stepPresetPicker.visible,
        state_refs_.sequencer.stepPresetPicker.mode,
        state_refs_.sequencer.stepPresetPicker.selectedIndex,
        state_refs_.sequencer.stepPresetPicker.entryCount,
        state_refs_.sequencer.stepPresetPicker.hasPreviousPage,
        state_refs_.sequencer.stepPresetPicker.revision,
        state_refs_.sequencer.stepPresetPicker.actionGuard,
        state_refs_.sequencer.stepPresetPicker.operationFeedback
    ) && bound;
    return bound;
}

FLASHMEM void SequencerOverlayPresenter::requestStepEditRender() {
    render_scheduler_.request(RENDER_STEP_EDIT | RENDER_STEP_EDIT_ACTIONS);
}

FLASHMEM void SequencerOverlayPresenter::requestStepEditActionsRender() {
    render_scheduler_.request(RENDER_STEP_EDIT_ACTIONS);
}

FLASHMEM void SequencerOverlayPresenter::requestStepPresetRender() {
    render_scheduler_.request(RENDER_STEP_PRESET | RENDER_STEP_PRESET_ACTIONS);
}

FLASHMEM void SequencerOverlayPresenter::requestStepPresetActionsRender() {
    render_scheduler_.request(RENDER_STEP_PRESET_ACTIONS);
}

FLASHMEM void SequencerOverlayPresenter::drainRenderQueue(void* context, uint32_t flags) {
    auto* self = static_cast<SequencerOverlayPresenter*>(context);
    if (self) self->renderPending(flags);
}

FLASHMEM void SequencerOverlayPresenter::renderPending(uint32_t flags) {
    if ((flags & RENDER_STEP_EDIT) != 0) renderStepEdit();
    if ((flags & RENDER_STEP_EDIT_ACTIONS) != 0) renderStepEditActionStrip();
    if ((flags & RENDER_STEP_PRESET) != 0) renderStepPresetPicker();
    if ((flags & RENDER_STEP_PRESET_ACTIONS) != 0) renderStepPresetActionStrip();
}

FLASHMEM void SequencerOverlayPresenter::renderStepEdit() {
    auto data = core::context::standalone::sequencer_overlay_presenter::buildStepEditRenderData({
        state_refs_.sequencer,
        state_refs_.tracks,
    });
    if (!data.visible || state_refs_.sequencer.stepPresetPicker.visible.get()) {
        step_edit_overlay_.render({.visible = false});
    } else {
        step_edit_overlay_.render(data.overlayProps);
    }
}

FLASHMEM void SequencerOverlayPresenter::renderStepEditActionStrip() {
    if (!state_refs_.sequencer.stepEdit.visible.get()) {
        step_edit_action_strip_.render({.visible = false});
    } else {
        step_edit_action_strip_.render(
            core::context::standalone::sequencer_overlay_presenter::buildStepEditActionStripProps({
                state_refs_.sequencer,
                state_refs_.tracks,
                state_refs_.structureClipboard,
            })
        );
    }
}

FLASHMEM void SequencerOverlayPresenter::renderStepPresetPicker() {
    const auto data =
        core::context::standalone::sequencer_overlay_presenter::
            buildStepPresetPickerRenderData({
                state_refs_.sequencer,
                state_refs_.tracks,
            });
    if (!data.visible) {
        step_preset_overlay_.render({.visible = false});
        return;
    }

    step_preset_overlay_.render({
        .title = data.title.data(),
        .meta = data.meta.data(),
        .items = data.items.data(),
        .itemCount = data.itemCount,
        .selectedIndex = data.selectedIndex,
        .showIndexColumn = false,
        .dimUnselected = false,
        .visible = true,
        .dataRevision = data.dataRevision,
    });
}

FLASHMEM void SequencerOverlayPresenter::renderStepPresetActionStrip() {
    if (!state_refs_.sequencer.stepPresetPicker.visible.get()) {
        step_preset_action_strip_.render({.visible = false});
        return;
    }

    step_preset_action_strip_.render(
        core::context::standalone::sequencer_overlay_presenter::
            buildStepPresetActionStripProps({
                state_refs_.sequencer,
                state_refs_.tracks,
            })
    );
}

}  // namespace core::context::standalone
