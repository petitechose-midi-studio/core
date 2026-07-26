#include "ui/sequencer/SequencerPatternTimelineModel.hpp"

#include <algorithm>

#include <config/PlatformCompat.hpp>
#include <oc/note/sequencer/StepSequencerPlaybackRegion.hpp>

#include "state/sequencer/SequencerCcLaneProjectionOps.hpp"
#include "state/sequencer/SequencerPatternRegionOps.hpp"
#include "state/sequencer/SequencerPatternState.hpp"
#include "state/sequencer/SequencerSnapshots.hpp"

namespace core::ui::sequencer {
namespace {

namespace seq = core::state::sequencer;

struct TimelinePatternSource {
    seq::SequencerPatternPlaybackRegion region{};
    oc::note::sequencer::StepBitMask128 enabledMask{};
    uint32_t stepDataRevision = 0U;
    uint32_t patternTimingRevision = 0U;
    uint32_t ccLaneRevision = 0U;
    uint32_t sourceFingerprint = 0U;
    const uint8_t* note = nullptr;
    const uint8_t* velocity = nullptr;
    const uint8_t* probability = nullptr;
    const uint16_t* gate = nullptr;
    const int8_t* nudge = nullptr;
};

[[nodiscard]] TimelinePatternSource timelineSource(
    const seq::SequencerPatternState& pattern
) {
    return {
        .region = seq::patternPlaybackRegion(pattern),
        .enabledMask = pattern.enabledMask.get(),
        .stepDataRevision = pattern.stepDataRevision.get(),
        .patternTimingRevision = pattern.patternTimingRevision.get(),
        .ccLaneRevision = pattern.ccLaneRevision.get(),
        .sourceFingerprint = 0U,
        .note = pattern.note.data(),
        .velocity = pattern.velocity.data(),
        .probability = pattern.probability.data(),
        .gate = pattern.gate.data(),
        .nudge = pattern.nudge.data(),
    };
}

constexpr uint32_t FNV_OFFSET = 2166136261U;
constexpr uint32_t FNV_PRIME = 16777619U;

void hashByte(uint32_t& hash, uint8_t value) {
    hash = (hash ^ value) * FNV_PRIME;
}

void hashU16(uint32_t& hash, uint16_t value) {
    hashByte(hash, static_cast<uint8_t>(value));
    hashByte(hash, static_cast<uint8_t>(value >> 8U));
}

void hashU64(uint32_t& hash, uint64_t value) {
    for (uint8_t shift = 0U; shift < 64U; shift += 8U) {
        hashByte(hash, static_cast<uint8_t>(value >> shift));
    }
}

[[nodiscard]] FLASHMEM uint32_t snapshotContentFingerprint(
    const seq::SequencerPatternSnapshot& snapshot
) {
    uint32_t hash = FNV_OFFSET;
    hashU64(hash, snapshot.enabledMask.low);
    hashU64(hash, snapshot.enabledMask.high);
    const uint8_t length = std::min<uint8_t>(
        snapshot.length,
        seq::SequencerPatternState::MAX_STEPS
    );
    for (uint16_t step = 0U; step < length; ++step) {
        hashByte(hash, snapshot.note[step]);
        hashByte(hash, snapshot.velocity[step]);
        hashByte(hash, snapshot.probability[step]);
        hashU16(hash, snapshot.gate[step]);
        hashByte(hash, static_cast<uint8_t>(snapshot.nudge[step]));
    }
    return hash == 0U ? 1U : hash;
}

[[nodiscard]] TimelinePatternSource timelineSource(
    const seq::SequencerPatternSnapshot& snapshot
) {
    return {
        .region = {
            snapshot.length,
            snapshot.playStart,
            snapshot.loopStart,
            snapshot.loopEnd,
        },
        .enabledMask = snapshot.enabledMask,
        .stepDataRevision = snapshot.stepDataRevision,
        .patternTimingRevision = snapshot.patternTimingRevision,
        .ccLaneRevision = 0U,
        .sourceFingerprint = snapshotContentFingerprint(snapshot),
        .note = snapshot.note.data(),
        .velocity = snapshot.velocity.data(),
        .probability = snapshot.probability.data(),
        .gate = snapshot.gate.data(),
        .nudge = snapshot.nudge.data(),
    };
}

[[nodiscard]] constexpr bool layerEnabled(
    uint8_t mask,
    SequencerPatternTimelineLayer layer
) {
    return (mask & sequencerPatternTimelineLayerBit(layer)) != 0U;
}

[[nodiscard]] constexpr uint8_t valueY(
    uint8_t value,
    uint8_t height
) {
    if (height <= 1U) return 0U;
    const uint16_t maximumY = static_cast<uint16_t>(height - 1U);
    const uint16_t ascending = static_cast<uint16_t>(
        (static_cast<uint32_t>(std::min<uint8_t>(value, 127U)) * maximumY +
         63U) /
        127U
    );
    return static_cast<uint8_t>(maximumY - ascending);
}

[[nodiscard]] constexpr uint8_t probabilityY(
    uint8_t probability,
    uint8_t height
) {
    const uint8_t midiScaled = static_cast<uint8_t>(
        (static_cast<uint16_t>(std::min<uint8_t>(probability, 100U)) * 127U +
         50U) /
        100U
    );
    return valueY(midiScaled, height);
}

[[nodiscard]] constexpr uint16_t boundaryX(
    uint16_t width,
    uint8_t contentLength,
    uint8_t stepBoundary
) {
    if (width == 0U || contentLength == 0U) return 0U;
    const uint8_t bounded = std::min(stepBoundary, contentLength);
    return static_cast<uint16_t>(
        (static_cast<uint32_t>(bounded) * width) / contentLength
    );
}

[[nodiscard]] constexpr uint16_t clippedTimelineX(
    int64_t timeHundredths,
    uint16_t width,
    uint8_t contentLength,
    bool allowRightBoundary
) {
    if (width == 0U || contentLength == 0U || timeHundredths <= 0) return 0U;
    const int64_t denominator = static_cast<int64_t>(contentLength) * 100;
    const int64_t projected =
        (timeHundredths * static_cast<int64_t>(width)) / denominator;
    const uint16_t maximum = allowRightBoundary
        ? width
        : static_cast<uint16_t>(width - 1U);
    if (projected >= maximum) return maximum;
    return static_cast<uint16_t>(projected);
}

void setCcValid(
    SequencerPatternTimelineGeometry& geometry,
    uint8_t laneSlot,
    uint16_t x
) {
    geometry.ccValid[laneSlot][x >> 3U] |= static_cast<uint8_t>(
        1U << (x & 7U)
    );
}

[[nodiscard]] bool validateTimelineInput(
    const TimelinePatternSource& source,
    const seq::SequencerCcLaneBank* ccLanes,
    const SequencerPatternTimelineViewport& viewport,
    seq::SequencerPatternPlaybackRegion& region,
    uint8_t& clippedWindowCount
) {
    if (viewport.width == 0U ||
        viewport.width > SEQUENCER_PATTERN_TIMELINE_MAX_WIDTH ||
        viewport.height == 0U ||
        viewport.windowStepCount == 0U ||
        (viewport.ccLaneMask & 0xF0U) != 0U) {
        return false;
    }
    region = source.region;
    if (!region.isValid() || source.note == nullptr ||
        source.velocity == nullptr || source.probability == nullptr ||
        source.gate == nullptr ||
        source.nudge == nullptr ||
        viewport.windowStartStep >= region.contentLength) {
        return false;
    }
    if (ccLanes != nullptr && !seq::validSequencerCcLaneBank(*ccLanes)) {
        return false;
    }
    clippedWindowCount = static_cast<uint8_t>(std::min<uint16_t>(
        viewport.windowStepCount,
        static_cast<uint16_t>(region.contentLength - viewport.windowStartStep)
    ));
    return clippedWindowCount > 0U;
}

[[nodiscard]] bool sampleCcAtColumn(
    const seq::SequencerCcLane& lane,
    const seq::SequencerPatternPlaybackRegion& region,
    uint16_t width,
    uint16_t x,
    uint8_t& outValue
) {
    // Sample at the physical pixel centre. This gives exactly one projection
    // per displayed X column, independent of Pattern length.
    const uint32_t denominator = static_cast<uint32_t>(width) * 2U;
    const uint32_t scaled =
        (static_cast<uint32_t>(x) * 2U + 1U) * region.contentLength;
    const uint8_t step = static_cast<uint8_t>(scaled / denominator);
    const uint32_t remainder = scaled % denominator;
    const float fraction = static_cast<float>(remainder) /
        static_cast<float>(denominator);
    uint32_t representativeOrdinal = 0U;
    if (!seq::representativeSequencerCcLaneOrdinalForStep(
            region,
            step,
            representativeOrdinal
        )) {
        return false;
    }
    return seq::projectSequencerCcLaneValue(
        lane,
        region,
        representativeOrdinal,
        fraction,
        outValue
    );
}

[[nodiscard]] SequencerPatternTimelineDamageBand makeDamageBand(
    const SequencerPatternTimelineGeometry& geometry,
    uint16_t column,
    uint8_t requestedWidth
) {
    const uint16_t surfaceWidth = geometry.key.width;
    if (surfaceWidth == 0U || geometry.key.height == 0U) return {};
    const uint16_t width = std::min<uint16_t>(
        std::max<uint8_t>(requestedWidth, 1U),
        surfaceWidth
    );
    const uint16_t half = static_cast<uint16_t>(width / 2U);
    uint16_t x = column > half ? static_cast<uint16_t>(column - half) : 0U;
    if (x + width > surfaceWidth) {
        x = static_cast<uint16_t>(surfaceWidth - width);
    }
    return {
        .x = x,
        .width = width,
        .y = 0U,
        .height = geometry.key.height,
    };
}

[[nodiscard]] constexpr bool sameGeometryKeyExceptWindow(
    const SequencerPatternTimelineRebuildKey& lhs,
    const SequencerPatternTimelineRebuildKey& rhs
) {
    return lhs.stepDataRevision == rhs.stepDataRevision &&
           lhs.patternTimingRevision == rhs.patternTimingRevision &&
           lhs.ccLaneRevision == rhs.ccLaneRevision &&
           lhs.ccBankRevision == rhs.ccBankRevision &&
           lhs.sourceFingerprint == rhs.sourceFingerprint &&
           lhs.activeStepsLow == rhs.activeStepsLow &&
           lhs.activeStepsHigh == rhs.activeStepsHigh &&
           lhs.width == rhs.width &&
           lhs.height == rhs.height &&
           lhs.contentLength == rhs.contentLength &&
           lhs.playStart == rhs.playStart &&
           lhs.loopStart == rhs.loopStart &&
           lhs.loopEnd == rhs.loopEnd &&
           lhs.layerMask == rhs.layerMask &&
           lhs.ccLaneMask == rhs.ccLaneMask;
}

void updateWindowBand(
    SequencerPatternTimelineGeometry& geometry,
    const SequencerPatternTimelineRebuildKey& key
) {
    geometry.windowStartX = 0U;
    geometry.windowEndX = 0U;
    if (!layerEnabled(
            key.layerMask,
            SequencerPatternTimelineLayer::WINDOW_BAND
        )) {
        return;
    }
    geometry.windowStartX = boundaryX(
        key.width,
        key.contentLength,
        key.windowStartStep
    );
    geometry.windowEndX = boundaryX(
        key.width,
        key.contentLength,
        static_cast<uint8_t>(key.windowStartStep + key.windowStepCount)
    );
}

FLASHMEM bool makeRebuildKeyFromSource(
    const TimelinePatternSource& source,
    const seq::SequencerCcLaneBank* ccLanes,
    const SequencerPatternTimelineViewport& viewport,
    SequencerPatternTimelineRebuildKey& out
) {
    seq::SequencerPatternPlaybackRegion region{};
    uint8_t clippedWindowCount = 0U;
    if (!validateTimelineInput(
            source,
            ccLanes,
            viewport,
            region,
            clippedWindowCount
        )) {
        return false;
    }
    const auto active = source.enabledMask;
    out = {
        .stepDataRevision = source.stepDataRevision,
        .patternTimingRevision = source.patternTimingRevision,
        .ccLaneRevision = source.ccLaneRevision,
        .ccBankRevision = ccLanes == nullptr ? 0U : ccLanes->revision,
        .sourceFingerprint = source.sourceFingerprint,
        .activeStepsLow = active.low,
        .activeStepsHigh = active.high,
        .width = viewport.width,
        .height = viewport.height,
        .contentLength = region.contentLength,
        .playStart = region.playStart,
        .loopStart = region.loopStart,
        .loopEnd = region.loopEnd,
        .windowStartStep = viewport.windowStartStep,
        .windowStepCount = clippedWindowCount,
        .layerMask = viewport.layerMask,
        .ccLaneMask = viewport.ccLaneMask,
    };
    return true;
}

FLASHMEM bool rebuildFromSource(
    const TimelinePatternSource& source,
    const seq::SequencerCcLaneBank* ccLanes,
    const SequencerPatternTimelineViewport& viewport,
    SequencerPatternTimelineGeometry& out
) {
    SequencerPatternTimelineRebuildKey key{};
    if (!makeRebuildKeyFromSource(
            source,
            ccLanes,
            viewport,
            key
        )) {
        return false;
    }

    if (out.key == key) return true;
    if (sameGeometryKeyExceptWindow(out.key, key)) {
        // Window navigation moves one retained band only. Notes and the four
        // pixel-sampled CC curves remain valid and are not projected again.
        out.key = key;
        updateWindowBand(out, key);
        return true;
    }

    // The caller owns this buffer in PSRAM. Reset it in place rather than
    // constructing a multi-kilobyte temporary on the embedded stack. Keeping
    // the reset typed also avoids relying on object-representation details.
    out.key = {};
    out.steps.fill({});
    out.activeSteps = {};
    for (auto& lane : out.ccY) lane.fill(0U);
    for (auto& lane : out.ccValid) lane.fill(0U);
    out.playStartX = 0U;
    out.loopStartX = 0U;
    out.loopEndX = 0U;
    out.windowStartX = 0U;
    out.windowEndX = 0U;
    out.ccLaneCount = 0U;
    out.key = key;
    out.sourceLaneIndex.fill(SEQUENCER_PATTERN_TIMELINE_INVALID_LANE);

    const bool showActivity = layerEnabled(
        key.layerMask,
        SequencerPatternTimelineLayer::ACTIVITY
    );
    if (showActivity) {
        out.activeSteps = source.enabledMask &
            oc::note::sequencer::StepBitMask128::prefixMask(key.contentLength);
    }
    const bool showNote = layerEnabled(
        key.layerMask,
        SequencerPatternTimelineLayer::NOTE
    );
    const bool showVelocity = layerEnabled(
        key.layerMask,
        SequencerPatternTimelineLayer::VELOCITY
    );
    const bool showGate = layerEnabled(
        key.layerMask,
        SequencerPatternTimelineLayer::GATE
    );
    const bool showNudge = layerEnabled(
        key.layerMask,
        SequencerPatternTimelineLayer::NUDGE
    );
    for (uint16_t step = 0U; step < key.contentLength; ++step) {
        const uint16_t gatePercent = showGate
            ? static_cast<uint16_t>(std::min<uint16_t>(
                source.gate[step],
                seq::SequencerPatternState::MAX_GATE_PERCENT
            ))
            : uint16_t{0U};
        const uint8_t noteY = showNote
            ? static_cast<uint8_t>(valueY(source.note[step], key.height))
            : uint8_t{0U};
        const uint8_t velocityY = showVelocity
            ? static_cast<uint8_t>(valueY(source.velocity[step], key.height))
            : uint8_t{0U};
        const uint8_t chanceY = showActivity
            ? probabilityY(source.probability[step], key.height)
            : uint8_t{0U};
        const int8_t nudgePercent = showNudge
            ? static_cast<int8_t>(seq::SequencerPatternState::clampNudge(
                source.nudge[step]
            ))
            : int8_t{0};
        out.steps[step] = {
            .gatePercent = gatePercent,
            .noteY = noteY,
            .velocityY = velocityY,
            .probabilityY = chanceY,
            .nudgePercent = nudgePercent,
        };
    }

    if (layerEnabled(
            key.layerMask,
            SequencerPatternTimelineLayer::PLAYBACK_REGION
        )) {
        out.playStartX = boundaryX(key.width, key.contentLength, key.playStart);
        out.loopStartX = boundaryX(key.width, key.contentLength, key.loopStart);
        out.loopEndX = boundaryX(key.width, key.contentLength, key.loopEnd);
    }
    updateWindowBand(out, key);

    if (ccLanes == nullptr || !layerEnabled(
            key.layerMask,
            SequencerPatternTimelineLayer::CC_LANES
        )) {
        return true;
    }
    const seq::SequencerPatternPlaybackRegion region{
        key.contentLength,
        key.playStart,
        key.loopStart,
        key.loopEnd,
    };
    for (uint8_t sourceIndex = 0U;
         sourceIndex < ccLanes->lanes.size() &&
             out.ccLaneCount < SEQUENCER_PATTERN_TIMELINE_MAX_CC_LANES;
         ++sourceIndex) {
        if ((key.ccLaneMask & (1U << sourceIndex)) == 0U) continue;
        const auto& lane = ccLanes->lanes[sourceIndex];
        if (!lane.occupied) continue;
        const uint8_t laneSlot = out.ccLaneCount++;
        out.sourceLaneIndex[laneSlot] = sourceIndex;
        for (uint16_t x = 0U; x < key.width; ++x) {
            uint8_t projected = 0U;
            if (!sampleCcAtColumn(lane, region, key.width, x, projected)) {
                continue;
            }
            out.ccY[laneSlot][x] = valueY(projected, key.height);
            setCcValid(out, laneSlot, x);
        }
    }
    return true;
}

}  // namespace

FLASHMEM bool makeSequencerPatternTimelineRebuildKey(
    const seq::SequencerPatternState& pattern,
    const seq::SequencerCcLaneBank* ccLanes,
    const SequencerPatternTimelineViewport& viewport,
    SequencerPatternTimelineRebuildKey& out
) {
    return makeRebuildKeyFromSource(
        timelineSource(pattern),
        ccLanes,
        viewport,
        out
    );
}

FLASHMEM bool makeSequencerPatternTimelineRebuildKey(
    const seq::SequencerPatternSnapshot& snapshot,
    const seq::SequencerCcLaneBank* ccLanes,
    const SequencerPatternTimelineViewport& viewport,
    SequencerPatternTimelineRebuildKey& out
) {
    return makeRebuildKeyFromSource(
        timelineSource(snapshot),
        ccLanes,
        viewport,
        out
    );
}

FLASHMEM bool rebuildSequencerPatternTimelineGeometry(
    const seq::SequencerPatternState& pattern,
    const seq::SequencerCcLaneBank* ccLanes,
    const SequencerPatternTimelineViewport& viewport,
    SequencerPatternTimelineGeometry& out
) {
    return rebuildFromSource(
        timelineSource(pattern),
        ccLanes,
        viewport,
        out
    );
}

FLASHMEM bool rebuildSequencerPatternTimelineGeometry(
    const seq::SequencerPatternSnapshot& snapshot,
    const seq::SequencerCcLaneBank* ccLanes,
    const SequencerPatternTimelineViewport& viewport,
    SequencerPatternTimelineGeometry& out
) {
    return rebuildFromSource(
        timelineSource(snapshot),
        ccLanes,
        viewport,
        out
    );
}

FLASHMEM bool sequencerPatternTimelineCcSampleValid(
    const SequencerPatternTimelineGeometry& geometry,
    uint8_t laneSlot,
    uint16_t x
) {
    if (laneSlot >= geometry.ccLaneCount || x >= geometry.key.width ||
        x >= SEQUENCER_PATTERN_TIMELINE_MAX_WIDTH) {
        return false;
    }
    return (geometry.ccValid[laneSlot][x >> 3U] &
            static_cast<uint8_t>(1U << (x & 7U))) != 0U;
}

FLASHMEM uint16_t sequencerPatternTimelineBoundaryX(
    const SequencerPatternTimelineGeometry& geometry,
    uint8_t stepBoundary
) {
    return boundaryX(
        geometry.key.width,
        geometry.key.contentLength,
        stepBoundary
    );
}

FLASHMEM uint16_t sequencerPatternTimelineStepOnsetX(
    const SequencerPatternTimelineGeometry& geometry,
    uint8_t step
) {
    if (step >= geometry.key.contentLength) return 0U;
    const auto& retained = geometry.steps[step];
    const int64_t time = static_cast<int64_t>(step) * 100 +
        retained.nudgePercent;
    return clippedTimelineX(
        time,
        geometry.key.width,
        geometry.key.contentLength,
        false
    );
}

FLASHMEM uint16_t sequencerPatternTimelineStepGateEndX(
    const SequencerPatternTimelineGeometry& geometry,
    uint8_t step
) {
    if (step >= geometry.key.contentLength) return 0U;
    const auto& retained = geometry.steps[step];
    const int64_t time = static_cast<int64_t>(step) * 100 +
        retained.nudgePercent + retained.gatePercent;
    return clippedTimelineX(
        time,
        geometry.key.width,
        geometry.key.contentLength,
        true
    );
}

bool projectSequencerPatternTimelinePlayhead(
    const SequencerPatternTimelineGeometry& geometry,
    uint32_t playbackOrdinal,
    uint16_t fractionInStepQ16,
    SequencerPatternTimelinePlayhead& out
) {
    const auto& key = geometry.key;
    const oc::note::sequencer::StepSequencerPlaybackRegion region{
        key.contentLength,
        key.playStart,
        key.loopStart,
        key.loopEnd,
    };
    if (key.width == 0U || key.height == 0U || !region.isValid()) return false;
    oc::note::sequencer::StepSequencerPlaybackPosition position{};
    if (!oc::note::sequencer::tryResolvePlaybackOrdinal(
            region,
            playbackOrdinal,
            position
        )) {
        return false;
    }
    constexpr uint64_t FRACTION_SCALE = 65536ULL;
    const uint64_t time =
        static_cast<uint64_t>(position.stepIndex) * FRACTION_SCALE +
        fractionInStepQ16;
    uint16_t column = static_cast<uint16_t>(
        (time * key.width) /
        (static_cast<uint64_t>(key.contentLength) * FRACTION_SCALE)
    );
    if (column >= key.width) column = static_cast<uint16_t>(key.width - 1U);
    out = {.column = column, .visible = true};
    return true;
}

bool projectSequencerPatternTimelinePlayheadLocalStep(
    const SequencerPatternTimelineGeometry& geometry,
    int16_t localStep,
    uint16_t fractionInStepQ16,
    SequencerPatternTimelinePlayhead& out
) {
    const auto& key = geometry.key;
    const oc::note::sequencer::StepSequencerPlaybackRegion region{
        key.contentLength,
        key.playStart,
        key.loopStart,
        key.loopEnd,
    };
    if (!region.isValid() || localStep < region.playStart ||
        localStep >= region.loopEnd) {
        return false;
    }
    const uint8_t step = static_cast<uint8_t>(localStep);
    const uint32_t ordinal = step < region.loopStart
        ? static_cast<uint32_t>(step - region.playStart)
        : static_cast<uint32_t>(region.preludeLength()) +
            static_cast<uint32_t>(step - region.loopStart);
    return projectSequencerPatternTimelinePlayhead(
        geometry,
        ordinal,
        fractionInStepQ16,
        out
    );
}

SequencerPatternTimelinePlayheadDamage
sequencerPatternTimelinePlayheadDamage(
    const SequencerPatternTimelineGeometry& geometry,
    SequencerPatternTimelinePlayhead previous,
    SequencerPatternTimelinePlayhead next,
    uint8_t bandWidth
) {
    SequencerPatternTimelinePlayheadDamage result{};
    if (geometry.key.width == 0U || geometry.key.height == 0U ||
        (previous.visible && next.visible &&
         previous.column == next.column)) {
        return result;
    }
    if (previous.visible) {
        result.bands[result.count++] = makeDamageBand(
            geometry,
            previous.column,
            bandWidth
        );
    }
    if (next.visible) {
        result.bands[result.count++] = makeDamageBand(
            geometry,
            next.column,
            bandWidth
        );
    }
    return result;
}

}  // namespace core::ui::sequencer
