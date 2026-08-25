#include "state/sequencer/SequencerClipLaunchQueue.hpp"

#include <algorithm>

#include <config/PlatformCompat.hpp>
#include <oc/note/clock/ClockConstants.hpp>
#include <oc/realtime/InterruptGuard.hpp>

namespace core::state::sequencer {

namespace {

constexpr uint32_t kTicksPerBeat = oc::note::clock::PPQN;
constexpr uint32_t kTicksPerBar = 4U * kTicksPerBeat;

constexpr uint16_t trackBit(uint8_t track) noexcept {
    return static_cast<uint16_t>(1U << track);
}

constexpr bool isFollowOrigin(SequencerClipLaunchOrigin origin) noexcept {
    return origin == SequencerClipLaunchOrigin::CLIP_FOLLOW ||
        origin == SequencerClipLaunchOrigin::SCENE_FOLLOW;
}

FLASHMEM uint32_t mixFollowSeed(uint32_t value) noexcept {
    value ^= value >> 16U;
    value *= 0x7FEB352DU;
    value ^= value >> 15U;
    value *= 0x846CA68BU;
    return value ^ (value >> 16U);
}

template <typename IsCandidate>
FLASHMEM
uint8_t resolveFollowChoice(
    SequencerLauncherFollowChoice choice,
    uint8_t current,
    uint32_t seed,
    const IsCandidate& isCandidate
) noexcept {
    if (sequencerLauncherFollowChoiceIsTarget(choice)) {
        return sequencerLauncherFollowTargetSlot(choice);
    }
    if (choice == SequencerLauncherFollowChoice::NEXT) {
        for (uint8_t offset = 1U;
             offset <= SequencerClipGridState::SLOT_COUNT;
             ++offset) {
            const uint8_t slot = static_cast<uint8_t>(
                (current + offset) % SequencerClipGridState::SLOT_COUNT
            );
            if (isCandidate(slot)) return slot;
        }
        return SequencerClipGridState::INVALID_SLOT;
    }
    if (choice == SequencerLauncherFollowChoice::FIRST) {
        for (uint8_t slot = 0U;
             slot < SequencerClipGridState::SLOT_COUNT;
             ++slot) {
            if (isCandidate(slot)) return slot;
        }
        return SequencerClipGridState::INVALID_SLOT;
    }
    if (choice != SequencerLauncherFollowChoice::RANDOM_OTHER &&
        choice != SequencerLauncherFollowChoice::RANDOM_ANY) {
        return SequencerClipGridState::INVALID_SLOT;
    }

    std::array<uint8_t, SequencerClipGridState::SLOT_COUNT> candidates{};
    uint8_t count = 0U;
    for (uint8_t slot = 0U;
         slot < SequencerClipGridState::SLOT_COUNT;
         ++slot) {
        if (!isCandidate(slot) ||
            (choice == SequencerLauncherFollowChoice::RANDOM_OTHER &&
             slot == current)) {
            continue;
        }
        candidates[count++] = slot;
    }
    return count == 0U
        ? SequencerClipGridState::INVALID_SLOT
        : candidates[mixFollowSeed(seed) % count];
}

FLASHMEM uint8_t resolveClipFollowChoice(
    const SequencerClipGridState& clips,
    uint8_t track,
    uint8_t current,
    SequencerLauncherFollowChoice choice,
    uint32_t seed
) noexcept {
    return resolveFollowChoice(
        choice,
        current,
        seed,
        [&clips, track](uint8_t slot) {
            return clips.slotKind({track, slot}) ==
                SequencerLauncherSlotKind::CLIP;
        }
    );
}

FLASHMEM uint8_t resolveSceneFollowChoice(
    const SequencerClipGridState& clips,
    uint16_t enabledTrackMask,
    uint8_t current,
    SequencerLauncherFollowChoice choice,
    uint32_t seed
) noexcept {
    return resolveFollowChoice(
        choice,
        current,
        seed,
        [&clips, enabledTrackMask](uint8_t slot) {
            for (uint8_t track = 0U;
                 track < SequencerClipGridState::TRACK_COUNT;
                 ++track) {
                if ((enabledTrackMask & trackBit(track)) != 0U &&
                    clips.slotKind({track, slot}) !=
                        SequencerLauncherSlotKind::EMPTY) {
                    return true;
                }
            }
            return false;
        }
    );
}

}  // namespace

uint32_t SequencerClipLaunchQueue::nextNonZeroGeneration_(
    uint32_t current
) noexcept {
    const uint32_t next = current + 1U;
    return next == 0U ? 1U : next;
}

bool SequencerClipLaunchQueue::pending_(Phase phase) noexcept {
    return phase == Phase::QUEUED || phase == Phase::STAGED ||
        phase == Phase::ROLLBACK_PENDING;
}

uint8_t SequencerClipLaunchQueue::priority_(
    SequencerClipLaunchOrigin origin
) noexcept {
    switch (origin) {
        case SequencerClipLaunchOrigin::DIRECT_STOP: return 3U;
        case SequencerClipLaunchOrigin::MANUAL_CLIP:
        case SequencerClipLaunchOrigin::MANUAL_SCENE: return 2U;
        case SequencerClipLaunchOrigin::SCENE_FOLLOW: return 1U;
        case SequencerClipLaunchOrigin::CLIP_FOLLOW:
        default: return 0U;
    }
}

SequencerClipLaunchStatus SequencerClipLaunchQueue::status_(Phase phase) noexcept {
    switch (phase) {
        case Phase::QUEUED:
        case Phase::STAGED:
        case Phase::ROLLBACK_PENDING:
            return SequencerClipLaunchStatus::QUEUED;
        case Phase::APPLIED_PENDING_TELEMETRY:
        case Phase::APPLIED:
            return SequencerClipLaunchStatus::APPLIED;
        case Phase::CANCELLED:
            return SequencerClipLaunchStatus::CANCELLED;
        case Phase::IDLE:
        default:
            return SequencerClipLaunchStatus::IDLE;
    }
}

SequencerClipLaunchQuantization SequencerClipLaunchQueue::followQuantization_(
    SequencerLauncherFollowQuantization quantization
) noexcept {
    switch (quantization) {
        case SequencerLauncherFollowQuantization::BEAT:
            return SequencerClipLaunchQuantization::BEAT;
        case SequencerLauncherFollowQuantization::BAR:
        case SequencerLauncherFollowQuantization::GLOBAL:
        default:
            return SequencerClipLaunchQuantization::BAR;
    }
}

bool SequencerClipLaunchQueue::due_(uint32_t now, uint32_t deadline) noexcept {
    return static_cast<int32_t>(now - deadline) >= 0;
}

FLASHMEM uint32_t SequencerClipLaunchQueue::nextBoundaryTick_(
    SequencerClipLaunchQuantization quantization,
    bool transportPlaying,
    bool includeCurrent
) const noexcept {
    if (!transportPlaying ||
        quantization == SequencerClipLaunchQuantization::IMMEDIATE) {
        return transport_tick_;
    }
    const uint32_t unit = quantization == SequencerClipLaunchQuantization::BEAT
        ? kTicksPerBeat
        : kTicksPerBar;
    const uint32_t remainder = transport_tick_ % unit;
    if (includeCurrent && remainder == 0U) return transport_tick_;
    return transport_tick_ + (unit - remainder);
}

FLASHMEM uint8_t SequencerClipLaunchQueue::beatsRemaining_(
    uint32_t dueTick
) const noexcept {
    if (!transport_playing_ || due_(transport_tick_, dueTick)) return 0U;
    const uint32_t ticks = dueTick - transport_tick_;
    return static_cast<uint8_t>(std::min<uint32_t>(
        255U,
        (ticks + kTicksPerBeat - 1U) / kTicksPerBeat
    ));
}

FLASHMEM uint8_t SequencerClipLaunchQueue::queuedRemainingQ8_(
    uint32_t dueTick,
    SequencerClipLaunchQuantization quantization
) const noexcept {
    if (!transport_playing_ || due_(transport_tick_, dueTick) ||
        quantization == SequencerClipLaunchQuantization::IMMEDIATE) {
        return 0U;
    }
    const uint32_t span = quantization == SequencerClipLaunchQuantization::BEAT
        ? kTicksPerBeat
        : kTicksPerBar;
    const uint32_t remaining = dueTick - transport_tick_;
    return static_cast<uint8_t>(std::min<uint32_t>(
        255U,
        (remaining * 255U + span - 1U) / span
    ));
}

void SequencerClipLaunchQueue::bumpTelemetryRevision_() {
    telemetry_revision_.set(nextNonZeroGeneration_(telemetry_revision_.get()));
}

FLASHMEM void SequencerClipLaunchQueue::resetEntry_(
    Entry& entry,
    uint8_t track,
    const SequencerClipGridState& clips,
    bool enabled
) noexcept {
    entry.phase = Phase::IDLE;
    entry.action = SequencerClipLaunchAction::NONE;
    entry.origin = SequencerClipLaunchOrigin::MANUAL_CLIP;
    entry.quantization = SequencerClipLaunchQuantization::IMMEDIATE;
    entry.activeSlot = enabled
        ? clips.residentSlot(track)
        : SequencerClipGridState::INVALID_SLOT;
    entry.targetSlot = SequencerClipGridState::INVALID_SLOT;
    entry.previousSnapshotIndex = 0U;
    entry.targetSnapshotIndex = 0U;
    entry.stopped = enabled &&
        entry.activeSlot >= SequencerClipGridState::SLOT_COUNT;
    entry.sourceGeneration = 0U;
    entry.dueTick = 0U;
    entry.generation = 0U;
    entry.groupGeneration = 0U;
    entry.pendingLoopTicks = 0U;
    entry.activeStartedTick = transport_tick_;
    entry.activeLoopTicks = entry.activeSlot <
            SequencerClipGridState::SLOT_COUNT
        ? kTicksPerBar : 0U;
    entry.activeFollowScheduled = false;
    entry.pendingBehavior = {};
    entry.activeBehavior = entry.activeSlot <
            SequencerClipGridState::SLOT_COUNT
        ? clips.clipBehavior({track, entry.activeSlot})
        : SequencerLauncherBehavior{};
}

FLASHMEM void SequencerClipLaunchQueue::reset(
    const SequencerClipGridState& clips,
    uint16_t enabledTrackMask
) {
    {
        oc::realtime::InterruptGuard lock;
        enabledTrackMask = static_cast<uint16_t>(
            enabledTrackMask & ALL_TRACKS_MASK);
        transport_tick_ = 0U;
        transport_playing_ = false;
        for (uint8_t track = 0U; track < TRACK_COUNT; ++track) {
            resetEntry_(
                entries_[track],
                track,
                clips,
                (enabledTrackMask & trackBit(track)) != 0U
            );
        }
        rollback_plan_ = {};
        rollback_track_mask_ = 0U;
        rollback_previous_snapshot_index_ = 0U;
        rollback_generation_ = 0U;
        next_generation_ = 0U;
        last_follow_process_tick_ = 0U;
        follow_process_started_ = false;
        enabled_track_mask_ = enabledTrackMask;
        published_beat_ = 0U;
        active_scene_ = SequencerClipGridState::INVALID_SLOT;
        queued_scene_ = SequencerClipGridState::INVALID_SLOT;
        scene_expected_mask_ = 0U;
        scene_applied_mask_ = 0U;
        active_scene_started_tick_ = 0U;
        active_scene_generation_ = 0U;
        queued_scene_generation_ = 0U;
        active_scene_follow_scheduled_ = false;
        scene_replaced_ = false;
        scene_status_ = SequencerClipLaunchStatus::IDLE;
        active_scene_behavior_ = {};
        queued_scene_behavior_ = {};
    }
    bumpTelemetryRevision_();
}

FLASHMEM void SequencerClipLaunchQueue::synchronizeEnabledTracks(
    const SequencerClipGridState& clips,
    uint16_t enabledTrackMask
) {
    enabledTrackMask = static_cast<uint16_t>(
        enabledTrackMask & ALL_TRACKS_MASK);
    bool telemetryChanged = enabledTrackMask != enabled_track_mask_;
    {
        oc::realtime::InterruptGuard lock;
        const uint16_t changed = static_cast<uint16_t>(
            enabledTrackMask ^ enabled_track_mask_);
        uint16_t resetTracks = changed;
        for (uint8_t track = 0U; track < TRACK_COUNT; ++track) {
            if ((changed & trackBit(track)) != 0U) {
                resetEntry_(
                    entries_[track],
                    track,
                    clips,
                    (enabledTrackMask & trackBit(track)) != 0U
                );
                rollback_plan_.requests[track] = {};
                continue;
            }
            auto& entry = entries_[track];
            const SequencerClipAddress active{track, entry.activeSlot};
            if ((enabledTrackMask & trackBit(track)) != 0U &&
                entry.activeSlot < SequencerClipGridState::SLOT_COUNT &&
                !clips.isOccupied(active)) {
                resetEntry_(entry, track, clips, true);
                rollback_plan_.requests[track] = {};
                resetTracks = static_cast<uint16_t>(
                    resetTracks | trackBit(track));
                telemetryChanged = true;
            }
        }
        rollback_track_mask_ = static_cast<uint16_t>(
            rollback_track_mask_ & enabledTrackMask &
            static_cast<uint16_t>(~resetTracks));
        rollback_plan_.queuedMask = static_cast<uint16_t>(
            rollback_plan_.queuedMask & enabledTrackMask &
            static_cast<uint16_t>(~resetTracks));
        rollback_plan_.sceneExpectedMask = static_cast<uint16_t>(
            rollback_plan_.sceneExpectedMask & enabledTrackMask &
            static_cast<uint16_t>(~resetTracks));
        scene_expected_mask_ = static_cast<uint16_t>(
            scene_expected_mask_ & enabledTrackMask &
            static_cast<uint16_t>(~resetTracks));
        scene_applied_mask_ = static_cast<uint16_t>(
            scene_applied_mask_ & enabledTrackMask &
            static_cast<uint16_t>(~resetTracks));
        if (resetTracks != 0U &&
            rollback_plan_.sceneExpectedMask == 0U) {
            rollback_plan_.sceneSlot =
                SequencerClipGridState::INVALID_SLOT;
            rollback_plan_.sceneGeneration = 0U;
            rollback_plan_.sceneBehavior = {};
        }
        if (resetTracks != 0U &&
            queued_scene_ != SequencerClipGridState::INVALID_SLOT &&
            scene_expected_mask_ == 0U) {
            queued_scene_ = SequencerClipGridState::INVALID_SLOT;
            queued_scene_generation_ = 0U;
            queued_scene_behavior_ = {};
            scene_status_ = SequencerClipLaunchStatus::CANCELLED;
        }
        enabled_track_mask_ = enabledTrackMask;
    }
    if (telemetryChanged) bumpTelemetryRevision_();
}

void SequencerClipLaunchQueue::updateTransportPosition(
    uint32_t tick,
    bool playing
) {
    const uint32_t beat = tick / kTicksPerBeat;
    const bool changed = playing != transport_playing_ ||
        (playing && beat != published_beat_);
    transport_tick_ = tick;
    transport_playing_ = playing;
    published_beat_ = beat;
    if (changed) bumpTelemetryRevision_();
}

FLASHMEM void SequencerClipLaunchQueue::captureDesiredPlan_(
    DesiredPlan& out
) const noexcept {
    if (rollback_track_mask_ != 0U) {
        out = rollback_plan_;
        return;
    }
    out = {};
    out.sceneSlot = queued_scene_;
    out.sceneExpectedMask = scene_expected_mask_;
    out.sceneGeneration = queued_scene_generation_;
    out.sceneBehavior = queued_scene_behavior_;
    for (uint8_t track = 0U; track < TRACK_COUNT; ++track) {
        const auto& entry = entries_[track];
        if (!pending_(entry.phase)) continue;
        auto& request = out.requests[track];
        request.action = entry.action;
        request.origin = entry.origin;
        request.quantization = entry.quantization;
        request.targetSlot = entry.targetSlot;
        request.sourceGeneration = entry.sourceGeneration;
        request.dueTick = entry.dueTick;
        request.generation = entry.generation;
        request.groupGeneration = entry.groupGeneration;
        request.loopTicks = entry.pendingLoopTicks;
        request.behavior = entry.pendingBehavior;
        out.queuedMask = static_cast<uint16_t>(out.queuedMask | trackBit(track));
    }
}

FLASHMEM void SequencerClipLaunchQueue::applyDesiredPlan_(
    const DesiredPlan& plan
) noexcept {
    for (uint8_t track = 0U; track < TRACK_COUNT; ++track) {
        auto& entry = entries_[track];
        const auto& request = plan.requests[track];
        if ((plan.queuedMask & trackBit(track)) == 0U || !request.queued()) {
            if (pending_(entry.phase)) {
                entry.phase = Phase::CANCELLED;
                entry.action = SequencerClipLaunchAction::NONE;
                entry.targetSlot = SequencerClipGridState::INVALID_SLOT;
                entry.groupGeneration = 0U;
            }
            continue;
        }
        entry.phase = Phase::QUEUED;
        entry.action = request.action;
        entry.origin = request.origin;
        entry.quantization = request.quantization;
        entry.targetSlot = request.targetSlot;
        entry.sourceGeneration = request.sourceGeneration;
        entry.dueTick = request.dueTick;
        entry.generation = request.generation;
        entry.groupGeneration = request.groupGeneration;
        entry.pendingLoopTicks = request.loopTicks;
        entry.pendingBehavior = request.behavior;
    }
    queued_scene_ = plan.sceneSlot;
    scene_expected_mask_ = plan.sceneExpectedMask;
    scene_applied_mask_ = 0U;
    queued_scene_generation_ = plan.sceneGeneration;
    queued_scene_behavior_ = plan.sceneBehavior;
    if (queued_scene_ != SequencerClipGridState::INVALID_SLOT) {
        scene_status_ = SequencerClipLaunchStatus::QUEUED;
    }
}

FLASHMEM void SequencerClipLaunchQueue::commitDesiredPlan_(
    const DesiredPlan& plan
) {
    uint16_t stagedMask = 0U;
    uint8_t previousIndex = 0U;
    {
        oc::realtime::InterruptGuard lock;
        for (uint8_t track = 0U; track < TRACK_COUNT; ++track) {
            if (entries_[track].phase != Phase::STAGED &&
                entries_[track].phase != Phase::ROLLBACK_PENDING) {
                continue;
            }
            stagedMask = static_cast<uint16_t>(stagedMask | trackBit(track));
            previousIndex = entries_[track].previousSnapshotIndex;
        }
        if (stagedMask == 0U) {
            applyDesiredPlan_(plan);
        } else {
            rollback_plan_ = plan;
            rollback_track_mask_ = stagedMask;
            rollback_previous_snapshot_index_ = previousIndex;
            rollback_generation_ = nextNonZeroGeneration_(rollback_generation_);
            for (uint8_t track = 0U; track < TRACK_COUNT; ++track) {
                if ((stagedMask & trackBit(track)) != 0U) {
                    entries_[track].phase = Phase::ROLLBACK_PENDING;
                }
            }
            queued_scene_ = plan.sceneSlot;
            scene_expected_mask_ = plan.sceneExpectedMask;
            queued_scene_generation_ = plan.sceneGeneration;
            queued_scene_behavior_ = plan.sceneBehavior;
            scene_status_ = queued_scene_ == SequencerClipGridState::INVALID_SLOT
                ? SequencerClipLaunchStatus::CANCELLED
                : SequencerClipLaunchStatus::QUEUED;
        }
    }
    bumpTelemetryRevision_();
}

FLASHMEM bool SequencerClipLaunchQueue::requestWithOrigin_(
    SequencerClipAddress target,
    const SequencerClipGridState& clips,
    bool transportPlaying,
    SequencerClipLaunchQuantization quantization,
    uint32_t loopTicks,
    SequencerClipLaunchOrigin origin
) {
    if (!SequencerClipGridState::validAddress(target) ||
        clips.slotKind(target) != SequencerLauncherSlotKind::CLIP) {
        return false;
    }
    DesiredPlan plan;
    captureDesiredPlan_(plan);
    auto& current = plan.requests[target.track];
    if (current.queued() && priority_(current.origin) > priority_(origin)) {
        return false;
    }
    if (current.groupGeneration != 0U &&
        current.groupGeneration == plan.sceneGeneration) {
        plan.sceneExpectedMask = static_cast<uint16_t>(
            plan.sceneExpectedMask &
            static_cast<uint16_t>(~trackBit(target.track))
        );
        if (plan.sceneExpectedMask == 0U) {
            plan.sceneSlot = SequencerClipGridState::INVALID_SLOT;
            plan.sceneGeneration = 0U;
            plan.sceneBehavior = {};
        }
    }
    next_generation_ = nextNonZeroGeneration_(next_generation_);
    current.action = SequencerClipLaunchAction::CLIP;
    current.origin = origin;
    current.quantization = transportPlaying
        ? quantization
        : SequencerClipLaunchQuantization::IMMEDIATE;
    current.targetSlot = target.slot;
    current.sourceGeneration = clips.generation(target);
    current.dueTick = nextBoundaryTick_(
        current.quantization,
        transportPlaying,
        isFollowOrigin(origin)
    );
    current.generation = next_generation_;
    current.groupGeneration = 0U;
    current.loopTicks = loopTicks == 0U ? kTicksPerBar : loopTicks;
    current.behavior = clips.clipBehavior(target);
    plan.queuedMask = static_cast<uint16_t>(
        plan.queuedMask | trackBit(target.track));
    commitDesiredPlan_(plan);
    return true;
}

FLASHMEM bool SequencerClipLaunchQueue::request(
    SequencerClipAddress target,
    const SequencerClipGridState& clips,
    bool transportPlaying,
    SequencerClipLaunchQuantization quantization,
    uint32_t loopTicks
) {
    return requestWithOrigin_(
        target,
        clips,
        transportPlaying,
        quantization,
        loopTicks,
        SequencerClipLaunchOrigin::MANUAL_CLIP
    );
}

FLASHMEM bool SequencerClipLaunchQueue::requestStop(
    uint8_t track,
    bool transportPlaying,
    SequencerClipLaunchQuantization quantization,
    SequencerClipLaunchOrigin origin,
    uint8_t sourceSlot
) {
    if (track >= TRACK_COUNT) return false;
    DesiredPlan plan;
    captureDesiredPlan_(plan);
    auto& current = plan.requests[track];
    if (current.queued() && priority_(current.origin) > priority_(origin)) {
        return false;
    }
    if (current.groupGeneration != 0U &&
        current.groupGeneration == plan.sceneGeneration) {
        plan.sceneExpectedMask = static_cast<uint16_t>(
            plan.sceneExpectedMask &
            static_cast<uint16_t>(~trackBit(track))
        );
        if (plan.sceneExpectedMask == 0U) {
            plan.sceneSlot = SequencerClipGridState::INVALID_SLOT;
            plan.sceneGeneration = 0U;
            plan.sceneBehavior = {};
        }
    }
    next_generation_ = nextNonZeroGeneration_(next_generation_);
    current = {};
    current.action = SequencerClipLaunchAction::STOP;
    current.origin = origin;
    current.quantization = transportPlaying
        ? quantization
        : SequencerClipLaunchQuantization::IMMEDIATE;
    current.targetSlot = sourceSlot;
    current.dueTick = nextBoundaryTick_(
        current.quantization,
        transportPlaying,
        isFollowOrigin(origin)
    );
    current.generation = next_generation_;
    plan.queuedMask = static_cast<uint16_t>(plan.queuedMask | trackBit(track));
    commitDesiredPlan_(plan);
    if (!transportPlaying) {
        bool applied = false;
        {
            oc::realtime::InterruptGuard lock;
            auto& entry = entries_[track];
            if (entry.phase == Phase::QUEUED &&
                entry.generation == current.generation) {
                entry.phase = Phase::APPLIED;
                entry.stopped = true;
                entry.activeFollowScheduled = false;
                applied = true;
            }
        }
        if (applied) bumpTelemetryRevision_();
    }
    return true;
}

FLASHMEM bool SequencerClipLaunchQueue::requestSceneWithOrigin_(
    uint8_t slot,
    const SequencerClipGridState& clips,
    uint16_t enabledTrackMask,
    bool transportPlaying,
    SequencerClipLaunchQuantization quantization,
    const std::array<uint32_t, TRACK_COUNT>* loopTicks,
    SequencerClipLaunchOrigin origin
) {
    if (slot >= SequencerClipGridState::SLOT_COUNT) return false;
    DesiredPlan plan;
    captureDesiredPlan_(plan);

    const uint32_t oldSceneGeneration = plan.sceneGeneration;
    const bool cancelSame = origin == SequencerClipLaunchOrigin::MANUAL_SCENE &&
        plan.sceneSlot == slot && oldSceneGeneration != 0U;
    for (uint8_t track = 0U; track < TRACK_COUNT; ++track) {
        if (plan.requests[track].groupGeneration == oldSceneGeneration &&
            oldSceneGeneration != 0U) {
            plan.requests[track] = {};
            plan.queuedMask = static_cast<uint16_t>(
                plan.queuedMask & static_cast<uint16_t>(~trackBit(track)));
        }
    }
    if (cancelSame) {
        plan.sceneSlot = SequencerClipGridState::INVALID_SLOT;
        plan.sceneExpectedMask = 0U;
        plan.sceneGeneration = 0U;
        plan.sceneBehavior = {};
        scene_replaced_ = false;
        scene_status_ = SequencerClipLaunchStatus::CANCELLED;
        commitDesiredPlan_(plan);
        return true;
    }

    next_generation_ = nextNonZeroGeneration_(next_generation_);
    const uint32_t groupGeneration = next_generation_;
    const auto effectiveQuantization = transportPlaying
        ? quantization
        : SequencerClipLaunchQuantization::IMMEDIATE;
    const uint32_t dueTick = nextBoundaryTick_(
        effectiveQuantization,
        transportPlaying,
        isFollowOrigin(origin)
    );
    uint16_t expectedMask = 0U;
    enabledTrackMask = static_cast<uint16_t>(enabledTrackMask & ALL_TRACKS_MASK);
    for (uint8_t track = 0U; track < TRACK_COUNT; ++track) {
        if ((enabledTrackMask & trackBit(track)) == 0U) continue;
        const SequencerClipAddress address{track, slot};
        const auto kind = clips.slotKind(address);
        if (kind == SequencerLauncherSlotKind::EMPTY) continue;
        auto& request = plan.requests[track];
        if (request.queued() && priority_(request.origin) > priority_(origin)) {
            continue;
        }
        request = {};
        request.action = kind == SequencerLauncherSlotKind::STOP
            ? SequencerClipLaunchAction::STOP
            : SequencerClipLaunchAction::CLIP;
        request.origin = origin;
        request.quantization = effectiveQuantization;
        request.targetSlot = slot;
        request.sourceGeneration = kind == SequencerLauncherSlotKind::CLIP
            ? clips.generation(address)
            : 0U;
        request.dueTick = dueTick;
        request.generation = groupGeneration;
        request.groupGeneration = groupGeneration;
        request.loopTicks = loopTicks != nullptr && (*loopTicks)[track] != 0U
            ? (*loopTicks)[track]
            : kTicksPerBar;
        request.behavior = kind == SequencerLauncherSlotKind::CLIP
            ? clips.clipBehavior(address)
            : SequencerLauncherBehavior{};
        expectedMask = static_cast<uint16_t>(expectedMask | trackBit(track));
        plan.queuedMask = static_cast<uint16_t>(
            plan.queuedMask | trackBit(track));
    }
    if (expectedMask == 0U) return false;

    scene_replaced_ = plan.sceneSlot != SequencerClipGridState::INVALID_SLOT &&
        plan.sceneSlot != slot;
    plan.sceneSlot = slot;
    plan.sceneExpectedMask = expectedMask;
    plan.sceneGeneration = groupGeneration;
    plan.sceneBehavior = clips.sceneBehavior(slot);
    commitDesiredPlan_(plan);
    return true;
}

FLASHMEM bool SequencerClipLaunchQueue::requestScene(
    uint8_t slot,
    const SequencerClipGridState& clips,
    uint16_t enabledTrackMask,
    bool transportPlaying,
    SequencerClipLaunchQuantization quantization,
    const std::array<uint32_t, TRACK_COUNT>* loopTicks
) {
    return requestSceneWithOrigin_(
        slot,
        clips,
        enabledTrackMask,
        transportPlaying,
        quantization,
        loopTicks,
        SequencerClipLaunchOrigin::MANUAL_SCENE
    );
}

void SequencerClipLaunchQueue::processFollowActions(
    const SequencerClipGridState& clips,
    uint16_t enabledTrackMask,
    bool transportPlaying,
    const std::array<uint32_t, TRACK_COUNT>* loopTicks
) {
    if (!transportPlaying) {
        follow_process_started_ = false;
        return;
    }
    if (follow_process_started_ &&
        last_follow_process_tick_ == transport_tick_) {
        return;
    }
    last_follow_process_tick_ = transport_tick_;
    follow_process_started_ = true;
    std::array<SequencerLauncherFollowChoice, TRACK_COUNT> clipChoices{};
    std::array<uint8_t, TRACK_COUNT> clipCurrentSlots{};
    std::array<uint32_t, TRACK_COUNT> clipSeeds{};
    std::array<SequencerLauncherFollowQuantization, TRACK_COUNT>
        clipQuantizations{};
    clipChoices.fill(SequencerLauncherFollowChoice::NONE);
    clipCurrentSlots.fill(SequencerClipGridState::INVALID_SLOT);
    SequencerLauncherFollowChoice sceneChoice =
        SequencerLauncherFollowChoice::NONE;
    uint8_t sceneCurrentSlot = SequencerClipGridState::INVALID_SLOT;
    uint32_t sceneSeed = 0U;
    SequencerLauncherFollowQuantization sceneQuantization =
        SequencerLauncherFollowQuantization::GLOBAL;
    {
        oc::realtime::InterruptGuard lock;
        for (uint8_t track = 0U; track < TRACK_COUNT; ++track) {
            auto& entry = entries_[track];
            if (entry.stopped || entry.activeFollowScheduled ||
                !entry.activeBehavior.enabled() ||
                entry.activeLoopTicks == 0U) {
                continue;
            }
            const uint32_t deadline = entry.activeStartedTick +
                static_cast<uint32_t>(entry.activeBehavior.length) *
                    entry.activeLoopTicks;
            if (!due_(transport_tick_, deadline)) continue;
            entry.activeFollowScheduled = true;
            clipChoices[track] = entry.activeBehavior.follow;
            clipCurrentSlots[track] = entry.activeSlot;
            clipSeeds[track] = transport_tick_ ^ entry.activeStartedTick ^
                (static_cast<uint32_t>(track + 1U) * 0x9E3779B9U) ^
                static_cast<uint32_t>(entry.activeSlot + 1U);
            clipQuantizations[track] = entry.activeBehavior.quantization;
        }
        if (!active_scene_follow_scheduled_ &&
            active_scene_behavior_.enabled()) {
            const uint32_t deadline = active_scene_started_tick_ +
                static_cast<uint32_t>(active_scene_behavior_.length) *
                    kTicksPerBar;
            if (due_(transport_tick_, deadline)) {
                active_scene_follow_scheduled_ = true;
                sceneChoice = active_scene_behavior_.follow;
                sceneCurrentSlot = active_scene_;
                sceneSeed = transport_tick_ ^ active_scene_started_tick_ ^
                    (static_cast<uint32_t>(active_scene_ + 1U) *
                     0x85EBCA6BU);
                sceneQuantization = active_scene_behavior_.quantization;
            }
        }
    }

    for (uint8_t track = 0U; track < TRACK_COUNT; ++track) {
        if (clipChoices[track] == SequencerLauncherFollowChoice::NONE) {
            continue;
        }
        const uint8_t targetSlot = resolveClipFollowChoice(
            clips,
            track,
            clipCurrentSlots[track],
            clipChoices[track],
            clipSeeds[track]
        );
        if (targetSlot >= SequencerClipGridState::SLOT_COUNT) continue;
        const SequencerClipAddress target{track, targetSlot};
        if (clips.isStop(target)) {
            (void)requestStop(
                track,
                true,
                followQuantization_(clipQuantizations[track]),
                SequencerClipLaunchOrigin::CLIP_FOLLOW,
                target.slot
            );
        } else {
            (void)requestWithOrigin_(
                target,
                clips,
                true,
                followQuantization_(clipQuantizations[track]),
                loopTicks != nullptr ? (*loopTicks)[track] : 0U,
                SequencerClipLaunchOrigin::CLIP_FOLLOW
            );
        }
    }
    const uint8_t sceneTarget = sceneChoice ==
            SequencerLauncherFollowChoice::NONE
        ? SequencerClipGridState::INVALID_SLOT
        : resolveSceneFollowChoice(
            clips,
            enabledTrackMask,
            sceneCurrentSlot,
            sceneChoice,
            sceneSeed
        );
    if (sceneTarget < SequencerClipGridState::SLOT_COUNT) {
        (void)requestSceneWithOrigin_(
            sceneTarget,
            clips,
            enabledTrackMask,
            true,
            followQuantization_(sceneQuantization),
            loopTicks,
            SequencerClipLaunchOrigin::SCENE_FOLLOW
        );
    }
}

SequencerClipLaunchRollbackPublication
SequencerClipLaunchQueue::captureRollbackPublication() const noexcept {
    if (rollback_track_mask_ == 0U) return {};
    return {
        .trackMask = rollback_track_mask_,
        .previousSnapshotIndex = rollback_previous_snapshot_index_,
        .generation = rollback_generation_,
    };
}

void SequencerClipLaunchQueue::applyRollbackPublication(
    const SequencerClipLaunchRollbackPublication& publication
) noexcept {
    if (publication.empty() || publication.trackMask != rollback_track_mask_ ||
        publication.generation != rollback_generation_) {
        return;
    }
    applyDesiredPlan_(rollback_plan_);
    rollback_plan_ = {};
    rollback_track_mask_ = 0U;
    rollback_previous_snapshot_index_ = 0U;
}

bool SequencerClipLaunchQueue::queueFallback_(
    uint8_t track,
    const SequencerClipGridState& clips,
    bool transportPlaying
) {
    auto& entry = entries_[track];
    if (entry.stopped) return false;
    const uint8_t resident = clips.residentSlot(track);
    if (resident >= SequencerClipGridState::SLOT_COUNT ||
        !clips.isOccupied({track, resident})) {
        entry.activeSlot = SequencerClipGridState::INVALID_SLOT;
        entry.targetSlot = SequencerClipGridState::INVALID_SLOT;
        entry.phase = Phase::CANCELLED;
        return true;
    }
    next_generation_ = nextNonZeroGeneration_(next_generation_);
    entry.phase = Phase::QUEUED;
    entry.action = SequencerClipLaunchAction::CLIP;
    entry.origin = SequencerClipLaunchOrigin::MANUAL_CLIP;
    entry.quantization = transportPlaying
        ? SequencerClipLaunchQuantization::BAR
        : SequencerClipLaunchQuantization::IMMEDIATE;
    entry.targetSlot = resident;
    entry.sourceGeneration = clips.generation({track, resident});
    entry.dueTick = nextBoundaryTick_(entry.quantization, transportPlaying);
    entry.generation = next_generation_;
    entry.pendingLoopTicks = kTicksPerBar;
    entry.pendingBehavior = clips.clipBehavior({track, resident});
    return true;
}

SequencerClipLaunchRuntimePublication
SequencerClipLaunchQueue::captureRuntimePublication(
    const SequencerClipGridState& clips,
    bool transportPlaying
) {
    SequencerClipLaunchRuntimePublication publication{};
    bool telemetryChanged = false;
    {
        oc::realtime::InterruptGuard lock;
        for (uint8_t track = 0U; track < TRACK_COUNT; ++track) {
            auto& entry = entries_[track];
            const SequencerClipAddress active{track, entry.activeSlot};
            if (!pending_(entry.phase) && !entry.stopped &&
                !clips.isOccupied(active) &&
                clips.residentSlot(track) < SequencerClipGridState::SLOT_COUNT) {
                telemetryChanged = queueFallback_(
                    track,
                    clips,
                    transportPlaying) || telemetryChanged;
            }
            if (entry.phase != Phase::QUEUED) continue;

            if (entry.action == SequencerClipLaunchAction::CLIP) {
                const SequencerClipAddress target{track, entry.targetSlot};
                if (!clips.isOccupied(target) ||
                    clips.generation(target) != entry.sourceGeneration) {
                    const uint32_t cancelledGroup = entry.groupGeneration;
                    entry.phase = Phase::CANCELLED;
                    entry.action = SequencerClipLaunchAction::NONE;
                    entry.targetSlot = SequencerClipGridState::INVALID_SLOT;
                    if (cancelledGroup != 0U &&
                        cancelledGroup == queued_scene_generation_) {
                        scene_expected_mask_ = static_cast<uint16_t>(
                            scene_expected_mask_ &
                            static_cast<uint16_t>(~trackBit(track))
                        );
                        if (scene_expected_mask_ == 0U) {
                            queued_scene_ = SequencerClipGridState::INVALID_SLOT;
                            queued_scene_generation_ = 0U;
                            queued_scene_behavior_ = {};
                            scene_status_ = SequencerClipLaunchStatus::CANCELLED;
                        }
                    }
                    telemetryChanged = true;
                    continue;
                }
            }
            publication.queuedMask = static_cast<uint16_t>(
                publication.queuedMask | trackBit(track));
            publication.actions[track] = entry.action;
            publication.slots[track] = entry.targetSlot;
            publication.generations[track] = entry.generation;
            publication.sourceGenerations[track] = entry.sourceGeneration;
            publication.dueTicks[track] = entry.dueTick;
            publication.quantizations[track] = entry.quantization;
        }
    }
    if (telemetryChanged) bumpTelemetryRevision_();
    return publication;
}

bool SequencerClipLaunchQueue::captureRuntimeSources(
    const SequencerClipGridState& clips,
    SequencerClipRuntimeSources& out
) const noexcept {
    std::array<uint8_t, TRACK_COUNT> slots{};
    {
        oc::realtime::InterruptGuard lock;
        for (uint8_t track = 0U; track < TRACK_COUNT; ++track) {
            const auto& entry = entries_[track];
            const bool targetClip = entry.phase == Phase::QUEUED &&
                entry.action == SequencerClipLaunchAction::CLIP;
            slots[track] = targetClip ? entry.targetSlot : entry.activeSlot;
        }
    }
    for (uint8_t track = 0U; track < TRACK_COUNT; ++track) {
        const SequencerClipAddress address{track, slots[track]};
        auto& source = out[track];
        source = {
            .address = address,
            .generation = clips.generation(address),
            .document = clips.inactiveDocument(address),
            .valid = clips.isOccupied(address),
        };
    }
    return true;
}

void SequencerClipLaunchQueue::applyRuntimePublication(
    const SequencerClipLaunchRuntimePublication& publication,
    uint8_t previousSnapshotIndex,
    uint8_t targetSnapshotIndex
) noexcept {
    for (uint8_t track = 0U; track < TRACK_COUNT; ++track) {
        if ((publication.queuedMask & trackBit(track)) == 0U) continue;
        auto& entry = entries_[track];
        if (entry.phase != Phase::QUEUED ||
            entry.generation != publication.generations[track] ||
            entry.action != publication.actions[track] ||
            entry.targetSlot != publication.slots[track] ||
            entry.sourceGeneration != publication.sourceGenerations[track]) {
            continue;
        }
        entry.previousSnapshotIndex = static_cast<uint8_t>(
            previousSnapshotIndex & 0x1U);
        entry.targetSnapshotIndex = static_cast<uint8_t>(
            targetSnapshotIndex & 0x1U);
        entry.phase = Phase::STAGED;
    }
}

SequencerClipLaunchRealtimeView SequencerClipLaunchQueue::realtimeView(
    uint8_t track
) const noexcept {
    if (track >= TRACK_COUNT) return {};
    const auto& entry = entries_[track];
    SequencerClipLaunchRealtimeView view{
        .action = entry.action,
        .quantization = entry.quantization,
        .slot = entry.targetSlot,
        .previousSnapshotIndex = entry.previousSnapshotIndex,
        .dueTick = entry.dueTick,
        .generation = entry.generation,
    };
    if (entry.phase == Phase::ROLLBACK_PENDING) {
        view.disposition = SequencerClipLaunchRealtimeView::Disposition::FROZEN;
    } else if (entry.phase == Phase::STAGED) {
        view.disposition = SequencerClipLaunchRealtimeView::Disposition::STAGED;
    }
    return view;
}

bool SequencerClipLaunchQueue::markAppliedFromRealtime(
    uint8_t track,
    uint32_t generation,
    uint32_t tick
) noexcept {
    if (track >= TRACK_COUNT) return false;
    auto& entry = entries_[track];
    if (entry.phase != Phase::STAGED || entry.generation != generation) {
        return false;
    }
    if (entry.action == SequencerClipLaunchAction::STOP) {
        entry.activeSlot = SequencerClipGridState::INVALID_SLOT;
        entry.stopped = true;
        entry.activeBehavior = {};
        entry.activeLoopTicks = 0U;
    } else if (entry.action == SequencerClipLaunchAction::CLIP) {
        entry.activeSlot = entry.targetSlot;
        entry.stopped = false;
        entry.activeBehavior = entry.pendingBehavior;
        entry.activeLoopTicks = entry.pendingLoopTicks;
        entry.activeStartedTick = tick;
        entry.activeFollowScheduled = false;
    } else {
        return false;
    }
    if (entry.groupGeneration != 0U &&
        entry.groupGeneration == queued_scene_generation_) {
        scene_applied_mask_ = static_cast<uint16_t>(
            scene_applied_mask_ | trackBit(track));
        if ((scene_applied_mask_ & scene_expected_mask_) == scene_expected_mask_) {
            active_scene_ = queued_scene_;
            active_scene_generation_ = queued_scene_generation_;
            active_scene_started_tick_ = tick;
            active_scene_behavior_ = queued_scene_behavior_;
            active_scene_follow_scheduled_ = false;
            queued_scene_ = SequencerClipGridState::INVALID_SLOT;
            queued_scene_generation_ = 0U;
            scene_expected_mask_ = 0U;
            scene_applied_mask_ = 0U;
            scene_status_ = SequencerClipLaunchStatus::APPLIED;
            scene_replaced_ = false;
        }
    }
    entry.phase = Phase::APPLIED_PENDING_TELEMETRY;
    return true;
}

uint16_t SequencerClipLaunchQueue::publishRealtimeTelemetry() {
    uint16_t releasedMask = 0U;
    {
        oc::realtime::InterruptGuard lock;
        for (uint8_t track = 0U; track < TRACK_COUNT; ++track) {
            auto& entry = entries_[track];
            if (entry.phase != Phase::APPLIED_PENDING_TELEMETRY) continue;
            entry.phase = Phase::APPLIED;
            releasedMask = static_cast<uint16_t>(
                releasedMask | trackBit(track));
        }
    }
    if (releasedMask != 0U) bumpTelemetryRevision_();
    return releasedMask;
}

uint16_t SequencerClipLaunchQueue::pendingTrackMask() const noexcept {
    if (rollback_track_mask_ != 0U) return rollback_plan_.queuedMask;
    uint16_t mask = 0U;
    for (uint8_t track = 0U; track < TRACK_COUNT; ++track) {
        if (pending_(entries_[track].phase)) {
            mask = static_cast<uint16_t>(mask | trackBit(track));
        }
    }
    return mask;
}

uint16_t SequencerClipLaunchQueue::stagedTrackMask() const noexcept {
    uint16_t mask = 0U;
    for (uint8_t track = 0U; track < TRACK_COUNT; ++track) {
        if (entries_[track].phase == Phase::STAGED ||
            entries_[track].phase == Phase::ROLLBACK_PENDING) {
            mask = static_cast<uint16_t>(mask | trackBit(track));
        }
    }
    return mask;
}

bool SequencerClipLaunchQueue::references(
    SequencerClipAddress address
) const noexcept {
    if (!SequencerClipGridState::validAddress(address)) return false;
    const auto& entry = entries_[address.track];
    if (entry.activeSlot == address.slot) return true;
    if (rollback_track_mask_ != 0U) {
        const auto& request = rollback_plan_.requests[address.track];
        return request.queued() && request.targetSlot == address.slot;
    }
    return pending_(entry.phase) && entry.targetSlot == address.slot;
}

void SequencerClipLaunchQueue::refreshBehavior(
    SequencerClipAddress address,
    SequencerLauncherBehavior behavior
) noexcept {
    if (!SequencerClipGridState::validAddress(address)) return;
    oc::realtime::InterruptGuard lock;
    auto& entry = entries_[address.track];
    if (entry.activeSlot == address.slot) entry.activeBehavior = behavior;
    if (pending_(entry.phase) && entry.targetSlot == address.slot) {
        entry.pendingBehavior = behavior;
    }
    if (rollback_track_mask_ != 0U) {
        auto& request = rollback_plan_.requests[address.track];
        if (request.queued() && request.targetSlot == address.slot) {
            request.behavior = behavior;
        }
    }
}

uint8_t SequencerClipLaunchQueue::activeSlot(uint8_t track) const noexcept {
    return track < TRACK_COUNT
        ? entries_[track].activeSlot
        : SequencerClipGridState::INVALID_SLOT;
}

bool SequencerClipLaunchQueue::stopped(uint8_t track) const noexcept {
    return track >= TRACK_COUNT || entries_[track].stopped;
}

FLASHMEM SequencerClipLaunchTelemetry SequencerClipLaunchQueue::telemetry(
    uint8_t track
) const noexcept {
    if (track >= TRACK_COUNT) return {};
    const auto& entry = entries_[track];
    const uint32_t loopTicks = entry.activeLoopTicks;
    const uint8_t activePhaseQ8 = !entry.stopped &&
            entry.activeSlot < SequencerClipGridState::SLOT_COUNT &&
            loopTicks != 0U
        ? static_cast<uint8_t>((
            static_cast<uint64_t>(
                (transport_tick_ - entry.activeStartedTick) % loopTicks
            ) << 8U
        ) / loopTicks)
        : 0U;
    const uint32_t activeElapsedTicks = !entry.stopped &&
            entry.activeSlot < SequencerClipGridState::SLOT_COUNT
        ? transport_tick_ - entry.activeStartedTick
        : 0U;
    uint8_t activeRemainingQ8 = 0U;
    if (transport_playing_ && !entry.stopped &&
        entry.activeBehavior.enabled() && loopTicks != 0U) {
        const uint32_t duration =
            static_cast<uint32_t>(entry.activeBehavior.length) * loopTicks;
        const uint32_t elapsed = transport_tick_ - entry.activeStartedTick;
        if (duration != 0U && elapsed < duration) {
            const uint32_t remaining = duration - elapsed;
            if (remaining <= loopTicks) {
                activeRemainingQ8 = static_cast<uint8_t>(std::min<uint32_t>(
                    255U,
                    (remaining * 255U + loopTicks - 1U) / loopTicks
                ));
            }
        }
    }
    if (rollback_track_mask_ != 0U) {
        const auto& request = rollback_plan_.requests[track];
        return {
            .status = request.queued()
                ? SequencerClipLaunchStatus::QUEUED
                : SequencerClipLaunchStatus::CANCELLED,
            .action = request.action,
            .origin = request.origin,
            .quantization = request.quantization,
            .activeSlot = entry.activeSlot,
            .queuedSlot = request.queued()
                ? request.targetSlot
                : SequencerClipGridState::INVALID_SLOT,
            .beatsRemaining = static_cast<uint8_t>(request.queued()
                ? beatsRemaining_(request.dueTick)
                : 0U),
            .queuedRemainingQ8 = static_cast<uint8_t>(request.queued()
                ? queuedRemainingQ8_(request.dueTick, request.quantization)
                : 0U),
            .activePhaseQ8 = activePhaseQ8,
            .activeRemainingQ8 = activeRemainingQ8,
            .activeElapsedTicks = activeElapsedTicks,
            .generation = request.generation,
            .stopped = entry.stopped,
        };
    }
    return {
        .status = status_(entry.phase),
        .action = entry.action,
        .origin = entry.origin,
        .quantization = entry.quantization,
        .activeSlot = entry.activeSlot,
        .queuedSlot = pending_(entry.phase)
            ? entry.targetSlot
            : SequencerClipGridState::INVALID_SLOT,
        .beatsRemaining = static_cast<uint8_t>(pending_(entry.phase)
            ? beatsRemaining_(entry.dueTick)
            : 0U),
        .queuedRemainingQ8 = static_cast<uint8_t>(pending_(entry.phase)
            ? queuedRemainingQ8_(entry.dueTick, entry.quantization)
            : 0U),
        .activePhaseQ8 = activePhaseQ8,
        .activeRemainingQ8 = activeRemainingQ8,
        .activeElapsedTicks = activeElapsedTicks,
        .generation = entry.generation,
        .stopped = entry.stopped,
    };
}

FLASHMEM SequencerSceneLaunchTelemetry
SequencerClipLaunchQueue::sceneTelemetry() const noexcept {
    uint8_t beats = 0U;
    uint8_t queuedRemainingQ8 = 0U;
    if (queued_scene_ != SequencerClipGridState::INVALID_SLOT) {
        for (uint8_t track = 0U; track < TRACK_COUNT; ++track) {
            const uint32_t group = rollback_track_mask_ != 0U
                ? rollback_plan_.requests[track].groupGeneration
                : entries_[track].groupGeneration;
            if (group != queued_scene_generation_) continue;
            const auto telemetryForTrack = telemetry(track);
            if (telemetryForTrack.status == SequencerClipLaunchStatus::QUEUED) {
                beats = std::max<uint8_t>(beats, telemetryForTrack.beatsRemaining);
                queuedRemainingQ8 = std::max<uint8_t>(
                    queuedRemainingQ8,
                    telemetryForTrack.queuedRemainingQ8
                );
            }
        }
    }
    uint8_t activeRemainingQ8 = 0U;
    if (transport_playing_ && active_scene_behavior_.enabled()) {
        const uint32_t duration =
            static_cast<uint32_t>(active_scene_behavior_.length) * kTicksPerBar;
        const uint32_t elapsed = transport_tick_ - active_scene_started_tick_;
        if (duration != 0U && elapsed < duration) {
            const uint32_t remaining = duration - elapsed;
            if (remaining <= kTicksPerBar) {
                activeRemainingQ8 = static_cast<uint8_t>(
                    (remaining * 255U + kTicksPerBar - 1U) / kTicksPerBar
                );
            }
        }
    }
    return {
        .status = scene_status_,
        .activeScene = active_scene_,
        .queuedScene = queued_scene_,
        .beatsRemaining = beats,
        .queuedRemainingQ8 = queuedRemainingQ8,
        .activeRemainingQ8 = activeRemainingQ8,
        .generation = queued_scene_ != SequencerClipGridState::INVALID_SLOT
            ? queued_scene_generation_
            : active_scene_generation_,
        .replaced = scene_replaced_,
    };
}

FLASHMEM bool canDeleteSequencerClip(
    const SequencerClipGridState& clips,
    const SequencerClipLaunchQueue& launches,
    SequencerClipAddress target,
    bool transportPlaying
) noexcept {
    if (!SequencerClipGridState::validAddress(target) ||
        !clips.isOccupied(target)) {
        return false;
    }
    if (!clips.isResident(target)) return !launches.references(target);

    const uint16_t trackBit = static_cast<uint16_t>(1U << target.track);
    return !transportPlaying &&
        (launches.pendingTrackMask() & trackBit) == 0U &&
        (launches.stagedTrackMask() & trackBit) == 0U;
}

}  // namespace core::state::sequencer
