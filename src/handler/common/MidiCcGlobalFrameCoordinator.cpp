#include "handler/common/MidiCcGlobalFrameCoordinator.hpp"

#include <algorithm>

#include <config/PlatformCompat.hpp>
#include <oc/diagnostics/Performance.hpp>
#include <oc/note/clock/ClockConstants.hpp>
#include <oc/realtime/InterruptGuard.hpp>
#include <oc/time/Time.hpp>

#include "state/macro/MacroConstants.hpp"

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

bool MidiCcGlobalFrameCoordinator::replacePersistentAuthor(
    const MidiCcCandidate& candidate,
    uint16_t& publishedCandidateCount
) {
    publishedCandidateCount = 0;
    constexpr uint16_t kMacroAuthorCount =
        core::state::macro::TRACK_COUNT *
        core::state::macro::PAGE_COUNT *
        core::state::macro::MACRO_COUNT;
    if (!validPersistentClass(candidate.author.candidateClass) ||
        !validCandidateBody(candidate) ||
        candidate.author.stableAddress >= kMacroAuthorCount ||
        (candidate.destination.routeValidity ==
             core::state::shared::MidiCcRouteValidity::VALID &&
         candidate.destination.identity.port != output_port_)) {
        return false;
    }

    uint8_t writeIndex = 0;
    uint8_t sourceIndex = 0;
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

    bool replaced = false;
    for (uint16_t index = 0; index < frame.candidateCount; ++index) {
        if (frame.candidates[index].author.stableAddress !=
            candidate.author.stableAddress) {
            continue;
        }
        frame.candidates[index] = candidate;
        replaced = true;
        break;
    }
    if (!replaced) return false;
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
            1
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
    }

    uint8_t writeIndex = 0;
    {
        oc::realtime::InterruptGuard lock;
        writeIndex = writableSourceIndex(
            lane_frames_,
            active_lane_index_,
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

bool MidiCcGlobalFrameCoordinator::needsLiveResolution() const {
    uint32_t persistentRevision = 0;
    uint32_t laneRevision = 0;
    {
        oc::realtime::InterruptGuard lock;
        persistentRevision =
            persistent_frames_[active_persistent_index_].revision;
        laneRevision = lane_frames_[active_lane_index_].revision;
    }
    return retry_requested_ ||
           persistentRevision != last_live_persistent_revision_ ||
           laneRevision != last_live_lane_revision_;
}

MidiCcGlobalFrameResult MidiCcGlobalFrameCoordinator::resolveLive(
    uint32_t deadlineUs
) {
    if (!needsLiveResolution()) {
        return {
            .status = MidiCcGlobalFrameStatus::NO_CHANGE,
            .resolveStatus = MidiCcResolveStatus::OK,
            .queueStatus = core::sequencer::RealtimeMidiQueueBatchStatus::OK,
        };
    }
    return resolve_(deadlineUs);
}

void MidiCcGlobalFrameCoordinator::discardPendingRetryForTransportStop() {
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
    }
    {
        oc::realtime::InterruptGuard lock;
        reading_persistent_index_ = NO_SOURCE_READER;
        reading_lane_index_ = NO_SOURCE_READER;
    }
    return valid;
}

MidiCcGlobalFrameResult MidiCcGlobalFrameCoordinator::resolve_(uint32_t deadlineUs) {
    MidiCcGlobalFrameResult result{};
    if (!captureCombinedCandidates_()) {
        result.status = MidiCcGlobalFrameStatus::INVALID_SOURCE_FRAME;
        return result;
    }
    result.candidateCount = combined_candidate_count_;

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
        return result;
    }
    auto& pendingTelemetry = telemetry_frames_[pendingTelemetryIndex];
    result.resolveStatus = core::state::shared::resolveMidiCcDestinations(
        combined_candidates_.data(),
        combined_candidate_count_,
        MidiCcResolutionMode::LIVE,
        pendingTelemetry
    );
    if (result.resolveStatus != MidiCcResolveStatus::OK) {
        OC_PERF_UNITS(perfResolve, combined_candidate_count_, 0);
        result.status = MidiCcGlobalFrameStatus::RESOLVE_FAILED;
        return result;
    }
    OC_PERF_UNITS(
        perfResolve,
        combined_candidate_count_,
        pendingTelemetry.destinationCount
    );

    result.destinationCount = pendingTelemetry.destinationCount;
    result.conflictCount = pendingTelemetry.conflictCount;
    result.noRouteCount = pendingTelemetry.noRouteCount;
    result.eligibleEmissionCount = pendingTelemetry.emissionCount;

    uint16_t pendingDesiredCount = 0;
    uint16_t pendingEventCount = 0;
    for (uint16_t i = 0; i < pendingTelemetry.destinationCount; ++i) {
        const auto& resolved = pendingTelemetry.destinations[i];
        if (!resolved.shouldEmit) continue;
        if (resolved.destination.identity.port != output_port_ ||
            pendingDesiredCount >= pending_desired_values_.size()) {
            result.resolveStatus = MidiCcResolveStatus::INVALID_INPUT;
            result.status = MidiCcGlobalFrameStatus::RESOLVE_FAILED;
            return result;
        }

        const DesiredValue desired{
            .identity = resolved.destination.identity,
            .value = resolved.finalValue,
        };
        pending_desired_values_[pendingDesiredCount++] = desired;
        if (dispatchedValueMatches_(desired)) continue;
        if (pendingEventCount >= pending_events_.size()) {
            result.resolveStatus = MidiCcResolveStatus::CAPACITY_EXCEEDED;
            result.status = MidiCcGlobalFrameStatus::RESOLVE_FAILED;
            return result;
        }
        pending_events_[pendingEventCount++] = RealtimeMidiEvent{
            .deadlineUs = deadlineUs,
            .type = RealtimeMidiEventType::ControlChange,
            .channel = desired.identity.channel,
            .controller = desired.identity.controller,
            .value = desired.value,
            .trackIndex = trackForAuthor_(resolved.winner.author),
        };
    }

    replacing_pending_controls_ = true;
    const auto queueResult = queue_.replaceControlChangeEventsWithBatch(
        pending_events_.data(),
        pendingEventCount
    );
    replacing_pending_controls_ = false;
    result.queueStatus = queueResult.status;
    if (!queueResult.ok()) {
        retry_requested_ = true;
        diagnostics_.queueRejectedFrameCount =
            core::sequencer::realtimeMidiSaturatingAdd(
                diagnostics_.queueRejectedFrameCount,
                1
            );
        result.status = MidiCcGlobalFrameStatus::QUEUE_REJECTED;
        return result;
    }

    publishDesiredAndPruneDispatched_(
        pending_desired_values_,
        pendingDesiredCount
    );
    {
        oc::realtime::InterruptGuard lock;
        published_telemetry_index_ = pendingTelemetryIndex;
    }
    // Record the exact immutable source revisions that were resolved. A newer
    // publish during resolution must remain visible to needsLiveResolution().
    last_live_persistent_revision_ = captured_persistent_revision_;
    last_live_lane_revision_ = captured_lane_revision_;
    retry_requested_ = false;
    diagnostics_.resolvedLiveFrameCount =
        core::sequencer::realtimeMidiSaturatingAdd(
            diagnostics_.resolvedLiveFrameCount,
            1
        );
    result.queuedEmissionCount = pendingEventCount;
    result.status = MidiCcGlobalFrameStatus::OK;
    return result;
}

bool MidiCcGlobalFrameCoordinator::dispatchedValueMatches_(
    const DesiredValue& desired
) const {
    for (uint16_t i = 0; i < dispatched_value_count_; ++i) {
        if (sameIdentity(dispatched_values_[i].identity, desired.identity)) {
            return dispatched_values_[i].value == desired.value;
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
    diagnostics_.pendingRemovalRetryCount =
        core::sequencer::realtimeMidiSaturatingAdd(
            diagnostics_.pendingRemovalRetryCount,
            1
        );
}

void MidiCcGlobalFrameCoordinator::onRealtimeMidiEventDispatched(
    const RealtimeMidiEvent& event
) {
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

FLASHMEM void MidiCcGlobalFrameCoordinator::resetProject() {
    if (lifecycle_attached_) {
        replacing_pending_controls_ = true;
        (void)queue_.replaceControlChangeEventsWithBatch(nullptr, 0);
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
    combined_candidate_count_ = 0;
    telemetry_frames_ = {};
    published_telemetry_index_ = 0;
    reading_telemetry_index_ = NO_SOURCE_READER;
    desired_value_count_ = 0;
    dispatched_value_count_ = 0;
    retry_requested_ = false;
    replacing_pending_controls_ = false;
    control_time_ = {};
    control_tick_started_us_ = 0;
    last_control_sequencer_tick_ = 0;
    control_clock_initialized_ = false;
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
