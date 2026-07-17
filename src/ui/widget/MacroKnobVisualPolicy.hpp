#pragma once

#include <cstdint>

namespace core::ui::macro_knob_visual {

inline constexpr uint16_t START_ANGLE = 135;
inline constexpr uint16_t SWEEP_DEGREES = 270;
inline constexpr uint16_t END_ANGLE = START_ANGLE + SWEEP_DEGREES;
inline constexpr uint16_t IDLE_MARK_DEGREES = 6;
inline constexpr int16_t MODULATION_RAIL_WIDTH = 3;
inline constexpr int16_t INVALIDATION_MARGIN = 2;

struct ArcSpan {
    uint16_t start = START_ANGLE;
    uint16_t end = START_ANGLE;
};

struct ResolvedInvalidationPlan {
    uint16_t previousMainAngle = START_ANGLE;
    uint16_t nextMainAngle = START_ANGLE;
    ArcSpan previousRail{};
    ArcSpan nextRail{};
    bool mainArcChanged = false;
    bool railChanged = false;
};

/** Visible signed contribution span, including the zero-delta active mark. */
constexpr ArcSpan modulationSpan(uint16_t baseAngle, uint16_t outputAngle) {
    ArcSpan span{
        baseAngle < outputAngle ? baseAngle : outputAngle,
        baseAngle < outputAngle ? outputAngle : baseAngle,
    };
    if (span.start != span.end) return span;
    if (span.end < END_ANGLE) {
        span.end = static_cast<uint16_t>(span.end + IDLE_MARK_DEGREES);
    } else {
        span.start = static_cast<uint16_t>(span.start - IDLE_MARK_DEGREES);
    }
    return span;
}

/**
 * Place the rail against the Base arc. Integer rounding can overlap by half a
 * pixel, but never introduces an empty gutter between the two trajectories.
 */
constexpr uint16_t modulationRailRadius(
    uint16_t baseRadius,
    int16_t baseWidth
) {
    return static_cast<uint16_t>(
        baseRadius + (baseWidth + MODULATION_RAIL_WIDTH) / 2
    );
}

constexpr int16_t modulationRailInvalidationWidth() {
    return MODULATION_RAIL_WIDTH + INVALIDATION_MARGIN;
}

/** Exact hot-path damage plan: one main-arc delta plus old/new rail spans. */
constexpr ResolvedInvalidationPlan resolvedInvalidationPlan(
    bool modulationActive,
    uint16_t previousBaseAngle,
    uint16_t previousOutputAngle,
    bool previousClipped,
    uint16_t nextBaseAngle,
    uint16_t nextOutputAngle,
    bool nextClipped
) {
    const uint16_t previousMain = modulationActive
        ? previousBaseAngle
        : previousOutputAngle;
    const uint16_t nextMain = modulationActive
        ? nextBaseAngle
        : nextOutputAngle;
    return {
        previousMain,
        nextMain,
        modulationSpan(previousBaseAngle, previousOutputAngle),
        modulationSpan(nextBaseAngle, nextOutputAngle),
        previousMain != nextMain,
        modulationActive &&
            (previousBaseAngle != nextBaseAngle ||
             previousOutputAngle != nextOutputAngle ||
             previousClipped != nextClipped),
    };
}

}  // namespace core::ui::macro_knob_visual
