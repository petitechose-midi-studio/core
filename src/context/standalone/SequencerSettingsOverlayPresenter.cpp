#include "context/standalone/SequencerSettingsOverlayPresenter.hpp"

#include <config/PlatformCompat.hpp>
#include <ms/ui/widget/VirtualListKeyValueOverlay.hpp>
#include <ms/ui/widget/VirtualListSelectorOverlay.hpp>

#include "context/standalone/SequencerSettingsOverlayPresenterFormatters.hpp"
#include "state/ViewSelectorItems.hpp"

namespace core::context::standalone {

FLASHMEM SequencerSettingsOverlayPresenter::~SequencerSettingsOverlayPresenter() {}

FLASHMEM SequencerSettingsOverlayPresenter::SequencerSettingsOverlayPresenter(
    StateRefs stateRefs,
    ms::ui::VirtualListKeyValueOverlay& overlay,
    ms::ui::VirtualListSelectorOverlay& selectorOverlay
)
    : state_refs_(stateRefs)
    , overlay_(overlay)
    , selector_overlay_(selectorOverlay)
    , render_scheduler_(
          core::ui::renderSchedulerDebugLabel("SequencerSettings"),
          &SequencerSettingsOverlayPresenter::drainRenderQueue,
          this
      ) {}

FLASHMEM bool SequencerSettingsOverlayPresenter::bind() {
    bool bound = render_scheduler_.valid();
    overlay_watcher_.bind<&SequencerSettingsOverlayPresenter::requestOverlayRender>(
        *this, 0, "SequencerSettings.overlay"
    );
    bound = overlay_watcher_.watchAll(
        state_refs_.sequencerSettings.visible,
        state_refs_.sequencerSettings.focusedRow,
        state_refs_.trackBank.projectScaleRevisionSignal()
    ) && bound;

    selector_watcher_.bind<&SequencerSettingsOverlayPresenter::requestSelectorRender>(
        *this, 1, "SequencerSettings.selector"
    );
    bound = selector_watcher_.watchAll(
        state_refs_.sequencerSettings.flowPhase,
        state_refs_.sequencerSettings.selector.visible,
        state_refs_.sequencerSettings.selector.editingRow,
        state_refs_.sequencerSettings.selector.selectedIndex
    ) && bound;
    return bound;
}

FLASHMEM void SequencerSettingsOverlayPresenter::requestOverlayRender() {
    render_scheduler_.request(RENDER_OVERLAY);
}

FLASHMEM void SequencerSettingsOverlayPresenter::requestSelectorRender() {
    render_scheduler_.request(RENDER_SELECTOR);
}

FLASHMEM void SequencerSettingsOverlayPresenter::drainRenderQueue(
    void* context,
    uint32_t flags
) {
    auto* self = static_cast<SequencerSettingsOverlayPresenter*>(context);
    if (self) self->renderPending(flags);
}

FLASHMEM void SequencerSettingsOverlayPresenter::renderPending(uint32_t flags) {
    if ((flags & RENDER_OVERLAY) != 0) renderOverlay();
    if ((flags & RENDER_SELECTOR) != 0) renderSelector();
}

FLASHMEM void SequencerSettingsOverlayPresenter::renderOverlay() {
    const bool visible = state_refs_.sequencerSettings.visible.get();
    if (!visible) {
        overlay_.render({.visible = false});
        return;
    }

    const auto data = sequencer_settings_presenter::buildOverlayRenderData({
        state_refs_.sequencerSettings,
        state_refs_.trackBank,
    });

    overlay_.render({
        .title = core::state::SETTINGS_SECTION_LABEL,
        .meta = core::state::viewSelectorItemLabel(core::state::ViewSelectorItem::SEQUENCER),
        .rows = data.rows.data(),
        .rowCount = static_cast<int>(data.rows.size()),
        .selectedIndex = data.selectedIndex,
        .visible = true,
        .dataRevision = data.dataRevision,
    });
}

FLASHMEM void SequencerSettingsOverlayPresenter::renderSelector() {
    const auto data = sequencer_settings_presenter::buildSelectorRenderData({
        state_refs_.sequencerSettings,
        state_refs_.trackBank,
    });
    if (!data.visible) {
        selector_overlay_.render({.visible = false});
        return;
    }

    selector_overlay_.render({
        .title = data.title,
        .meta = data.meta,
        .items = data.items,
        .itemCount = data.itemCount,
        .selectedIndex = data.selectedIndex,
        .showIndexColumn = false,
        .visible = true,
        .dataRevision = data.dataRevision,
    });
}

}  // namespace core::context::standalone
