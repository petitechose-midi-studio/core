#ifdef NDEBUG
#undef NDEBUG
#endif

#include <array>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <memory>
#include <utility>

#include <oc/state/NotificationQueue.hpp>

#include "app/ExtmemAllocator.hpp"
#include "state/sequencer/SequencerSnapshotOps.hpp"
#include "state/sequencer/SequencerTrackBankOps.hpp"

#if !defined(MS_CORE_ENABLE_EXTMEM_FAILURE_INJECTION)
#error "This test requires native EXTMEM failure injection"
#endif

namespace {

namespace seq = core::state::sequencer;

using Graph = oc::note::sequencer::StepSequencerGraph;
using SnapshotOwner = std::unique_ptr<seq::SequencerPatternSnapshot>;

struct OwnerSet {
    const Graph* graph = nullptr;
    const seq::SequencerCcLaneBank* ccLanes = nullptr;
};

void seedFlat(seq::SequencerPatternState& pattern, uint8_t tag) {
    const uint8_t length = static_cast<uint8_t>(16U + tag);
    pattern.length.set(length);
    pattern.playStart = static_cast<uint8_t>(tag & 0x01U);
    pattern.loopStart = pattern.playStart;
    pattern.loopEnd = length;
    pattern.stepsPerBeat.set(static_cast<uint8_t>(3U + tag));

    auto enabled = oc::note::sequencer::StepBitMask128{};
    enabled.setBit(0U, true);
    enabled.setBit(tag, true);
    pattern.enabledMask.set(enabled);
    pattern.note[0] = static_cast<uint8_t>(48U + tag);
    pattern.velocity[1] = static_cast<uint8_t>(80U + tag);
    pattern.gate[2] = static_cast<uint16_t>(90U + tag);
    pattern.nudge[3] = static_cast<int8_t>(tag);
    pattern.probability[4] = static_cast<uint8_t>(70U + tag);
    pattern.swingOffsetPercent.set(static_cast<int8_t>(tag));
    pattern.patternNudgePercent.set(static_cast<int8_t>(-tag));
    pattern.stepDataRevision.set(100U + tag);
    pattern.patternVariationRevision.set(200U + tag);
    pattern.patternScaleRevision.set(300U + tag);
    pattern.patternTimingRevision.set(400U + tag);
    pattern.graphRevision.set(500U + tag);
    pattern.ccLaneRevision.set(600U + tag);
}

void installDistinctOwners(seq::SequencerPatternState& pattern) {
    pattern.graph = core::app::makeExtmemUnique<Graph>();
    pattern.ccLanes = core::app::makeExtmemUnique<seq::SequencerCcLaneBank>();
    assert(pattern.graph);
    assert(pattern.ccLanes);
    pattern.graph->enabled = true;
}

SnapshotOwner captureFlat(const seq::SequencerPatternState& pattern) {
    auto snapshot = std::make_unique<seq::SequencerPatternSnapshot>();
    assert(snapshot);
    seq::captureSnapshot(pattern, *snapshot);
    return snapshot;
}

SnapshotOwner makeFinalFlat(
    const seq::SequencerPatternSnapshot& source,
    uint8_t tag
) {
    auto snapshot = std::make_unique<seq::SequencerPatternSnapshot>(source);
    assert(snapshot);
    snapshot->note[0] = static_cast<uint8_t>(60U + tag);
    snapshot->velocity[1] = static_cast<uint8_t>(90U + tag);
    snapshot->gate[2] = static_cast<uint16_t>(110U + tag);
    snapshot->nudge[3] = static_cast<int8_t>(-tag);
    snapshot->probability[4] = static_cast<uint8_t>(80U + tag);
    snapshot->stepDataRevision += static_cast<uint32_t>(10U + tag);
    snapshot->patternVariationRevision += static_cast<uint32_t>(20U + tag);
    snapshot->patternScaleRevision += static_cast<uint32_t>(30U + tag);
    snapshot->patternTimingRevision += static_cast<uint32_t>(40U + tag);
    snapshot->graphRevision += static_cast<uint32_t>(50U + tag);
    return snapshot;
}

seq::SequencerTrackFlatSnapshotView flatView(
    const SnapshotOwner& snapshot,
    uint32_t ccLaneRevision
) {
    return {
        .snapshot = snapshot.get(),
        .ccLaneRevision = ccLaneRevision,
    };
}

OwnerSet owners(const seq::SequencerPatternState& pattern) {
    return {
        .graph = pattern.graph.get(),
        .ccLanes = pattern.ccLanes.get(),
    };
}

void seedTransientTrackState(seq::SequencerState& active) {
    active.stepEdit.stepIndex.set(7U);
    active.stepEdit.focusedRow.set(3U);
    active.stepEdit.localVariationEditActive.set(true);

    active.contextSelector.visible = true;
    active.contextSelector.previewFocus = core::state::StructureNavigationFocus::TRACK;

    active.ccLaneUi.overlayVisible.set(true);
    active.ccLaneUi.mode = seq::SequencerCcLaneUiMode::LANE_GRID;
    active.ccLaneUi.selectorIndex = 2U;
    active.ccLaneUi.draftDirty = true;

    active.stepPropertyInlineSelector.selecting.set(true);
    active.stepPropertyInlineSelector.macroLocalVariationEditActive.set(true);
    active.stepPropertyInlineSelector.selectedIndex.set(4);
    active.stepPropertyInlineSelector.snapshotValid = true;
    active.stepPropertyInlineSelector.suppressOpeningRelease = true;

    active.stepInlineFeedback.show(5U, seq::StepProperty::VELOCITY, 100U);
    active.patternQuickControls.selecting.set(true);
    active.patternQuickControls.feedbackVisible.set(true);
    active.patternQuickControls.focusedItem.set(seq::PatternQuickControlItem::OFFSET);
    active.patternQuickControls.offsetSteps.set(3);
    active.patternQuickControls.hideAtMs = 900U;

    active.contentView.kind.set(seq::SequencerContentViewKind::MICRO_SEQUENCE);
    active.contentView.parentStep.set(5U);
    active.contentView.ownerNodeId.set(7U);
    active.contentView.sequenceId.set(9U);
    active.contentView.length.set(4U);
    active.contentView.depth.set(1U);
    active.contentView.stackDepth = 1U;

    active.stepContentDraft.kind.set(seq::SequencerStepContentDraftKind::CHORD);
    active.stepContentDraft.exitPromptVisible.set(true);
    active.stepContentDraft.exitChoice.set(seq::SequencerStepContentDraftExitChoice::DISCARD);
    active.stepContentDraft.pristineGraphRevision = 77U;
    active.stepContentDraft.ownerStep = 5U;
    active.stepContentDraft.failure = seq::SequencerStepContentDraftFailure::HISTORY_UNAVAILABLE;
    active.stepContentDraft.blockedTransition =
        seq::SequencerStepContentDraftBlockedTransition::HISTORY;
    active.stepContentDraft.scratch =
        core::app::makeExtmemUnique<seq::SequencerPatternState>();
    assert(active.stepContentDraft.scratch);
}

void assertTransientTrackStateReset(
    const seq::SequencerState& active,
    const seq::SequencerPatternState* expectedDraftScratch
) {
    assert(active.stepEdit.stepIndex.get() == 0U);
    assert(active.stepEdit.focusedRow.get() == 0U);
    assert(!active.stepEdit.localVariationEditActive.get());

    assert(!active.contextSelector.visible);
    assert(active.contextSelector.previewFocus ==
           core::state::StructureNavigationFocus::PAGE);

    assert(!active.ccLaneUi.overlayVisible.get());
    assert(active.ccLaneUi.mode == seq::SequencerCcLaneUiMode::CLOSED);
    assert(active.ccLaneUi.selectorIndex == 0U);
    assert(!active.ccLaneUi.draftDirty);

    assert(!active.stepPropertyInlineSelector.selecting.get());
    assert(!active.stepPropertyInlineSelector.macroLocalVariationEditActive.get());
    assert(active.stepPropertyInlineSelector.selectedIndex.get() == 0);
    assert(!active.stepPropertyInlineSelector.snapshotValid);
    assert(!active.stepPropertyInlineSelector.suppressOpeningRelease);

    assert(!active.stepInlineFeedback.visible.get());
    assert(!active.stepInlineFeedback.touchedMask.get().any());
    assert(active.stepInlineFeedback.property.get() == seq::StepProperty::NOTE);
    assert(!active.patternQuickControls.selecting.get());
    assert(!active.patternQuickControls.feedbackVisible.get());
    assert(active.patternQuickControls.focusedItem.get() ==
           seq::PatternQuickControlItem::LENGTH);
    assert(active.patternQuickControls.offsetSteps.get() == 0);
    assert(active.patternQuickControls.hideAtMs == 0U);

    assert(active.contentView.kind.get() == seq::SequencerContentViewKind::ROOT);
    assert(active.contentView.parentStep.get() == 0U);
    assert(active.contentView.stackDepth == 0U);
    assert(active.contentView.depth.get() == 0U);

    assert(!active.stepContentDraft.active.get());
    assert(active.stepContentDraft.kind.get() == seq::SequencerStepContentDraftKind::NONE);
    assert(!active.stepContentDraft.exitPromptVisible.get());
    assert(active.stepContentDraft.exitChoice.get() ==
           seq::SequencerStepContentDraftExitChoice::SAVE);
    assert(active.stepContentDraft.pristineGraphRevision == 0U);
    assert(active.stepContentDraft.ownerStep == 0U);
    assert(active.stepContentDraft.failure == seq::SequencerStepContentDraftFailure::NONE);
    assert(active.stepContentDraft.blockedTransition ==
           seq::SequencerStepContentDraftBlockedTransition::NONE);
    assert(active.stepContentDraft.scratch.get() == expectedDraftScratch);
}

void test_canonical_track_pattern_resolves_editor_and_bank_authority() {
    seq::SequencerState active;
    seq::SequencerTrackBankState bank;
    bank.syncSharedTrackState(0x0006U, 1U);

    active.pattern.note[0] = 61U;
    bank.track(1U).note[0] = 62U;
    bank.track(2U).note[0] = 63U;
    bank.track(seq::SequencerTrackBankState::TRACK_COUNT - 1U).note[0] = 64U;

    assert(&seq::canonicalTrackPattern(bank, active, 1U) == &active.pattern);
    assert(seq::canonicalTrackPattern(bank, active, 1U).note[0] == 61U);
    assert(&seq::canonicalTrackPattern(bank, active, 2U) == &bank.track(2U));
    assert(seq::canonicalTrackPattern(bank, active, 2U).note[0] == 63U);
    assert(seq::canonicalTrackPattern(
        bank,
        active,
        seq::SequencerTrackBankState::TRACK_COUNT
    ).note[0] == 64U);

    std::cout << "[PASS] canonical Track reads resolve editor and bank authority\n";
}

void test_prepared_rotation_preserves_payloads_without_publication_or_allocation() {
    seq::SequencerState active;
    seq::SequencerTrackBankState bank;
    bank.syncSharedTrackState(0x0007U, 0U);

    seedFlat(active.pattern, 1U);
    seedFlat(bank.track(0U), 2U);
    seedFlat(bank.track(1U), 3U);
    seedFlat(bank.track(2U), 4U);
    installDistinctOwners(active.pattern);
    installDistinctOwners(bank.track(0U));
    installDistinctOwners(bank.track(1U));
    installDistinctOwners(bank.track(2U));
    seedTransientTrackState(active);

    const uint8_t focusedBefore = 13U;
    const uint8_t pageBefore = 1U;
    active.focusedStep.set(focusedBefore);
    active.page.set(pageBefore);

    const auto expectedOutgoing = captureFlat(active.pattern);
    const auto expectedIncoming = captureFlat(bank.track(1U));
    const auto finalOutgoing = makeFinalFlat(*expectedOutgoing, 5U);
    const auto finalIncoming = makeFinalFlat(*expectedIncoming, 6U);
    const uint32_t expectedIncomingCcRevision = bank.track(1U).ccLaneRevision.get();
    const uint32_t finalOutgoingCcRevision = active.pattern.ccLaneRevision.get() + 50U;
    const uint32_t finalIncomingCcRevision = expectedIncomingCcRevision + 60U;

    const auto editorBefore = owners(active.pattern);
    const auto outgoingScratchBefore = owners(bank.track(0U));
    const auto incomingBefore = owners(bank.track(1U));
    const auto draftScratchBefore = active.stepContentDraft.scratch.get();
    std::array<OwnerSet, seq::SequencerTrackBankState::TRACK_COUNT> bankOwnersBefore{};
    for (uint8_t track = 0U; track < seq::SequencerTrackBankState::TRACK_COUNT; ++track) {
        bankOwnersBefore[track] = owners(bank.track(track));
    }

    int enabledNotifications = 0;
    int activeNotifications = 0;
    int flatNotifications = 0;
    const Graph* graphObservedByFlatCallback = nullptr;
    bool transientsResetWhenObserved = false;
    auto enabledSubscription = bank.enabledMaskSignal().subscribe(
        [&](const uint16_t&) { ++enabledNotifications; }
    );
    auto activeSubscription = bank.activeTrackSignal().subscribe(
        [&](const uint8_t&) { ++activeNotifications; }
    );
    auto flatSubscription = active.pattern.stepDataRevision.subscribe(
        [&](const uint32_t&) {
            ++flatNotifications;
            graphObservedByFlatCallback = active.pattern.graph.get();
            transientsResetWhenObserved = !active.stepEdit.localVariationEditActive.get() &&
                                          !active.contextSelector.visible &&
                                          active.contentView.kind.get() ==
                                              seq::SequencerContentViewKind::ROOT;
        }
    );
    assert(enabledSubscription);
    assert(activeSubscription);
    assert(flatSubscription);

    auto& queue = oc::state::NotificationQueue::instance();
    queue.flush();
    queue.resetOverflowCount();

    seq::SequencerPreparedActiveTrackRotation prepared{};
    {
        core::app::testing::ScopedExtmemAllocationFailure failure(1U);
        assert(seq::prepareActiveTrackOwnerRotation(
            bank,
            active,
            1U,
            flatView(expectedOutgoing, active.pattern.ccLaneRevision.get()),
            flatView(expectedIncoming, expectedIncomingCcRevision),
            flatView(finalOutgoing, finalOutgoingCcRevision),
            flatView(finalIncoming, finalIncomingCcRevision),
            seq::SequencerActiveTrackIncomingOwnerPolicy::Preserve,
            prepared
        ));
        assert(core::app::testing::extmemAllocationAttempt == 0U);
        assert(seq::preparedActiveTrackOwnerRotationMatches(bank, active, prepared));

        seq::rotateActiveTrackOwnersNoPublish(bank, active, prepared);
        assert(core::app::testing::extmemAllocationAttempt == 0U);
        assert(core::app::testing::extmemAllocationFailureOrdinal == 1U);
    }

    assert(bank.currentEnabledMask() == 0x0007U);
    assert(bank.activeTrackIndex() == 0U);
    assert(enabledNotifications == 0);
    assert(activeNotifications == 0);
    assert(flatNotifications == 0);
    assert(active.focusedStep.get() == focusedBefore);
    assert(active.page.get() == pageBefore);

    assert(owners(bank.track(0U)).graph == editorBefore.graph);
    assert(owners(bank.track(0U)).ccLanes == editorBefore.ccLanes);
    assert(owners(active.pattern).graph == incomingBefore.graph);
    assert(owners(active.pattern).ccLanes == incomingBefore.ccLanes);
    assert(owners(bank.track(1U)).graph == outgoingScratchBefore.graph);
    assert(owners(bank.track(1U)).ccLanes == outgoingScratchBefore.ccLanes);
    for (uint8_t track = 2U; track < seq::SequencerTrackBankState::TRACK_COUNT; ++track) {
        assert(owners(bank.track(track)).graph == bankOwnersBefore[track].graph);
        assert(owners(bank.track(track)).ccLanes == bankOwnersBefore[track].ccLanes);
    }

    assert(seq::sequencerPatternMatchesFlatSnapshot(
        bank.track(0U),
        flatView(finalOutgoing, finalOutgoingCcRevision)
    ));
    assert(seq::sequencerPatternMatchesFlatSnapshot(
        active.pattern,
        flatView(finalIncoming, finalIncomingCcRevision)
    ));
    assert(seq::sequencerPatternMatchesFlatSnapshot(
        bank.track(1U),
        flatView(expectedIncoming, expectedIncomingCcRevision)
    ));
    assertTransientTrackStateReset(active, draftScratchBefore);

    assert(queue.pendingCount() == 1U);
    assert(!queue.hasOverflowed());
    queue.flush();
    assert(flatNotifications == 1);
    assert(graphObservedByFlatCallback == incomingBefore.graph);
    assert(transientsResetWhenObserved);
    assert(enabledNotifications == 0);
    assert(activeNotifications == 0);

    std::cout << "[PASS] prepared owner rotation is exact, deferred and allocation-free\n";
}

void test_prepared_rotation_can_reset_incoming_payload_without_allocation() {
    seq::SequencerState active;
    seq::SequencerTrackBankState bank;
    bank.syncSharedTrackState(0x0001U, 0U);
    seedFlat(active.pattern, 1U);
    seedFlat(bank.track(0U), 2U);
    seedFlat(bank.track(1U), 3U);
    installDistinctOwners(active.pattern);
    installDistinctOwners(bank.track(0U));
    installDistinctOwners(bank.track(1U));

    const auto expectedOutgoing = captureFlat(active.pattern);
    const auto expectedIncoming = captureFlat(bank.track(1U));
    const auto finalOutgoing = makeFinalFlat(*expectedOutgoing, 7U);
    auto canonicalPattern = std::make_unique<seq::SequencerPatternState>();
    assert(canonicalPattern);
    const auto finalIncoming = captureFlat(*canonicalPattern);

    const auto editorBefore = owners(active.pattern);
    const auto outgoingScratchBefore = owners(bank.track(0U));
    const auto incomingBefore = owners(bank.track(1U));
    seq::SequencerPreparedActiveTrackRotation prepared{};
    {
        core::app::testing::ScopedExtmemAllocationFailure failure(1U);
        assert(seq::prepareActiveTrackOwnerRotation(
            bank,
            active,
            1U,
            flatView(expectedOutgoing, active.pattern.ccLaneRevision.get()),
            flatView(expectedIncoming, bank.track(1U).ccLaneRevision.get()),
            flatView(finalOutgoing, active.pattern.ccLaneRevision.get() + 70U),
            flatView(finalIncoming, canonicalPattern->ccLaneRevision.get()),
            seq::SequencerActiveTrackIncomingOwnerPolicy::Reset,
            prepared
        ));
        seq::rotateActiveTrackOwnersNoPublish(bank, active, prepared);
        assert(core::app::testing::extmemAllocationAttempt == 0U);
        assert(core::app::testing::extmemAllocationFailureOrdinal == 1U);
    }

    assert(owners(bank.track(0U)).graph == editorBefore.graph);
    assert(owners(bank.track(0U)).ccLanes == editorBefore.ccLanes);
    assert(owners(bank.track(1U)).graph == outgoingScratchBefore.graph);
    assert(owners(bank.track(1U)).ccLanes == outgoingScratchBefore.ccLanes);
    assert(active.pattern.graph == nullptr);
    assert(active.pattern.ccLanes == nullptr);
    assert(incomingBefore.graph != nullptr);
    assert(incomingBefore.ccLanes != nullptr);
    assert(bank.activeTrackIndex() == 0U);
    assert(bank.currentEnabledMask() == 0x0001U);
    assert(seq::sequencerPatternMatchesFlatSnapshot(
        active.pattern,
        flatView(finalIncoming, canonicalPattern->ccLaneRevision.get())
    ));

    std::cout << "[PASS] reset policy discards the incoming payload in the no-fail tail\n";
}

void test_invalid_and_stale_preflight_are_side_effect_free() {
    seq::SequencerState active;
    seq::SequencerTrackBankState bank;
    bank.syncSharedTrackState(0x0003U, 0U);
    seedFlat(active.pattern, 1U);
    seedFlat(bank.track(0U), 2U);
    seedFlat(bank.track(1U), 3U);
    installDistinctOwners(active.pattern);
    installDistinctOwners(bank.track(0U));
    installDistinctOwners(bank.track(1U));

    const auto expectedOutgoing = captureFlat(active.pattern);
    const auto expectedIncoming = captureFlat(bank.track(1U));
    const auto finalOutgoing = makeFinalFlat(*expectedOutgoing, 4U);
    const auto finalIncoming = makeFinalFlat(*expectedIncoming, 5U);
    const auto outgoingView = flatView(expectedOutgoing, active.pattern.ccLaneRevision.get());
    const auto incomingView = flatView(expectedIncoming, bank.track(1U).ccLaneRevision.get());
    const auto editorOwner = owners(active.pattern);
    const auto incomingOwner = owners(bank.track(1U));

    auto nonCanonical = std::make_unique<seq::SequencerPatternSnapshot>(
        *expectedOutgoing
    );
    assert(nonCanonical);
    nonCanonical->variationRanges.pitchSemitones = 0xFFU;
    assert(!seq::sequencerPatternMatchesFlatSnapshot(
        active.pattern,
        flatView(nonCanonical, active.pattern.ccLaneRevision.get())
    ));
    *nonCanonical = *expectedOutgoing;
    nonCanonical->scaleOverride.root = 12U;
    assert(!seq::sequencerPatternMatchesFlatSnapshot(
        active.pattern,
        flatView(nonCanonical, active.pattern.ccLaneRevision.get())
    ));

    seq::SequencerPreparedActiveTrackRotation invalid{};
    assert(!seq::prepareActiveTrackOwnerRotation(
        bank,
        active,
        seq::SequencerTrackBankState::TRACK_COUNT,
        outgoingView,
        incomingView,
        flatView(finalOutgoing, 700U),
        flatView(finalIncoming, 800U),
        seq::SequencerActiveTrackIncomingOwnerPolicy::Preserve,
        invalid
    ));
    assert(invalid.outgoingTrack == seq::SequencerTrackBankState::TRACK_COUNT);
    assert(seq::sequencerPatternMatchesFlatSnapshot(active.pattern, outgoingView));
    assert(seq::sequencerPatternMatchesFlatSnapshot(bank.track(1U), incomingView));
    assert(owners(active.pattern).graph == editorOwner.graph);
    assert(owners(active.pattern).ccLanes == editorOwner.ccLanes);

    seq::SequencerPreparedActiveTrackRotation prepared{};
    assert(seq::prepareActiveTrackOwnerRotation(
        bank,
        active,
        1U,
        outgoingView,
        incomingView,
        flatView(finalOutgoing, 700U),
        flatView(finalIncoming, 800U),
        seq::SequencerActiveTrackIncomingOwnerPolicy::Preserve,
        prepared
    ));

    active.pattern.note[0] = static_cast<uint8_t>(active.pattern.note[0] + 1U);
    const uint8_t staleNote = active.pattern.note[0];
    assert(!seq::preparedActiveTrackOwnerRotationMatches(bank, active, prepared));
    assert(active.pattern.note[0] == staleNote);
    assert(owners(active.pattern).graph == editorOwner.graph);
    assert(owners(bank.track(1U)).graph == incomingOwner.graph);
    active.pattern.note[0] = expectedOutgoing->note[0];

    std::swap(active.pattern.graph, bank.track(1U).graph);
    const auto swappedEditorGraph = active.pattern.graph.get();
    const auto swappedIncomingGraph = bank.track(1U).graph.get();
    assert(!seq::preparedActiveTrackOwnerRotationMatches(bank, active, prepared));
    assert(active.pattern.graph.get() == swappedEditorGraph);
    assert(bank.track(1U).graph.get() == swappedIncomingGraph);
    std::swap(active.pattern.graph, bank.track(1U).graph);

    active.stepContentDraft.active.set(true);
    const uint32_t draftRevision = active.stepContentDraft.revision.get();
    assert(!seq::preparedActiveTrackOwnerRotationMatches(bank, active, prepared));
    assert(active.stepContentDraft.active.get());
    assert(active.stepContentDraft.revision.get() == draftRevision);
    assert(active.stepContentDraft.failure == seq::SequencerStepContentDraftFailure::NONE);
    assert(active.stepContentDraft.blockedTransition ==
           seq::SequencerStepContentDraftBlockedTransition::NONE);

    assert(bank.currentEnabledMask() == 0x0003U);
    assert(bank.activeTrackIndex() == 0U);
    std::cout << "[PASS] invalid and stale preflight reject without live mutation\n";
}

}  // namespace

int main() {
    test_canonical_track_pattern_resolves_editor_and_bank_authority();
    test_prepared_rotation_preserves_payloads_without_publication_or_allocation();
    test_prepared_rotation_can_reset_incoming_payload_without_allocation();
    test_invalid_and_stale_preflight_are_side_effect_free();
    std::cout << "All SequencerTrackBankOps tests passed.\n";
    return 0;
}
