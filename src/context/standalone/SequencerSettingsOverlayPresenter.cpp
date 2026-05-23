#include "context/standalone/SequencerSettingsOverlayPresenter.hpp"

#include <config/PlatformCompat.hpp>
#include <ms/ui/widget/VirtualListKeyValueOverlay.hpp>

namespace core::context::standalone {

SequencerSettingsOverlayPresenter::SequencerSettingsOverlayPresenter(
    StateRefs stateRefs,
    ms::ui::VirtualListKeyValueOverlay& overlay
)
    : state_refs_(stateRefs)
    , overlay_(overlay) {}

FLASHMEM void SequencerSettingsOverlayPresenter::bind() {
    overlay_watcher_.watchAll(
        [this]() { renderOverlay(); },
        state_refs_.sequencerSettings.visible,
        state_refs_.sequencerSettings.focusedRow
    );
}

FLASHMEM void SequencerSettingsOverlayPresenter::renderOverlay() {
    const bool visible = state_refs_.sequencerSettings.visible.get();
    if (!visible) {
        overlay_.render({.visible = false});
        return;
    }

    static constexpr ms::ui::KeyValueRow rows[] = {
        {.key = "Scale", .value = "Chromatic"},
    };

    overlay_.render({
        .title = "SEQ SETTINGS",
        .meta = "PROJECT",
        .rows = rows,
        .rowCount = 1,
        .selectedIndex = static_cast<int>(state_refs_.sequencerSettings.focusedRow.get()),
        .visible = true,
        .dataRevision = 1,
    });
}

}  // namespace core::context::standalone
