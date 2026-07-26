#include "state/sequencer/SequencerPatternRegionOps.hpp"

#include <algorithm>
#include <cstdint>

#include <config/PlatformCompat.hpp>
#include <oc/note/sequencer/StepBitMask128.hpp>

#include "state/sequencer/SequencerPatternState.hpp"

namespace core::state::sequencer {

namespace {

constexpr SequencerPatternPlaybackRegion invalidRegion() {
    return {0, 0, 0, 0};
}

FLASHMEM uint8_t clampContentLength(uint16_t value) {
    return static_cast<uint8_t>(std::clamp<uint16_t>(
        value,
        SequencerPatternPlaybackRegion::MIN_CONTENT_LENGTH,
        SequencerPatternPlaybackRegion::MAX_CONTENT_LENGTH
    ));
}

FLASHMEM SequencerPatternPlaybackRegion repairBounds(
    SequencerPatternPlaybackRegion region
) {
    region.contentLength = clampContentLength(region.contentLength);
    const uint8_t lastStep = static_cast<uint8_t>(region.contentLength - 1U);
    region.playStart = std::min(region.playStart, lastStep);
    region.loopStart = std::clamp(region.loopStart, region.playStart, lastStep);
    region.loopEnd = std::clamp<uint8_t>(
        region.loopEnd,
        static_cast<uint8_t>(region.loopStart + 1U),
        region.contentLength
    );
    return region;
}

FLASHMEM uint8_t shiftBoundaryForInsert(
    uint8_t boundary,
    uint8_t insertAt,
    uint8_t insertedLength
) {
    if (boundary < insertAt) return boundary;
    return static_cast<uint8_t>(
        std::min<uint16_t>(
            SequencerPatternPlaybackRegion::MAX_CONTENT_LENGTH,
            static_cast<uint16_t>(boundary) + insertedLength
        )
    );
}

FLASHMEM uint8_t shiftBoundaryForRemoval(
    uint8_t boundary,
    uint8_t removeAt,
    uint8_t removeEnd,
    uint8_t removedLength
) {
    if (boundary <= removeAt) return boundary;
    if (boundary >= removeEnd) {
        return static_cast<uint8_t>(boundary - removedLength);
    }
    return removeAt;
}

}  // namespace

FLASHMEM SequencerPatternPlaybackRegion patternPlaybackRegion(
    const SequencerPatternState& pattern
) {
    return {
        pattern.length.get(),
        pattern.playStart,
        pattern.loopStart,
        pattern.loopEnd,
    };
}

FLASHMEM SequencerPatternPlaybackRegion resizedPatternPlaybackRegion(
    const SequencerPatternPlaybackRegion& region,
    uint8_t newContentLength
) {
    if (!region.isValid() ||
        newContentLength < SequencerPatternPlaybackRegion::MIN_CONTENT_LENGTH ||
        newContentLength > SequencerPatternPlaybackRegion::MAX_CONTENT_LENGTH) {
        return invalidRegion();
    }

    SequencerPatternPlaybackRegion next = region;
    const bool loopFollowedContent = region.loopEnd == region.contentLength;
    next.contentLength = newContentLength;
    if (newContentLength > region.contentLength && loopFollowedContent) {
        next.loopEnd = newContentLength;
    }
    return repairBounds(next);
}

FLASHMEM SequencerPatternPlaybackRegion insertedPatternPlaybackRegion(
    const SequencerPatternPlaybackRegion& region,
    uint8_t insertAt,
    uint8_t insertedLength
) {
    if (!region.isValid() || insertedLength == 0U || insertAt > region.contentLength ||
        static_cast<uint16_t>(region.contentLength) + insertedLength >
            SequencerPatternPlaybackRegion::MAX_CONTENT_LENGTH) {
        return invalidRegion();
    }

    SequencerPatternPlaybackRegion next{
        static_cast<uint8_t>(region.contentLength + insertedLength),
        shiftBoundaryForInsert(region.playStart, insertAt, insertedLength),
        shiftBoundaryForInsert(region.loopStart, insertAt, insertedLength),
        shiftBoundaryForInsert(region.loopEnd, insertAt, insertedLength),
    };
    return repairBounds(next);
}

FLASHMEM SequencerPatternPlaybackRegion removedPatternPlaybackRegion(
    const SequencerPatternPlaybackRegion& region,
    uint8_t removeAt,
    uint8_t removedLength
) {
    const uint16_t removeEndWide = static_cast<uint16_t>(removeAt) + removedLength;
    if (!region.isValid() || removedLength == 0U || removeAt >= region.contentLength ||
        removeEndWide > region.contentLength || removedLength >= region.contentLength) {
        return invalidRegion();
    }

    const uint8_t removeEnd = static_cast<uint8_t>(removeEndWide);
    SequencerPatternPlaybackRegion next{
        static_cast<uint8_t>(region.contentLength - removedLength),
        shiftBoundaryForRemoval(region.playStart, removeAt, removeEnd, removedLength),
        shiftBoundaryForRemoval(region.loopStart, removeAt, removeEnd, removedLength),
        shiftBoundaryForRemoval(region.loopEnd, removeAt, removeEnd, removedLength),
    };
    return repairBounds(next);
}

FLASHMEM bool setPatternPlaybackRegion(
    SequencerPatternState& pattern,
    const SequencerPatternPlaybackRegion& region
) {
    if (!region.isValid()) return false;

    const auto current = patternPlaybackRegion(pattern);
    if (current.contentLength == region.contentLength &&
        current.playStart == region.playStart &&
        current.loopStart == region.loopStart &&
        current.loopEnd == region.loopEnd) {
        return false;
    }

    bool ccChanged = false;
    if (region.contentLength < current.contentLength && pattern.ccLanes) {
        ccChanged = trimSequencerCcLaneBank(*pattern.ccLanes, region.contentLength);
    }

    // Publish the observable length Signal last so listeners never see a new
    // length paired with stale markers.
    pattern.playStart = region.playStart;
    pattern.loopStart = region.loopStart;
    pattern.loopEnd = region.loopEnd;
    pattern.length.set(region.contentLength);
    pattern.enabledMask.set(
        pattern.enabledMask.get() &
        oc::note::sequencer::StepBitMask128::prefixMask(region.contentLength)
    );
    pattern.bumpPatternTimingRevision();
    if (ccChanged) pattern.bumpCcLaneRevision();
    return true;
}

FLASHMEM bool resizePatternContent(
    SequencerPatternState& pattern,
    uint8_t newContentLength
) {
    const auto next = resizedPatternPlaybackRegion(
        patternPlaybackRegion(pattern),
        newContentLength
    );
    return next.isValid() && setPatternPlaybackRegion(pattern, next);
}

FLASHMEM bool SequencerPatternState::setContentLength(uint8_t newContentLength) {
    return resizePatternContent(*this, newContentLength);
}

FLASHMEM bool insertPatternRegionSpan(
    SequencerPatternState& pattern,
    uint8_t insertAt,
    uint8_t insertedLength
) {
    const auto next = insertedPatternPlaybackRegion(
        patternPlaybackRegion(pattern),
        insertAt,
        insertedLength
    );
    return next.isValid() && setPatternPlaybackRegion(pattern, next);
}

FLASHMEM bool removePatternRegionSpan(
    SequencerPatternState& pattern,
    uint8_t removeAt,
    uint8_t removedLength
) {
    const auto next = removedPatternPlaybackRegion(
        patternPlaybackRegion(pattern),
        removeAt,
        removedLength
    );
    return next.isValid() && setPatternPlaybackRegion(pattern, next);
}

}  // namespace core::state::sequencer
