#include "context/standalone/GlobalSettingsOverlayPresenter.hpp"

#include <cstdio>

#include <ms/ui/widget/VirtualListKeyValueOverlay.hpp>
#include <ms/ui/widget/VirtualListSelectorOverlay.hpp>

namespace core::context::standalone {

GlobalSettingsOverlayPresenter::GlobalSettingsOverlayPresenter(
    core::state::CoreState& state,
    ms::ui::VirtualListKeyValueOverlay& overlay,
    ms::ui::VirtualListSelectorOverlay& selectorOverlay
)
    : state_(state)
    , overlay_(overlay)
    , selector_overlay_(selectorOverlay) {}

void GlobalSettingsOverlayPresenter::bind() {
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

void GlobalSettingsOverlayPresenter::renderOverlay() {
    const bool visible = state_.globalSettings.visible.get();
    if (!visible) {
        overlay_.render({.visible = false});
        return;
    }

    const auto mode = state_.midiSync.mode.get();
    const bool followTransport = state_.midiSync.followTransport.get();
    const uint16_t fallbackMs = state_.midiSync.autoFallbackMs.get();
    const uint8_t lockCount = state_.midiSync.autoLockClockCount.get();

    const char* modeLabel = "AUTO";
    switch (mode) {
        case core::state::MidiSyncMode::MASTER: modeLabel = "MASTER"; break;
        case core::state::MidiSyncMode::SLAVE: modeLabel = "SLAVE"; break;
        case core::state::MidiSyncMode::AUTO:
        default:
            break;
    }

    const char* sourceLabel =
        (state_.midiSync.activeSource.get() == core::state::ClockSourceActive::EXTERNAL) ? "EXT" : "INT";
    const char* signalLabel = state_.midiSync.externalClockPresent.get() ? "IN" : "-";

    char meta[24];
    std::snprintf(meta, sizeof(meta), "%s  CLK %s", sourceLabel, signalLabel);

    char fallbackStr[16];
    std::snprintf(fallbackStr, sizeof(fallbackStr), "%ums", static_cast<unsigned>(fallbackMs));

    char lockStr[16];
    std::snprintf(lockStr, sizeof(lockStr), "%u clocks", static_cast<unsigned>(lockCount));

    const ms::ui::KeyValueRow rows[] = {
        {.key = "Mode", .value = modeLabel},
        {.key = "Follow", .value = followTransport ? "ON" : "OFF"},
        {.key = "Timeout", .value = fallbackStr},
        {.key = "Lock", .value = lockStr},
    };

    const uint32_t dataRevision =
        (static_cast<uint32_t>(mode) << 24) |
        (static_cast<uint32_t>(followTransport ? 1 : 0) << 20) |
        (static_cast<uint32_t>(fallbackMs) << 4) |
        (static_cast<uint32_t>(lockCount) & 0x0F);

    overlay_.render({
        .title = "SETTINGS",
        .meta = meta,
        .rows = rows,
        .rowCount = 4,
        .selectedIndex = state_.globalSettings.focusedRow.get(),
        .visible = true,
        .dataRevision = dataRevision,
    });
}

void GlobalSettingsOverlayPresenter::renderSelector() {
    const bool visible = state_.globalSettings.selector.visible.get();
    if (!visible) {
        selector_overlay_.render({.visible = false});
        return;
    }

    static const char* const MODE_ITEMS[] = {"MASTER", "SLAVE", "AUTO"};
    static const char* const FOLLOW_ITEMS[] = {"OFF", "ON"};
    static const char* const FALLBACK_ITEMS[] = {"150 ms", "250 ms", "500 ms", "750 ms", "1000 ms", "1500 ms", "2000 ms"};
    static const char* const LOCK_ITEMS[] = {"1", "2", "3", "4", "6", "8", "12", "24"};

    const uint8_t row = state_.globalSettings.selector.editingRow.get();
    const char* title = "VALUE";
    const char* meta = "GLOBAL";
    const char* const* items = MODE_ITEMS;
    int itemCount = 3;

    switch (row) {
        case 0:
            title = "SYNC MODE";
            break;
        case 1:
            title = "FOLLOW";
            items = FOLLOW_ITEMS;
            itemCount = 2;
            break;
        case 2:
            title = "AUTO TIMEOUT";
            items = FALLBACK_ITEMS;
            itemCount = 7;
            break;
        case 3:
            title = "AUTO LOCK";
            items = LOCK_ITEMS;
            itemCount = 8;
            break;
        default:
            break;
    }

    const uint32_t dataRevision =
        (static_cast<uint32_t>(row) << 24) |
        (static_cast<uint32_t>(state_.midiSync.mode.get()) << 16) |
        (static_cast<uint32_t>(state_.midiSync.followTransport.get() ? 1 : 0) << 12) |
        (static_cast<uint32_t>(state_.midiSync.autoFallbackMs.get()) & 0x0FFF);

    selector_overlay_.render({
        .title = title,
        .meta = meta,
        .items = items,
        .itemCount = itemCount,
        .selectedIndex = state_.globalSettings.selector.selectedIndex.get(),
        .showIndexColumn = false,
        .visible = true,
        .dataRevision = dataRevision,
    });
}

}  // namespace core::context::standalone
