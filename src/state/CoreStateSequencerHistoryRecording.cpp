#include "state/CoreState.hpp"

#include <new>
#include <cstdio>
#include <utility>

#include <config/PlatformCompat.hpp>
#include <oc/log/Log.hpp>
#include <oc/time/Time.hpp>

#if defined(ARDUINO_TEENSY41) && !defined(OC_DESKTOP)
#include <wiring.h>
#endif

#include "state/CoreStateBootstrap.hpp"
#include "state/CoreStateLifecycle.hpp"
#include "state/shared/SharedTrackCoordinator.hpp"
#include "macro/MacroWorkflow.hpp"
#include "midi/MidiUtils.hpp"
#include "state/sequencer/SequencerCcLanePatternOps.hpp"
#include "state/sequencer/SequencerContentViewOps.hpp"
#include "state/sequencer/SequencerStructureHistory.hpp"
#include "state/sequencer/SequencerTrackBankOps.hpp"
#include "state/project/ProjectMenuModel.hpp"
#include "state/project/ProjectTrackDomainServices.hpp"

namespace core::state {

namespace {

FLASHMEM int32_t sequencerHistoryValueForProperty(
    const sequencer::SequencerHistoryPatternSnapshot& snapshot,
    uint8_t step,
    sequencer::StepProperty property
) {
    if (step >= sequencer::SequencerPatternState::MAX_STEPS) {
        return 0;
    }

    switch (property) {
        case sequencer::StepProperty::NOTE:
            return snapshot.flat.note[step];
        case sequencer::StepProperty::VELOCITY:
            return snapshot.flat.velocity[step];
        case sequencer::StepProperty::GATE:
            return snapshot.flat.gate[step];
        case sequencer::StepProperty::NUDGE:
            return snapshot.flat.nudge[step];
        case sequencer::StepProperty::PROBABILITY:
            return snapshot.flat.probability[step];
        default:
            return 0;
    }
}

FLASHMEM sequencer::SequencerHistoryDescriptor makeStepPropertyHistoryDescriptor(
    uint8_t track,
    uint8_t step,
    sequencer::StepProperty property,
    const sequencer::SequencerHistoryPatternSnapshot& before,
    const sequencer::SequencerHistoryPatternSnapshot& after
) {
    const int32_t beforeValue = sequencerHistoryValueForProperty(before, step, property);
    const int32_t afterValue = sequencerHistoryValueForProperty(after, step, property);
    if (beforeValue == afterValue) {
        return sequencer::SequencerHistoryDescriptor{
            .kind = sequencer::SequencerHistoryActionKind::StepEdit,
            .trackIndex = track,
            .stepIndex = step,
            .property = property,
            .hasValue = false,
        };
    }

    return sequencer::SequencerHistoryDescriptor{
        .kind = sequencer::SequencerHistoryActionKind::StepPropertyEdit,
        .trackIndex = track,
        .stepIndex = step,
        .property = property,
        .hasValue = true,
        .beforeValue = beforeValue,
        .afterValue = afterValue,
    };
}

FLASHMEM sequencer::SequencerHistoryDescriptor makeStepStateHistoryDescriptor(
    uint8_t track,
    uint8_t step,
    const sequencer::SequencerHistoryPatternSnapshot& before,
    const sequencer::SequencerHistoryPatternSnapshot& after
) {
    const bool beforeEnabled = before.flat.enabledMask.test(step);
    const bool afterEnabled = after.flat.enabledMask.test(step);
    return sequencer::SequencerHistoryDescriptor{
        .kind = sequencer::SequencerHistoryActionKind::StepToggle,
        .trackIndex = track,
        .stepIndex = step,
        .property = sequencer::StepProperty::NOTE,
        .hasValue = beforeEnabled != afterEnabled,
        .beforeValue = beforeEnabled ? 1 : 0,
        .afterValue = afterEnabled ? 1 : 0,
    };
}

}  // namespace

FLASHMEM bool CoreState::recordSequencerPatternHistory(
    sequencer::SequencerHistoryPatternSnapshot before,
    sequencer::SequencerHistoryPatternSnapshot after,
    sequencer::SequencerHistoryDescriptor descriptor,
    sequencer::SequencerHistoryPatternStorage storage
) {
    const uint8_t activeTrack = sequencerTracks.activeTrackIndex();
    uint8_t targetTrack = activeTrack;
    if (descriptor.trackIndex == sequencer::SequencerHistoryDescriptor::INVALID_INDEX) {
        descriptor.trackIndex = activeTrack;
    } else {
        targetTrack = sequencer::SequencerTrackBankState::clampTrackIndex(descriptor.trackIndex);
        descriptor.trackIndex = targetTrack;
    }

    const bool recorded = storage == sequencer::SequencerHistoryPatternStorage::FlatOnly
        ? sequencerHistory.recordFlatPattern(
              targetTrack,
              std::move(before),
              std::move(after),
              descriptor
          )
        : sequencerHistory.recordPattern(
              targetTrack,
              std::move(before),
              std::move(after),
              descriptor
          );
    if (!recorded) {
        return false;
    }

    const bool synchronized = storage == sequencer::SequencerHistoryPatternStorage::FlatOnly
        ? sequencer::storeActiveTrackPreservingGraph(sequencerTracks, sequencer)
        : sequencer::storeActiveTrack(sequencerTracks, sequencer);
    if (!synchronized) {
        OC_LOG_ERROR("[CoreState] Failed to synchronize active sequencer graph after history");
    }
    markProjectMutated();
    refreshSharedTrackStateFromSequencer();
    return true;
}

FLASHMEM bool CoreState::recordSequencerPatternHistory(
    sequencer::SequencerHistoryPatternChangePtr change
) {
    if (!change) return false;

    const uint8_t activeTrack = sequencerTracks.activeTrackIndex();
    const uint8_t targetTrack =
        change->descriptor.trackIndex == sequencer::SequencerHistoryDescriptor::INVALID_INDEX
            ? activeTrack
            : sequencer::SequencerTrackBankState::clampTrackIndex(
                  change->descriptor.trackIndex
              );
    change->trackIndex = targetTrack;
    change->descriptor.trackIndex = targetTrack;
    const auto storage = change->storage;
    if (!sequencerHistory.recordPattern(std::move(change))) return false;

    const bool synchronized = storage == sequencer::SequencerHistoryPatternStorage::FlatOnly
        ? sequencer::storeActiveTrackPreservingGraph(sequencerTracks, sequencer)
        : sequencer::storeActiveTrack(sequencerTracks, sequencer);
    if (!synchronized) {
        OC_LOG_ERROR("[CoreState] Failed to synchronize active sequencer graph after history");
    }
    markProjectMutated();
    refreshSharedTrackStateFromSequencer();
    return true;
}

FLASHMEM bool CoreState::recordSequencerBankHistory(
    sequencer::SequencerHistoryTrackBankSnapshot before,
    sequencer::SequencerHistoryTrackBankSnapshot after,
    sequencer::SequencerHistoryDescriptor descriptor
) {
    if (!sequencerHistory.recordFullBank(
            std::move(before),
            std::move(after),
            descriptor
        )) {
        return false;
    }

    markSequencerProjectMutated_();
    refreshSharedTrackStateFromSequencer();
    return true;
}

FLASHMEM bool CoreState::recordSequencerBankHistory(
    sequencer::SequencerHistoryFullBankChangePtr change
) {
    if (!sequencerHistory.recordFullBank(std::move(change))) {
        return false;
    }

    markSequencerProjectMutated_();
    refreshSharedTrackStateFromSequencer();
    return true;
}

FLASHMEM bool CoreState::canRecordSequencerBankHistory(
    const sequencer::SequencerHistoryFullBankChange& change
) const {
    // installTrackBankState rejects the complete bank install while a Step Draft
    // is active, including content-only changes with unchanged topology. Keep
    // admission identical to that live-write guard so History can never publish
    // an `after` snapshot that was not installed.
    return !sequencer.stepContentDraft.active.get() &&
        sequencerHistory.canRecordFullBank(change);
}

FLASHMEM void CoreState::recordPreparedSequencerBankHistory(
    sequencer::SequencerHistoryFullBankChangePtr change
) {
    if (!change || !canRecordSequencerBankHistory(*change)) return;
    const uint16_t enabledMask = change->after.flat.enabledMask;
    const uint8_t activeTrack = change->after.flat.activeTrack;
    if (!publishPreparedSequencerTrackState(enabledMask, activeTrack)) return;
    sequencerHistory.recordPreparedFullBank(std::move(change));
    publishPreparedSequencerMutation();
}

FLASHMEM bool CoreState::recordSequencerStructureHistory(
    sequencer::SequencerHistoryTrackStructureChangePtr change
) {
    if (!sequencerHistory.recordStructure(std::move(change))) {
        return false;
    }

    markSequencerProjectMutated_();
    refreshSharedTrackStateFromSequencer();
    return true;
}

FLASHMEM bool CoreState::canRecordSequencerStructureHistory(
    const sequencer::SequencerHistoryTrackStructureChange& change
) const {
    return !sequencer.stepContentDraft.active.get() &&
        sequencerHistory.canRecordStructure(change);
}

FLASHMEM void CoreState::recordPreparedSequencerStructureHistory(
    sequencer::SequencerHistoryTrackStructureChangePtr change
) {
    if (!change || !canRecordSequencerStructureHistory(*change)) return;
    const uint16_t enabledMask = change->after.enabledMask;
    const uint8_t activeTrack = change->after.activeTrack;
    if (!publishPreparedSequencerTrackState(enabledMask, activeTrack)) return;
    sequencerHistory.recordPreparedStructure(std::move(change));
    publishPreparedSequencerMutation();
}

FLASHMEM void CoreState::publishPreparedSequencerMutation() {
    auto* coalescer = sequencerDomain_.mutationCoalescer.get();
    if (coalescer != nullptr) {
        // The prepared transaction already performed the coalescer action's
        // editor-to-bank synchronization. Cancel only this coalescer's queued
        // callbacks (including later entries in an active notification wave)
        // and consume an already-armed mark before publishing directly.
        coalescer->consumePendingChangesWithoutAction();
    }
    markProjectMutated();
}

FLASHMEM bool CoreState::beginOrContinueSequencerPatternHistoryCoalescing(
    uint8_t step,
    sequencer::StepProperty property,
    uint32_t nowMs,
    bool stateProperty
) {
    auto& pending = sequencerDomain_.coalescedPatternHistory;
    const uint8_t activeTrack = sequencerTracks.activeTrackIndex();

    if (pending.matchesStepProperty(
            activeTrack,
            step,
            property,
            stateProperty
        )) {
        pending.lastTouchedMs = nowMs;
        return true;
    }

    if (pending.pending) {
        commitSequencerPatternHistoryCoalescing();
    }

    sequencer::SequencerHistoryPatternSnapshot before;
    if (!sequencer::captureHistorySnapshot(sequencer, before)) {
        pending.clear();
        return false;
    }

    pending.clear();
    pending.pending = true;
    pending.kind = SequencerDomainState::CoalescedPatternHistory::Kind::StepProperty;
    pending.activeTrack = activeTrack;
    pending.step = step;
    pending.property = property;
    pending.stateProperty = stateProperty;
    pending.lastTouchedMs = nowMs;
    pending.before = std::move(before);
    return true;
}

FLASHMEM bool CoreState::beginOrContinueSequencerCcLaneEventHistoryCoalescing(
    uint8_t lane,
    uint8_t step,
    int32_t beforeValue,
    int32_t afterValue,
    const sequencer::SequencerCcLaneBank* afterBank,
    uint32_t nowMs
) {
    if (lane >= sequencer::SequencerCcLaneBank::MAX_LANES ||
        step >= sequencer::SequencerCcLaneBank::MAX_STEPS ||
        beforeValue < -1 || beforeValue > 127 ||
        afterValue < 0 || afterValue > 127 || afterBank == nullptr ||
        !afterBank->lanes[lane].occupied ||
        !afterBank->lanes[lane].activeMask.test(step) ||
        afterBank->lanes[lane].values[step] != afterValue) {
        return false;
    }

    auto& pending = sequencerDomain_.coalescedPatternHistory;
    const uint8_t activeTrack = sequencerTracks.activeTrackIndex();
    const auto captureAfter = [this, afterBank](
                                  sequencer::SequencerHistoryPatternChange& change
                              ) {
        if (!sequencer::captureHistorySnapshotUsingReservedGraph(
                sequencer,
                change.after
            ) ||
            !sequencer::captureSequencerCcLaneBankUsingReservedStorage(
                afterBank,
                change.after.ccLanes
            )) {
            return false;
        }
        change.after.ccLanesCaptured = true;
        return true;
    };

    if (pending.matchesCcLaneEvent(activeTrack, lane, step)) {
        auto* change = pending.preparedCcLaneChange.get();
        if (change == nullptr || !captureAfter(*change)) {
            if (change != nullptr) {
                (void)sequencer::applyHistorySnapshotToEditor(
                    sequencer,
                    change->before
                );
            }
            pending.clear();
            return false;
        }
        change->descriptor.afterValue = afterValue;
        const bool noChange = sequencer::sameMusicalHistorySnapshot(
            change->before,
            change->after
        );
        if (!noChange && !sequencerHistory.canRecordPattern(*change)) {
            (void)sequencer::applyHistorySnapshotToEditor(
                sequencer,
                change->before
            );
            pending.clear();
            return false;
        }
        pending.lastTouchedMs = nowMs;
        return true;
    }

    if (pending.pending) {
        commitSequencerPatternHistoryCoalescing();
    }

    auto change = core::app::makeExtmemUnique<
        sequencer::SequencerHistoryPatternChange
    >();
    if (!change) {
        pending.clear();
        return false;
    }
    change->trackIndex = activeTrack;
    change->storage = sequencer::SequencerHistoryPatternStorage::FullGraph;
    change->descriptor = {
        .kind = sequencer::SequencerHistoryActionKind::CcLaneEventEdit,
        .trackIndex = activeTrack,
        .laneIndex = lane,
        .stepIndex = step,
        .hasValue = true,
        .beforeValue = beforeValue,
        .afterValue = afterValue,
    };
    if (!sequencer::captureHistorySnapshot(sequencer, change->before) ||
        !captureAfter(*change) ||
        !sequencerHistory.canRecordPattern(*change)) {
        pending.clear();
        return false;
    }

    pending.clear();
    pending.pending = true;
    pending.kind = SequencerDomainState::CoalescedPatternHistory::Kind::CcLaneEvent;
    pending.activeTrack = activeTrack;
    pending.step = step;
    pending.lane = lane;
    pending.lastTouchedMs = nowMs;
    pending.preparedCcLaneChange = std::move(change);
    return true;
}

FLASHMEM bool CoreState::commitSequencerPatternHistoryCoalescing() {
    auto& pending = sequencerDomain_.coalescedPatternHistory;
    if (!pending.pending) {
        return false;
    }
    const auto abandonUnsafeHistory = [this](const char* reason) {
        OC_LOG_ERROR(
            "[CoreState] Coalesced Sequencer history unavailable ({}); "
            "clearing Project history boundary",
            reason
        );
        if (!clearProjectHistory()) {
            OC_LOG_ERROR("[CoreState] Failed to close Project history boundary");
        }
        return false;
    };

    const uint8_t targetTrack = pending.activeTrack;
    const uint8_t targetStep = pending.step;
    const auto targetProperty = pending.property;
    const bool targetStateProperty = pending.stateProperty;
    const auto targetKind = pending.kind;

    const bool ccLaneEvent = targetKind ==
        SequencerDomainState::CoalescedPatternHistory::Kind::CcLaneEvent;
    if (ccLaneEvent) {
        auto change = std::move(pending.preparedCcLaneChange);
        if (!change) {
            pending.clear();
            return abandonUnsafeHistory("prepared CC Lane entry missing");
        }
        if (sequencer::sameMusicalHistorySnapshot(
                change->before,
                change->after
            )) {
            pending.clear();
            return false;
        }
        if (!sequencerHistory.canRecordPattern(*change)) {
            const bool restored = sequencer::applyHistorySnapshotToTrack(
                sequencerTracks,
                sequencer,
                targetTrack,
                change->before
            );
            pending.clear();
            return restored
                ? false
                : abandonUnsafeHistory("CC Lane rollback failed");
        }

        pending.clear();
        sequencerHistory.recordPreparedPattern(std::move(change));
        markSequencerProjectMutated_();
        return true;
    }

    sequencer::SequencerHistoryPatternStorage storage =
        sequencer::SequencerHistoryPatternStorage::FullGraph;
    const uint32_t currentGraphRevision =
        targetTrack == sequencerTracks.activeTrackIndex()
            ? sequencer.pattern.graphRevision.get()
            : sequencerTracks.track(targetTrack).graphRevision.get();
    storage = pending.before.flat.graphRevision == currentGraphRevision
        ? sequencer::SequencerHistoryPatternStorage::FlatOnly
        : sequencer::SequencerHistoryPatternStorage::FullGraph;

    sequencer::SequencerHistoryPatternSnapshot after;
    if (storage == sequencer::SequencerHistoryPatternStorage::FlatOnly) {
        sequencer::captureFlatHistorySnapshot(sequencerTracks, sequencer, targetTrack, after);
    } else if (!sequencer::captureHistorySnapshot(
                   sequencerTracks,
                   sequencer,
                   targetTrack,
                   after
               )) {
        return abandonUnsafeHistory("snapshot capture failed");
    }

    auto change = core::app::makeExtmemUnique<
        sequencer::SequencerHistoryPatternChange
    >();
    if (!change) return abandonUnsafeHistory("entry allocation failed");

    change->trackIndex = targetTrack;
    change->storage = storage;
    change->before = std::move(pending.before);
    change->after = std::move(after);

    auto descriptor = targetStateProperty
        ? makeStepStateHistoryDescriptor(
              targetTrack,
              targetStep,
              change->before,
              change->after
          )
        : makeStepPropertyHistoryDescriptor(
              targetTrack,
              targetStep,
              targetProperty,
              change->before,
              change->after
          );
    change->descriptor = descriptor;

    if (!sequencerHistory.canRecordPattern(*change)) {
        const bool noChange = sequencer::sameMusicalHistorySnapshot(
            change->before,
            change->after
        );
        pending.before = std::move(change->before);
        if (noChange) {
            pending.clear();
            return false;
        }
        return abandonUnsafeHistory("retained history budget exceeded");
    }

    pending.clear();
    if (recordSequencerPatternHistory(std::move(change))) return true;
    return abandonUnsafeHistory("prepared entry commit failed");
}

FLASHMEM bool CoreState::updateSequencerPatternHistoryCoalescing(uint32_t nowMs) {
    const auto& pending = sequencerDomain_.coalescedPatternHistory;
    if (!pending.pending) {
        return false;
    }

    const uint32_t idleMs = pending.kind ==
            SequencerDomainState::CoalescedPatternHistory::Kind::CcLaneEvent
        ? SequencerDomainState::COALESCED_CC_LANE_HISTORY_IDLE_MS
        : SequencerDomainState::COALESCED_PATTERN_HISTORY_IDLE_MS;
    if (static_cast<uint32_t>(nowMs - pending.lastTouchedMs) < idleMs) {
        return false;
    }

    return commitSequencerPatternHistoryCoalescing();
}

bool CoreState::hasPendingSequencerPatternHistoryCoalescing() const {
    return sequencerDomain_.coalescedPatternHistory.pending;
}

}  // namespace core::state
