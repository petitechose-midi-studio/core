#include "state/sequencer/SequencerClipLaunchQueue.hpp"

#include <oc/realtime/InterruptGuard.hpp>

namespace core::state::sequencer {

namespace {

constexpr uint16_t trackBit(uint8_t track) noexcept {
    return static_cast<uint16_t>(1U << track);
}

}  // namespace

uint32_t SequencerClipLaunchQueue::nextNonZeroGeneration_(
    uint32_t current
) noexcept {
    const uint32_t next = current + 1U;
    return next == 0U ? 1U : next;
}

bool SequencerClipLaunchQueue::pending_(Phase phase) noexcept {
    return phase == Phase::QUEUED || phase == Phase::STAGED;
}

SequencerClipLaunchStatus SequencerClipLaunchQueue::status_(Phase phase) noexcept {
    switch (phase) {
        case Phase::QUEUED:
        case Phase::STAGED:
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

void SequencerClipLaunchQueue::bumpTelemetryRevision_() {
    telemetry_revision_.set(nextNonZeroGeneration_(telemetry_revision_.get()));
}

void SequencerClipLaunchQueue::reset(
    const SequencerClipGridState& clips,
    uint16_t enabledTrackMask
) {
    {
        oc::realtime::InterruptGuard lock;
        enabledTrackMask = static_cast<uint16_t>(
            enabledTrackMask & ALL_TRACKS_MASK);
        for (uint8_t track = 0U; track < TRACK_COUNT; ++track) {
            auto& entry = entries_[track];
            entry.phase = Phase::IDLE;
            entry.quantization = SequencerClipLaunchQuantization::IMMEDIATE;
            entry.activeSlot = (enabledTrackMask & trackBit(track)) != 0U
                ? clips.residentSlot(track)
                : SequencerClipGridState::INVALID_SLOT;
            entry.targetSlot = SequencerClipGridState::INVALID_SLOT;
            entry.previousSnapshotIndex = 0U;
            entry.targetSnapshotIndex = 0U;
            entry.sourceGeneration = 0U;
            entry.generation = 0U;
        }
        next_generation_ = 0U;
    }
    bumpTelemetryRevision_();
}

bool SequencerClipLaunchQueue::request(
    SequencerClipAddress target,
    const SequencerClipGridState& clips,
    bool transportPlaying,
    SequencerClipLaunchQuantization quantization
) {
    if (!SequencerClipGridState::validAddress(target) ||
        !clips.isOccupied(target)) {
        return false;
    }

    {
        oc::realtime::InterruptGuard lock;
        auto& entry = entries_[target.track];
        if (entry.phase == Phase::STAGED ||
            entry.phase == Phase::APPLIED_PENDING_TELEMETRY) {
            return false;
        }
        if (entry.activeSlot == target.slot && !pending_(entry.phase)) {
            return true;
        }

        next_generation_ = nextNonZeroGeneration_(next_generation_);
        entry.phase = Phase::QUEUED;
        entry.quantization = transportPlaying
            ? quantization
            : SequencerClipLaunchQuantization::IMMEDIATE;
        entry.targetSlot = target.slot;
        entry.sourceGeneration = clips.generation(target);
        entry.generation = next_generation_;
    }
    bumpTelemetryRevision_();
    return true;
}

bool SequencerClipLaunchQueue::queueFallback_(
    uint8_t track,
    const SequencerClipGridState& clips,
    bool transportPlaying
) {
    const uint8_t resident = clips.residentSlot(track);
    if (resident >= SequencerClipGridState::SLOT_COUNT ||
        !clips.isOccupied({track, resident})) {
        entries_[track].activeSlot = SequencerClipGridState::INVALID_SLOT;
        entries_[track].targetSlot = SequencerClipGridState::INVALID_SLOT;
        entries_[track].phase = Phase::CANCELLED;
        return true;
    }

    next_generation_ = nextNonZeroGeneration_(next_generation_);
    auto& entry = entries_[track];
    entry.phase = Phase::QUEUED;
    entry.quantization = transportPlaying
        ? SequencerClipLaunchQuantization::BAR
        : SequencerClipLaunchQuantization::IMMEDIATE;
    entry.targetSlot = resident;
    entry.sourceGeneration = clips.generation({track, resident});
    entry.generation = next_generation_;
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
            if (!pending_(entry.phase) && !clips.isOccupied(active) &&
                clips.residentSlot(track) < SequencerClipGridState::SLOT_COUNT) {
                telemetryChanged = queueFallback_(
                    track,
                    clips,
                    transportPlaying) || telemetryChanged;
            }
            if (entry.phase != Phase::QUEUED) continue;

            const SequencerClipAddress target{track, entry.targetSlot};
            if (!clips.isOccupied(target) ||
                clips.generation(target) != entry.sourceGeneration) {
                entry.phase = Phase::CANCELLED;
                entry.targetSlot = SequencerClipGridState::INVALID_SLOT;
                telemetryChanged = true;
                continue;
            }

            publication.queuedMask = static_cast<uint16_t>(
                publication.queuedMask | trackBit(track));
            publication.slots[track] = entry.targetSlot;
            publication.generations[track] = entry.generation;
            publication.sourceGenerations[track] = entry.sourceGeneration;
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
    for (uint8_t track = 0U; track < TRACK_COUNT; ++track) {
        const auto& entry = entries_[track];
        const uint8_t slot = entry.phase == Phase::QUEUED
            ? entry.targetSlot
            : entry.activeSlot;
        const SequencerClipAddress address{track, slot};
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
        .quantization = entry.quantization,
        .slot = entry.targetSlot,
        .previousSnapshotIndex = entry.previousSnapshotIndex,
        .generation = entry.generation,
    };
    if (entry.phase == Phase::QUEUED) {
        view.disposition = SequencerClipLaunchRealtimeView::Disposition::FROZEN;
    } else if (entry.phase == Phase::STAGED) {
        view.disposition = SequencerClipLaunchRealtimeView::Disposition::STAGED;
    }
    return view;
}

bool SequencerClipLaunchQueue::markAppliedFromRealtime(
    uint8_t track,
    uint32_t generation
) noexcept {
    if (track >= TRACK_COUNT) return false;
    auto& entry = entries_[track];
    if (entry.phase != Phase::STAGED || entry.generation != generation) {
        return false;
    }
    entry.activeSlot = entry.targetSlot;
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
        if (entries_[track].phase == Phase::STAGED) {
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
    return entry.activeSlot == address.slot ||
        (pending_(entry.phase) && entry.targetSlot == address.slot);
}

uint8_t SequencerClipLaunchQueue::activeSlot(uint8_t track) const noexcept {
    return track < TRACK_COUNT
        ? entries_[track].activeSlot
        : SequencerClipGridState::INVALID_SLOT;
}

SequencerClipLaunchTelemetry SequencerClipLaunchQueue::telemetry(
    uint8_t track
) const noexcept {
    if (track >= TRACK_COUNT) return {};
    const auto& entry = entries_[track];
    return {
        .status = status_(entry.phase),
        .quantization = entry.quantization,
        .activeSlot = entry.activeSlot,
        .queuedSlot = pending_(entry.phase)
            ? entry.targetSlot
            : SequencerClipGridState::INVALID_SLOT,
        .generation = entry.generation,
    };
}

}  // namespace core::state::sequencer
