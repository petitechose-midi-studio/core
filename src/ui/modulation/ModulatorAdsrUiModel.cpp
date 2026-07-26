#include "ui/modulation/ModulatorAdsrUiModel.hpp"

#include <config/PlatformCompat.hpp>

namespace core::ui::modulation::adsr {

FLASHMEM void formatDuration(
    char* out,
    std::size_t size,
    uint16_t duration,
    core::state::modulation::ModulatorTimingMode timing
) {
    using namespace core::state::modulation;
    if (out == nullptr || size == 0U) return;
    if (timing == ModulatorTimingMode::FREE) {
        if (duration >= 1000U) {
            const uint32_t hundredths =
                (static_cast<uint32_t>(duration) + 5U) / 10U;
            std::snprintf(
                out,
                size,
                "%u.%02us",
                static_cast<unsigned>(hundredths / 100U),
                static_cast<unsigned>(hundredths % 100U)
            );
        } else {
            std::snprintf(out, size, "%ums", static_cast<unsigned>(duration));
        }
        return;
    }

    constexpr const char* labels[]{
        "0", "1/64", "1/32", "1/16", "1/8", "1/4", "1/2",
        "1 bar", "2 bars", "4 bars", "8 bars", "16 bars",
    };
    for (uint8_t index = 0U;
         index < MODULATOR_ENVELOPE_SYNC_BASE_TICKS.size();
         ++index) {
        if (duration == MODULATOR_ENVELOPE_SYNC_BASE_TICKS[index]) {
            std::snprintf(out, size, "%s", labels[index]);
            return;
        }
    }

    const uint32_t tenths =
        (static_cast<uint32_t>(duration) * 10U +
         MODULATOR_ENVELOPE_TICKS_PER_BEAT / 2U) /
        MODULATOR_ENVELOPE_TICKS_PER_BEAT;
    std::snprintf(
        out,
        size,
        "%u.%u beats",
        static_cast<unsigned>(tenths / 10U),
        static_cast<unsigned>(tenths % 10U)
    );
}

}  // namespace core::ui::modulation::adsr
