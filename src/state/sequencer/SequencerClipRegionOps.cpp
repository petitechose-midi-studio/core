#include "state/sequencer/SequencerClipRegionOps.hpp"

#include <algorithm>
#include <cstdint>

#include <config/PlatformCompat.hpp>
#include <oc/note/clock/ClockConstants.hpp>
#include <oc/note/sequencer/StepBitMask128.hpp>

#include "state/sequencer/SequencerPatternState.hpp"
#include "state/sequencer/SequencerState.hpp"

namespace core::state::sequencer {
namespace {

constexpr SequencerClipPlaybackRegion invalidRegion() noexcept {
    return {0U, 0U, 0U, 0U};
}

FLASHMEM uint8_t clampContentLength(uint16_t value) noexcept {
    return static_cast<uint8_t>(std::clamp<uint16_t>(
        value,
        SequencerClipPlaybackRegion::MIN_CONTENT_LENGTH,
        SequencerClipPlaybackRegion::MAX_CONTENT_LENGTH
    ));
}

FLASHMEM SequencerClipPlaybackRegion repairBounds(
    SequencerClipPlaybackRegion region
) noexcept {
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
) noexcept {
    if (boundary < insertAt) return boundary;
    return static_cast<uint8_t>(std::min<uint16_t>(
        SequencerClipPlaybackRegion::MAX_CONTENT_LENGTH,
        static_cast<uint16_t>(boundary) + insertedLength
    ));
}

FLASHMEM uint8_t shiftBoundaryForRemoval(
    uint8_t boundary,
    uint8_t removeAt,
    uint8_t removeEnd,
    uint8_t removedLength
) noexcept {
    if (boundary <= removeAt) return boundary;
    if (boundary >= removeEnd) {
        return static_cast<uint8_t>(boundary - removedLength);
    }
    return removeAt;
}

FLASHMEM bool publishActiveClipMutation(
    SequencerState& sequencer,
    bool changed
) noexcept {
    if (changed) sequencer.bumpClipRevision();
    return changed;
}

}  // namespace

FLASHMEM uint16_t sequencerTicksPerStep(uint8_t stepsPerBeat) noexcept {
    if (stepsPerBeat == 0U || stepsPerBeat > oc::note::clock::PPQN ||
        oc::note::clock::PPQN % stepsPerBeat != 0U) {
        return 0U;
    }
    return static_cast<uint16_t>(oc::note::clock::PPQN / stepsPerBeat);
}

FLASHMEM uint16_t patternContentEndTick(
    const SequencerPatternState& pattern
) noexcept {
    return static_cast<uint16_t>(
        pattern.length * sequencerTicksPerStep(pattern.stepsPerBeat)
    );
}

FLASHMEM bool validClipRegion(
    const SequencerPatternState& pattern,
    const SequencerClipState& clip
) noexcept {
    const uint16_t ticksPerStep = sequencerTicksPerStep(
        pattern.stepsPerBeat
    );
    const uint16_t contentEnd = patternContentEndTick(pattern);
    return ticksPerStep != 0U && contentEnd != 0U &&
           clip.playStartTick % ticksPerStep == 0U &&
           clip.loopStartTick % ticksPerStep == 0U &&
           clip.loopEndTick % ticksPerStep == 0U &&
           clip.playStartTick <= clip.loopStartTick &&
           clip.loopStartTick < clip.loopEndTick &&
           clip.loopEndTick <= contentEnd;
}

FLASHMEM SequencerClipPlaybackRegion clipPlaybackRegion(
    const SequencerPatternState& pattern,
    const SequencerClipState& clip
) noexcept {
    if (!validClipRegion(pattern, clip)) return invalidRegion();
    const uint16_t ticksPerStep = sequencerTicksPerStep(
        pattern.stepsPerBeat
    );
    return {
        pattern.length,
        static_cast<uint8_t>(clip.playStartTick / ticksPerStep),
        static_cast<uint8_t>(clip.loopStartTick / ticksPerStep),
        static_cast<uint8_t>(clip.loopEndTick / ticksPerStep),
    };
}

FLASHMEM SequencerClipState clipStateForPlaybackRegion(
    const SequencerPatternState& pattern,
    const SequencerClipPlaybackRegion& region
) noexcept {
    const uint16_t ticksPerStep = sequencerTicksPerStep(
        pattern.stepsPerBeat
    );
    if (!region.isValid() || region.contentLength != pattern.length ||
        ticksPerStep == 0U) {
        return {};
    }
    return {
        static_cast<uint16_t>(region.playStart * ticksPerStep),
        static_cast<uint16_t>(region.loopStart * ticksPerStep),
        static_cast<uint16_t>(region.loopEnd * ticksPerStep),
    };
}

FLASHMEM void resetClipToPattern(
    SequencerClipState& clip,
    const SequencerPatternState& pattern
) noexcept {
    clip.playStartTick = 0U;
    clip.loopStartTick = 0U;
    clip.loopEndTick = patternContentEndTick(pattern);
}

FLASHMEM SequencerClipPlaybackRegion resizedClipPlaybackRegion(
    const SequencerClipPlaybackRegion& region,
    uint8_t newContentLength
) {
    if (!region.isValid() ||
        newContentLength < SequencerClipPlaybackRegion::MIN_CONTENT_LENGTH ||
        newContentLength > SequencerClipPlaybackRegion::MAX_CONTENT_LENGTH) {
        return invalidRegion();
    }
    SequencerClipPlaybackRegion next = region;
    const bool loopFollowedContent = region.loopEnd == region.contentLength;
    next.contentLength = newContentLength;
    if (newContentLength > region.contentLength && loopFollowedContent) {
        next.loopEnd = newContentLength;
    }
    return repairBounds(next);
}

FLASHMEM SequencerClipPlaybackRegion insertedClipPlaybackRegion(
    const SequencerClipPlaybackRegion& region,
    uint8_t insertAt,
    uint8_t insertedLength
) {
    if (!region.isValid() || insertedLength == 0U ||
        insertAt > region.contentLength ||
        static_cast<uint16_t>(region.contentLength) + insertedLength >
            SequencerClipPlaybackRegion::MAX_CONTENT_LENGTH) {
        return invalidRegion();
    }
    return repairBounds({
        static_cast<uint8_t>(region.contentLength + insertedLength),
        shiftBoundaryForInsert(region.playStart, insertAt, insertedLength),
        shiftBoundaryForInsert(region.loopStart, insertAt, insertedLength),
        shiftBoundaryForInsert(region.loopEnd, insertAt, insertedLength),
    });
}

FLASHMEM SequencerClipPlaybackRegion removedClipPlaybackRegion(
    const SequencerClipPlaybackRegion& region,
    uint8_t removeAt,
    uint8_t removedLength
) {
    const uint16_t removeEndWide = static_cast<uint16_t>(removeAt) +
        removedLength;
    if (!region.isValid() || removedLength == 0U ||
        removeAt >= region.contentLength ||
        removeEndWide > region.contentLength ||
        removedLength >= region.contentLength) {
        return invalidRegion();
    }
    const uint8_t removeEnd = static_cast<uint8_t>(removeEndWide);
    return repairBounds({
        static_cast<uint8_t>(region.contentLength - removedLength),
        shiftBoundaryForRemoval(
            region.playStart, removeAt, removeEnd, removedLength
        ),
        shiftBoundaryForRemoval(
            region.loopStart, removeAt, removeEnd, removedLength
        ),
        shiftBoundaryForRemoval(
            region.loopEnd, removeAt, removeEnd, removedLength
        ),
    });
}

FLASHMEM bool setClipPlaybackRegion(
    SequencerPatternState& pattern,
    SequencerClipState& clip,
    const SequencerClipPlaybackRegion& region
) {
    if (!region.isValid()) return false;
    const uint16_t ticksPerStep = sequencerTicksPerStep(
        pattern.stepsPerBeat
    );
    if (ticksPerStep == 0U) return false;
    const SequencerClipState next{
        static_cast<uint16_t>(region.playStart * ticksPerStep),
        static_cast<uint16_t>(region.loopStart * ticksPerStep),
        static_cast<uint16_t>(region.loopEnd * ticksPerStep),
    };
    const bool clipChanged = clip.playStartTick != next.playStartTick ||
        clip.loopStartTick != next.loopStartTick ||
        clip.loopEndTick != next.loopEndTick;
    const bool lengthChanged = pattern.length != region.contentLength;
    if (!clipChanged && !lengthChanged) return false;

    clip = next;
    if (lengthChanged) pattern.setContentLength(region.contentLength);
    return true;
}

FLASHMEM bool resizeClipPatternContent(
    SequencerPatternState& pattern,
    SequencerClipState& clip,
    uint8_t newContentLength
) {
    return setClipPlaybackRegion(
        pattern,
        clip,
        resizedClipPlaybackRegion(
            clipPlaybackRegion(pattern, clip),
            newContentLength
        )
    );
}

FLASHMEM bool insertClipPatternSpan(
    SequencerPatternState& pattern,
    SequencerClipState& clip,
    uint8_t insertAt,
    uint8_t insertedLength
) {
    return setClipPlaybackRegion(
        pattern,
        clip,
        insertedClipPlaybackRegion(
            clipPlaybackRegion(pattern, clip),
            insertAt,
            insertedLength
        )
    );
}

FLASHMEM bool removeClipPatternSpan(
    SequencerPatternState& pattern,
    SequencerClipState& clip,
    uint8_t removeAt,
    uint8_t removedLength
) {
    return setClipPlaybackRegion(
        pattern,
        clip,
        removedClipPlaybackRegion(
            clipPlaybackRegion(pattern, clip),
            removeAt,
            removedLength
        )
    );
}

FLASHMEM bool setClipPatternStepsPerBeat(
    SequencerPatternState& pattern,
    SequencerClipState& clip,
    uint8_t stepsPerBeat
) {
    const auto region = clipPlaybackRegion(pattern, clip);
    if (!region.isValid() || sequencerTicksPerStep(stepsPerBeat) == 0U ||
        pattern.stepsPerBeat == stepsPerBeat) {
        return false;
    }
    const uint16_t ticksPerStep = sequencerTicksPerStep(stepsPerBeat);
    const SequencerClipState next{
        static_cast<uint16_t>(region.playStart * ticksPerStep),
        static_cast<uint16_t>(region.loopStart * ticksPerStep),
        static_cast<uint16_t>(region.loopEnd * ticksPerStep),
    };
    clip = next;
    pattern.setStepsPerBeat(stepsPerBeat);
    pattern.bumpPatternTimingRevision();
    return true;
}

FLASHMEM bool setClipPlaybackRegion(
    SequencerState& sequencer,
    const SequencerClipPlaybackRegion& region
) {
    return publishActiveClipMutation(
        sequencer,
        setClipPlaybackRegion(sequencer.pattern(), sequencer.clip(), region)
    );
}

FLASHMEM bool resizeClipPatternContent(
    SequencerState& sequencer,
    uint8_t newContentLength
) {
    return publishActiveClipMutation(
        sequencer,
        resizeClipPatternContent(
            sequencer.pattern(),
            sequencer.clip(),
            newContentLength
        )
    );
}

FLASHMEM bool insertClipPatternSpan(
    SequencerState& sequencer,
    uint8_t insertAt,
    uint8_t insertedLength
) {
    return publishActiveClipMutation(
        sequencer,
        insertClipPatternSpan(
            sequencer.pattern(),
            sequencer.clip(),
            insertAt,
            insertedLength
        )
    );
}

FLASHMEM bool removeClipPatternSpan(
    SequencerState& sequencer,
    uint8_t removeAt,
    uint8_t removedLength
) {
    return publishActiveClipMutation(
        sequencer,
        removeClipPatternSpan(
            sequencer.pattern(),
            sequencer.clip(),
            removeAt,
            removedLength
        )
    );
}

FLASHMEM bool setClipPatternStepsPerBeat(
    SequencerState& sequencer,
    uint8_t stepsPerBeat
) {
    return publishActiveClipMutation(
        sequencer,
        setClipPatternStepsPerBeat(
            sequencer.pattern(),
            sequencer.clip(),
            stepsPerBeat
        )
    );
}

FLASHMEM bool SequencerPatternState::setContentLength(
    uint8_t newContentLength
) {
    if (newContentLength < SequencerClipPlaybackRegion::MIN_CONTENT_LENGTH ||
        newContentLength > SequencerClipPlaybackRegion::MAX_CONTENT_LENGTH ||
        length == newContentLength) {
        return false;
    }

    bool ccChanged = false;
    if (newContentLength < length && ccLanes) {
        ccChanged = trimSequencerCcLaneBank(*ccLanes, newContentLength);
    }
    setEnabledMask(
        enabledMask &
        oc::note::sequencer::StepBitMask128::prefixMask(newContentLength)
    );
    setLength(newContentLength);
    bumpPatternTimingRevision();
    if (ccChanged) bumpCcLaneRevision();
    return true;
}

}  // namespace core::state::sequencer
