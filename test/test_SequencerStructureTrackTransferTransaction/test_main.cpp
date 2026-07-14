#include <cassert>
#include <cstdint>
#include <iostream>
#include <utility>

#include "handler/common/SharedTrackDomainServices.hpp"
#include "handler/sequencer/SequencerHistoryDomainServices.hpp"
#include "handler/sequencer/SequencerStructureTrackTransferTransaction.hpp"
#include "sequencer/SequencerCcLaneRuntime.hpp"
#include "state/CoreState.hpp"
#include "state/StructureClipboardState.hpp"
#include "state/sequencer/SequencerSnapshotOps.hpp"
#include "state/sequencer/SequencerCcLanePatternOps.hpp"
#include "state/sequencer/SequencerCcLaneRouting.hpp"
#include "../support/CoreStorages.hpp"
#include "../support/NotificationTestUtils.hpp"

namespace {

struct PublishRecorder {
    bool called = false;
};

void recordPreparedPublish(void* context, uint16_t, uint8_t) {
    auto* recorder = static_cast<PublishRecorder*>(context);
    assert(recorder != nullptr);
    recorder->called = true;
}

bool canRecordPreparedHistory(
    void* context,
    const core::state::sequencer::SequencerHistoryTrackStructureChange& change
) {
    auto* history = static_cast<core::state::sequencer::SequencerHistoryService*>(context);
    return history != nullptr && history->canRecordStructure(change);
}

void recordPreparedHistory(
    void* context,
    core::state::sequencer::SequencerHistoryTrackStructureChangePtr change
) {
    auto* history = static_cast<core::state::sequencer::SequencerHistoryService*>(context);
    assert(history != nullptr);
    history->recordPreparedStructure(std::move(change));
}

void storeSourceClipboard(
    core::state::StructureClipboardState& clipboard,
    const core::state::sequencer::SequencerState& editor
) {
    core::state::sequencer::SequencerPatternSnapshot snapshot;
    core::state::sequencer::captureSnapshot(editor.pattern, snapshot);
    assert(clipboard.storeSequencerTrack(
        snapshot,
        nullptr,
        0,
        core::state::sequencer::sequencerCcLaneView(editor.pattern)
    ));
}

void authorInheritedAndPinnedLanes(
    core::state::sequencer::SequencerPatternState& pattern
) {
    namespace seq = core::state::sequencer;
    auto* bank = seq::ensureSequencerCcLaneBank(pattern);
    assert(bank != nullptr);

    seq::SequencerCcLaneDraft inherited{};
    inherited.destination.controller = 74;
    inherited.destination.routePolicy = seq::SequencerCcLaneRoutePolicy::INHERIT_TRACK;
    assert(seq::createSequencerCcLane(*bank, 0, inherited).changed());
    assert(seq::setSequencerCcLaneEvent(*bank, 0, 0, 64).changed());

    seq::SequencerCcLaneDraft pinned{};
    pinned.destination.controller = 1;
    pinned.destination.routePolicy = seq::SequencerCcLaneRoutePolicy::PINNED;
    pinned.destination.pinnedPort = 2;
    pinned.destination.pinnedChannel = 7;
    assert(seq::createSequencerCcLane(*bank, 1, pinned).changed());
    assert(seq::setSequencerCcLaneEvent(*bank, 1, 4, 96).changed());
    pattern.bumpCcLaneRevision();
}

void test_missing_prepared_publication_blocks_before_mutation() {
    core::state::sequencer::SequencerTrackBankState tracks;
    core::state::sequencer::SequencerState editor;
    tracks.reset();
    editor.pattern.note[0] = 77;
    editor.pattern.setEnabled(0, true);
    core::state::StructureClipboardState clipboard;
    storeSourceClipboard(clipboard, editor);

    oc::state::Signal<uint8_t, 8> activeTrack{0};
    oc::state::Signal<uint16_t, 16> enabledMask{0x0001};
    const core::handler::SharedTrackDomainServices shared{
        {activeTrack, enabledMask}
    };
    const core::handler::SequencerHistoryDomainServices history;

    const auto prepared = core::handler::prepareSequencerTrackTransfer(
        tracks,
        editor,
        clipboard,
        shared,
        history,
        1
    );

    assert(prepared.status ==
           core::handler::SequencerTrackTransferStatus::PUBLICATION_UNAVAILABLE);
    assert(tracks.currentEnabledMask() == 0x0001);
    assert(tracks.activeTrackIndex() == 0);
    assert(tracks.track(1).note[0] ==
           core::state::sequencer::SequencerPatternState::DEFAULT_NOTE);
    assert(editor.pattern.note[0] == 77);

    std::cout << "[PASS] test_missing_prepared_publication_blocks_before_mutation\n";
}

void test_missing_prepared_history_blocks_before_mutation() {
    core::state::sequencer::SequencerTrackBankState tracks;
    core::state::sequencer::SequencerState editor;
    tracks.reset();
    editor.pattern.note[0] = 79;
    editor.pattern.setEnabled(0, true);
    tracks.track(1).midiChannel.set(8);
    core::state::StructureClipboardState clipboard;
    storeSourceClipboard(clipboard, editor);

    oc::state::Signal<uint8_t, 8> activeTrack{0};
    oc::state::Signal<uint16_t, 16> enabledMask{0x0001};
    PublishRecorder recorder;
    const core::handler::SharedTrackDomainServices shared{
        {activeTrack, enabledMask},
        {
            .context = &recorder,
            .publishPreparedSequencerState = recordPreparedPublish,
        },
    };
    const core::handler::SequencerHistoryDomainServices history;

    const auto prepared = core::handler::prepareSequencerTrackTransfer(
        tracks,
        editor,
        clipboard,
        shared,
        history,
        1
    );

    assert(prepared.status ==
           core::handler::SequencerTrackTransferStatus::HISTORY_UNAVAILABLE);
    assert(!recorder.called);
    assert(tracks.currentEnabledMask() == 0x0001);
    assert(tracks.activeTrackIndex() == 0);
    assert(tracks.track(1).midiChannel.get() == 8);
    assert(tracks.track(1).note[0] ==
           core::state::sequencer::SequencerPatternState::DEFAULT_NOTE);
    assert(editor.pattern.note[0] == 79);

    std::cout << "[PASS] test_missing_prepared_history_blocks_before_mutation\n";
}

void test_outgoing_live_route_change_invalidates_preparation_atomically() {
    core::state::sequencer::SequencerTrackBankState tracks;
    core::state::sequencer::SequencerState editor;
    tracks.reset();
    editor.pattern.note[0] = 81;
    editor.pattern.setEnabled(0, true);
    core::state::StructureClipboardState clipboard;
    storeSourceClipboard(clipboard, editor);

    oc::state::Signal<uint8_t, 8> activeTrack{0};
    oc::state::Signal<uint16_t, 16> enabledMask{0x0001};
    PublishRecorder recorder;
    const core::handler::SharedTrackDomainServices shared{
        {activeTrack, enabledMask},
        {
            .context = &recorder,
            .publishPreparedSequencerState = recordPreparedPublish,
        },
    };
    core::state::sequencer::SequencerHistoryService historyService;
    const core::handler::SequencerHistoryDomainServices history{
        {
            .context = &historyService,
            .canRecordStructure = canRecordPreparedHistory,
            .recordPreparedStructure = recordPreparedHistory,
        }
    };

    auto prepared = core::handler::prepareSequencerTrackTransfer(
        tracks,
        editor,
        clipboard,
        shared,
        history,
        1
    );
    assert(prepared.ready());

    editor.pattern.midiChannel.set(5);
    const auto result = core::handler::commitPreparedSequencerTrackTransfer(
        tracks,
        editor,
        clipboard,
        shared,
        history,
        std::move(prepared)
    );

    assert(result.status == core::handler::SequencerTrackTransferStatus::STALE);
    assert(!recorder.called);
    assert(historyService.undoCount() == 0);
    assert(tracks.currentEnabledMask() == 0x0001);
    assert(tracks.activeTrackIndex() == 0);
    assert(tracks.track(1).note[0] ==
           core::state::sequencer::SequencerPatternState::DEFAULT_NOTE);
    assert(editor.pattern.note[0] == 81);
    assert(editor.pattern.midiChannel.get() == 5);

    std::cout
        << "[PASS] test_outgoing_live_route_change_invalidates_preparation_atomically\n";
}

core::handler::SequencerTrackTransferResult pasteTrackZeroToOne(
    core::state::CoreState& state
) {
    state.sequencer.pattern.note[0] = 82;
    state.sequencer.pattern.setEnabled(0, true);
    state.sequencer.pattern.bumpStepDataRevision();
    state.sequencerTracks.track(1).midiChannel.set(5);
    storeSourceClipboard(state.structureClipboard, state.sequencer);
    return core::handler::executeSequencerTrackTransfer(
        state.sequencerTracks,
        state.sequencer,
        state.structureClipboard,
        core::handler::SharedTrackDomainServices::fromCoreState(state),
        core::handler::SequencerHistoryDomainServices::fromCoreState(state),
        1,
        0,
        &state.sequencerTrackActivations,
        true
    );
}

void publishActivationGeneration(core::state::CoreState& state) {
    const auto publication =
        state.sequencerTrackActivations.captureRuntimePublication();
    state.sequencerTrackActivations.applyRuntimePublication(publication);
}

uint32_t applyTrackOneActivation(core::state::CoreState& state) {
    publishActivationGeneration(state);
    const auto realtime = state.sequencerTrackActivations.realtimeView(1);
    assert(realtime.disposition ==
           core::state::sequencer::SequencerTrackActivationRealtimeView::Disposition::STAGED);
    assert(state.sequencerTrackActivations.markAppliedFromRealtime(
        1,
        realtime.generation
    ));
    state.sequencerTrackActivations.publishRealtimeTelemetry();
    return realtime.generation;
}

core::handler::SequencerTrackTransferResult pasteExternalNoteToTrackOne(
    core::state::CoreState& state,
    uint8_t note
) {
    core::state::sequencer::SequencerState source;
    source.pattern.note[0] = note;
    source.pattern.setEnabled(0, true);
    source.pattern.bumpStepDataRevision();
    storeSourceClipboard(state.structureClipboard, source);
    return core::handler::executeSequencerTrackTransfer(
        state.sequencerTracks,
        state.sequencer,
        state.structureClipboard,
        core::handler::SharedTrackDomainServices::fromCoreState(state),
        core::handler::SequencerHistoryDomainServices::fromCoreState(state),
        1,
        0,
        &state.sequencerTrackActivations,
        true
    );
}

void test_second_paste_same_track_is_blocked_by_canonical_pending_plan() {
    test_support::CoreStorages storages;
    core::state::CoreState state(
        storages.settings,
        storages.macroLibrary,
        storages.sequencerPatternLibrary,
        storages.sequencerSetLibrary
    );
    const auto first = pasteTrackZeroToOne(state);
    assert(first.applied());
    assert(first.activationGeneration != 0);
    assert(first.operationId != 0);
    assert(first.activationGeneration ==
           state.sequencerTrackActivations.telemetry(1).generation);
    assert(state.sequencerHistory.undoCount() == 1);
    const uint32_t editorRevision = state.sequencer.pattern.stepDataRevision.get();

    const auto second = core::handler::executeSequencerTrackTransfer(
        state.sequencerTracks,
        state.sequencer,
        state.structureClipboard,
        core::handler::SharedTrackDomainServices::fromCoreState(state),
        core::handler::SequencerHistoryDomainServices::fromCoreState(state),
        1,
        0,
        &state.sequencerTrackActivations,
        true
    );
    assert(!second.applied());
    assert(second.plan.reason == core::state::ClipboardTransferReason::PASTE_PENDING);
    assert(state.sequencerHistory.undoCount() == 1);
    assert(state.sequencer.pattern.stepDataRevision.get() == editorRevision);
    test_support::drainNotifications();

    std::cout
        << "[PASS] test_second_paste_same_track_is_blocked_by_canonical_pending_plan\n";
}

void test_undo_before_activation_cancels_and_redo_requeues_without_audible_after() {
    test_support::CoreStorages storages;
    core::state::CoreState state(
        storages.settings,
        storages.macroLibrary,
        storages.sequencerPatternLibrary,
        storages.sequencerSetLibrary
    );
    const auto paste = pasteTrackZeroToOne(state);
    assert(paste.applied());
    const uint32_t pasteGeneration =
        state.sequencerTrackActivations.telemetry(1).generation;
    assert(state.sequencer.pattern.note[0] == 82);

    assert(state.undoSequencerHistory());
    assert(state.sequencerTracks.activeTrackIndex() == 0);
    assert(state.sequencerTracks.track(1).note[0] ==
           core::state::sequencer::SequencerPatternState::DEFAULT_NOTE);
    assert(state.sequencerTrackActivations.telemetry(1).status ==
           core::state::sequencer::SequencerTrackActivationStatus::CANCELLED);
    assert(state.sequencerTrackActivations.realtimeView(1).disposition ==
           core::state::sequencer::SequencerTrackActivationRealtimeView::Disposition::FROZEN);

    assert(state.redoSequencerHistory());
    assert(state.sequencerTracks.activeTrackIndex() == 1);
    assert(state.sequencer.pattern.note[0] == 82);
    assert(state.sequencerTrackActivations.telemetry(1).status ==
           core::state::sequencer::SequencerTrackActivationStatus::QUEUED);
    assert(state.sequencerTrackActivations.telemetry(1).generation != pasteGeneration);
    test_support::drainNotifications();

    std::cout
        << "[PASS] test_undo_before_activation_cancels_and_redo_requeues_without_audible_after\n";
}

void test_undo_and_redo_after_activation_use_new_inverse_generations() {
    test_support::CoreStorages storages;
    core::state::CoreState state(
        storages.settings,
        storages.macroLibrary,
        storages.sequencerPatternLibrary,
        storages.sequencerSetLibrary
    );
    const auto paste = pasteTrackZeroToOne(state);
    assert(paste.applied());
    const uint32_t pasteGeneration =
        state.sequencerTrackActivations.telemetry(1).generation;
    publishActivationGeneration(state);
    assert(state.sequencerTrackActivations.markAppliedFromRealtime(1, pasteGeneration));
    state.sequencerTrackActivations.publishRealtimeTelemetry();

    assert(state.undoSequencerHistory());
    assert(state.sequencerTracks.activeTrackIndex() == 0);
    assert(state.sequencerTracks.track(1).note[0] ==
           core::state::sequencer::SequencerPatternState::DEFAULT_NOTE);
    assert(state.sequencerTrackActivations.telemetry(1).status ==
           core::state::sequencer::SequencerTrackActivationStatus::QUEUED);
    const uint32_t undoGeneration =
        state.sequencerTrackActivations.telemetry(1).generation;
    assert(undoGeneration != pasteGeneration);

    publishActivationGeneration(state);
    assert(state.sequencerTrackActivations.markAppliedFromRealtime(1, undoGeneration));
    state.sequencerTrackActivations.publishRealtimeTelemetry();
    assert(state.redoSequencerHistory());
    assert(state.sequencerTracks.activeTrackIndex() == 1);
    assert(state.sequencer.pattern.note[0] == 82);
    assert(state.sequencerTrackActivations.telemetry(1).status ==
           core::state::sequencer::SequencerTrackActivationStatus::QUEUED);
    assert(state.sequencerTrackActivations.telemetry(1).generation != undoGeneration);
    test_support::drainNotifications();

    std::cout
        << "[PASS] test_undo_and_redo_after_activation_use_new_inverse_generations\n";
}

void test_stacked_same_track_history_supersedes_pending_intermediate_targets() {
    test_support::CoreStorages storages;
    core::state::CoreState state(
        storages.settings,
        storages.macroLibrary,
        storages.sequencerPatternLibrary,
        storages.sequencerSetLibrary
    );

    assert(pasteTrackZeroToOne(state).applied());
    applyTrackOneActivation(state);
    assert(pasteExternalNoteToTrackOne(state, 91).applied());
    assert(state.sequencer.pattern.note[0] == 91);
    assert(state.sequencerHistory.undoCount() == 2);

    // B is still pending. Undo B cancels it, then Undo A immediately rebinds
    // the one-slot queue to A's Base target without making B or A audible.
    assert(state.undoSequencerHistory());
    assert(state.sequencer.pattern.note[0] == 82);
    assert(state.undoSequencerHistory());
    assert(state.sequencerTracks.track(1).note[0] ==
           core::state::sequencer::SequencerPatternState::DEFAULT_NOTE);
    assert(state.sequencerHistory.undoCount() == 0);
    assert(state.sequencerHistory.redoCount() == 2);
    applyTrackOneActivation(state);

    // The Redo targets are also traversed faster than the scheduler boundary:
    // B supersedes the queued A generation and stages the final B snapshot.
    assert(state.redoSequencerHistory());
    assert(state.sequencer.pattern.note[0] == 82);
    assert(state.redoSequencerHistory());
    assert(state.sequencer.pattern.note[0] == 91);
    assert(state.sequencerHistory.undoCount() == 2);
    assert(state.sequencerHistory.redoCount() == 0);
    applyTrackOneActivation(state);
    test_support::drainNotifications();

    std::cout
        << "[PASS] test_stacked_same_track_history_supersedes_pending_intermediate_targets\n";
}

void test_stacked_same_track_history_traverses_after_each_boundary() {
    test_support::CoreStorages storages;
    core::state::CoreState state(
        storages.settings,
        storages.macroLibrary,
        storages.sequencerPatternLibrary,
        storages.sequencerSetLibrary
    );

    assert(pasteTrackZeroToOne(state).applied());
    applyTrackOneActivation(state);
    assert(pasteExternalNoteToTrackOne(state, 91).applied());
    applyTrackOneActivation(state);

    assert(state.undoSequencerHistory());
    assert(state.sequencer.pattern.note[0] == 82);
    applyTrackOneActivation(state);
    assert(state.undoSequencerHistory());
    assert(state.sequencerTracks.track(1).note[0] ==
           core::state::sequencer::SequencerPatternState::DEFAULT_NOTE);
    applyTrackOneActivation(state);

    assert(state.redoSequencerHistory());
    assert(state.sequencer.pattern.note[0] == 82);
    applyTrackOneActivation(state);
    assert(state.redoSequencerHistory());
    assert(state.sequencer.pattern.note[0] == 91);
    applyTrackOneActivation(state);
    test_support::drainNotifications();

    std::cout
        << "[PASS] test_stacked_same_track_history_traverses_after_each_boundary\n";
}

void test_track_paste_rebinds_inherited_lane_and_preserves_pin_through_history() {
    namespace seq = core::state::sequencer;
    test_support::CoreStorages storages;
    core::state::CoreState state(
        storages.settings,
        storages.macroLibrary,
        storages.sequencerPatternLibrary,
        storages.sequencerSetLibrary
    );

    state.sequencer.pattern.midiChannel.set(1);   // Source: MIDI channel 2.
    state.sequencerTracks.track(1).midiChannel.set(10);  // Target: channel 11.
    authorInheritedAndPinnedLanes(state.sequencer.pattern);
    storeSourceClipboard(state.structureClipboard, state.sequencer);

    const auto result = core::handler::executeSequencerTrackTransfer(
        state.sequencerTracks,
        state.sequencer,
        state.structureClipboard,
        core::handler::SharedTrackDomainServices::fromCoreState(state),
        core::handler::SequencerHistoryDomainServices::fromCoreState(state),
        1,
        0,
        &state.sequencerTrackActivations,
        true
    );
    assert(result.applied());
    assert(result.plan.inheritedLaneCount == 1);
    assert(result.plan.pinnedLaneCount == 1);
    assert((result.plan.bindingPolicy &
            core::state::CLIPBOARD_TRANSFER_REBIND_INHERITED) != 0);
    assert((result.plan.bindingPolicy &
            core::state::CLIPBOARD_TRANSFER_PRESERVE_PINNED) != 0);
    assert(result.plan.entries[0].targetMidiChannel == 10);
    assert(result.plan.entries[0].inheritedLaneCount == 1);
    assert(result.plan.entries[0].pinnedLaneCount == 1);

    assert(state.sequencerTracks.activeTrackIndex() == 1);
    assert(state.sequencer.pattern.midiChannel.get() == 10);
    const auto* pasted = seq::sequencerCcLaneView(state.sequencer.pattern);
    assert(pasted != nullptr);
    assert(pasted->lanes[0].destination.routePolicy ==
           seq::SequencerCcLaneRoutePolicy::INHERIT_TRACK);
    const auto inheritedRoute = seq::resolveSequencerCcLaneDestination(
        pasted->lanes[0],
        {.port = 0,
         .channel = state.sequencer.pattern.midiChannel.get(),
         .validity = core::state::shared::MidiCcRouteValidity::VALID}
    );
    assert(inheritedRoute.ok());
    assert(inheritedRoute.destination.identity.port == 0);
    assert(inheritedRoute.destination.identity.channel == 10);
    assert(pasted->lanes[1].destination.routePolicy ==
           seq::SequencerCcLaneRoutePolicy::PINNED);
    assert(pasted->lanes[1].destination.pinnedPort == 2);
    assert(pasted->lanes[1].destination.pinnedChannel == 7);

    assert(state.undoSequencerHistory());
    assert(state.sequencerTracks.activeTrackIndex() == 0);
    assert(seq::sequencerCcLaneView(state.sequencerTracks.track(1)) == nullptr);

    assert(state.redoSequencerHistory());
    assert(state.sequencerTracks.activeTrackIndex() == 1);
    const auto* restored = seq::sequencerCcLaneView(state.sequencer.pattern);
    assert(restored != nullptr);
    const auto restoredInheritedRoute = seq::resolveSequencerCcLaneDestination(
        restored->lanes[0],
        {.port = 0,
         .channel = state.sequencer.pattern.midiChannel.get(),
         .validity = core::state::shared::MidiCcRouteValidity::VALID}
    );
    assert(restoredInheritedRoute.ok());
    assert(restoredInheritedRoute.destination.identity.channel == 10);
    assert(restored->lanes[1].destination.pinnedPort == 2);
    assert(restored->lanes[1].destination.pinnedChannel == 7);
    test_support::drainNotifications();

    std::cout
        << "[PASS] Track paste rebinds Inherit, preserves Pin, and history is exact\n";
}

void test_track_paste_rebases_lane_lifecycle_and_clears_destination_hold() {
    namespace seq = core::state::sequencer;
    test_support::CoreStorages storages;
    core::state::CoreState state(
        storages.settings,
        storages.macroLibrary,
        storages.sequencerPatternLibrary,
        storages.sequencerSetLibrary
    );

    // Prime the destination lane with generation 1 and an audible held value.
    auto& destination = state.sequencerTracks.track(1);
    destination.midiChannel.set(5);
    auto* destinationBank = seq::ensureSequencerCcLaneBank(destination);
    assert(destinationBank != nullptr);
    seq::SequencerCcLaneDraft laneDraft{};
    laneDraft.destination.controller = 74;
    assert(seq::createSequencerCcLane(*destinationBank, 0, laneDraft).changed());
    assert(seq::setSequencerCcLaneEvent(*destinationBank, 0, 0, 41).changed());
    destination.bumpCcLaneRevision();
    assert(destinationBank->lanes[0].lifecycleGeneration == 1);

    core::sequencer::SequencerCcLaneRuntime runtime;
    core::sequencer::SequencerCcLaneRuntime::Inputs inputs{};
    inputs[1] = {
        .lanes = destinationBank,
        .route = seq::makeSequencerCcTrackRoute(0, 5),
        .step = 0,
        .enabled = true,
        .muted = false,
        .stepTriggered = true,
        .frozen = false,
    };
    core::sequencer::SequencerCcLaneRuntimeFrame frame{};
    assert(runtime.buildMusicalTickFrame(inputs, true, frame) ==
           core::sequencer::SequencerCcLaneRuntimeStatus::OK);
    assert(frame.candidateCount == 1);
    assert(frame.candidates[0].localValue == 41);
    assert(runtime.hasHeldValue(1, 0));

    // The source independently also has generation 1, with its first authored
    // value later in the pattern. Raw generation copying would retain 41.
    core::state::sequencer::SequencerState source;
    auto* sourceBank = seq::ensureSequencerCcLaneBank(source.pattern);
    assert(sourceBank != nullptr);
    assert(seq::createSequencerCcLane(*sourceBank, 0, laneDraft).changed());
    assert(seq::setSequencerCcLaneEvent(*sourceBank, 0, 2, 93).changed());
    source.pattern.bumpCcLaneRevision();
    assert(sourceBank->lanes[0].lifecycleGeneration == 1);
    storeSourceClipboard(state.structureClipboard, source);

    auto prepared = core::handler::prepareSequencerTrackTransfer(
        state.sequencerTracks,
        state.sequencer,
        state.structureClipboard,
        core::handler::SharedTrackDomainServices::fromCoreState(state),
        core::handler::SequencerHistoryDomainServices::fromCoreState(state),
        1,
        0,
        &state.sequencerTrackActivations,
        false
    );
    assert(prepared.ready());
    const uint16_t rebasedGeneration =
        seq::nextSequencerCcLaneLifecycleGeneration(1);
    assert(prepared.history->after.tracks[1].ccLanes->lanes[0]
               .lifecycleGeneration == rebasedGeneration);
    assert(prepared.bankCcLanes[0]->lanes[0].lifecycleGeneration ==
           rebasedGeneration);
    assert(prepared.editorCcLanes->lanes[0].lifecycleGeneration ==
           rebasedGeneration);

    const auto paste = core::handler::commitPreparedSequencerTrackTransfer(
        state.sequencerTracks,
        state.sequencer,
        state.structureClipboard,
        core::handler::SharedTrackDomainServices::fromCoreState(state),
        core::handler::SequencerHistoryDomainServices::fromCoreState(state),
        std::move(prepared)
    );
    assert(paste.applied());
    const auto* pasted = seq::sequencerCcLaneView(state.sequencer.pattern);
    assert(pasted != nullptr);
    assert(pasted->lanes[0].lifecycleGeneration == rebasedGeneration);

    inputs = {};
    inputs[1] = {
        .lanes = pasted,
        .route = seq::makeSequencerCcTrackRoute(0, 5),
        .step = 1,
        .enabled = true,
        .muted = false,
        .stepTriggered = true,
        .frozen = false,
    };
    assert(runtime.buildMusicalTickFrame(inputs, true, frame) ==
           core::sequencer::SequencerCcLaneRuntimeStatus::OK);
    assert(frame.candidateCount == 0);
    assert(!runtime.hasHeldValue(1, 0));
    inputs[1].step = 2;
    assert(runtime.buildMusicalTickFrame(inputs, true, frame) ==
           core::sequencer::SequencerCcLaneRuntimeStatus::OK);
    assert(frame.candidateCount == 1);
    assert(frame.candidates[0].localValue == 93);

    // Undo and Redo alternate the exact captured generations. Each transition
    // invalidates the previous hold before accepting its own next event.
    assert(state.undoSequencerHistory());
    const auto* undone = seq::sequencerCcLaneView(state.sequencerTracks.track(1));
    assert(undone != nullptr);
    assert(undone->lanes[0].lifecycleGeneration == 1);
    inputs[1].lanes = undone;
    inputs[1].step = 1;
    assert(runtime.buildMusicalTickFrame(inputs, true, frame) ==
           core::sequencer::SequencerCcLaneRuntimeStatus::OK);
    assert(frame.candidateCount == 0);
    inputs[1].step = 0;
    assert(runtime.buildMusicalTickFrame(inputs, true, frame) ==
           core::sequencer::SequencerCcLaneRuntimeStatus::OK);
    assert(frame.candidateCount == 1);
    assert(frame.candidates[0].localValue == 41);

    assert(state.redoSequencerHistory());
    const auto* redone = seq::sequencerCcLaneView(state.sequencer.pattern);
    assert(redone != nullptr);
    assert(redone->lanes[0].lifecycleGeneration == rebasedGeneration);
    inputs[1].lanes = redone;
    inputs[1].step = 1;
    assert(runtime.buildMusicalTickFrame(inputs, true, frame) ==
           core::sequencer::SequencerCcLaneRuntimeStatus::OK);
    assert(frame.candidateCount == 0);
    inputs[1].step = 2;
    assert(runtime.buildMusicalTickFrame(inputs, true, frame) ==
           core::sequencer::SequencerCcLaneRuntimeStatus::OK);
    assert(frame.candidateCount == 1);
    assert(frame.candidates[0].localValue == 93);

    test_support::drainNotifications();
    std::cout
        << "[PASS] Track paste rebases lane lifecycle and Undo/Redo reset holds\n";
}

}  // namespace

int main() {
    test_missing_prepared_publication_blocks_before_mutation();
    test_missing_prepared_history_blocks_before_mutation();
    test_outgoing_live_route_change_invalidates_preparation_atomically();
    test_second_paste_same_track_is_blocked_by_canonical_pending_plan();
    test_undo_before_activation_cancels_and_redo_requeues_without_audible_after();
    test_undo_and_redo_after_activation_use_new_inverse_generations();
    test_stacked_same_track_history_supersedes_pending_intermediate_targets();
    test_stacked_same_track_history_traverses_after_each_boundary();
    test_track_paste_rebinds_inherited_lane_and_preserves_pin_through_history();
    test_track_paste_rebases_lane_lifecycle_and_clears_destination_hold();
    std::cout << "All SequencerStructureTrackTransferTransaction tests passed\n";
    return 0;
}
