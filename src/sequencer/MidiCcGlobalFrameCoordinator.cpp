#include "sequencer/MidiCcGlobalFrameCoordinator.hpp"

#include <algorithm>

#include <config/PlatformCompat.hpp>
#include <oc/diagnostics/Performance.hpp>
#include <oc/realtime/InterruptGuard.hpp>

#include "state/macro/MacroConstants.hpp"
#include "sequencer/MidiCcCandidateSemantics.hpp"
#include "sequencer/ProjectTrackRuntimeSnapshotBank.hpp"

namespace core::sequencer {

namespace {

using core::sequencer::RealtimeMidiEvent;
using core::sequencer::RealtimeMidiEventType;
using core::state::shared::MidiCcCandidate;
using core::state::shared::MidiCcCandidateClass;
using core::state::shared::MidiCcDestinationIdentity;
using core::state::shared::MidiCcResolutionMode;
using core::state::shared::MidiCcResolveStatus;

}  // namespace

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
        const auto& telemetry = telemetry_exchange_.published();
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
        if (!previous.present ||
            !midi_cc::sameCandidate(previous.candidate, candidate) ||
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
    const auto pendingTelemetryLease = telemetry_exchange_.beginWrite();
    if (!pendingTelemetryLease) {
        result.status = MidiCcGlobalFrameStatus::RESOLVE_FAILED;
        return false;
    }
    auto& pendingTelemetry = *pendingTelemetryLease.telemetry;
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
    telemetry_exchange_.publish(pendingTelemetryLease);
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
        if (midi_cc::sameIdentity(values[index].identity, desired.identity)) {
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
            if (midi_cc::sameIdentity(
                    dispatched_values_[i].identity,
                    desired[j].identity
                ) &&
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

}  // namespace core::sequencer
