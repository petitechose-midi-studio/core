#include "context/standalone/SequencerSettingsOverlayPresenter.hpp"

#include <config/PlatformCompat.hpp>
#include <ms/ui/widget/VirtualListKeyValueOverlay.hpp>
#include <ms/ui/widget/VirtualListSelectorOverlay.hpp>

#include "context/standalone/SequencerSettingsOverlayPresenterFormatters.hpp"
#include "state/ViewSelectorItems.hpp"

namespace core::context::standalone {

SequencerSettingsOverlayPresenter::SequencerSettingsOverlayPresenter(
    StateRefs stateRefs,
    ms::ui::VirtualListKeyValueOverlay& overlay,
    ms::ui::VirtualListSelectorOverlay& selectorOverlay
)
    : state_refs_(stateRefs)
    , overlay_(overlay)
    , selector_overlay_(selectorOverlay) {}

FLASHMEM void SequencerSettingsOverlayPresenter::bind() {
    overlay_watcher_.watchAll(
        [this]() { renderOverlay(); },
        state_refs_.sequencerSettings.visible,
        state_refs_.sequencerSettings.focusedRow,
        state_refs_.trackBank.projectScaleRevisionSignal()
    );

    selector_watcher_.watchAll(
        [this]() { renderSelector(); },
        state_refs_.sequencerSettings.flowPhase,
        state_refs_.sequencerSettings.selector.visible,
        state_refs_.sequencerSettings.selector.editingRow,
        state_refs_.sequencerSettings.selector.selectedIndex
    );
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
