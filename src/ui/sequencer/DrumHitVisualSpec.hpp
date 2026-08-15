#pragma once

#include <cstdint>

namespace core::ui::sequencer::drum_hit_visual {

/**
 * Layout-neutral visual properties for one Drum hit.
 *
 * The overview renderer maps Gate to horizontal span, Velocity to visual
 * weight and Nudge to onset displacement. A resolved event may be rendered as
 * a quiet outline beside the authored hit.
 */
struct DrumHitVisualSpec {
    uint16_t gatePercent = 100U;
    uint8_t velocity = 64U;
    int8_t nudge = 0;
    uint32_t color = 0U;
    uint8_t opacity = 255U;
    bool ghost = false;
};

inline DrumHitVisualSpec build(
    uint8_t velocity,
    uint16_t gatePercent,
    int8_t nudge,
    uint32_t color,
    bool active,
    bool ghost = false
) {
    const uint8_t opacity = ghost
        ? 102U
        : active
            ? static_cast<uint8_t>(
                  96U +
                  (static_cast<uint16_t>(velocity) * 144U) / 127U
              )
            : 255U;
    return {
        .gatePercent = gatePercent,
        .velocity = velocity,
        .nudge = nudge,
        .color = color,
        .opacity = opacity,
        .ghost = ghost,
    };
}

}  // namespace core::ui::sequencer::drum_hit_visual
