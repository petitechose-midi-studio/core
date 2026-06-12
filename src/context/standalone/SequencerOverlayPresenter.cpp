#include "context/standalone/SequencerOverlayPresenter.hpp"

#include <config/PlatformCompat.hpp>
#include <ms/ui/widget/VirtualListKeyValueOverlay.hpp>

#include "context/standalone/SequencerOverlayPresenterFormatters.hpp"

namespace core::context::standalone {

SequencerOverlayPresenter::SequencerOverlayPresenter(
    StateRefs stateRefs,
    ms::ui::VirtualListKeyValueOverlay& stepEditOverlay,
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
        state_refs_.sequencer.pattern.stepDataRevision,
        state_refs_.sequencer.pattern.graphRevision,
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

    step_edit_overlay_.render({
        .title = data.title.data(),
        .meta = data.meta.data(),
        .rows = data.rows.data(),
        .rowCount = data.rowCount,
        .selectedIndex = data.selectedIndex,
        .visible = true,
        .dataRevision = data.dataRevision,
    });
    step_edit_action_strip_.render(
        core::context::standalone::sequencer_overlay_presenter::buildStepEditActionStripProps({
            state_refs_.sequencer,
            state_refs_.tracks,
            state_refs_.structureClipboard,
        })
    );
}

}  // namespace core::context::standalone
