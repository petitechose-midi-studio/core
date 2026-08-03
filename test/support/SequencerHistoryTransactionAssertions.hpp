#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <utility>

#include "app/ExtmemAllocator.hpp"
#include "state/CoreState.hpp"
#include "state/sequencer/SequencerHistory.hpp"
#include "state/sequencer/SequencerSnapshotOps.hpp"
#include "state/sequencer/SequencerStructureHistory.hpp"
#include "state/sequencer/SequencerTrackBankOps.hpp"

namespace test_support::sequencer_transaction {

/**
 * Test-only admission authority for direct History fixtures.
 *
 * Product code must enter History through an owner-specific prepared
 * transaction. These helpers keep low-level History behavior tests readable
 * without restoring the retired allocate-and-record compatibility surface.
 */
inline bool commitAdmittedPattern(
    core::state::sequencer::SequencerHistoryService& history,
    uint8_t trackIndex,
    core::state::sequencer::SequencerHistoryPatternSnapshot before,
    core::state::sequencer::SequencerHistoryPatternSnapshot after,
    core::state::sequencer::SequencerHistoryDescriptor descriptor = {},
    core::state::sequencer::SequencerHistoryPatternStorage storage =
        core::state::sequencer::SequencerHistoryPatternStorage::FullGraph
) {
    namespace seq = core::state::sequencer;
    auto change = core::app::makeExtmemUnique<seq::SequencerHistoryPatternChange>();
    if (!change) return false;

    const uint8_t targetTrack = seq::SequencerTrackBankState::clampTrackIndex(
        trackIndex
    );
    if (descriptor.trackIndex == seq::SequencerHistoryDescriptor::INVALID_INDEX) {
        descriptor.trackIndex = targetTrack;
    }
    change->trackIndex = targetTrack;
    change->storage = storage;
    change->descriptor = descriptor;
    change->before = std::move(before);
    change->after = std::move(after);
    if (!history.canRecordPattern(*change)) return false;

    history.recordPreparedPattern(std::move(change));
    return true;
}

inline bool commitAdmittedPattern(
    core::state::sequencer::SequencerHistoryService& history,
    core::state::sequencer::SequencerHistoryPatternSnapshot before,
    core::state::sequencer::SequencerHistoryPatternSnapshot after,
    core::state::sequencer::SequencerHistoryDescriptor descriptor = {},
    core::state::sequencer::SequencerHistoryPatternStorage storage =
        core::state::sequencer::SequencerHistoryPatternStorage::FullGraph
) {
    return commitAdmittedPattern(
        history,
        0U,
        std::move(before),
        std::move(after),
        descriptor,
        storage
    );
}

inline bool commitAdmittedFullBank(
    core::state::sequencer::SequencerHistoryService& history,
    core::state::sequencer::SequencerHistoryTrackBankSnapshot before,
    core::state::sequencer::SequencerHistoryTrackBankSnapshot after,
    core::state::sequencer::SequencerHistoryDescriptor descriptor = {}
) {
    namespace seq = core::state::sequencer;
    auto change = core::app::makeExtmemUnique<seq::SequencerHistoryFullBankChange>();
    if (!change) return false;

    change->descriptor = descriptor;
    change->before = std::move(before);
    change->after = std::move(after);
    if (!history.canRecordFullBank(*change)) return false;

    history.commitAdmittedFullBank(std::move(change));
    return true;
}

inline bool commitAdmittedStructure(
    core::state::sequencer::SequencerHistoryService& history,
    core::state::sequencer::SequencerHistoryTrackStructureChangePtr change
) {
    if (!change || !history.canRecordStructure(*change)) return false;
    history.commitAdmittedStructure(std::move(change));
    return true;
}

/** Reproduces the retired Core facade only inside integration tests. */
inline bool commitAdmittedPattern(
    core::state::CoreState& state,
    core::state::sequencer::SequencerHistoryPatternSnapshot before,
    core::state::sequencer::SequencerHistoryPatternSnapshot after,
    core::state::sequencer::SequencerHistoryDescriptor descriptor = {},
    core::state::sequencer::SequencerHistoryPatternStorage storage =
        core::state::sequencer::SequencerHistoryPatternStorage::FullGraph
) {
    namespace seq = core::state::sequencer;
    const uint8_t activeTrack = state.sequencerTracks.activeTrackIndex();
    const uint8_t targetTrack =
        descriptor.trackIndex == seq::SequencerHistoryDescriptor::INVALID_INDEX
            ? activeTrack
            : seq::SequencerTrackBankState::clampTrackIndex(descriptor.trackIndex);
    descriptor.trackIndex = targetTrack;

    if (!commitAdmittedPattern(
            state.sequencerHistory,
            targetTrack,
            std::move(before),
            std::move(after),
            descriptor,
            storage
        )) {
        return false;
    }

    const bool synchronized =
        storage == seq::SequencerHistoryPatternStorage::FlatOnly
            ? seq::storeActiveTrackPreservingGraph(
                  state.sequencerTracks,
                  state.sequencer
              )
            : seq::storeActiveTrack(state.sequencerTracks, state.sequencer);
    assert(synchronized);
    state.markProjectMutated();
    state.refreshSharedTrackStateFromSequencer();
    return true;
}

inline bool publishAdmittedPattern(
    core::state::CoreState& state,
    core::state::sequencer::SequencerHistoryPatternChangePtr change
) {
    if (!change || !state.sequencerHistory.canRecordPattern(*change)) return false;
    state.sequencerHistory.recordPreparedPattern(std::move(change));
    state.publishPreparedSequencerMutation();
    return true;
}

inline bool canPublishAdmittedFullBank(
    const core::state::CoreState& state,
    const core::state::sequencer::SequencerHistoryFullBankChange& change
) {
    return !state.sequencer.stepContentDraft.active.get() &&
           state.sequencerHistory.canRecordFullBank(change);
}

inline bool publishAdmittedFullBank(
    core::state::CoreState& state,
    core::state::sequencer::SequencerHistoryFullBankChangePtr change
) {
    if (!change || !canPublishAdmittedFullBank(state, *change)) return false;
    const uint16_t enabledMask = change->after.flat.enabledMask;
    const uint8_t activeTrack = change->after.flat.activeTrack;
    if (!state.publishPreparedSequencerTrackState(enabledMask, activeTrack)) {
        return false;
    }
    state.sequencerHistory.commitAdmittedFullBank(std::move(change));
    state.publishPreparedSequencerMutation();
    return true;
}

inline void mixFingerprintBytes(
    uint64_t& hash,
    const void* bytes,
    std::size_t size
) {
    constexpr uint64_t prime = 1099511628211ULL;
    const auto* cursor = static_cast<const uint8_t*>(bytes);
    for (std::size_t index = 0U; index < size; ++index) {
        hash ^= cursor[index];
        hash *= prime;
    }
}

template <typename T>
inline void mixFingerprintValue(uint64_t& hash, const T& value) {
    mixFingerprintBytes(hash, &value, sizeof(value));
}

/** Padding-independent fingerprint of the complete authored flat pattern. */
inline uint64_t flatPatternFingerprint(
    const core::state::sequencer::SequencerPatternState& pattern,
    bool includeRevisions = true
) {
    namespace seq = core::state::sequencer;
    seq::SequencerPatternSnapshot snapshot{};
    seq::captureSnapshot(pattern, snapshot);
    uint64_t hash = 1469598103934665603ULL;
    mixFingerprintValue(hash, snapshot.length);
    mixFingerprintValue(hash, snapshot.playStart);
    mixFingerprintValue(hash, snapshot.loopStart);
    mixFingerprintValue(hash, snapshot.loopEnd);
    mixFingerprintValue(hash, snapshot.stepsPerBeat);
    mixFingerprintValue(hash, snapshot.enabledMask.low);
    mixFingerprintValue(hash, snapshot.enabledMask.high);
    if (includeRevisions) {
        mixFingerprintValue(hash, snapshot.stepDataRevision);
        mixFingerprintValue(hash, snapshot.patternVariationRevision);
        mixFingerprintValue(hash, snapshot.patternScaleRevision);
        mixFingerprintValue(hash, snapshot.patternTimingRevision);
        mixFingerprintValue(hash, snapshot.graphRevision);
        const uint32_t ccLaneRevision = pattern.ccLaneRevision.get();
        mixFingerprintValue(hash, ccLaneRevision);
    }
    mixFingerprintValue(hash, snapshot.swingOffsetPercent);
    mixFingerprintValue(hash, snapshot.patternNudgePercent);
    mixFingerprintValue(hash, snapshot.effectiveSwingPercent);
    mixFingerprintValue(hash, snapshot.variationRanges.pitchSemitones);
    mixFingerprintValue(hash, snapshot.variationRanges.velocity);
    mixFingerprintValue(hash, snapshot.variationRanges.gatePercent);
    mixFingerprintValue(hash, snapshot.variationRanges.nudge);
    mixFingerprintValue(hash, snapshot.scalePolicy);
    mixFingerprintValue(hash, snapshot.scaleOverride.root);
    mixFingerprintValue(hash, snapshot.scaleOverride.type);
    mixFingerprintValue(hash, snapshot.scaleOverride.mode);
    mixFingerprintValue(hash, snapshot.pitchEditMode);
    mixFingerprintValue(hash, snapshot.effectiveScaleSettings.root);
    mixFingerprintValue(hash, snapshot.effectiveScaleSettings.type);
    mixFingerprintValue(hash, snapshot.effectiveScaleSettings.mode);
    mixFingerprintBytes(hash, snapshot.note.data(), sizeof(snapshot.note));
    mixFingerprintBytes(
        hash,
        snapshot.velocity.data(),
        sizeof(snapshot.velocity)
    );
    mixFingerprintBytes(hash, snapshot.gate.data(), sizeof(snapshot.gate));
    mixFingerprintBytes(hash, snapshot.nudge.data(), sizeof(snapshot.nudge));
    mixFingerprintBytes(
        hash,
        snapshot.probability.data(),
        sizeof(snapshot.probability)
    );
    return hash;
}

struct StateInvariant {
    const void* editorGraphOwner = nullptr;
    const void* editorCcOwner = nullptr;
    const void* bankGraphOwner = nullptr;
    const void* bankCcOwner = nullptr;
    uint32_t editorStepDataRevision = 0U;
    uint32_t editorPatternVariationRevision = 0U;
    uint32_t editorPatternScaleRevision = 0U;
    uint32_t editorPatternTimingRevision = 0U;
    uint32_t editorGraphRevision = 0U;
    uint32_t editorCcRevision = 0U;
    uint32_t bankStepDataRevision = 0U;
    uint32_t bankPatternVariationRevision = 0U;
    uint32_t bankPatternScaleRevision = 0U;
    uint32_t bankPatternTimingRevision = 0U;
    uint32_t bankGraphRevision = 0U;
    uint32_t bankCcRevision = 0U;
    uint32_t modifiedCounter = 0U;
    bool dirty = false;
    bool sessionSavePending = false;
    uint32_t sessionSaveTimestampMs = 0U;
    uint8_t sequencerUndoCount = 0U;
    uint8_t sequencerRedoCount = 0U;
    uint8_t projectUndoCount = 0U;
    uint8_t projectRedoCount = 0U;
    uintptr_t sequencerUndoIdentity = 0U;
    uintptr_t sequencerRedoIdentity = 0U;
    std::size_t retainedBytes = 0U;
};

inline StateInvariant captureStateInvariant(const core::state::CoreState& state) {
    const auto& editor = state.sequencer.pattern;
    const auto& bank = state.sequencerTracks.track(
        state.sequencerTracks.activeTrackIndex()
    );
    return {
        .editorGraphOwner = editor.graph.get(),
        .editorCcOwner = editor.ccLanes.get(),
        .bankGraphOwner = bank.graph.get(),
        .bankCcOwner = bank.ccLanes.get(),
        .editorStepDataRevision = editor.stepDataRevision.get(),
        .editorPatternVariationRevision = editor.patternVariationRevision.get(),
        .editorPatternScaleRevision = editor.patternScaleRevision.get(),
        .editorPatternTimingRevision = editor.patternTimingRevision.get(),
        .editorGraphRevision = editor.graphRevision.get(),
        .editorCcRevision = editor.ccLaneRevision.get(),
        .bankStepDataRevision = bank.stepDataRevision.get(),
        .bankPatternVariationRevision = bank.patternVariationRevision.get(),
        .bankPatternScaleRevision = bank.patternScaleRevision.get(),
        .bankPatternTimingRevision = bank.patternTimingRevision.get(),
        .bankGraphRevision = bank.graphRevision.get(),
        .bankCcRevision = bank.ccLaneRevision.get(),
        .modifiedCounter = state.project.metadata.modifiedCounter,
        .dirty = state.project.metadata.dirty,
        .sessionSavePending = state.hasPendingProjectSessionSave(),
        .sessionSaveTimestampMs = state.projectSessionSaveTimestampMs(),
        .sequencerUndoCount = state.sequencerHistory.undoCount(),
        .sequencerRedoCount = state.sequencerHistory.redoCount(),
        .projectUndoCount = state.projectHistory.undoCount(),
        .projectRedoCount = state.projectHistory.redoCount(),
        .sequencerUndoIdentity = state.sequencerHistory.projectHistoryUndoIdentity(),
        .sequencerRedoIdentity = state.sequencerHistory.projectHistoryRedoIdentity(),
        .retainedBytes = state.sequencerHistory.retainedBytes(),
    };
}

inline void assertStateInvariant(
    const core::state::CoreState& state,
    const StateInvariant& expected
) {
    const auto actual = captureStateInvariant(state);
    assert(actual.editorGraphOwner == expected.editorGraphOwner);
    assert(actual.editorCcOwner == expected.editorCcOwner);
    assert(actual.bankGraphOwner == expected.bankGraphOwner);
    assert(actual.bankCcOwner == expected.bankCcOwner);
    assert(actual.editorStepDataRevision == expected.editorStepDataRevision);
    assert(
        actual.editorPatternVariationRevision ==
        expected.editorPatternVariationRevision
    );
    assert(actual.editorPatternScaleRevision == expected.editorPatternScaleRevision);
    assert(actual.editorPatternTimingRevision == expected.editorPatternTimingRevision);
    assert(actual.editorGraphRevision == expected.editorGraphRevision);
    assert(actual.editorCcRevision == expected.editorCcRevision);
    assert(actual.bankStepDataRevision == expected.bankStepDataRevision);
    assert(
        actual.bankPatternVariationRevision ==
        expected.bankPatternVariationRevision
    );
    assert(actual.bankPatternScaleRevision == expected.bankPatternScaleRevision);
    assert(actual.bankPatternTimingRevision == expected.bankPatternTimingRevision);
    assert(actual.bankGraphRevision == expected.bankGraphRevision);
    assert(actual.bankCcRevision == expected.bankCcRevision);
    assert(actual.modifiedCounter == expected.modifiedCounter);
    assert(actual.dirty == expected.dirty);
    assert(actual.sessionSavePending == expected.sessionSavePending);
    assert(actual.sessionSaveTimestampMs == expected.sessionSaveTimestampMs);
    assert(actual.sequencerUndoCount == expected.sequencerUndoCount);
    assert(actual.sequencerRedoCount == expected.sequencerRedoCount);
    assert(actual.projectUndoCount == expected.projectUndoCount);
    assert(actual.projectRedoCount == expected.projectRedoCount);
    assert(actual.sequencerUndoIdentity == expected.sequencerUndoIdentity);
    assert(actual.sequencerRedoIdentity == expected.sequencerRedoIdentity);
    assert(actual.retainedBytes == expected.retainedBytes);
}

inline void captureMusicalSnapshot(
    const core::state::CoreState& state,
    core::state::sequencer::SequencerHistoryPatternSnapshot& out
) {
    assert(core::state::sequencer::captureHistorySnapshot(state.sequencer, out));
}

inline void assertMusicalSnapshot(
    const core::state::CoreState& state,
    const core::state::sequencer::SequencerHistoryPatternSnapshot& expected
) {
    core::state::sequencer::SequencerHistoryPatternSnapshot actual;
    captureMusicalSnapshot(state, actual);
    assert(core::state::sequencer::sameMusicalHistorySnapshot(actual, expected));
}

inline void assertSingleCommittedPublication(
    const core::state::CoreState& state,
    const StateInvariant& before,
    bool projectMutationExpected
) {
    const auto after = captureStateInvariant(state);
    assert(after.editorGraphOwner == before.editorGraphOwner);
    assert(after.editorCcOwner == before.editorCcOwner);
    assert(after.bankGraphOwner == before.bankGraphOwner);
    assert(after.bankCcOwner == before.bankCcOwner);
    assert(after.editorGraphRevision == before.editorGraphRevision);
    assert(after.editorCcRevision == before.editorCcRevision);
    assert(after.bankGraphRevision == before.bankGraphRevision);
    assert(after.bankCcRevision == before.bankCcRevision);
    assert(after.sequencerUndoCount == before.sequencerUndoCount + 1U);
    assert(after.sequencerRedoCount == 0U);
    assert(after.projectUndoCount == before.projectUndoCount + 1U);
    assert(after.projectRedoCount == 0U);
    assert(after.sequencerUndoIdentity != 0U);
    assert(after.sequencerUndoIdentity != before.sequencerUndoIdentity);
    assert(after.retainedBytes > before.retainedBytes);
    if (projectMutationExpected) {
        assert(after.modifiedCounter == before.modifiedCounter + 1U);
        assert(after.dirty);
        assert(after.sessionSavePending);
    } else {
        assert(after.modifiedCounter == before.modifiedCounter);
        assert(after.dirty == before.dirty);
        assert(after.sessionSavePending == before.sessionSavePending);
        assert(after.sessionSaveTimestampMs == before.sessionSaveTimestampMs);
    }
}

#if defined(MS_CORE_ENABLE_EXTMEM_FAILURE_INJECTION)
inline void assertFailureConsumed(std::size_t ordinal) {
    assert(core::app::testing::extmemAllocationAttempt == ordinal);
    assert(core::app::testing::extmemAllocationFailureOrdinal == 0U);
}

inline void assertMaxPlusOneStillArmed(std::size_t expectedAttempts) {
    assert(core::app::testing::extmemAllocationAttempt == expectedAttempts);
    assert(
        core::app::testing::extmemAllocationFailureOrdinal ==
        expectedAttempts + 1U
    );
}

inline void assertFailureInjectionReset() {
    assert(core::app::testing::extmemAllocationAttempt == 0U);
    assert(core::app::testing::extmemAllocationFailureOrdinal == 0U);
}
#endif

}  // namespace test_support::sequencer_transaction
