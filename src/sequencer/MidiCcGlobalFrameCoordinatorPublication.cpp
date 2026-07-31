#include "sequencer/MidiCcGlobalFrameCoordinator.hpp"

#include <algorithm>

#include <config/PlatformCompat.hpp>
#include <oc/realtime/InterruptGuard.hpp>

#include "sequencer/MidiCcCandidateSemantics.hpp"
#include "state/macro/MacroConstants.hpp"

namespace core::sequencer {
namespace {

using core::state::shared::MidiCcCandidate;
using core::state::shared::MidiCcCandidateClass;
using core::state::shared::MidiCcDestinationIdentity;

FLASHMEM bool validPersistentClass(MidiCcCandidateClass candidateClass) {
    return candidateClass == MidiCcCandidateClass::LIVE_MANUAL ||
           candidateClass == MidiCcCandidateClass::MACRO_COMPUTED ||
           candidateClass == MidiCcCandidateClass::MACRO_STATIC;
}

FLASHMEM bool validCandidateBody(const MidiCcCandidate& candidate) {
    const auto validity = candidate.destination.routeValidity;
    if ((validity != core::state::shared::MidiCcRouteValidity::VALID &&
         validity != core::state::shared::MidiCcRouteValidity::NO_ROUTE) ||
        candidate.destination.identity.controller > 127U ||
        candidate.localValue > 127U) {
        return false;
    }

    const auto& identity = candidate.destination.identity;
    if (validity == core::state::shared::MidiCcRouteValidity::VALID) {
        return identity.port != MidiCcDestinationIdentity::INVALID_PORT &&
               identity.channel <= 15U;
    }
    return identity.channel <= 15U ||
           identity.channel == MidiCcDestinationIdentity::INVALID_CHANNEL;
}

FLASHMEM bool validPersistentPair(
    const MidiCcCandidate& lhs,
    const MidiCcCandidate& rhs
) {
    if (lhs.author.stableAddress != rhs.author.stableAddress) return true;
    const bool lhsLive = lhs.author.candidateClass ==
        MidiCcCandidateClass::LIVE_MANUAL;
    const bool rhsLive = rhs.author.candidateClass ==
        MidiCcCandidateClass::LIVE_MANUAL;
    // Base-vs-Base duplication is rejected by sameTemporalAuthorSlot. A Live
    // and its Base may coexist only on one exact physical destination.
    return lhsLive == rhsLive || midi_cc::sameDestination(lhs, rhs);
}

FLASHMEM bool sameTemporalAuthorSlot(
    const core::state::shared::MidiCcAuthor& lhs,
    const core::state::shared::MidiCcAuthor& rhs
) {
    uint16_t lhsSlot = 0U;
    uint16_t rhsSlot = 0U;
    return TemporalMidiCcAuthorSpool::authorSlotIndex(lhs, lhsSlot) &&
           TemporalMidiCcAuthorSpool::authorSlotIndex(rhs, rhsSlot) &&
           lhsSlot == rhsSlot;
}

bool samePersistentFrame(
    const MidiCcPersistentAuthorFrame& frame,
    const MidiCcCandidate* candidates,
    uint16_t candidateCount
) {
    if (frame.candidateCount != candidateCount) return false;
    for (uint16_t index = 0U; index < candidateCount; ++index) {
        if (!midi_cc::sameCandidate(
                frame.candidates[index],
                candidates[index]
            )) {
            return false;
        }
    }
    return true;
}

template <typename Frames>
uint8_t writableSourceIndex(
    const Frames& frames,
    uint8_t activeIndex,
    uint8_t readingIndex
) {
    for (uint8_t index = 0U; index < frames.size(); ++index) {
        if (index != activeIndex && index != readingIndex) return index;
    }
    // Three buffers and at most one active plus one reader make this
    // unreachable. Fail closed rather than overwrite either protected frame.
    return 0xFFU;
}

FLASHMEM uint32_t nextRevision(uint32_t& revision) {
    const uint32_t result = revision++;
    if (revision == 0U) revision = 1U;
    return result;
}

}  // namespace

bool MidiCcGlobalFrameCoordinator::publishPersistentAuthors(
    const MidiCcCandidate* candidates,
    size_t candidateCount
) {
    if (candidateCount > MidiCcPersistentAuthorFrame::MAX_CANDIDATES ||
        (candidateCount > 0U && candidates == nullptr)) {
        return false;
    }
    constexpr uint16_t MACRO_AUTHOR_COUNT =
        core::state::macro::TRACK_COUNT *
        core::state::macro::PAGE_COUNT *
        core::state::macro::MACRO_COUNT;
    for (size_t index = 0U; index < candidateCount; ++index) {
        if (!validPersistentClass(candidates[index].author.candidateClass) ||
            !validCandidateBody(candidates[index]) ||
            candidates[index].author.stableAddress >= MACRO_AUTHOR_COUNT ||
            (candidates[index].destination.routeValidity ==
                 core::state::shared::MidiCcRouteValidity::VALID &&
             candidates[index].destination.identity.port != output_port_)) {
            return false;
        }
        for (size_t previous = 0U; previous < index; ++previous) {
            if (sameTemporalAuthorSlot(
                    candidates[previous].author,
                    candidates[index].author
                ) ||
                !validPersistentPair(
                    candidates[previous],
                    candidates[index]
                )) {
                return false;
            }
        }
    }

    uint8_t writeIndex = 0U;
    {
        oc::realtime::InterruptGuard lock;
        writeIndex = writableSourceIndex(
            persistent_frames_,
            active_persistent_index_,
            reading_persistent_index_
        );
    }
    if (writeIndex == NO_SOURCE_READER) return false;
    auto& frame = persistent_frames_[writeIndex];
    frame.candidateCount = static_cast<uint16_t>(candidateCount);
    if (candidateCount > 0U) {
        std::copy_n(candidates, candidateCount, frame.candidates.begin());
    }
    if (samePersistentFrame(
            persistent_frames_[active_persistent_index_],
            frame.candidates.data(),
            frame.candidateCount
        )) {
        return true;
    }
    frame.revision = nextRevision(next_persistent_revision_);
    {
        oc::realtime::InterruptGuard lock;
        active_persistent_index_ = writeIndex;
    }
    diagnostics_.publishedPersistentFrameCount =
        realtimeMidiSaturatingAdd(
            diagnostics_.publishedPersistentFrameCount,
            1U
        );
    return true;
}

bool MidiCcGlobalFrameCoordinator::publishPersistentAuthorsGenerated(
    PersistentAuthorProducer producer,
    void* context
) {
    if (producer == nullptr) return false;
    uint8_t writeIndex = 0U;
    {
        oc::realtime::InterruptGuard lock;
        writeIndex = writableSourceIndex(
            persistent_frames_,
            active_persistent_index_,
            reading_persistent_index_
        );
    }
    if (writeIndex == NO_SOURCE_READER) return false;
    auto& frame = persistent_frames_[writeIndex];
    uint16_t candidateCount = 0U;
    if (!producer(
            context,
            frame.candidates.data(),
            static_cast<uint16_t>(frame.candidates.size()),
            candidateCount
        ) ||
        candidateCount > frame.candidates.size()) {
        return false;
    }
    constexpr uint16_t MACRO_AUTHOR_COUNT =
        core::state::macro::TRACK_COUNT *
        core::state::macro::PAGE_COUNT *
        core::state::macro::MACRO_COUNT;
    for (uint16_t index = 0U; index < candidateCount; ++index) {
        const auto& candidate = frame.candidates[index];
        if (!validPersistentClass(candidate.author.candidateClass) ||
            !validCandidateBody(candidate) ||
            candidate.author.stableAddress >= MACRO_AUTHOR_COUNT ||
            (candidate.destination.routeValidity ==
                 core::state::shared::MidiCcRouteValidity::VALID &&
             candidate.destination.identity.port != output_port_)) {
            return false;
        }
        for (uint16_t previous = 0U; previous < index; ++previous) {
            if (sameTemporalAuthorSlot(
                    frame.candidates[previous].author,
                    candidate.author
                ) ||
                !validPersistentPair(
                    frame.candidates[previous],
                    candidate
                )) {
                return false;
            }
        }
    }
    frame.candidateCount = candidateCount;
    if (samePersistentFrame(
            persistent_frames_[active_persistent_index_],
            frame.candidates.data(),
            frame.candidateCount
        )) {
        return true;
    }
    frame.revision = nextRevision(next_persistent_revision_);
    {
        oc::realtime::InterruptGuard lock;
        active_persistent_index_ = writeIndex;
    }
    diagnostics_.publishedPersistentFrameCount =
        realtimeMidiSaturatingAdd(
            diagnostics_.publishedPersistentFrameCount,
            1U
        );
    return true;
}

bool MidiCcGlobalFrameCoordinator::upsertPersistentAuthor(
    const MidiCcCandidate& candidate,
    uint16_t& publishedCandidateCount
) {
    publishedCandidateCount = 0U;
    constexpr uint16_t MACRO_AUTHOR_COUNT =
        core::state::macro::TRACK_COUNT *
        core::state::macro::PAGE_COUNT *
        core::state::macro::MACRO_COUNT;
    if (candidate.author.candidateClass !=
            MidiCcCandidateClass::LIVE_MANUAL ||
        !validCandidateBody(candidate) ||
        candidate.author.stableAddress >= MACRO_AUTHOR_COUNT ||
        (candidate.destination.routeValidity ==
             core::state::shared::MidiCcRouteValidity::VALID &&
         candidate.destination.identity.port != output_port_)) {
        return false;
    }

    uint8_t writeIndex = 0U;
    uint8_t sourceIndex = 0U;
    {
        oc::realtime::InterruptGuard lock;
        sourceIndex = active_persistent_index_;
        writeIndex = writableSourceIndex(
            persistent_frames_,
            active_persistent_index_,
            reading_persistent_index_
        );
    }
    if (writeIndex == NO_SOURCE_READER) return false;

    const auto& source = persistent_frames_[sourceIndex];
    auto& frame = persistent_frames_[writeIndex];
    frame.candidateCount = source.candidateCount;
    if (source.candidateCount > 0U) {
        std::copy_n(
            source.candidates.begin(),
            source.candidateCount,
            frame.candidates.begin()
        );
    }

    uint16_t targetSlot = 0U;
    if (!TemporalMidiCcAuthorSpool::authorSlotIndex(
            candidate.author,
            targetSlot
        )) {
        return false;
    }
    int replacementIndex = -1;
    bool matchingBasePresent = false;
    for (uint16_t index = 0U; index < frame.candidateCount; ++index) {
        const auto& existing = frame.candidates[index];
        if (existing.author.stableAddress ==
                candidate.author.stableAddress &&
            (existing.author.candidateClass ==
                 MidiCcCandidateClass::MACRO_COMPUTED ||
             existing.author.candidateClass ==
                 MidiCcCandidateClass::MACRO_STATIC) &&
            midi_cc::sameDestination(existing, candidate)) {
            matchingBasePresent = true;
        }
        uint16_t sourceSlot = 0U;
        if (!TemporalMidiCcAuthorSpool::authorSlotIndex(
                frame.candidates[index].author,
                sourceSlot
            ) ||
            sourceSlot != targetSlot) {
            continue;
        }
        replacementIndex = static_cast<int>(index);
    }
    // Immediate input may only layer a LIVE author on top of the same exact
    // physical destination as its already-published Macro Base.
    if (!matchingBasePresent) return false;
    if (replacementIndex >= 0) {
        frame.candidates[static_cast<uint16_t>(replacementIndex)] = candidate;
    } else {
        if (frame.candidateCount >= frame.candidates.size()) return false;
        frame.candidates[frame.candidateCount++] = candidate;
    }
    publishedCandidateCount = frame.candidateCount;

    if (samePersistentFrame(
            source,
            frame.candidates.data(),
            frame.candidateCount
        )) {
        return true;
    }
    frame.revision = nextRevision(next_persistent_revision_);
    {
        oc::realtime::InterruptGuard lock;
        active_persistent_index_ = writeIndex;
    }
    diagnostics_.publishedPersistentFrameCount =
        realtimeMidiSaturatingAdd(
            diagnostics_.publishedPersistentFrameCount,
            1U
        );
    return true;
}

bool MidiCcGlobalFrameCoordinator::publishSequencerLanes(
    const SequencerCcLaneRuntimeFrame& source
) {
    if (!source.ok() || source.candidateCount > source.candidates.size()) {
        return false;
    }
    for (uint8_t index = 0U; index < source.candidateCount; ++index) {
        if (source.candidates[index].author.candidateClass !=
                MidiCcCandidateClass::SEQUENCER_CC_LANE ||
            !validCandidateBody(source.candidates[index]) ||
            source.candidates[index].author.stableAddress >=
                SequencerCcLaneRuntime::ADDRESS_COUNT ||
            (source.candidates[index].destination.routeValidity ==
                 core::state::shared::MidiCcRouteValidity::VALID &&
             source.candidates[index].destination.identity.port !=
                 output_port_)) {
            return false;
        }
        for (uint8_t previous = 0U; previous < index; ++previous) {
            if (sameTemporalAuthorSlot(
                    source.candidates[previous].author,
                    source.candidates[index].author
                )) {
                return false;
            }
        }
    }

    uint8_t writeIndex = 0U;
    uint8_t activeIndex = 0U;
    {
        oc::realtime::InterruptGuard lock;
        activeIndex = active_lane_index_;
        writeIndex = writableSourceIndex(
            lane_frames_,
            activeIndex,
            reading_lane_index_
        );
    }
    if (writeIndex == NO_SOURCE_READER) return false;
    auto& frame = lane_frames_[writeIndex];
    frame.candidateCount = source.candidateCount;
    std::copy_n(
        source.candidates.begin(),
        source.candidateCount,
        frame.candidates.begin()
    );
    frame.lifecycleGenerations = source.lifecycleGenerations;
    frame.predictiveAuthorMask = source.predictiveAuthorMask;
    const auto& active = lane_frames_[activeIndex];
    bool semanticallyIdentical =
        active.candidateCount == frame.candidateCount &&
        active.lifecycleGenerations == frame.lifecycleGenerations &&
        active.predictiveAuthorMask == frame.predictiveAuthorMask;
    for (uint8_t index = 0U;
         semanticallyIdentical && index < frame.candidateCount;
         ++index) {
        semanticallyIdentical = midi_cc::sameCandidate(
            active.candidates[index],
            frame.candidates[index]
        );
    }
    if (semanticallyIdentical) return true;
    frame.revision = nextRevision(next_lane_revision_);
    {
        oc::realtime::InterruptGuard lock;
        active_lane_index_ = writeIndex;
    }
    diagnostics_.publishedLaneFrameCount =
        realtimeMidiSaturatingAdd(
            diagnostics_.publishedLaneFrameCount,
            1U
        );
    return true;
}

bool MidiCcGlobalFrameCoordinator::captureCombinedCandidates_() {
    uint8_t persistentIndex = 0U;
    uint8_t laneIndex = 0U;
    {
        oc::realtime::InterruptGuard lock;
        persistentIndex = active_persistent_index_;
        laneIndex = active_lane_index_;
        reading_persistent_index_ = persistentIndex;
        reading_lane_index_ = laneIndex;
    }
    const auto& persistent = persistent_frames_[persistentIndex];
    const auto& lanes = lane_frames_[laneIndex];
    const uint32_t total =
        static_cast<uint32_t>(persistent.candidateCount) +
        static_cast<uint32_t>(lanes.candidateCount);
    const bool valid = total <= combined_candidates_.size();
    if (valid) {
        combined_candidate_count_ = static_cast<uint16_t>(total);
        std::copy_n(
            persistent.candidates.begin(),
            persistent.candidateCount,
            combined_candidates_.begin()
        );
        std::copy_n(
            lanes.candidates.begin(),
            lanes.candidateCount,
            combined_candidates_.begin() + persistent.candidateCount
        );
        captured_persistent_revision_ = persistent.revision;
        captured_lane_revision_ = lanes.revision;
        captured_lane_lifecycle_generations_ = lanes.lifecycleGenerations;
        captured_lane_predictive_author_mask_ =
            lanes.predictiveAuthorMask;
    }
    {
        oc::realtime::InterruptGuard lock;
        reading_persistent_index_ = NO_SOURCE_READER;
        reading_lane_index_ = NO_SOURCE_READER;
    }
    return valid;
}

}  // namespace core::sequencer
