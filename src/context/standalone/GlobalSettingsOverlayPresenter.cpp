#include "context/standalone/GlobalSettingsOverlayPresenter.hpp"

#include "context/standalone/GlobalSettingsOverlayPresenterFormatters.hpp"
#include <config/PlatformCompat.hpp>
#include <ms/ui/widget/VirtualListKeyValueOverlay.hpp>
#include <ms/ui/widget/VirtualListSelectorOverlay.hpp>
#include "state/CoreState.hpp"

namespace core::context::standalone {

GlobalSettingsOverlayPresenter::GlobalSettingsOverlayPresenter(
    core::state::CoreState& state,
    ms::ui::VirtualListKeyValueOverlay& overlay,
    ms::ui::VirtualListSelectorOverlay& selectorOverlay
)
    : state_(state)
    , overlay_(overlay)
    , selector_overlay_(selectorOverlay) {}

FLASHMEM void GlobalSettingsOverlayPresenter::bind() {
    overlay_watcher_.watchAll(
        [this]() { renderOverlay(); },
        state_.globalSettings.visible,
        state_.globalSettings.focusedRow,
        state_.midiSync.mode,
        state_.midiSync.followTransport,
        state_.midiSync.autoFallbackMs,
        state_.midiSync.autoLockClockCount,
        state_.midiSync.activeSource,
        state_.midiSync.externalClockPresent
    );

    selector_watcher_.watchAll(
        [this]() { renderSelector(); },
        state_.globalSettings.selector.visible,
        state_.globalSettings.selector.selectedIndex,
        state_.globalSettings.selector.editingRow,
        state_.midiSync.mode,
        state_.midiSync.followTransport,
        state_.midiSync.autoFallbackMs,
        state_.midiSync.autoLockClockCount
    );
}

FLASHMEM void GlobalSettingsOverlayPresenter::renderOverlay() {
    const bool visible = state_.globalSettings.visible.get();
    if (!visible) {
        overlay_.render({.visible = false});
        return;
    }
    const auto data = global_settings_presenter::buildOverlayRenderData(state_);

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
    const auto data = global_settings_presenter::buildSelectorRenderData(state_);
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
