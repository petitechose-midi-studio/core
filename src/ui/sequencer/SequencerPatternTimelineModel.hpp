#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include <oc/note/sequencer/StepBitMask128.hpp>

#include "state/sequencer/SequencerCcLaneDomain.hpp"

namespace core::state::sequencer {
struct SequencerPatternState;
struct SequencerPatternSnapshot;
}

namespace core::ui::sequencer {

inline constexpr uint16_t SEQUENCER_PATTERN_TIMELINE_MAX_WIDTH = 320U;
inline constexpr uint8_t SEQUENCER_PATTERN_TIMELINE_MAX_CC_LANES = 4U;
inline constexpr uint8_t SEQUENCER_PATTERN_TIMELINE_INVALID_LANE = UINT8_MAX;

enum class SequencerPatternTimelineLayer : uint8_t {
    ACTIVITY = 1U << 0U,
    NOTE = 1U << 1U,
    VELOCITY = 1U << 2U,
    GATE = 1U << 3U,
    NUDGE = 1U << 4U,
    CC_LANES = 1U << 5U,
    PLAYBACK_REGION = 1U << 6U,
    WINDOW_BAND = 1U << 7U,
};

inline constexpr uint8_t SEQUENCER_PATTERN_TIMELINE_ALL_LAYERS = UINT8_MAX;

[[nodiscard]] constexpr uint8_t sequencerPatternTimelineLayerBit(
    SequencerPatternTimelineLayer layer
) {
    return static_cast<uint8_t>(layer);
}

/**
 * Physical retained surface and the currently focused eight-step window.
 *
 * Width is capped to the hardware's long screen axis. Height deliberately
 * remains uint8_t because the retained curves store one absolute Y byte per
 * physical X column.
 */
struct SequencerPatternTimelineViewport {
    uint16_t width = 0U;
    uint8_t height = 0U;
    uint8_t windowStartStep = 0U;
    uint8_t windowStepCount = 8U;
    uint8_t layerMask = SEQUENCER_PATTERN_TIMELINE_ALL_LAYERS;
    uint8_t ccLaneMask = 0x0FU;
};

/** Exact cold-geometry cache key. Runtime/playhead facts are excluded. */
struct SequencerPatternTimelineRebuildKey {
    uint32_t stepDataRevision = 0U;
    uint32_t patternTimingRevision = 0U;
    uint32_t ccLaneRevision = 0U;
    uint32_t ccBankRevision = 0U;
    // Zero for live Signal-backed state. Snapshot previews use an exact cold
    // content fingerprint because different deterministic drafts may inherit
    // the same persisted revision.
    uint32_t sourceFingerprint = 0U;
    uint64_t activeStepsLow = 0U;
    uint64_t activeStepsHigh = 0U;
    uint16_t width = 0U;
    uint8_t height = 0U;
    uint8_t contentLength = 0U;
    uint8_t playStart = 0U;
    uint8_t loopStart = 0U;
    uint8_t loopEnd = 0U;
    uint8_t windowStartStep = 0U;
    uint8_t windowStepCount = 0U;
    uint8_t layerMask = 0U;
    uint8_t ccLaneMask = 0U;

    constexpr bool operator==(
        const SequencerPatternTimelineRebuildKey& other
    ) const {
        return stepDataRevision == other.stepDataRevision &&
            patternTimingRevision == other.patternTimingRevision &&
            ccLaneRevision == other.ccLaneRevision &&
            ccBankRevision == other.ccBankRevision &&
            sourceFingerprint == other.sourceFingerprint &&
            activeStepsLow == other.activeStepsLow &&
            activeStepsHigh == other.activeStepsHigh &&
            width == other.width && height == other.height &&
            contentLength == other.contentLength &&
            playStart == other.playStart && loopStart == other.loopStart &&
            loopEnd == other.loopEnd &&
            windowStartStep == other.windowStartStep &&
            windowStepCount == other.windowStepCount &&
            layerMask == other.layerMask && ccLaneMask == other.ccLaneMask;
    }
};

/**
 * Per-step retained facts. X remains implicit and is derived from step index,
 * Content Length, nudge and gate through the helpers below.
 */
struct SequencerPatternTimelineStepGeometry {
    uint16_t gatePercent = 0U;
    uint8_t noteY = 0U;
    uint8_t velocityY = 0U;
    uint8_t probabilityY = 0U;
    int8_t nudgePercent = 0;
};

/**
 * Single retained geometry owner for the visible Pattern Editor.
 *
 * The eventual widget owns exactly one instance through ExtmemUniquePtr. The
 * model itself is POD: rebuilding and drawing require no allocation, and no
 * per-step/per-point LVGL object is represented here.
 */
struct SequencerPatternTimelineGeometry {
    static constexpr std::size_t CC_VALID_BYTES_PER_LANE =
        (SEQUENCER_PATTERN_TIMELINE_MAX_WIDTH + 7U) / 8U;

    SequencerPatternTimelineRebuildKey key{};
    std::array<SequencerPatternTimelineStepGeometry, 128U> steps{};
    oc::note::sequencer::StepBitMask128 activeSteps{};
    std::array<
        std::array<uint8_t, SEQUENCER_PATTERN_TIMELINE_MAX_WIDTH>,
        SEQUENCER_PATTERN_TIMELINE_MAX_CC_LANES
    > ccY{};
    std::array<
        std::array<uint8_t, CC_VALID_BYTES_PER_LANE>,
        SEQUENCER_PATTERN_TIMELINE_MAX_CC_LANES
    > ccValid{};
    std::array<uint8_t, SEQUENCER_PATTERN_TIMELINE_MAX_CC_LANES>
        sourceLaneIndex{};
    uint16_t playStartX = 0U;
    uint16_t loopStartX = 0U;
    uint16_t loopEndX = 0U;
    uint16_t windowStartX = 0U;
    uint16_t windowEndX = 0U;
    uint8_t ccLaneCount = 0U;
};

/** Validate input and derive the exact cache key without changing geometry. */
[[nodiscard]] bool makeSequencerPatternTimelineRebuildKey(
    const core::state::sequencer::SequencerPatternState& pattern,
    const core::state::sequencer::SequencerCcLaneBank* ccLanes,
    const SequencerPatternTimelineViewport& viewport,
    SequencerPatternTimelineRebuildKey& out
);

[[nodiscard]] bool makeSequencerPatternTimelineRebuildKey(
    const core::state::sequencer::SequencerPatternSnapshot& snapshot,
    const core::state::sequencer::SequencerCcLaneBank* ccLanes,
    const SequencerPatternTimelineViewport& viewport,
    SequencerPatternTimelineRebuildKey& out
);

/**
 * Rebuild all cold geometry in place. Invalid input leaves `out` untouched.
 * The function performs no heap allocation and is intended to write directly
 * into a PSRAM-owned geometry instance.
 */
[[nodiscard]] bool rebuildSequencerPatternTimelineGeometry(
    const core::state::sequencer::SequencerPatternState& pattern,
    const core::state::sequencer::SequencerCcLaneBank* ccLanes,
    const SequencerPatternTimelineViewport& viewport,
    SequencerPatternTimelineGeometry& out
);

/** Exact non-published preview path used by deterministic Randomize drafts. */
[[nodiscard]] bool rebuildSequencerPatternTimelineGeometry(
    const core::state::sequencer::SequencerPatternSnapshot& snapshot,
    const core::state::sequencer::SequencerCcLaneBank* ccLanes,
    const SequencerPatternTimelineViewport& viewport,
    SequencerPatternTimelineGeometry& out
);

[[nodiscard]] bool sequencerPatternTimelineCcSampleValid(
    const SequencerPatternTimelineGeometry& geometry,
    uint8_t laneSlot,
    uint16_t x
);

/** Exact step-boundary projection; Content Length maps to width. */
[[nodiscard]] uint16_t sequencerPatternTimelineBoundaryX(
    const SequencerPatternTimelineGeometry& geometry,
    uint8_t stepBoundary
);

/** Implicit note onset including signed Pattern nudge, clipped to the surface. */
[[nodiscard]] uint16_t sequencerPatternTimelineStepOnsetX(
    const SequencerPatternTimelineGeometry& geometry,
    uint8_t step
);

/** Implicit exclusive gate end, clipped to [0,width]. */
[[nodiscard]] uint16_t sequencerPatternTimelineStepGateEndX(
    const SequencerPatternTimelineGeometry& geometry,
    uint8_t step
);

struct SequencerPatternTimelinePlayhead {
    uint16_t column = 0U;
    bool visible = false;
};

/**
 * O(1) hot projection through the common Prelude/Loop resolver.
 * Geometry is const and no allocation or rebuild is performed.
 */
[[nodiscard]] bool projectSequencerPatternTimelinePlayhead(
    const SequencerPatternTimelineGeometry& geometry,
    uint32_t playbackOrdinal,
    uint16_t fractionInStepQ16,
    SequencerPatternTimelinePlayhead& out
);

/**
 * Adapter for Core's current resolved local-step telemetry. It rejects steps
 * outside Prelude/Loop, derives a representative ordinal and delegates to the
 * common region projection above.
 */
[[nodiscard]] bool projectSequencerPatternTimelinePlayheadLocalStep(
    const SequencerPatternTimelineGeometry& geometry,
    int16_t localStep,
    uint16_t fractionInStepQ16,
    SequencerPatternTimelinePlayhead& out
);

struct SequencerPatternTimelineDamageBand {
    uint16_t x = 0U;
    uint16_t width = 0U;
    uint8_t y = 0U;
    uint8_t height = 0U;
};

struct SequencerPatternTimelinePlayheadDamage {
    std::array<SequencerPatternTimelineDamageBand, 2U> bands{};
    uint8_t count = 0U;
};

/** At most the old and new vertical playhead bands; never full-surface damage. */
[[nodiscard]] SequencerPatternTimelinePlayheadDamage
sequencerPatternTimelinePlayheadDamage(
    const SequencerPatternTimelineGeometry& geometry,
    SequencerPatternTimelinePlayhead previous,
    SequencerPatternTimelinePlayhead next,
    uint8_t bandWidth = 3U
);

static_assert(sizeof(SequencerPatternTimelineStepGeometry) <= 6U);
static_assert(std::is_trivially_copyable_v<SequencerPatternTimelineGeometry>);
static_assert(sizeof(SequencerPatternTimelineGeometry) < 4096U);

}  // namespace core::ui::sequencer
