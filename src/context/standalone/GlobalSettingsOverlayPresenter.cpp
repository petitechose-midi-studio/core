#include "context/standalone/GlobalSettingsOverlayPresenter.hpp"

#include <config/PlatformCompat.hpp>
#include <ms/ui/widget/VirtualListKeyValueOverlay.hpp>
#include <ms/ui/widget/VirtualListSelectorOverlay.hpp>

namespace core::context::standalone {

GlobalSettingsOverlayPresenter::GlobalSettingsOverlayPresenter(
    StateRefs stateRefs,
    ms::ui::VirtualListKeyValueOverlay& overlay,
    ms::ui::VirtualListSelectorOverlay& selectorOverlay
)
    : state_refs_(stateRefs)
    , overlay_(overlay)
    , selector_overlay_(selectorOverlay) {}

FLASHMEM void GlobalSettingsOverlayPresenter::bind() {
    overlay_watcher_.watchAll(
        [this]() { renderOverlay(); },
        state_refs_.globalSettings.visible,
        state_refs_.globalSettings.focusedRow,
        state_refs_.midiSync.mode,
        state_refs_.midiSync.followTransport,
        state_refs_.midiSync.autoFallbackMs,
        state_refs_.midiSync.autoLockClockCount,
        state_refs_.midiSync.activeSource,
        state_refs_.midiSync.externalClockPresent
    );

    selector_watcher_.watchAll(
        [this]() { renderSelector(); },
        state_refs_.globalSettings.flowPhase,
        state_refs_.globalSettings.selector.selectedIndex,
        state_refs_.globalSettings.selector.editingRow,
        state_refs_.midiSync.mode,
        state_refs_.midiSync.followTransport,
        state_refs_.midiSync.autoFallbackMs,
        state_refs_.midiSync.autoLockClockCount
    );
}

FLASHMEM void GlobalSettingsOverlayPresenter::renderOverlay() {
    const bool visible = state_refs_.globalSettings.visible.get();
    if (!visible) {
        overlay_.render({.visible = false});
        return;
    }
    const auto data = global_settings_presenter::buildOverlayRenderData(state_refs_);

    overlay_.render({
        .title = "SETTINGS",
        .meta = data.meta.data(),
        .rows = data.rows.data(),
        .rowCount = static_cast<int>(data.rows.size()),
        .selectedIndex = data.selectedIndex,
        .visible = true,
        .dataRevision = data.dataRevision,
    });
}

FLASHMEM void GlobalSettingsOverlayPresenter::renderSelector() {
    const auto data = global_settings_presenter::buildSelectorRenderData(state_refs_);
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
