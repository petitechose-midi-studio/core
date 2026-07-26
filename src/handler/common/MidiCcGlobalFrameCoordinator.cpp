#include "handler/common/MidiCcGlobalFrameCoordinator.hpp"

#include <algorithm>

#include <config/PlatformCompat.hpp>
#include <oc/diagnostics/Performance.hpp>
#include <oc/note/clock/ClockConstants.hpp>
#include <oc/realtime/InterruptGuard.hpp>
#include <oc/time/Time.hpp>

#include "state/macro/MacroConstants.hpp"
#include "sequencer/ProjectTrackRuntimeSnapshotBank.hpp"

namespace core::handler {

namespace {

using core::sequencer::RealtimeMidiEvent;
using core::sequencer::RealtimeMidiEventType;
using core::state::shared::MidiCcCandidate;
using core::state::shared::MidiCcCandidateClass;
using core::state::shared::MidiCcDestinationIdentity;
using core::state::shared::MidiCcResolutionMode;
using core::state::shared::MidiCcResolveStatus;

FLASHMEM bool sameIdentity(
    const MidiCcDestinationIdentity& lhs,
    const MidiCcDestinationIdentity& rhs
) {
    return core::state::shared::sameMidiCcDestinationIdentity(lhs, rhs);
}

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

bool sameCandidate(const MidiCcCandidate& lhs, const MidiCcCandidate& rhs) {
    return sameIdentity(
               lhs.destination.identity,
               rhs.destination.identity
           ) &&
           lhs.destination.routeValidity == rhs.destination.routeValidity &&
           lhs.author.candidateClass == rhs.author.candidateClass &&
           lhs.author.stableAddress == rhs.author.stableAddress &&
           lhs.localValue == rhs.localValue;
}

FLASHMEM bool sameDestination(
    const MidiCcCandidate& lhs,
    const MidiCcCandidate& rhs
) {
    return sameIdentity(lhs.destination.identity, rhs.destination.identity) &&
           lhs.destination.routeValidity == rhs.destination.routeValidity;
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
    return lhsLive == rhsLive || sameDestination(lhs, rhs);
}

FLASHMEM bool sameTemporalAuthorSlot(
    const core::state::shared::MidiCcAuthor& lhs,
    const core::state::shared::MidiCcAuthor& rhs
) {
    uint16_t lhsSlot = 0U;
    uint16_t rhsSlot = 0U;
    return core::sequencer::TemporalMidiCcAuthorSpool::authorSlotIndex(
               lhs,
               lhsSlot
           ) &&
           core::sequencer::TemporalMidiCcAuthorSpool::authorSlotIndex(
               rhs,
               rhsSlot
           ) && lhsSlot == rhsSlot;
}

bool samePersistentFrame(
    const MidiCcPersistentAuthorFrame& frame,
    const MidiCcCandidate* candidates,
    uint16_t candidateCount
) {
    if (frame.candidateCount != candidateCount) return false;
    for (uint16_t i = 0; i < candidateCount; ++i) {
        if (!sameCandidate(frame.candidates[i], candidates[i])) return false;
    }
    return true;
}

template <typename Frames>
uint8_t writableSourceIndex(
    const Frames& frames,
    uint8_t activeIndex,
    uint8_t readingIndex
) {
    for (uint8_t i = 0; i < frames.size(); ++i) {
        if (i != activeIndex && i != readingIndex) return i;
    }
    // Three buffers and at most one active plus one reader make this
    // unreachable. Fail closed rather than overwrite either protected frame.
    return 0xFF;
}

FLASHMEM uint32_t nextRevision(uint32_t& revision) {
    const uint32_t result = revision++;
    if (revision == 0) revision = 1;
    return result;
}

}  // namespace

MidiCcGlobalFrameCoordinator::TelemetryReadView::TelemetryReadView(
    const MidiCcGlobalFrameCoordinator& owner,
    const Telemetry& telemetry,
    uint8_t index
)
    : owner_(&owner)
    , telemetry_(&telemetry)
    , index_(index) {}

MidiCcGlobalFrameCoordinator::TelemetryReadView::~TelemetryReadView() {
    release_();
}

MidiCcGlobalFrameCoordinator::TelemetryReadView::TelemetryReadView(
    TelemetryReadView&& other
) noexcept
    : owner_(other.owner_)
    , telemetry_(other.telemetry_)
    , index_(other.index_) {
    other.owner_ = nullptr;
    other.telemetry_ = nullptr;
    other.index_ = 0xFF;
}

MidiCcGlobalFrameCoordinator::TelemetryReadView&
MidiCcGlobalFrameCoordinator::TelemetryReadView::operator=(
    TelemetryReadView&& other
) noexcept {
    if (this == &other) return *this;
    release_();
    owner_ = other.owner_;
    telemetry_ = other.telemetry_;
    index_ = other.index_;
    other.owner_ = nullptr;
    other.telemetry_ = nullptr;
    other.index_ = 0xFF;
    return *this;
}

void MidiCcGlobalFrameCoordinator::TelemetryReadView::release_() {
    if (owner_ != nullptr) owner_->releaseTelemetryReader_(index_);
    owner_ = nullptr;
    telemetry_ = nullptr;
    index_ = 0xFF;
}

FLASHMEM MidiCcGlobalFrameCoordinator::MidiCcGlobalFrameCoordinator(
    core::sequencer::RealtimeMidiQueue& queue,
    uint8_t outputPort
)
    : queue_(queue)
    , output_port_(outputPort) {
    queue_.attachLifecycleObserver(*this);
    lifecycle_attached_ = true;
    resetProject();
}

FLASHMEM MidiCcGlobalFrameCoordinator::~MidiCcGlobalFrameCoordinator() {
    queue_.detachLifecycleObserver(*this);
    lifecycle_attached_ = false;
}

bool MidiCcGlobalFrameCoordinator::publishPersistentAuthors(
    const MidiCcCandidate* candidates,
    size_t candidateCount
) {
    if (candidateCount > MidiCcPersistentAuthorFrame::MAX_CANDIDATES ||
        (candidateCount > 0 && candidates == nullptr)) {
        return false;
    }
    constexpr uint16_t kMacroAuthorCount =
        core::state::macro::TRACK_COUNT *
        core::state::macro::PAGE_COUNT *
        core::state::macro::MACRO_COUNT;
    for (size_t i = 0; i < candidateCount; ++i) {
        if (!validPersistentClass(candidates[i].author.candidateClass) ||
            !validCandidateBody(candidates[i]) ||
            candidates[i].author.stableAddress >= kMacroAuthorCount ||
            (candidates[i].destination.routeValidity ==
                 core::state::shared::MidiCcRouteValidity::VALID &&
             candidates[i].destination.identity.port != output_port_)) {
            return false;
        }
        for (size_t previous = 0U; previous < i; ++previous) {
            if (sameTemporalAuthorSlot(
                    candidates[previous].author,
                    candidates[i].author
                ) || !validPersistentPair(
                    candidates[previous],
                    candidates[i]
                )) return false;
        }
    }

    uint8_t writeIndex = 0;
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
    if (candidateCount > 0) {
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
        core::sequencer::realtimeMidiSaturatingAdd(
            diagnostics_.publishedPersistentFrameCount,
            1
        );
    return true;
}

bool MidiCcGlobalFrameCoordinator::publishPersistentAuthorsGenerated(
    PersistentAuthorProducer producer,
    void* context
) {
    if (producer == nullptr) return false;
    uint8_t writeIndex = 0;
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
    uint16_t candidateCount = 0;
    if (!producer(
            context,
            frame.candidates.data(),
            static_cast<uint16_t>(frame.candidates.size()),
            candidateCount
        ) || candidateCount > frame.candidates.size()) {
        return false;
    }
    constexpr uint16_t kMacroAuthorCount =
        core::state::macro::TRACK_COUNT *
        core::state::macro::PAGE_COUNT *
        core::state::macro::MACRO_COUNT;
    for (uint16_t i = 0; i < candidateCount; ++i) {
        const auto& candidate = frame.candidates[i];
        if (!validPersistentClass(candidate.author.candidateClass) ||
            !validCandidateBody(candidate) ||
            candidate.author.stableAddress >= kMacroAuthorCount ||
            (candidate.destination.routeValidity ==
                 core::state::shared::MidiCcRouteValidity::VALID &&
             candidate.destination.identity.port != output_port_)) {
            return false;
        }
        for (uint16_t previous = 0U; previous < i; ++previous) {
            if (sameTemporalAuthorSlot(
                    frame.candidates[previous].author,
                    candidate.author
                ) || !validPersistentPair(
                    frame.candidates[previous],
                    candidate
                )) return false;
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
        core::sequencer::realtimeMidiSaturatingAdd(
            diagnostics_.publishedPersistentFrameCount,
            1
        );
    return true;
}

bool MidiCcGlobalFrameCoordinator::upsertPersistentAuthor(
    const MidiCcCandidate& candidate,
    uint16_t& publishedCandidateCount
) {
    publishedCandidateCount = 0U;
    constexpr uint16_t kMacroAuthorCount =
        core::state::macro::TRACK_COUNT *
        core::state::macro::PAGE_COUNT *
        core::state::macro::MACRO_COUNT;
    if (candidate.author.candidateClass !=
            MidiCcCandidateClass::LIVE_MANUAL ||
        !validCandidateBody(candidate) ||
        candidate.author.stableAddress >= kMacroAuthorCount ||
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
    if (!core::sequencer::TemporalMidiCcAuthorSpool::authorSlotIndex(
            candidate.author,
            targetSlot
        )) {
        return false;
    }
    int replacementIndex = -1;
    bool matchingBasePresent = false;
    for (uint16_t index = 0U; index < frame.candidateCount; ++index) {
        const auto& existing = frame.candidates[index];
        if (existing.author.stableAddress == candidate.author.stableAddress &&
            (existing.author.candidateClass ==
                 MidiCcCandidateClass::MACRO_COMPUTED ||
             existing.author.candidateClass ==
                 MidiCcCandidateClass::MACRO_STATIC) &&
            sameDestination(existing, candidate)) {
            matchingBasePresent = true;
        }
        uint16_t sourceSlot = 0U;
        if (!core::sequencer::TemporalMidiCcAuthorSpool::authorSlotIndex(
                frame.candidates[index].author,
                sourceSlot
            ) || sourceSlot != targetSlot) {
            continue;
        }
        replacementIndex = static_cast<int>(index);
    }
    // Immediate input may only layer a LIVE author on top of the same exact
    // physical destination as its already-published Macro Base. This cannot
    // resurrect a stale page/route or create a second ghost CC destination.
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
        core::sequencer::realtimeMidiSaturatingAdd(
            diagnostics_.publishedPersistentFrameCount,
            1U
        );
    return true;
}

bool MidiCcGlobalFrameCoordinator::publishSequencerLanes(
    const core::sequencer::SequencerCcLaneRuntimeFrame& source
) {
    if (!source.ok() || source.candidateCount > source.candidates.size()) {
        return false;
    }
    for (uint8_t i = 0; i < source.candidateCount; ++i) {
        if (source.candidates[i].author.candidateClass !=
                MidiCcCandidateClass::SEQUENCER_CC_LANE ||
            !validCandidateBody(source.candidates[i]) ||
            source.candidates[i].author.stableAddress >=
                core::sequencer::SequencerCcLaneRuntime::ADDRESS_COUNT ||
            (source.candidates[i].destination.routeValidity ==
                 core::state::shared::MidiCcRouteValidity::VALID &&
             source.candidates[i].destination.identity.port != output_port_)) {
            return false;
        }
        for (uint8_t previous = 0U; previous < i; ++previous) {
            if (sameTemporalAuthorSlot(
                    source.candidates[previous].author,
                    source.candidates[i].author
                )) return false;
        }
    }

    uint8_t writeIndex = 0;
    uint8_t activeIndex = 0;
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
        semanticallyIdentical = sameCandidate(
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
        core::sequencer::realtimeMidiSaturatingAdd(
            diagnostics_.publishedLaneFrameCount,
            1
        );
    return true;
}

void MidiCcGlobalFrameCoordinator::publishProjectControlClock(
    uint32_t sequencerTick,
    bool playing,
    uint32_t nowUs,
    uint32_t sequencerTickPeriodUs
) {
    static_assert(
        core::state::modulation::PROJECT_CONTROL_TICKS_PER_BEAT %
            oc::note::clock::PPQN == 0U
    );
    constexpr uint32_t kProjectTicksPerSequencerTick =
        core::state::modulation::PROJECT_CONTROL_TICKS_PER_BEAT /
        oc::note::clock::PPQN;

    oc::realtime::InterruptGuard lock;
    // Stop keeps an accepted CC intent as a logical fence so unrelated Macro
    // edits can still resolve without re-emitting a held Lane. The first
    // playing clock removes that fence and reconciles once against physical
    // dispatch state. This also handles a stop/resume with no intervening
    // stopped runtime tick because discardPendingRetryForTransportStop() is
    // itself the authoritative Stop boundary.
    if (playing && transport_retry_deferred_until_resume_) {
        planned_values_valid_ = false;
        retry_requested_ = true;
        // Stop may have cancelled a future Lane author transition even when no
        // CC had reached the realtime queue. Re-stage the still-current source
        // frame explicitly; semantic publication deduplication must not be
        // defeated just to recreate that transport-boundary intent.
        source_restage_required_ = true;
        transport_retry_deferred_until_resume_ = false;
    }
    const bool resynchronized = control_clock_initialized_ &&
        sequencerTick < last_control_sequencer_tick_;
    if (!control_clock_initialized_ ||
        sequencerTick != last_control_sequencer_tick_) {
        control_tick_started_us_ = nowUs;
    }
    const uint32_t baseTick = static_cast<uint32_t>(
        static_cast<uint64_t>(sequencerTick) *
        kProjectTicksPerSequencerTick
    );
    uint32_t musicalTick = baseTick;
    uint16_t fractionQ16 = 0;
    if (playing && sequencerTickPeriodUs > 0U) {
        const uint32_t elapsedUs = std::min<uint32_t>(
            nowUs - control_tick_started_us_,
            sequencerTickPeriodUs - 1U
        );
        const uint64_t subTickQ16 =
            (static_cast<uint64_t>(elapsedUs) *
             kProjectTicksPerSequencerTick * 65536ULL) /
            sequencerTickPeriodUs;
        musicalTick = static_cast<uint32_t>(
            musicalTick + static_cast<uint32_t>(subTickQ16 >> 16U)
        );
        fractionQ16 = static_cast<uint16_t>(subTickQ16 & 0xFFFFU);
    }

    const uint32_t nowMs = oc::time::millis();
    const bool transportStarted = playing &&
        (!control_time_.playing || resynchronized || !control_clock_initialized_);
    if (transportStarted) {
        ++control_time_.transportGeneration;
        if (control_time_.transportGeneration == 0U) {
            control_time_.transportGeneration = 1U;
        }
        control_time_.transportStartMusicalTick = musicalTick;
        control_time_.transportStartMonotonicMs = nowMs;
    }
    control_time_.musicalTick = musicalTick;
    control_time_.musicalTickFractionQ16 = fractionQ16;
    control_time_.monotonicMs = nowMs;
    control_time_.playing = playing;
    control_time_.reserved = 0;
    last_control_sequencer_tick_ = sequencerTick;
    control_clock_initialized_ = true;
}

core::state::modulation::ProjectControlTimeSnapshot
MidiCcGlobalFrameCoordinator::projectControlTimeSnapshot() const {
    oc::realtime::InterruptGuard lock;
    return control_time_;
}

bool MidiCcGlobalFrameCoordinator::needsLiveResolution(uint32_t nowUs) const {
    uint32_t persistentRevision = 0U;
    uint32_t laneRevision = 0U;
    {
        oc::realtime::InterruptGuard lock;
        persistentRevision =
            persistent_frames_[active_persistent_index_].revision;
        laneRevision = lane_frames_[active_lane_index_].revision;
    }
    return retry_requested_ || source_restage_required_ || effective_dirty_ ||
           temporal_spool_.hasDue(nowUs) ||
           persistentRevision != last_live_persistent_revision_ ||
           laneRevision != last_live_lane_revision_;
}

MidiCcGlobalFrameResult MidiCcGlobalFrameCoordinator::resolveLive(
    uint32_t nowUs,
    const core::sequencer::ProjectTrackRuntimeSnapshot& projectTracks,
    uint32_t tickPeriodUs,
    bool allowPredictiveLookahead
) {
    if (!needsLiveResolution(nowUs)) {
        return {
            .status = MidiCcGlobalFrameStatus::NO_CHANGE,
            .resolveStatus = MidiCcResolveStatus::OK,
            .queueStatus = core::sequencer::RealtimeMidiQueueBatchStatus::OK,
        };
    }
    return resolve_(
        nowUs,
        projectTracks,
        tickPeriodUs,
        allowPredictiveLookahead
    );
}

FLASHMEM void MidiCcGlobalFrameCoordinator::invalidateTrack(uint8_t trackIndex) {
    if (trackIndex >= 16U) return;
    (void)temporal_spool_.cancelTrack(trackIndex);
    clearTrackAuthorStates_(trackIndex);
    source_restage_required_ = true;
    effective_dirty_ = true;
    planned_values_valid_ = false;
    retry_requested_ = true;
    diagnostics_.trackInvalidationCount =
        core::sequencer::realtimeMidiSaturatingAdd(
            diagnostics_.trackInvalidationCount,
            1U
        );
}

FLASHMEM void MidiCcGlobalFrameCoordinator::discardPendingRetryForTransportStop() {
    // queue.clear() runs before this boundary and reports every removed CC via
    // onRealtimeMidiEventRemoved(). Preserve the accepted desired set: copying
    // dispatched over it would lose a due value that had entered the queue but
    // had not reached the MIDI transport yet.
    const size_t cancelledLaneTransitions =
        temporal_spool_.cancelCandidateClass(
            MidiCcCandidateClass::SEQUENCER_CC_LANE
        );
    transport_retry_deferred_until_resume_ =
        transport_retry_deferred_until_resume_ || retry_requested_ ||
        !planned_values_valid_ || cancelledLaneTransitions > 0U;
    synchronizeStoppedLaneLogicalState_();
    // Treat the preserved desired set as logically planned only while stopped.
    // Therefore an unrelated persistent Macro publication can still emit its
    // changed destinations without replaying the removed Lane hold. Resume
    // invalidates this fence above and compares with dispatched_values_.
    planned_values_valid_ = true;
    retry_requested_ = false;
}

bool MidiCcGlobalFrameCoordinator::captureCombinedCandidates_() {
    uint8_t persistentIndex = 0;
    uint8_t laneIndex = 0;
    {
        oc::realtime::InterruptGuard lock;
        persistentIndex = active_persistent_index_;
        laneIndex = active_lane_index_;
        reading_persistent_index_ = persistentIndex;
        reading_lane_index_ = laneIndex;
    }
    const auto& persistent = persistent_frames_[persistentIndex];
    const auto& lanes = lane_frames_[laneIndex];
    const uint32_t total = static_cast<uint32_t>(persistent.candidateCount) +
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
        captured_lane_predictive_author_mask_ = lanes.predictiveAuthorMask;
    }
    {
        oc::realtime::InterruptGuard lock;
        reading_persistent_index_ = NO_SOURCE_READER;
        reading_lane_index_ = NO_SOURCE_READER;
    }
    return valid;
}

MidiCcGlobalFrameResult MidiCcGlobalFrameCoordinator::resolve_(
    uint32_t nowUs,
    const core::sequencer::ProjectTrackRuntimeSnapshot& projectTracks,
    uint32_t tickPeriodUs,
    bool allowPredictiveLookahead
) {
    OC_PERF_SCOPE(perfFrame, "midi.cc.global-frame");
    OC_PERF_UNITS(
        perfFrame,
        effective_active_slot_count_,
        static_cast<uint32_t>(temporal_spool_.size())
    );
    MidiCcGlobalFrameResult result{
        .status = MidiCcGlobalFrameStatus::NO_CHANGE,
        .resolveStatus = MidiCcResolveStatus::OK,
        .queueStatus = core::sequencer::RealtimeMidiQueueBatchStatus::OK,
    };

    uint32_t persistentRevision = 0U;
    uint32_t laneRevision = 0U;
    {
        oc::realtime::InterruptGuard lock;
        persistentRevision = persistent_frames_[active_persistent_index_].revision;
        laneRevision = lane_frames_[active_lane_index_].revision;
    }
    const bool sourceChanged = source_restage_required_ ||
        persistentRevision != last_live_persistent_revision_ ||
        laneRevision != last_live_lane_revision_;

    // Capacity back-pressure must never prevent older physical deadlines from
    // making progress. Drain the accepted generation first; only then may a
    // newer complete source frame reserve additional spool nodes.
    if (!processDueGroups_(nowUs, result)) return result;
    if (sourceChanged && temporal_spool_.hasDue(nowUs)) {
        // The bounded per-call group budget was reached. Keep the source
        // revision unconsumed and continue draining on the next runtime pass.
        return result;
    }
    if (sourceChanged) {
        if (!captureCombinedCandidates_()) {
            result.status = MidiCcGlobalFrameStatus::INVALID_SOURCE_FRAME;
            return result;
        }
        if (!stageLogicalFrame_(
                nowUs,
                projectTracks,
                tickPeriodUs,
                allowPredictiveLookahead,
                result
            )) {
            return result;
        }
        // Publish only the exact immutable generations accepted by the spool.
        last_live_persistent_revision_ = captured_persistent_revision_;
        last_live_lane_revision_ = captured_lane_revision_;
        source_restage_required_ = false;
        result.status = MidiCcGlobalFrameStatus::OK;
        // Zero/negative-clamped deadlines staged above belong to this same
        // physical pass and must arbitrate before returning.
        if (!processDueGroups_(nowUs, result)) return result;
    }
    if (effective_dirty_ || retry_requested_) {
        if (!resolveEffective_(nowUs, result)) return result;
        effective_dirty_ = false;
    }
    if (sourceChanged && result.destinationCount == 0U &&
        effective_active_slot_count_ > 0U) {
        const auto& telemetry = telemetry_frames_[published_telemetry_index_];
        result.candidateCount = telemetry.candidateCount;
        result.destinationCount = telemetry.destinationCount;
        result.conflictCount = telemetry.conflictCount;
        result.noRouteCount = telemetry.noRouteCount;
        result.eligibleEmissionCount = telemetry.emissionCount;
    }
    OC_PERF_UNITS(
        perfFrame,
        result.candidateCount,
        result.destinationCount
    );
    return result;
}

bool MidiCcGlobalFrameCoordinator::stageLogicalFrame_(
    uint32_t nowUs,
    const core::sequencer::ProjectTrackRuntimeSnapshot& projectTracks,
    uint32_t tickPeriodUs,
    bool allowPredictiveLookahead,
    MidiCcGlobalFrameResult& result
) {
    OC_PERF_SCOPE(perfStage, "midi.cc.global-stage");
    uint16_t seenGeneration = next_target_seen_generation_++;
    if (seenGeneration == 0U || next_target_seen_generation_ == 0U) {
        target_seen_generation_.fill(0U);
        seenGeneration = 1U;
        next_target_seen_generation_ = 2U;
    }

    // Lifecycle replacement invalidates already planned mutations even when
    // the new generation has not authored its first held value yet.
    uint64_t changedLaneAuthors = 0U;
    for (uint16_t address = 0U;
         address < captured_lane_lifecycle_generations_.size();
         ++address) {
        const uint16_t previous = logical_lane_lifecycle_generations_[address];
        const uint16_t next = captured_lane_lifecycle_generations_[address];
        if (previous == next) continue;
        changedLaneAuthors |= UINT64_C(1) << address;
        if (previous != 0U) {
            diagnostics_.laneGenerationInvalidationCount =
                core::sequencer::realtimeMidiSaturatingAdd(
                    diagnostics_.laneGenerationInvalidationCount,
                    1U
                );
        }
    }
    (void)temporal_spool_.cancelLaneAuthors(changedLaneAuthors);

    size_t transitionCount = 0U;
    uint16_t targetCount = 0U;
    for (uint16_t index = 0U; index < combined_candidate_count_; ++index) {
        auto candidate = combined_candidates_[index];
        const uint8_t track = trackForAuthor_(candidate.author);
        const uint16_t bit = static_cast<uint16_t>(1U << track);
        if ((projectTracks.audibleMask & bit) == 0U) continue;

        uint16_t slot = 0U;
        if (!core::sequencer::TemporalMidiCcAuthorSpool::authorSlotIndex(
                candidate.author,
                slot
            ) || target_seen_generation_[slot] == seenGeneration ||
            targetCount >= target_author_slots_.size()) {
            result.status = MidiCcGlobalFrameStatus::INVALID_SOURCE_FRAME;
            return false;
        }
        target_seen_generation_[slot] = seenGeneration;
        target_author_slots_[targetCount] = slot;
        combined_candidates_[targetCount] = candidate;

        const uint16_t lifecycle =
            candidate.author.candidateClass ==
                    MidiCcCandidateClass::SEQUENCER_CC_LANE
            ? captured_lane_lifecycle_generations_[candidate.author.stableAddress]
            : 0U;
        const auto& previous = logical_authors_[slot];
        if (!previous.present || !sameCandidate(previous.candidate, candidate) ||
            previous.lifecycleGeneration != lifecycle) {
            if (transitionCount >= transition_scratch_.size()) {
                result.status = MidiCcGlobalFrameStatus::INVALID_SOURCE_FRAME;
                return false;
            }
            transition_scratch_[transitionCount++] = {
                .deadlineUs = deadlineForAuthor_(
                    candidate.author,
                    nowUs,
                    projectTracks,
                    tickPeriodUs,
                    allowPredictiveLookahead
                ),
                .author = candidate.author,
                .destination = candidate.destination,
                .localValue = candidate.localValue,
                .trackIndex = track,
                .operation =
                    core::sequencer::TemporalMidiCcAuthorOperation::UPDATE,
            };
        }
        ++targetCount;
    }

    for (uint16_t index = 0U; index < logical_active_slot_count_; ++index) {
        const uint16_t slot = logical_active_slots_[index];
        if (!logical_authors_[slot].present ||
            target_seen_generation_[slot] == seenGeneration) {
            continue;
        }
        if (transitionCount >= transition_scratch_.size()) {
            result.status = MidiCcGlobalFrameStatus::INVALID_SOURCE_FRAME;
            return false;
        }
        const auto& previous = logical_authors_[slot].candidate;
        transition_scratch_[transitionCount++] = {
            .deadlineUs = deadlineForAuthor_(
                previous.author,
                nowUs,
                projectTracks,
                tickPeriodUs,
                allowPredictiveLookahead
            ),
            .author = previous.author,
            .trackIndex = trackForAuthor_(previous.author),
            .operation =
                core::sequencer::TemporalMidiCcAuthorOperation::REMOVE,
        };
    }

    const auto spoolResult = temporal_spool_.pushBatch(
        transition_scratch_.data(),
        transitionCount
    );
    OC_PERF_UNITS(perfStage, targetCount, transitionCount);
    if (!spoolResult.ok()) {
        result.status = MidiCcGlobalFrameStatus::TEMPORAL_REJECTED;
        diagnostics_.temporalRejectedFrameCount =
            core::sequencer::realtimeMidiSaturatingAdd(
                diagnostics_.temporalRejectedFrameCount,
                1U
            );
        OC_PERF_RECORD(
            "midi.cc.global-reject",
            0U,
            static_cast<uint32_t>(result.status),
            static_cast<uint32_t>(transitionCount)
        );
        return false;
    }

    for (uint16_t index = 0U; index < logical_active_slot_count_; ++index) {
        logical_authors_[logical_active_slots_[index]].present = false;
    }
    logical_active_slot_count_ = targetCount;
    for (uint16_t index = 0U; index < targetCount; ++index) {
        const uint16_t slot = target_author_slots_[index];
        const auto& candidate = combined_candidates_[index];
        logical_active_slots_[index] = slot;
        logical_authors_[slot] = {
            .candidate = candidate,
            .lifecycleGeneration = static_cast<uint16_t>(
                candidate.author.candidateClass ==
                        MidiCcCandidateClass::SEQUENCER_CC_LANE
                ? captured_lane_lifecycle_generations_[candidate.author.stableAddress]
                : 0U
            ),
            .present = true,
        };
    }
    logical_lane_lifecycle_generations_ =
        captured_lane_lifecycle_generations_;
    combined_candidate_count_ = targetCount;
    result.candidateCount = targetCount;
    diagnostics_.stagedAuthorTransitionCount =
        core::sequencer::realtimeMidiSaturatingAdd(
            diagnostics_.stagedAuthorTransitionCount,
            static_cast<uint32_t>(transitionCount)
        );
    return true;
}

bool MidiCcGlobalFrameCoordinator::processDueGroups_(
    uint32_t nowUs,
    MidiCcGlobalFrameResult& result
) {
    constexpr uint8_t kMaxGroupsPerCall = 8U;
    for (uint8_t group = 0U; group < kMaxGroupsPerCall; ++group) {
        const auto due = temporal_spool_.beginDue(
            nowUs,
            transition_scratch_.data(),
            transition_scratch_.size()
        );
        if (!due.ok()) {
            result.status = MidiCcGlobalFrameStatus::TEMPORAL_REJECTED;
            diagnostics_.temporalRejectedFrameCount =
                core::sequencer::realtimeMidiSaturatingAdd(
                    diagnostics_.temporalRejectedFrameCount,
                    1U
                );
            OC_PERF_RECORD(
                "midi.cc.global-reject",
                0U,
                static_cast<uint32_t>(result.status),
                static_cast<uint32_t>(temporal_spool_.size())
            );
            return false;
        }
        if (due.transferredCount == 0U) return true;
        OC_PERF_SCOPE(perfDue, "midi.cc.global-due-group");
        OC_PERF_UNITS(
            perfDue,
            due.transferredCount,
            effective_active_slot_count_
        );

        effective_active_slot_count_rollback_ = effective_active_slot_count_;
        std::copy_n(
            effective_active_slots_.begin(),
            effective_active_slot_count_,
            effective_active_slots_rollback_.begin()
        );
        for (uint16_t index = 0U; index < due.transferredCount; ++index) {
            const auto& transition = transition_scratch_[index];
            uint16_t slot = 0U;
            if (!core::sequencer::TemporalMidiCcAuthorSpool::authorSlotIndex(
                    transition.author,
                    slot
                )) {
                rollbackEffectiveTransitions_(index);
                (void)temporal_spool_.rollbackDue();
                result.status = MidiCcGlobalFrameStatus::INVALID_SOURCE_FRAME;
                return false;
            }
            transition_rollback_slots_[index] = slot;
            transition_rollback_states_[index] = effective_authors_[slot];
            if (transition.operation ==
                core::sequencer::TemporalMidiCcAuthorOperation::UPDATE) {
                if (!effective_authors_[slot].present) {
                    if (effective_active_slot_count_ >=
                        effective_active_slots_.size()) {
                        rollbackEffectiveTransitions_(index);
                        (void)temporal_spool_.rollbackDue();
                        result.status = MidiCcGlobalFrameStatus::RESOLVE_FAILED;
                        result.resolveStatus =
                            MidiCcResolveStatus::CAPACITY_EXCEEDED;
                        OC_PERF_RECORD(
                            "midi.cc.global-reject",
                            0U,
                            static_cast<uint32_t>(result.status),
                            due.transferredCount
                        );
                        return false;
                    }
                    effective_active_slots_[effective_active_slot_count_++] = slot;
                }
                effective_authors_[slot] = {
                    .candidate = transition.candidate(),
                    .lifecycleGeneration = 0U,
                    .present = true,
                };
            } else {
                if (effective_authors_[slot].present) {
                    for (uint16_t active = 0U;
                         active < effective_active_slot_count_;
                         ++active) {
                        if (effective_active_slots_[active] != slot) continue;
                        --effective_active_slot_count_;
                        effective_active_slots_[active] =
                            effective_active_slots_[effective_active_slot_count_];
                        break;
                    }
                }
                effective_authors_[slot].present = false;
            }
        }

        if (!resolveEffective_(transition_scratch_[0].deadlineUs, result)) {
            rollbackEffectiveTransitions_(due.transferredCount);
            (void)temporal_spool_.rollbackDue();
            return false;
        }
        if (!temporal_spool_.commitDue()) {
            result.status = MidiCcGlobalFrameStatus::TEMPORAL_REJECTED;
            OC_PERF_RECORD(
                "midi.cc.global-reject",
                0U,
                static_cast<uint32_t>(result.status),
                due.transferredCount
            );
            return false;
        }
        effective_dirty_ = false;
        diagnostics_.committedDeadlineGroupCount =
            core::sequencer::realtimeMidiSaturatingAdd(
                diagnostics_.committedDeadlineGroupCount,
                1U
            );
    }
    return true;
}

bool MidiCcGlobalFrameCoordinator::resolveEffective_(
    uint32_t deadlineUs,
    MidiCcGlobalFrameResult& result
) {
    combined_candidate_count_ = 0U;
    for (uint16_t index = 0U; index < effective_active_slot_count_; ++index) {
        const uint16_t slot = effective_active_slots_[index];
        if (!effective_authors_[slot].present) {
            result.status = MidiCcGlobalFrameStatus::RESOLVE_FAILED;
            return false;
        }
        if (combined_candidate_count_ >= combined_candidates_.size()) {
            result.resolveStatus = MidiCcResolveStatus::CAPACITY_EXCEEDED;
            result.status = MidiCcGlobalFrameStatus::RESOLVE_FAILED;
            OC_PERF_RECORD(
                "midi.cc.global-reject",
                0U,
                static_cast<uint32_t>(result.status),
                combined_candidate_count_
            );
            return false;
        }
        combined_candidates_[combined_candidate_count_++] =
            effective_authors_[slot].candidate;
    }

    OC_PERF_SCOPE(perfResolve, "midi.cc.global-resolve");
    uint8_t pendingTelemetryIndex = NO_SOURCE_READER;
    {
        oc::realtime::InterruptGuard lock;
        pendingTelemetryIndex = writableSourceIndex(
            telemetry_frames_,
            published_telemetry_index_,
            reading_telemetry_index_
        );
    }
    if (pendingTelemetryIndex == NO_SOURCE_READER) {
        result.status = MidiCcGlobalFrameStatus::RESOLVE_FAILED;
        return false;
    }
    auto& pendingTelemetry = telemetry_frames_[pendingTelemetryIndex];
    result.resolveStatus = core::state::shared::resolveMidiCcDestinations(
        combined_candidates_.data(),
        combined_candidate_count_,
        MidiCcResolutionMode::LIVE,
        pendingTelemetry
    );
    if (result.resolveStatus != MidiCcResolveStatus::OK) {
        OC_PERF_UNITS(perfResolve, combined_candidate_count_, 0U);
        result.status = MidiCcGlobalFrameStatus::RESOLVE_FAILED;
        OC_PERF_RECORD(
            "midi.cc.global-reject",
            0U,
            static_cast<uint32_t>(result.resolveStatus),
            combined_candidate_count_
        );
        return false;
    }
    OC_PERF_UNITS(
        perfResolve,
        combined_candidate_count_,
        pendingTelemetry.destinationCount
    );

    result.candidateCount = pendingTelemetry.candidateCount;
    result.destinationCount = pendingTelemetry.destinationCount;
    result.conflictCount = pendingTelemetry.conflictCount;
    result.noRouteCount = pendingTelemetry.noRouteCount;
    result.eligibleEmissionCount = pendingTelemetry.emissionCount;
    uint16_t pendingDesiredCount = 0U;
    uint16_t pendingEventCount = 0U;
    for (uint16_t index = 0U;
         index < pendingTelemetry.destinationCount;
         ++index) {
        const auto& resolved = pendingTelemetry.destinations[index];
        if (!resolved.shouldEmit) continue;
        if (resolved.destination.identity.port != output_port_ ||
            pendingDesiredCount >= pending_desired_values_.size()) {
            result.resolveStatus = MidiCcResolveStatus::INVALID_INPUT;
            result.status = MidiCcGlobalFrameStatus::RESOLVE_FAILED;
            return false;
        }
        const DesiredValue desired{
            .identity = resolved.destination.identity,
            .value = resolved.finalValue,
        };
        pending_desired_values_[pendingDesiredCount++] = desired;
        if (plannedValueMatches_(desired)) continue;
        if (pendingEventCount >= pending_events_.size()) {
            result.resolveStatus = MidiCcResolveStatus::CAPACITY_EXCEEDED;
            result.status = MidiCcGlobalFrameStatus::RESOLVE_FAILED;
            OC_PERF_RECORD(
                "midi.cc.global-reject",
                0U,
                static_cast<uint32_t>(result.resolveStatus),
                pendingEventCount
            );
            return false;
        }
        pending_events_[pendingEventCount++] = RealtimeMidiEvent{
            .deadlineUs = deadlineUs,
            .type = RealtimeMidiEventType::ControlChange,
            .trackIndex = trackForAuthor_(resolved.winner.author),
            .channel = desired.identity.channel,
            .controller = desired.identity.controller,
            .value = desired.value,
        };
    }

    const auto queueResult = queue_.pushBatch(
        pending_events_.data(),
        pendingEventCount
    );
    result.queueStatus = queueResult.status;
    if (!queueResult.ok()) {
        retry_requested_ = true;
        diagnostics_.queueRejectedFrameCount =
            core::sequencer::realtimeMidiSaturatingAdd(
                diagnostics_.queueRejectedFrameCount,
                1U
            );
        result.status = MidiCcGlobalFrameStatus::QUEUE_REJECTED;
        OC_PERF_RECORD(
            "midi.cc.global-reject",
            0U,
            static_cast<uint32_t>(result.status),
            pendingEventCount
        );
        return false;
    }

    publishDesiredAndPruneDispatched_(
        pending_desired_values_,
        pendingDesiredCount
    );
    planned_values_valid_ = true;
    {
        oc::realtime::InterruptGuard lock;
        published_telemetry_index_ = pendingTelemetryIndex;
    }
    retry_requested_ = false;
    diagnostics_.resolvedLiveFrameCount =
        core::sequencer::realtimeMidiSaturatingAdd(
            diagnostics_.resolvedLiveFrameCount,
            1U
        );
    result.queuedEmissionCount = static_cast<uint16_t>(
        std::min<uint32_t>(
            UINT16_MAX,
            static_cast<uint32_t>(result.queuedEmissionCount) +
                pendingEventCount
        )
    );
    result.status = MidiCcGlobalFrameStatus::OK;
    return true;
}

void MidiCcGlobalFrameCoordinator::rollbackEffectiveTransitions_(size_t count) {
    while (count > 0U) {
        --count;
        effective_authors_[transition_rollback_slots_[count]] =
            transition_rollback_states_[count];
    }
    effective_active_slot_count_ = effective_active_slot_count_rollback_;
    std::copy_n(
        effective_active_slots_rollback_.begin(),
        effective_active_slot_count_,
        effective_active_slots_.begin()
    );
}

uint32_t MidiCcGlobalFrameCoordinator::deadlineForAuthor_(
    const core::state::shared::MidiCcAuthor& author,
    uint32_t nowUs,
    const core::sequencer::ProjectTrackRuntimeSnapshot& projectTracks,
    uint32_t tickPeriodUs,
    bool allowPredictiveLookahead
) const {
    const int16_t delayMs = projectTracks.delayMs[trackForAuthor_(author)];
    if (delayMs >= 0) {
        return nowUs + static_cast<uint32_t>(delayMs) * 1000U;
    }
    const bool projectedLane =
        author.candidateClass == MidiCcCandidateClass::SEQUENCER_CC_LANE &&
        author.stableAddress <
            core::sequencer::SequencerCcLaneRuntime::ADDRESS_COUNT &&
        (captured_lane_predictive_author_mask_ &
         (UINT64_C(1) << author.stableAddress)) != 0U;
    if (!projectedLane || !allowPredictiveLookahead || tickPeriodUs == 0U) {
        return nowUs;
    }
    const uint32_t advanceUs = static_cast<uint32_t>(-delayMs) * 1000U;
    const uint32_t leadTicks = static_cast<uint32_t>(
        (static_cast<uint64_t>(advanceUs) + tickPeriodUs - 1U) /
        tickPeriodUs
    );
    const uint32_t residualUs = static_cast<uint32_t>(
        static_cast<uint64_t>(leadTicks) * tickPeriodUs - advanceUs
    );
    return nowUs + residualUs;
}

FLASHMEM void MidiCcGlobalFrameCoordinator::clearTrackAuthorStates_(
    uint8_t trackIndex
) {
    for (uint16_t slot = 0U; slot < logical_authors_.size(); ++slot) {
        const bool matches =
            (logical_authors_[slot].present &&
             trackForAuthor_(logical_authors_[slot].candidate.author) == trackIndex) ||
            (effective_authors_[slot].present &&
             trackForAuthor_(effective_authors_[slot].candidate.author) == trackIndex);
        if (!matches) continue;
        logical_authors_[slot].present = false;
        effective_authors_[slot].present = false;
    }
    logical_active_slot_count_ = 0U;
    effective_active_slot_count_ = 0U;
    for (uint16_t slot = 0U; slot < logical_authors_.size(); ++slot) {
        if (!logical_authors_[slot].present) continue;
        if (logical_active_slot_count_ < logical_active_slots_.size()) {
            logical_active_slots_[logical_active_slot_count_++] = slot;
        }
    }
    for (uint16_t slot = 0U; slot < effective_authors_.size(); ++slot) {
        if (!effective_authors_[slot].present) continue;
        if (effective_active_slot_count_ < effective_active_slots_.size()) {
            effective_active_slots_[effective_active_slot_count_++] = slot;
        }
    }
}

FLASHMEM void MidiCcGlobalFrameCoordinator::synchronizeStoppedLaneLogicalState_() {
    for (uint16_t slot = 0U;
         slot < core::sequencer::TemporalMidiCcAuthorSpool::LANE_AUTHOR_SLOT_COUNT;
         ++slot) {
        logical_authors_[slot] = effective_authors_[slot];
    }
    logical_active_slot_count_ = 0U;
    for (uint16_t slot = 0U; slot < logical_authors_.size(); ++slot) {
        if (!logical_authors_[slot].present) continue;
        if (logical_active_slot_count_ < logical_active_slots_.size()) {
            logical_active_slots_[logical_active_slot_count_++] = slot;
        }
    }
}

bool MidiCcGlobalFrameCoordinator::plannedValueMatches_(
    const DesiredValue& desired
) const {
    const auto& values = planned_values_valid_ ? desired_values_ : dispatched_values_;
    const uint16_t count = planned_values_valid_
        ? desired_value_count_
        : dispatched_value_count_;
    for (uint16_t index = 0U; index < count; ++index) {
        if (sameIdentity(values[index].identity, desired.identity)) {
            return values[index].value == desired.value;
        }
    }
    return false;
}

void MidiCcGlobalFrameCoordinator::publishDesiredAndPruneDispatched_(
    const std::array<
        DesiredValue,
        core::state::shared::MidiCcResolutionTelemetry::MAX_DESTINATIONS>& desired,
    uint16_t desiredCount
) {
    desired_value_count_ = desiredCount;
    std::copy_n(desired.begin(), desiredCount, desired_values_.begin());

    uint16_t write = 0;
    for (uint16_t i = 0; i < dispatched_value_count_; ++i) {
        bool retained = false;
        for (uint16_t j = 0; j < desiredCount; ++j) {
            if (sameIdentity(dispatched_values_[i].identity, desired[j].identity) &&
                dispatched_values_[i].value == desired[j].value) {
                retained = true;
                break;
            }
        }
        if (retained) dispatched_values_[write++] = dispatched_values_[i];
    }
    dispatched_value_count_ = write;
}

uint8_t MidiCcGlobalFrameCoordinator::trackForAuthor_(
    const core::state::shared::MidiCcAuthor& author
) {
    uint16_t track = 0;
    if (author.candidateClass == MidiCcCandidateClass::SEQUENCER_CC_LANE) {
        track = static_cast<uint16_t>(
            author.stableAddress /
            core::state::sequencer::SequencerCcLaneBank::MAX_LANES
        );
    } else {
        constexpr uint16_t kMacroAddressesPerTrack =
            core::state::macro::PAGE_COUNT * core::state::macro::MACRO_COUNT;
        track = static_cast<uint16_t>(author.stableAddress / kMacroAddressesPerTrack);
    }
    return static_cast<uint8_t>(std::min<uint16_t>(track, 15U));
}

void MidiCcGlobalFrameCoordinator::onRealtimeMidiEventEnqueued(
    const RealtimeMidiEvent&
) {}

void MidiCcGlobalFrameCoordinator::onRealtimeMidiEventRemoved(
    const RealtimeMidiEvent& event,
    core::sequencer::RealtimeMidiQueueLifecycleReason
) {
    if (event.type != RealtimeMidiEventType::ControlChange ||
        replacing_pending_controls_) {
        return;
    }
    retry_requested_ = true;
    planned_values_valid_ = false;
    diagnostics_.pendingRemovalRetryCount =
        core::sequencer::realtimeMidiSaturatingAdd(
            diagnostics_.pendingRemovalRetryCount,
            1
        );
}

void MidiCcGlobalFrameCoordinator::onRealtimeMidiEventDispatched(
    const RealtimeMidiEvent& event
) {
    if (event.type == RealtimeMidiEventType::NoteOn ||
        event.type == RealtimeMidiEventType::NoteOff) {
        const bool gateOn = event.type == RealtimeMidiEventType::NoteOn &&
            event.velocity > 0U;
        enqueueProjectModulationTrigger_({
            .trigger = {
                core::state::modulation::ModulationTriggerKind::TRACK_NOTE,
                event.trackIndex,
                event.channel,
                event.note,
            },
            .edge = gateOn
                ? core::state::modulation::ProjectModulationTriggerEdge::GATE_ON
                : core::state::modulation::ProjectModulationTriggerEdge::GATE_OFF,
            .velocity = event.velocity,
        });
    }
    if (event.type != RealtimeMidiEventType::ControlChange) return;
    const DesiredValue dispatched{
        .identity = MidiCcDestinationIdentity{
            .port = output_port_,
            .channel = event.channel,
            .controller = event.controller,
        },
        .value = event.value,
    };

    bool currentlyDesired = false;
    for (uint16_t i = 0; i < desired_value_count_; ++i) {
        if (sameIdentity(desired_values_[i].identity, dispatched.identity) &&
            desired_values_[i].value == dispatched.value) {
            currentlyDesired = true;
            break;
        }
    }
    if (!currentlyDesired) return;

    for (uint16_t i = 0; i < dispatched_value_count_; ++i) {
        if (!sameIdentity(dispatched_values_[i].identity, dispatched.identity)) {
            continue;
        }
        dispatched_values_[i].value = dispatched.value;
        return;
    }
    if (dispatched_value_count_ < dispatched_values_.size()) {
        dispatched_values_[dispatched_value_count_++] = dispatched;
    }
}

void MidiCcGlobalFrameCoordinator::enqueueProjectModulationTrigger_(
    const core::state::modulation::ProjectModulationTriggerEvent& event
) {
    const uint16_t write = project_trigger_write_sequence_.load(
        std::memory_order_relaxed
    );
    const uint16_t read = project_trigger_read_sequence_.load(
        std::memory_order_acquire
    );
    if (static_cast<uint16_t>(write - read) >= PROJECT_TRIGGER_RING_CAPACITY) {
        (void)project_trigger_overflow_sequence_.fetch_add(
            1U,
            std::memory_order_release
        );
        diagnostics_.projectTriggerEventOverflowCount =
            core::sequencer::realtimeMidiSaturatingAdd(
                diagnostics_.projectTriggerEventOverflowCount,
                1U
            );
        OC_PERF_RECORD(
            "midi.cc.project-trigger-overflow",
            0U,
            PROJECT_TRIGGER_RING_CAPACITY,
            static_cast<uint16_t>(write - read)
        );
        return;
    }
    project_trigger_events_[write & PROJECT_TRIGGER_RING_MASK] = event;
    project_trigger_write_sequence_.store(
        static_cast<uint16_t>(write + 1U),
        std::memory_order_release
    );
    diagnostics_.capturedProjectTriggerEventCount =
        core::sequencer::realtimeMidiSaturatingAdd(
            diagnostics_.capturedProjectTriggerEventCount,
            1U
        );
}

bool MidiCcGlobalFrameCoordinator::hasPendingProjectModulationTriggers() const {
    const uint16_t write = project_trigger_write_sequence_.load(
        std::memory_order_acquire
    );
    const uint16_t read = project_trigger_read_sequence_.load(
        std::memory_order_relaxed
    );
    const uint16_t overflow = project_trigger_overflow_sequence_.load(
        std::memory_order_acquire
    );
    return write != read ||
        overflow != project_trigger_last_drained_overflow_sequence_;
}

uint16_t MidiCcGlobalFrameCoordinator::drainProjectModulationTriggers(
    core::state::modulation::ProjectModulationTriggerFrame& out
) {
    out.count = 0U;
    const uint16_t overflow = project_trigger_overflow_sequence_.load(
        std::memory_order_acquire
    );
    out.droppedEventCount = static_cast<uint16_t>(
        overflow - project_trigger_last_drained_overflow_sequence_
    );
    project_trigger_last_drained_overflow_sequence_ = overflow;
    const uint16_t read = project_trigger_read_sequence_.load(
        std::memory_order_relaxed
    );
    const uint16_t write = project_trigger_write_sequence_.load(
        std::memory_order_acquire
    );
    const uint16_t available = static_cast<uint16_t>(write - read);
    const uint16_t count = std::min<uint16_t>(
        available,
        static_cast<uint16_t>(out.events.size())
    );
    for (uint16_t index = 0U; index < count; ++index) {
        out.events[index] = project_trigger_events_[
            static_cast<uint16_t>(read + index) & PROJECT_TRIGGER_RING_MASK
        ];
    }
    out.count = count;
    project_trigger_read_sequence_.store(
        static_cast<uint16_t>(read + count),
        std::memory_order_release
    );
    return count;
}

FLASHMEM void MidiCcGlobalFrameCoordinator::resetProject() {
    if (lifecycle_attached_) {
        replacing_pending_controls_ = true;
        (void)queue_.cancelControlChangeEvents();
        replacing_pending_controls_ = false;
    }
    persistent_frames_ = {};
    lane_frames_ = {};
    active_persistent_index_ = 0;
    active_lane_index_ = 0;
    reading_persistent_index_ = NO_SOURCE_READER;
    reading_lane_index_ = NO_SOURCE_READER;
    next_persistent_revision_ = 1;
    next_lane_revision_ = 1;
    last_live_persistent_revision_ = 0;
    last_live_lane_revision_ = 0;
    captured_persistent_revision_ = 0;
    captured_lane_revision_ = 0;
    captured_lane_lifecycle_generations_ = {};
    captured_lane_predictive_author_mask_ = 0U;
    logical_lane_lifecycle_generations_ = {};
    combined_candidate_count_ = 0;
    target_author_slots_ = {};
    logical_authors_ = {};
    effective_authors_ = {};
    effective_active_slots_ = {};
    effective_active_slot_count_ = 0U;
    effective_active_slots_rollback_ = {};
    effective_active_slot_count_rollback_ = 0U;
    logical_active_slots_ = {};
    logical_active_slot_count_ = 0U;
    target_seen_generation_ = {};
    next_target_seen_generation_ = 1U;
    temporal_spool_.clear();
    transition_scratch_ = {};
    transition_rollback_states_ = {};
    transition_rollback_slots_ = {};
    source_restage_required_ = false;
    effective_dirty_ = false;
    // A UI TelemetryReadView is a stable zero-copy lease. Reset must neither
    // overwrite its frame nor release its reader slot: doing either would let
    // a later view reuse the same slot while the old destructor can still run.
    // Triple buffering always leaves at least two non-reader frames, so reset
    // can publish a distinct zero frame and keep acquisitions fail-closed until
    // the held RAII view releases its exact index.
    uint8_t heldTelemetryIndex = NO_SOURCE_READER;
    {
        oc::realtime::InterruptGuard lock;
        heldTelemetryIndex = reading_telemetry_index_;
    }
    uint8_t zeroTelemetryIndex = NO_SOURCE_READER;
    for (uint8_t index = 0U; index < telemetry_frames_.size(); ++index) {
        if (index == heldTelemetryIndex) continue;
        telemetry_frames_[index] = {};
        if (zeroTelemetryIndex == NO_SOURCE_READER) {
            zeroTelemetryIndex = index;
        }
    }
    {
        oc::realtime::InterruptGuard lock;
        // TELEMETRY_FRAME_COUNT is three and at most one reader exists.
        published_telemetry_index_ = zeroTelemetryIndex;
    }
    desired_value_count_ = 0;
    planned_values_valid_ = true;
    dispatched_value_count_ = 0;
    retry_requested_ = false;
    transport_retry_deferred_until_resume_ = false;
    replacing_pending_controls_ = false;
    control_time_ = {};
    control_tick_started_us_ = 0;
    last_control_sequencer_tick_ = 0;
    control_clock_initialized_ = false;
    project_trigger_events_ = {};
    project_trigger_write_sequence_.store(0U, std::memory_order_relaxed);
    project_trigger_read_sequence_.store(0U, std::memory_order_relaxed);
    project_trigger_overflow_sequence_.store(0U, std::memory_order_relaxed);
    project_trigger_last_drained_overflow_sequence_ = 0U;
    diagnostics_ = {};
}

MidiCcGlobalFrameCoordinator::TelemetryReadView
MidiCcGlobalFrameCoordinator::readTelemetry() const {
    oc::realtime::InterruptGuard lock;
    if (reading_telemetry_index_ != NO_SOURCE_READER ||
        published_telemetry_index_ >= telemetry_frames_.size()) {
        return {};
    }
    const uint8_t index = published_telemetry_index_;
    reading_telemetry_index_ = index;
    return TelemetryReadView(*this, telemetry_frames_[index], index);
}

void MidiCcGlobalFrameCoordinator::releaseTelemetryReader_(uint8_t index) const {
    oc::realtime::InterruptGuard lock;
    if (reading_telemetry_index_ == index) {
        reading_telemetry_index_ = NO_SOURCE_READER;
    }
}

}  // namespace core::handler
