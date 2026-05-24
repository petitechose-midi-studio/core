#include "context/standalone/GlobalSettingsOverlayPresenterFormatters.hpp"

#include <config/PlatformCompat.hpp>
#include <oc/type/TextFormat.hpp>

#include "state/ViewSelectorItems.hpp"

namespace core::context::standalone::global_settings_presenter {

namespace {

constexpr const char* const ROW_KEYS[] = {"Mode", "Follow", "Timeout", "Lock"};
constexpr const char* const MODE_ITEMS[] = {"MASTER", "SLAVE", "AUTO"};
constexpr const char* const FOLLOW_ITEMS[] = {"OFF", "ON"};
constexpr const char* const FALLBACK_ITEMS[] = {"150 ms", "250 ms", "500 ms", "750 ms", "1000 ms", "1500 ms", "2000 ms"};
constexpr const char* const LOCK_ITEMS[] = {"1", "2", "3", "4", "6", "8", "12", "24"};

FLASHMEM const char* modeLabel(core::state::MidiSyncMode mode) {
    switch (mode) {
        case core::state::MidiSyncMode::MASTER: return "MASTER";
        case core::state::MidiSyncMode::SLAVE: return "SLAVE";
        case core::state::MidiSyncMode::AUTO:
        default:
            return "AUTO";
    }
}

}  // namespace

FLASHMEM OverlayRenderData buildOverlayRenderData(const Source& source) {
    OverlayRenderData data{};

    const auto mode = source.midiSync.mode.get();
    const bool followTransport = source.midiSync.followTransport.get();
    const uint16_t fallbackMs = source.midiSync.autoFallbackMs.get();
    const uint8_t lockCount = source.midiSync.autoLockClockCount.get();

    const char* sourceLabel =
        (source.midiSync.activeSource.get() == core::state::ClockSourceActive::EXTERNAL) ? "EXT" : "INT";
    const char* signalLabel = source.midiSync.externalClockPresent.get() ? "IN" : "-";

    size_t metaPos = oc::type::text::appendString(data.meta.data(), data.meta.size(), 0, sourceLabel);
    metaPos = oc::type::text::appendString(data.meta.data(), data.meta.size(), metaPos, "  CLK ");
    metaPos = oc::type::text::appendString(data.meta.data(), data.meta.size(), metaPos, signalLabel);
    oc::type::text::terminate(data.meta.data(), data.meta.size(), metaPos);

    size_t fallbackPos = oc::type::text::appendUnsigned(
        data.valueBuffers[0].data(),
        data.valueBuffers[0].size(),
        0,
        static_cast<unsigned>(fallbackMs)
    );
    fallbackPos = oc::type::text::appendString(data.valueBuffers[0].data(), data.valueBuffers[0].size(), fallbackPos, "ms");
    oc::type::text::terminate(data.valueBuffers[0].data(), data.valueBuffers[0].size(), fallbackPos);

    size_t lockPos = oc::type::text::appendUnsigned(
        data.valueBuffers[1].data(),
        data.valueBuffers[1].size(),
        0,
        static_cast<unsigned>(lockCount)
    );
    lockPos = oc::type::text::appendString(data.valueBuffers[1].data(), data.valueBuffers[1].size(), lockPos, " clocks");
    oc::type::text::terminate(data.valueBuffers[1].data(), data.valueBuffers[1].size(), lockPos);

    data.rows = {{
        {.key = ROW_KEYS[0], .value = modeLabel(mode)},
        {.key = ROW_KEYS[1], .value = followTransport ? "ON" : "OFF"},
        {.key = ROW_KEYS[2], .value = data.valueBuffers[0].data()},
        {.key = ROW_KEYS[3], .value = data.valueBuffers[1].data()},
    }};
    data.selectedIndex = source.globalSettings.focusedRow.get();
    data.dataRevision =
        (static_cast<uint32_t>(mode) << 24) |
        (static_cast<uint32_t>(followTransport ? 1 : 0) << 20) |
        (static_cast<uint32_t>(fallbackMs) << 4) |
        (static_cast<uint32_t>(lockCount) & 0x0F);

    return data;
}

FLASHMEM SelectorRenderData buildSelectorRenderData(const Source& source) {
    SelectorRenderData data{};
    if (source.globalSettings.flowPhase.get() != core::state::GlobalSettingsFlowPhase::VALUE_SELECTOR ||
        !source.globalSettings.selector.visible.get()) {
        return data;
    }

    const uint8_t row = source.globalSettings.selector.editingRow.get();
    data.visible = true;
    data.title = row < 4 ? ROW_KEYS[row] : "Value";
    data.meta = core::state::viewSelectorItemLabel(core::state::ViewSelectorItem::GLOBAL_SETTINGS);

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

    data.selectedIndex = source.globalSettings.selector.selectedIndex.get();
    data.dataRevision =
        (static_cast<uint32_t>(row) << 24) |
        (static_cast<uint32_t>(source.midiSync.mode.get()) << 16) |
        (static_cast<uint32_t>(source.midiSync.followTransport.get() ? 1 : 0) << 12) |
        (static_cast<uint32_t>(source.midiSync.autoFallbackMs.get()) & 0x0FFF);

    return data;
}

}  // namespace core::context::standalone::global_settings_presenter
