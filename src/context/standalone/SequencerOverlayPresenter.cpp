#include "context/standalone/SequencerOverlayPresenter.hpp"

#include <config/PlatformCompat.hpp>
#include <ms/ui/widget/VirtualListKeyValueOverlay.hpp>

#include "context/standalone/SequencerOverlayPresenterFormatters.hpp"

namespace core::context::standalone {

SequencerOverlayPresenter::SequencerOverlayPresenter(
    StateRefs stateRefs,
    ms::ui::VirtualListKeyValueOverlay& stepEditOverlay
)
    : state_refs_(stateRefs)
    , step_edit_overlay_(stepEditOverlay) {}

FLASHMEM void SequencerOverlayPresenter::bind() {
    step_edit_watcher_.watchAll(
        [this]() { renderStepEdit(); },
        state_refs_.sequencer.stepEdit.visible,
        state_refs_.sequencer.stepEdit.stepIndex,
        state_refs_.sequencer.stepEdit.focusedRow,
        state_refs_.sequencer.stepDataRevision
    );
}

FLASHMEM void SequencerOverlayPresenter::renderStepEdit() {
    auto data = core::context::standalone::sequencer_overlay_presenter::buildStepEditRenderData({
        state_refs_.sequencer,
    });
    if (!data.visible) {
        step_edit_overlay_.render({.visible = false});
        return;
    }

    step_edit_overlay_.render({
        .title = data.title.data(),
        .meta = data.meta.data(),
        .rows = data.rows.data(),
        .rowCount = static_cast<int>(data.rows.size()),
        .selectedIndex = data.selectedIndex,
        .visible = true,
        .dataRevision = data.dataRevision,
    });
}

}  // namespace core::context::standalone
