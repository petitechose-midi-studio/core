#include "context/standalone/DeviceSettingsSelectorPresenter.hpp"

#include <config/PlatformCompat.hpp>
#include <ms/ui/widget/VirtualListSelectorOverlay.hpp>

#include "state/settings/DeviceSettingsMenuModel.hpp"

namespace core::context::standalone {
namespace {

constexpr const char* const MODE_ITEMS[] = {"MASTER", "SLAVE", "AUTO"};
constexpr const char* const FOLLOW_ITEMS[] = {"OFF", "ON"};
constexpr const char* const FALLBACK_ITEMS[] = {
    "150 ms",
    "250 ms",
    "500 ms",
    "750 ms",
    "1000 ms",
    "1500 ms",
    "2000 ms"
};
constexpr const char* const LOCK_ITEMS[] = {"1", "2", "3", "4", "6", "8", "12", "24"};

struct SelectorRenderData {
    const char* title = "";
    const char* const* items = nullptr;
    int itemCount = 0;
    int selectedIndex = 0;
    uint32_t dataRevision = 0;
    bool visible = false;
};

FLASHMEM SelectorRenderData buildSelectorRenderData(
    const core::state::DeviceSettingsState& settings,
    const core::state::MidiSyncState& midiSync
) {
    SelectorRenderData data{};
    if (settings.flowPhase.get() != core::state::DeviceSettingsFlowPhase::VALUE_SELECTOR ||
        !settings.selector.visible.get()) {
        return data;
    }

    const uint8_t row = settings.selector.editingRow.get();
    data.visible = true;
    data.title = core::state::settings::deviceSettingsRowLabel(row);

    switch (row) {
        case 0:
            data.items = MODE_ITEMS;
            data.itemCount = 3;
            break;
        case 1:
            data.items = FOLLOW_ITEMS;
            data.itemCount = 2;
            break;
        case 2:
            data.items = FALLBACK_ITEMS;
            data.itemCount = 7;
            break;
        case 3:
            data.items = LOCK_ITEMS;
            data.itemCount = 8;
            break;
        default:
            data.items = MODE_ITEMS;
            data.itemCount = 3;
            break;
    }

    data.selectedIndex = settings.selector.selectedIndex.get();
    data.dataRevision =
        (static_cast<uint32_t>(row) << 24) |
        (static_cast<uint32_t>(midiSync.mode.get()) << 16) |
        (static_cast<uint32_t>(midiSync.followTransport.get() ? 1 : 0) << 12) |
        (static_cast<uint32_t>(midiSync.autoFallbackMs.get()) & 0x0FFF);

    return data;
}

}  // namespace

DeviceSettingsSelectorPresenter::DeviceSettingsSelectorPresenter(
    StateRefs stateRefs,
    ms::ui::VirtualListSelectorOverlay& selectorOverlay
)
    : state_refs_(stateRefs)
    , selector_overlay_(selectorOverlay) {}

FLASHMEM void DeviceSettingsSelectorPresenter::bind() {
    selector_watcher_.watchAll(
        [this]() { renderSelector(); },
        state_refs_.settings.flowPhase,
        state_refs_.settings.selector.selectedIndex,
        state_refs_.settings.selector.editingRow,
        state_refs_.midiSync.mode,
        state_refs_.midiSync.followTransport,
        state_refs_.midiSync.autoFallbackMs,
        state_refs_.midiSync.autoLockClockCount
    );
}

FLASHMEM void DeviceSettingsSelectorPresenter::renderSelector() {
    const auto data = buildSelectorRenderData(state_refs_.settings, state_refs_.midiSync);
    if (!data.visible) {
        selector_overlay_.render({.visible = false});
        return;
    }

    selector_overlay_.render({
        .title = data.title,
        .meta = "Device Settings",
        .items = data.items,
        .itemCount = data.itemCount,
        .selectedIndex = data.selectedIndex,
        .showIndexColumn = false,
        .visible = true,
        .dataRevision = data.dataRevision,
    });
}

}  // namespace core::context::standalone
