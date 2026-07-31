#include "sequencer/MidiCcGlobalFrameCoordinator.hpp"

#include <config/PlatformCompat.hpp>
#include <oc/diagnostics/Performance.hpp>
#include <oc/realtime/InterruptGuard.hpp>

#include "sequencer/MidiCcCandidateSemantics.hpp"

namespace core::sequencer {

FLASHMEM MidiCcGlobalFrameCoordinator::MidiCcGlobalFrameCoordinator(
    RealtimeMidiQueue& queue,
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

void MidiCcGlobalFrameCoordinator::publishProjectControlClock(
    uint32_t sequencerTick,
    bool playing,
    uint32_t nowUs,
    uint32_t sequencerTickPeriodUs
) {
    oc::realtime::InterruptGuard lock;
    // Stop keeps an accepted CC intent as a logical fence so unrelated Macro
    // edits can still resolve without re-emitting a held Lane. The first
    // playing clock removes that fence and reconciles once against physical
    // dispatch state.
    if (playing && transport_retry_deferred_until_resume_) {
        planned_values_valid_ = false;
        retry_requested_ = true;
        source_restage_required_ = true;
        transport_retry_deferred_until_resume_ = false;
    }
    control_clock_.publishLocked(
        sequencerTick,
        playing,
        nowUs,
        sequencerTickPeriodUs
    );
}

core::state::modulation::ProjectControlTimeSnapshot
MidiCcGlobalFrameCoordinator::projectControlTimeSnapshot() const {
    return control_clock_.snapshot();
}

FLASHMEM void MidiCcGlobalFrameCoordinator::invalidateTrack(
    uint8_t trackIndex
) {
    if (trackIndex >= 16U) return;
    (void)temporal_spool_.cancelTrack(trackIndex);
    clearTrackAuthorStates_(trackIndex);
    source_restage_required_ = true;
    effective_dirty_ = true;
    planned_values_valid_ = false;
    retry_requested_ = true;
    diagnostics_.trackInvalidationCount = realtimeMidiSaturatingAdd(
        diagnostics_.trackInvalidationCount,
        1U
    );
}

FLASHMEM void
MidiCcGlobalFrameCoordinator::discardPendingRetryForTransportStop() {
    // queue.clear() runs before this boundary and reports every removed CC.
    // Preserve the accepted desired set until the first resumed clock.
    const size_t cancelledLaneTransitions =
        temporal_spool_.cancelCandidateClass(
            core::state::shared::MidiCcCandidateClass::SEQUENCER_CC_LANE
        );
    transport_retry_deferred_until_resume_ =
        transport_retry_deferred_until_resume_ || retry_requested_ ||
        !planned_values_valid_ || cancelledLaneTransitions > 0U;
    synchronizeStoppedLaneLogicalState_();
    planned_values_valid_ = true;
    retry_requested_ = false;
}

void MidiCcGlobalFrameCoordinator::onRealtimeMidiEventEnqueued(
    const RealtimeMidiEvent&
) {}

void MidiCcGlobalFrameCoordinator::onRealtimeMidiEventRemoved(
    const RealtimeMidiEvent& event,
    RealtimeMidiQueueLifecycleReason
) {
    if (event.type != RealtimeMidiEventType::ControlChange ||
        replacing_pending_controls_) {
        return;
    }
    retry_requested_ = true;
    planned_values_valid_ = false;
    diagnostics_.pendingRemovalRetryCount = realtimeMidiSaturatingAdd(
        diagnostics_.pendingRemovalRetryCount,
        1U
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
                ? core::state::modulation::
                    ProjectModulationTriggerEdge::GATE_ON
                : core::state::modulation::
                    ProjectModulationTriggerEdge::GATE_OFF,
            .velocity = event.velocity,
        });
    }
    if (event.type != RealtimeMidiEventType::ControlChange) return;
    const DesiredValue dispatched{
        .identity = core::state::shared::MidiCcDestinationIdentity{
            .port = output_port_,
            .channel = event.channel,
            .controller = event.controller,
        },
        .value = event.value,
    };

    bool currentlyDesired = false;
    for (uint16_t index = 0U; index < desired_value_count_; ++index) {
        if (midi_cc::sameIdentity(
                desired_values_[index].identity,
                dispatched.identity
            ) &&
            desired_values_[index].value == dispatched.value) {
            currentlyDesired = true;
            break;
        }
    }
    if (!currentlyDesired) return;

    for (uint16_t index = 0U; index < dispatched_value_count_; ++index) {
        if (!midi_cc::sameIdentity(
                dispatched_values_[index].identity,
                dispatched.identity
            )) {
            continue;
        }
        dispatched_values_[index].value = dispatched.value;
        return;
    }
    if (dispatched_value_count_ < dispatched_values_.size()) {
        dispatched_values_[dispatched_value_count_++] = dispatched;
    }
}

void MidiCcGlobalFrameCoordinator::enqueueProjectModulationTrigger_(
    const core::state::modulation::ProjectModulationTriggerEvent& event
) {
    if (!project_trigger_queue_.enqueue(event)) {
        diagnostics_.projectTriggerEventOverflowCount =
            realtimeMidiSaturatingAdd(
                diagnostics_.projectTriggerEventOverflowCount,
                1U
            );
        OC_PERF_RECORD(
            "midi.cc.project-trigger-overflow",
            0U,
            ProjectModulationTriggerQueue::CAPACITY,
            ProjectModulationTriggerQueue::CAPACITY
        );
        return;
    }
    diagnostics_.capturedProjectTriggerEventCount = realtimeMidiSaturatingAdd(
        diagnostics_.capturedProjectTriggerEventCount,
        1U
    );
}

bool MidiCcGlobalFrameCoordinator::hasPendingProjectModulationTriggers() const {
    return project_trigger_queue_.hasPending();
}

uint16_t MidiCcGlobalFrameCoordinator::drainProjectModulationTriggers(
    core::state::modulation::ProjectModulationTriggerFrame& out
) {
    return project_trigger_queue_.drain(out);
}

FLASHMEM void MidiCcGlobalFrameCoordinator::resetProject() {
    if (lifecycle_attached_) {
        replacing_pending_controls_ = true;
        (void)queue_.cancelControlChangeEvents();
        replacing_pending_controls_ = false;
    }
    persistent_frames_ = {};
    lane_frames_ = {};
    active_persistent_index_ = 0U;
    active_lane_index_ = 0U;
    reading_persistent_index_ = NO_SOURCE_READER;
    reading_lane_index_ = NO_SOURCE_READER;
    next_persistent_revision_ = 1U;
    next_lane_revision_ = 1U;
    last_live_persistent_revision_ = 0U;
    last_live_lane_revision_ = 0U;
    captured_persistent_revision_ = 0U;
    captured_lane_revision_ = 0U;
    captured_lane_lifecycle_generations_ = {};
    captured_lane_predictive_author_mask_ = 0U;
    logical_lane_lifecycle_generations_ = {};
    combined_candidate_count_ = 0U;
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
    telemetry_exchange_.reset();
    desired_value_count_ = 0U;
    planned_values_valid_ = true;
    dispatched_value_count_ = 0U;
    retry_requested_ = false;
    transport_retry_deferred_until_resume_ = false;
    replacing_pending_controls_ = false;
    control_clock_.reset();
    project_trigger_queue_.reset();
    diagnostics_ = {};
}

MidiCcGlobalFrameCoordinator::TelemetryReadView
MidiCcGlobalFrameCoordinator::readTelemetry() const {
    return telemetry_exchange_.read();
}

}  // namespace core::sequencer
