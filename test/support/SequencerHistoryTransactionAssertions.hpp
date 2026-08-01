#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>

#include "app/ExtmemAllocator.hpp"
#include "state/CoreState.hpp"
#include "state/sequencer/SequencerHistory.hpp"

namespace test_support::sequencer_transaction {

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
