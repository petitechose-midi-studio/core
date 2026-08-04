#include "state/sequencer/SequencerTrackBankOps.hpp"

#include <algorithm>
#include <utility>

#include <config/PlatformCompat.hpp>
#include "app/ExtmemAllocator.hpp"
#include "state/sequencer/SequencerGraphOps.hpp"
#include "state/sequencer/SequencerCcLanePatternOps.hpp"
#include "state/sequencer/SequencerSnapshotOps.hpp"

namespace core::state::sequencer {

namespace {

[[noreturn]] FLASHMEM void failPreparedTrackRotationInvariant() noexcept {
#if defined(__GNUC__) || defined(__clang__)
    __builtin_trap();
#else
    for (;;) {}
#endif
}

FLASHMEM bool sameVariationRangesExact(
    const oc::note::sequencer::StepSequencerVariationRanges& lhs,
    const oc::note::sequencer::StepSequencerVariationRanges& rhs
) noexcept {
    return lhs.pitchSemitones == rhs.pitchSemitones &&
           lhs.velocity == rhs.velocity &&
           lhs.gatePercent == rhs.gatePercent &&
           lhs.nudge == rhs.nudge;
}

FLASHMEM bool sameScaleSettingsExact(
    const oc::note::sequencer::StepSequencerScaleSettings& lhs,
    const oc::note::sequencer::StepSequencerScaleSettings& rhs
) noexcept {
    return lhs.root == rhs.root && lhs.type == rhs.type && lhs.mode == rhs.mode;
}

FLASHMEM bool flatSnapshotIsCanonical(
    const SequencerPatternSnapshot& snapshot
) noexcept {
    if (snapshot.length == 0U ||
        snapshot.length > SequencerPatternState::MAX_STEPS ||
        snapshot.playStart >= snapshot.length ||
        snapshot.loopStart < snapshot.playStart ||
        snapshot.loopStart >= snapshot.length ||
        snapshot.loopEnd <= snapshot.loopStart ||
        snapshot.loopEnd > snapshot.length ||
        snapshot.stepsPerBeat == 0U ||
        !((snapshot.enabledMask & lengthMask(snapshot.length)) == snapshot.enabledMask) ||
        snapshot.swingOffsetPercent != SequencerPatternState::clampPatternSwingOffsetPercent(
            snapshot.swingOffsetPercent
        ) ||
        snapshot.patternNudgePercent != SequencerPatternState::clampPatternNudgePercent(
            snapshot.patternNudgePercent
        ) ||
        static_cast<uint8_t>(snapshot.scalePolicy) >
            static_cast<uint8_t>(SequencerPatternScalePolicy::OVERRIDE) ||
        !validPitchEditMode(static_cast<uint8_t>(snapshot.pitchEditMode))) {
        return false;
    }

    auto variationRanges = snapshot.variationRanges;
    variationRanges.clamp();
    if (!sameVariationRangesExact(snapshot.variationRanges, variationRanges)) {
        return false;
    }

    auto scaleOverride = snapshot.scaleOverride;
    scaleOverride.clamp();
    if (!sameScaleSettingsExact(snapshot.scaleOverride, scaleOverride)) {
        return false;
    }

    const auto effectiveScale = resolveEffectiveScaleSettings(
        {},
        snapshot.scalePolicy,
        snapshot.scaleOverride
    );
    if (snapshot.effectiveSwingPercent !=
            SequencerPatternState::clampEffectiveSwingPercent(snapshot.swingOffsetPercent) ||
        !sameScaleSettingsExact(snapshot.effectiveScaleSettings, effectiveScale)) {
        return false;
    }

    for (uint16_t i = 0U; i < SequencerPatternState::MAX_STEPS; ++i) {
        if (snapshot.note[i] != SequencerPatternState::clampMidi7(snapshot.note[i]) ||
            snapshot.velocity[i] !=
                SequencerPatternState::clampMidi7(snapshot.velocity[i]) ||
            snapshot.gate[i] !=
                SequencerPatternState::clampGatePercent(snapshot.gate[i]) ||
            snapshot.nudge[i] != SequencerPatternState::clampNudge(snapshot.nudge[i]) ||
            snapshot.probability[i] !=
                SequencerPatternState::clampProbability(snapshot.probability[i])) {
            return false;
        }
    }
    return true;
}

template <typename T>
FLASHMEM bool distinctNonNullOwners(const T* first, const T* second, const T* third) noexcept {
    return (first == nullptr || second == nullptr || first != second) &&
           (first == nullptr || third == nullptr || first != third) &&
           (second == nullptr || third == nullptr || second != third);
}

FLASHMEM void installPreparedFlat(
    SequencerPatternState& target,
    SequencerTrackFlatSnapshotView prepared
) noexcept {
    if (prepared.snapshot == nullptr) failPreparedTrackRotationInvariant();
    applySnapshotPreservingGraph(target, *prepared.snapshot);

    // Snapshot projection setters intentionally maintain their own live
    // revision semantics. A prepared Structure commit instead installs the
    // already-final revisions exactly and lets deferred Signal delivery
    // coalesce any intermediate setter notification.
    target.stepDataRevision.set(prepared.snapshot->stepDataRevision);
    target.patternVariationRevision.set(prepared.snapshot->patternVariationRevision);
    target.patternScaleRevision.set(prepared.snapshot->patternScaleRevision);
    target.patternTimingRevision.set(prepared.snapshot->patternTimingRevision);
    target.graphRevision.set(prepared.snapshot->graphRevision);
    target.ccLaneRevision.set(prepared.ccLaneRevision);
}

FLASHMEM void copyFlatPatternToEditor(
    SequencerState& target,
    const SequencerPatternState& source
) {
    const uint8_t focusedBefore = target.focusedStep.get();
    SequencerPatternSnapshot snapshot;
    captureSnapshot(source, snapshot);
    applySnapshotToEditorPreservingGraph(target, snapshot);
    target.pattern.ccLaneRevision.set(source.ccLaneRevision.get());

    const uint8_t length = target.pattern.length.get();
    const uint8_t focused =
        (focusedBefore >= length) ? static_cast<uint8_t>(length - 1U) : focusedBefore;
    target.focusedStep.set(focused);
    target.page.set(target.pageForStep(focused));
}

FLASHMEM bool copyEditorToPattern(SequencerPatternState& target, const SequencerState& source) {
    return copyPatternState(target, source.pattern);
}

FLASHMEM void copyFlatEditorToPattern(
    SequencerPatternState& target,
    const SequencerState& source
) {
    SequencerPatternSnapshot snapshot;
    captureSnapshot(source.pattern, snapshot);
    applySnapshotPreservingGraph(target, snapshot);
    target.ccLaneRevision.set(source.pattern.ccLaneRevision.get());
}

FLASHMEM void exchangeColdPayload(
    SequencerPatternState& left,
    SequencerPatternState& right
) {
    using std::swap;
    swap(left.graph, right.graph);
    swap(left.ccLanes, right.ccLanes);
}

}  // namespace

FLASHMEM bool sequencerPatternMatchesFlatSnapshot(
    const SequencerPatternState& pattern,
    SequencerTrackFlatSnapshotView expected
) noexcept {
    if (expected.snapshot == nullptr || !flatSnapshotIsCanonical(*expected.snapshot)) {
        return false;
    }

    const auto& snapshot = *expected.snapshot;
    return pattern.length.get() == snapshot.length &&
           pattern.playStart == snapshot.playStart &&
           pattern.loopStart == snapshot.loopStart &&
           pattern.loopEnd == snapshot.loopEnd &&
           pattern.stepsPerBeat.get() == snapshot.stepsPerBeat &&
           pattern.enabledMask.get() == snapshot.enabledMask &&
           pattern.stepDataRevision.get() == snapshot.stepDataRevision &&
           pattern.patternVariationRevision.get() == snapshot.patternVariationRevision &&
           pattern.patternScaleRevision.get() == snapshot.patternScaleRevision &&
           pattern.patternTimingRevision.get() == snapshot.patternTimingRevision &&
           pattern.graphRevision.get() == snapshot.graphRevision &&
           pattern.ccLaneRevision.get() == expected.ccLaneRevision &&
           pattern.swingOffsetPercent.get() == snapshot.swingOffsetPercent &&
           pattern.patternNudgePercent.get() == snapshot.patternNudgePercent &&
           sameVariationRangesExact(pattern.variationRanges, snapshot.variationRanges) &&
           pattern.scalePolicy == snapshot.scalePolicy &&
           sameScaleSettingsExact(pattern.scaleOverride, snapshot.scaleOverride) &&
           pattern.pitchEditMode == snapshot.pitchEditMode &&
           pattern.note == snapshot.note &&
           pattern.velocity == snapshot.velocity &&
           pattern.gate == snapshot.gate &&
           pattern.nudge == snapshot.nudge &&
           pattern.probability == snapshot.probability;
}

FLASHMEM bool preparedActiveTrackOwnerRotationMatches(
    const SequencerTrackBankState& bank,
    const SequencerState& active,
    const SequencerPreparedActiveTrackRotation& prepared
) noexcept {
    if (prepared.outgoingTrack >= SequencerTrackBankState::TRACK_COUNT ||
        prepared.incomingTrack >= SequencerTrackBankState::TRACK_COUNT ||
        prepared.outgoingTrack == prepared.incomingTrack ||
        bank.activeTrackIndex() != prepared.outgoingTrack ||
        bank.currentEnabledMask() != prepared.expectedEnabledMask ||
        active.stepContentDraft.active.get() ||
        prepared.expectedOutgoing.snapshot == nullptr ||
        prepared.expectedIncoming.snapshot == nullptr ||
        prepared.finalOutgoing.snapshot == nullptr ||
        prepared.finalIncoming.snapshot == nullptr ||
        !flatSnapshotIsCanonical(*prepared.finalOutgoing.snapshot) ||
        !flatSnapshotIsCanonical(*prepared.finalIncoming.snapshot)) {
        return false;
    }

    switch (prepared.incomingOwnerPolicy) {
        case SequencerActiveTrackIncomingOwnerPolicy::Preserve:
        case SequencerActiveTrackIncomingOwnerPolicy::Reset:
            break;
        default:
            return false;
    }

    const auto& outgoing = bank.track(prepared.outgoingTrack);
    const auto& incoming = bank.track(prepared.incomingTrack);
    if (active.pattern.graph.get() != prepared.expectedEditorGraphOwner ||
        active.pattern.ccLanes.get() != prepared.expectedEditorCcLaneOwner ||
        outgoing.graph.get() != prepared.expectedOutgoingGraphOwner ||
        outgoing.ccLanes.get() != prepared.expectedOutgoingCcLaneOwner ||
        incoming.graph.get() != prepared.expectedIncomingGraphOwner ||
        incoming.ccLanes.get() != prepared.expectedIncomingCcLaneOwner ||
        !distinctNonNullOwners(
            prepared.expectedEditorGraphOwner,
            prepared.expectedOutgoingGraphOwner,
            prepared.expectedIncomingGraphOwner
        ) ||
        !distinctNonNullOwners(
            prepared.expectedEditorCcLaneOwner,
            prepared.expectedOutgoingCcLaneOwner,
            prepared.expectedIncomingCcLaneOwner
        )) {
        return false;
    }

    return sequencerPatternMatchesFlatSnapshot(active.pattern, prepared.expectedOutgoing) &&
           sequencerPatternMatchesFlatSnapshot(incoming, prepared.expectedIncoming);
}

FLASHMEM bool prepareActiveTrackOwnerRotation(
    const SequencerTrackBankState& bank,
    const SequencerState& active,
    uint8_t incomingTrack,
    SequencerTrackFlatSnapshotView expectedOutgoing,
    SequencerTrackFlatSnapshotView expectedIncoming,
    SequencerTrackFlatSnapshotView finalOutgoing,
    SequencerTrackFlatSnapshotView finalIncoming,
    SequencerActiveTrackIncomingOwnerPolicy incomingOwnerPolicy,
    SequencerPreparedActiveTrackRotation& out
) noexcept {
    out = {};
    const uint8_t outgoingTrack = bank.activeTrackIndex();
    if (outgoingTrack >= SequencerTrackBankState::TRACK_COUNT ||
        incomingTrack >= SequencerTrackBankState::TRACK_COUNT ||
        outgoingTrack == incomingTrack ||
        finalOutgoing.snapshot == nullptr ||
        finalIncoming.snapshot == nullptr) {
        return false;
    }

    const auto& outgoing = bank.track(outgoingTrack);
    const auto& incoming = bank.track(incomingTrack);
    out.expectedOutgoing = expectedOutgoing;
    out.expectedIncoming = expectedIncoming;
    out.finalOutgoing = finalOutgoing;
    out.finalIncoming = finalIncoming;
    out.expectedEditorGraphOwner = active.pattern.graph.get();
    out.expectedEditorCcLaneOwner = active.pattern.ccLanes.get();
    out.expectedOutgoingGraphOwner = outgoing.graph.get();
    out.expectedOutgoingCcLaneOwner = outgoing.ccLanes.get();
    out.expectedIncomingGraphOwner = incoming.graph.get();
    out.expectedIncomingCcLaneOwner = incoming.ccLanes.get();
    out.expectedEnabledMask = bank.currentEnabledMask();
    out.outgoingTrack = outgoingTrack;
    out.incomingTrack = incomingTrack;
    out.incomingOwnerPolicy = incomingOwnerPolicy;

    if (!preparedActiveTrackOwnerRotationMatches(bank, active, out)) {
        out = {};
        return false;
    }
    return true;
}

FLASHMEM void rotateActiveTrackOwnersNoPublish(
    SequencerTrackBankState& bank,
    SequencerState& active,
    const SequencerPreparedActiveTrackRotation& prepared
) noexcept {
    if (!preparedActiveTrackOwnerRotationMatches(bank, active, prepared)) {
        failPreparedTrackRotationInvariant();
    }

    auto& outgoing = bank.track(prepared.outgoingTrack);
    auto& incoming = bank.track(prepared.incomingTrack);
    if (prepared.incomingOwnerPolicy ==
        SequencerActiveTrackIncomingOwnerPolicy::Reset) {
        incoming.graph.reset();
        incoming.ccLanes.reset();
    }

    exchangeColdPayload(active.pattern, outgoing);
    exchangeColdPayload(active.pattern, incoming);
    installPreparedFlat(outgoing, prepared.finalOutgoing);
    installPreparedFlat(active.pattern, prepared.finalIncoming);
    resetTransientTrackState(active);
}

FLASHMEM void resetTransientTrackState(SequencerState& state) {
    if (state.stepContentDraft.active.get()) {
        state.stepContentDraft.noteBlockedTransition(
            SequencerStepContentDraftBlockedTransition::RESET
        );
        return;
    }
    state.stepEdit.reset();
    state.contextSelector.reset();
    state.ccLaneUi.reset();
    state.stepPropertyInlineSelector.reset();
    state.stepInlineFeedback.reset();
    state.patternQuickControls.reset();
    state.contentView.reset();
    state.stepContentDraft.resetSession();
}

FLASHMEM bool initializeTrackBankFromActive(
    SequencerTrackBankState& bank,
    const SequencerState& active
) {
    bank.reset();
    if (!copyEditorToPattern(bank.track(0), active)) return false;
    bank.syncSharedTrackState(0x0001, 0);
    return true;
}

FLASHMEM bool storeActiveTrack(SequencerTrackBankState& bank, const SequencerState& active) {
    return copyEditorToPattern(bank.track(bank.activeTrackIndex()), active);
}

FLASHMEM bool storeActiveTrackPreservingGraph(
    SequencerTrackBankState& bank,
    const SequencerState& active
) {
    auto& target = bank.track(bank.activeTrackIndex());
    const bool targetHasGraph = graphView(target) != nullptr;
    const bool sourceHasGraph = graphView(active.pattern) != nullptr;
    const bool graphSynchronized =
        targetHasGraph == sourceHasGraph &&
        target.graphRevision.get() == active.pattern.graphRevision.get();

    if (!graphSynchronized) {
        return copyEditorToPattern(target, active);
    }

    return copyPatternStatePreservingGraph(target, active.pattern);
}

FLASHMEM bool switchActiveTrack(
    SequencerTrackBankState& bank,
    SequencerState& active,
    uint8_t nextTrack
) {
    const uint8_t current = bank.activeTrackIndex();
    const uint8_t clampedNext = SequencerTrackBankState::clampTrackIndex(nextTrack);
    if (clampedNext == current) {
        return false;
    }
    if (active.stepContentDraft.active.get()) {
        active.stepContentDraft.noteBlockedTransition(
            SequencerStepContentDraftBlockedTransition::TRACK
        );
        return false;
    }

    auto& outgoing = bank.track(current);
    auto& incoming = bank.track(clampedNext);

    // Signals in the retained editor must keep their addresses because UI
    // bindings subscribe to them. Copy only the flat values, then rotate the
    // two PSRAM-owned payload pointers through the editor. The active bank slot
    // is intentionally a spare while that Track is edited, as it already was
    // for flat values before this refactor.
    copyFlatEditorToPattern(outgoing, active);
    exchangeColdPayload(active.pattern, outgoing);
    copyFlatPatternToEditor(active, incoming);
    exchangeColdPayload(active.pattern, incoming);
    resetTransientTrackState(active);

    bank.syncSharedTrackState(bank.currentEnabledMask(), clampedNext);
    return true;
}

FLASHMEM void captureTrackBankSnapshot(
    const SequencerTrackBankState& bank,
    const SequencerState& active,
    SequencerTrackBankSnapshot& out
) {
    const uint8_t activeTrack = SequencerTrackBankState::clampTrackIndex(bank.activeTrackIndex());
    out.activeTrack = activeTrack;
    out.enabledMask = bank.currentEnabledMask();
    out.projectScaleRevision = bank.projectScaleRevisionSignal().get();
    out.projectScaleSettings = bank.projectScaleSettings();

    for (uint8_t i = 0; i < SequencerTrackBankState::TRACK_COUNT; ++i) {
        captureSnapshot((i == activeTrack) ? active.pattern : bank.track(i), out.tracks[i]);
    }
}

FLASHMEM void applyTrackBankSnapshot(
    SequencerTrackBankState& bank,
    SequencerState& active,
    const SequencerTrackBankSnapshot& snapshot
) {
    if (active.stepContentDraft.active.get()) {
        active.stepContentDraft.noteBlockedTransition(
            SequencerStepContentDraftBlockedTransition::PROJECT_LOAD
        );
        return;
    }
    bank.reset();
    bank.syncSharedTrackState(snapshot.enabledMask, snapshot.activeTrack);
    bank.setProjectScaleSettings(snapshot.projectScaleSettings);
    bank.projectScaleRevisionSignal().set(snapshot.projectScaleRevision);

    for (uint8_t i = 0; i < SequencerTrackBankState::TRACK_COUNT; ++i) {
        applySnapshot(bank.track(i), snapshot.tracks[i]);
    }

    const uint8_t activeTrack = bank.activeTrackIndex();
    applySnapshotToEditor(active, snapshot.tracks[activeTrack]);
    resetTransientTrackState(active);
}

}  // namespace core::state::sequencer
