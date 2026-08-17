#include <cassert>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include <ms/device_support/v1/Buffers.hpp>
#include <ms/device_support/v1/ControlLayout.hpp>
#include <ms/device_support/v1/Hardware.hpp>
#include <ms/device_support/v1/InputConfig.hpp>
#include <ms/device_support/v1/LvglMemory.hpp>
#include <ms/device_support/v1/Version.hpp>

namespace device = ms::device_support::v1;

static_assert(__cplusplus == 201703L, "device-support must remain C++17");
static_assert(device::API_VERSION == 1);

static_assert(device::timing::INPUT_APP_ADMISSION_HZ == 1'920);
static_assert(device::timing::LVGL_SERVICE_HZ == 240);
static_assert(device::timing::DEBOUNCE_MS == 12);
static_assert(device::timing::LONG_PRESS_MS == 500);
static_assert(device::timing::LATCH_THRESHOLD_MS == 200);
static_assert(device::timing::DOUBLE_TAP_MS == 300);

static_assert(std::is_same_v<std::underlying_type_t<device::ButtonID>,
                             oc::type::ButtonID>);
static_assert(std::is_same_v<std::underlying_type_t<device::EncoderID>,
                             oc::type::EncoderID>);
static_assert(device::MACRO_COUNT == 8);
static_assert(static_cast<oc::type::ButtonID>(device::ButtonID::LEFT_TOP) == 10);
static_assert(static_cast<oc::type::ButtonID>(device::ButtonID::NAV) == 40);
static_assert(static_cast<oc::type::EncoderID>(device::EncoderID::MACRO_1) ==
              301);
static_assert(static_cast<oc::type::EncoderID>(device::EncoderID::OPT) == 410);
static_assert(device::control::MACRO_ENCODERS.size() == device::MACRO_COUNT);
static_assert(device::control::MACRO_BUTTONS.size() == device::MACRO_COUNT);
static_assert(
    device::control::MACRO_ENCODERS.front() == device::EncoderID::MACRO_1);
static_assert(
    device::control::MACRO_ENCODERS.back() == device::EncoderID::MACRO_8);
static_assert(
    device::control::MACRO_BUTTONS.front() == device::ButtonID::MACRO_1);
static_assert(
    device::control::MACRO_BUTTONS.back() == device::ButtonID::MACRO_8);

static_assert(device::input::CONFIG.longPressMs == 500);
static_assert(device::input::CONFIG.doubleTapWindowMs == 300);
static_assert(device::input::CONFIG.latchThresholdMs == 200);
static_assert(device::input::CONFIG.debounceMs == 12);
static_assert(
    device::input::CONFIG.releaseRoutingPolicy ==
    oc::core::input::ReleaseRoutingPolicy::OwnerOnly);
static_assert(
    device::input::CONFIG.gestureRoutingPolicy ==
    oc::core::input::GestureRoutingPolicy::PressScoped);
static_assert(
    device::input::CONFIG.ambiguityPolicy ==
    oc::core::input::BindingAmbiguityPolicy::FailClosed);
static_assert(
    device::input::CONFIG.globalRoutingPolicy ==
    oc::core::input::GlobalRoutingPolicy::ExplicitPassThroughOnly);

static_assert(device::display::CONFIG.width == 320);
static_assert(device::display::CONFIG.height == 240);
static_assert(device::display::CONFIG.csPin == 28);
static_assert(device::display::CONFIG.dcPin == 0);
static_assert(device::display::CONFIG.rstPin == 29);
static_assert(device::display::CONFIG.mosiPin == 26);
static_assert(device::display::CONFIG.sckPin == 27);
static_assert(device::display::CONFIG.misoPin == 1);
static_assert(device::display::CONFIG.spiSpeed == 50'000'000);
static_assert(device::display::CONFIG.rotation == 3);
static_assert(device::display::CONFIG.invertDisplay);
static_assert(device::display::CONFIG.vsyncSpacing == 1);
static_assert(device::display::CONFIG.diffGap == 8);
static_assert(device::display::CONFIG.irqPriority == 160);
static_assert(
    device::timing::MUSICAL_REALTIME_IRQ_PRIORITY <
    device::display::CONFIG.irqPriority);
static_assert(device::display::CONFIG.lateStartRatio == 0.2f);
static_assert(device::display::PHYSICAL_REFRESH_TARGET_HZ ==
              device::timing::UI_FRAME_HZ);
static_assert(device::display::CONFIG.refreshRate ==
              device::timing::UI_FRAME_HZ);
static_assert(device::display::FRAMEBUFFER_PIXEL_COUNT == 76'800);
static_assert(device::display::DIFF_BUFFER_SIZE_BYTES == 8'192);
static_assert(
    device::display::LVGL_CONFIG.renderMode == LV_DISPLAY_RENDER_MODE_DIRECT);
static_assert(device::display::LVGL_CONFIG.buffer2 == nullptr);
static_assert(device::timing::UI_FRAME_SERVICE_DIVISOR == 2);
static_assert(device::timing::UI_FRAME_HZ == 120);
static_assert(device::display::LVGL_CONFIG.refreshHz ==
              device::timing::UI_FRAME_HZ);

static_assert(device::mux::BUTTON_READS_PER_APP_TICK == 1);
static_assert(device::mux::CONFIG.selectPins[0] == 3);
static_assert(device::mux::CONFIG.selectPins[1] == 2);
static_assert(device::mux::CONFIG.selectPins[2] == 5);
static_assert(device::mux::CONFIG.selectPins[3] == 6);
static_assert(device::mux::CONFIG.signalPin == 4);
static_assert(device::mux::CONFIG.settleTimeUs == 20);
static_assert(device::mux::CONFIG.signalPullup);

static_assert(device::encoder::ENCODERS.size() == 10);
static_assert(device::encoder::ENCODERS[0].id == 301);
static_assert(device::encoder::ENCODERS[0].pinA == 22);
static_assert(device::encoder::ENCODERS[0].pinB == 23);
static_assert(device::encoder::ENCODERS[8].id == 400);
static_assert(device::encoder::ENCODERS[8].ticksPerEvent == 4);
static_assert(!device::encoder::ENCODERS[8].invertDirection);
static_assert(device::encoder::ENCODERS[9].id == 410);
static_assert(device::encoder::ENCODERS[9].ppr == 600);

static_assert(device::button::BUTTONS.size() == 15);
static_assert(device::button::MUX_BUTTON_COUNT == 14);
static_assert(device::button::MUX_SCAN_TICKS == 14);
static_assert(device::button::MUX_SCAN_PERIOD_US == 7'292);
static_assert(device::button::BUTTONS[0].id == 10);
static_assert(device::button::BUTTONS[0].pin.pin == 9);
static_assert(
    device::button::BUTTONS[0].pin.source == device::button::Source::MUX);
static_assert(device::button::BUTTONS[6].id == 40);
static_assert(device::button::BUTTONS[6].pin.pin == 32);
static_assert(
    device::button::BUTTONS[6].pin.source == device::button::Source::MCU);
static_assert(device::button::BUTTONS[14].id == 38);

static_assert(sizeof(device::buffers::framebuffer) == 153'600);
static_assert(sizeof(device::buffers::diff1) == 8'192);
static_assert(sizeof(device::buffers::diff2) == 8'192);
static_assert(sizeof(device::buffers::lvgl) == 153'600);
static_assert(device::LVGL_MEMORY_POOL_SIZE_BYTES == 1'048'576);

int main() {
    std::uint8_t macroIndex = 0xff;
    assert(device::control::macroEncoderIndex(301, macroIndex));
    assert(macroIndex == 0);
    assert(device::control::macroButtonIndex(38, macroIndex));
    assert(macroIndex == 7);
    assert(!device::control::macroEncoderIndex(0, macroIndex));

    std::uint8_t* const first =
        getLvglMemoryPool(device::LVGL_MEMORY_POOL_SIZE_BYTES);
    std::uint8_t* const second = getLvglMemoryPool(1);

    assert(first != nullptr);
    assert(second == first);

    first[0] = 0x5a;
    first[device::LVGL_MEMORY_POOL_SIZE_BYTES - 1] = 0xa5;
    assert(first[0] == 0x5a);
    assert(first[device::LVGL_MEMORY_POOL_SIZE_BYTES - 1] == 0xa5);
    return 0;
}
