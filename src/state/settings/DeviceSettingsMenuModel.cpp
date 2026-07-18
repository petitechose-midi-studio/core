#include "state/settings/DeviceSettingsMenuModel.hpp"

#include <config/PlatformCompat.hpp>
#include <oc/type/TextFormat.hpp>

namespace core::state::settings {
namespace {

constexpr const char* const ROW_LABELS[] PROGMEM = {"Mode", "Follow", "Timeout", "Lock"};

FLASHMEM const char* modeLabel(core::state::MidiSyncMode mode) {
    switch (mode) {
        case core::state::MidiSyncMode::MASTER:
            return "MASTER";
        case core::state::MidiSyncMode::SLAVE:
            return "SLAVE";
        case core::state::MidiSyncMode::AUTO:
        default:
            return "AUTO";
    }
}

}  // namespace

constexpr uint8_t deviceSettingsRowCount() {
    return static_cast<uint8_t>(DeviceSettingsMenuPage::MAX_ROWS);
}

FLASHMEM const char* deviceSettingsRowLabel(uint8_t row) {
    return row < deviceSettingsRowCount() ? ROW_LABELS[row] : "Value";
}

FLASHMEM DeviceSettingsMenuPage buildDeviceSettingsMenuPage(
    const core::state::DeviceSettingsState& state,
    const DeviceSettingsMenuContext& context
) {
    DeviceSettingsMenuPage page{};
    page.rowCount = deviceSettingsRowCount();
    page.selectedIndex = state.focusedRow.get();

    size_t fallbackPos = oc::type::text::appendUnsigned(
        page.valueBuffers[0].data(),
        page.valueBuffers[0].size(),
        0,
        static_cast<unsigned>(context.autoFallbackMs)
    );
    fallbackPos = oc::type::text::appendString(
        page.valueBuffers[0].data(),
        page.valueBuffers[0].size(),
        fallbackPos,
        "ms"
    );
    oc::type::text::terminate(page.valueBuffers[0].data(), page.valueBuffers[0].size(), fallbackPos);

    size_t lockPos = oc::type::text::appendUnsigned(
        page.valueBuffers[1].data(),
        page.valueBuffers[1].size(),
        0,
        static_cast<unsigned>(context.autoLockClockCount)
    );
    lockPos = oc::type::text::appendString(
        page.valueBuffers[1].data(),
        page.valueBuffers[1].size(),
        lockPos,
        " clocks"
    );
    oc::type::text::terminate(page.valueBuffers[1].data(), page.valueBuffers[1].size(), lockPos);

    page.rows = {{
        {.label = ROW_LABELS[0], .value = modeLabel(context.mode)},
        {.label = ROW_LABELS[1], .value = context.followTransport ? "ON" : "OFF"},
        {.label = ROW_LABELS[2], .value = page.valueBuffers[0].data()},
        {.label = ROW_LABELS[3], .value = page.valueBuffers[1].data()},
    }};

    page.dataRevision =
        (static_cast<uint32_t>(context.mode) << 24) |
        (static_cast<uint32_t>(context.followTransport ? 1 : 0) << 20) |
        (static_cast<uint32_t>(context.autoFallbackMs) << 4) |
        (static_cast<uint32_t>(context.autoLockClockCount) & 0x0F);

    return page;
}

}  // namespace core::state::settings
