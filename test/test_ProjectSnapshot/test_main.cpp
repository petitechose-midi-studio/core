#include <algorithm>
#include <array>
#include <cassert>
#include <cstring>
#include <iostream>

#include "../../src/state/CoreState.hpp"
#include "../../src/state/macro/MacroWorkflow.hpp"
#include "../../src/state/modulation/ProjectControlMacroOps.hpp"
#include "../../src/state/project/ProjectSnapshot.hpp"
#include "../../src/state/project/ProjectTrackDomainServices.hpp"
#include "../../src/state/project/ProjectTrackDomainOps.hpp"
#include "../../src/state/sequencer/SequencerCcLaneDomain.hpp"
#include "../../src/state/sequencer/SequencerCcLanePatternOps.hpp"
#include "../../src/state/sequencer/SequencerGraphOps.hpp"
#include "../../src/state/sequencer/SequencerScaleState.hpp"
#include "../../src/state/sequencer/SequencerStepContentDraftOps.hpp"
#include "../support/CoreStorages.hpp"
#include "../support/NotificationTestUtils.hpp"
#include "../support/ProjectControlTestUtils.hpp"

namespace {

namespace project = core::state::project;
namespace sequencer = core::state::sequencer;
namespace modulation = core::state::modulation;

using oc::note::sequencer::StepSequencerScaleConstraintMode;
using oc::note::sequencer::StepSequencerScaleSettings;
using oc::note::sequencer::StepSequencerScaleType;

core::state::CoreState makeCoreState(test_support::CoreStorages& storages) {
    return core::state::CoreState{
        storages.settings,
    };
}

core::state::modulation::ProjectModulationResult beginLfoAudition(
    core::state::CoreState& state,
    uint8_t macro = 1U
) {
    const auto address = core::state::macro::MacroAutomationSlotAddress{
        .track = state.pages.currentActiveTrack(),
        .page = state.pages.currentActivePage(),
        .macro = macro,
    };
    modulation::ModulatorLfoDraft source{};
    source.name = "Snapshot guard";
    source.parameters.periodTicks = modulation::PROJECT_CONTROL_TICKS_PER_BEAT;
    source.parameters.shape = modulation::ModulatorLfoShape::SINE;
    source.parameters.retrigger = modulation::ModulatorRetriggerPolicy::TRANSPORT;
    source.parameters.timing = modulation::ModulatorTimingMode::SYNC;

    modulation::ModulationBindingDraft binding{};
    binding.destination = modulation::projectControlDestination(address);
    binding.amountQ15 = 8192;
    binding.application = modulation::ModulationApplication::NATURAL;
    return state.macroHistory.beginLfoModulatorAudition(
        state.pages,
        address,
        source,
        binding
    );
}

bool sameScale(const StepSequencerScaleSettings& lhs,
               const StepSequencerScaleSettings& rhs) {
    return lhs.root == rhs.root && lhs.type == rhs.type && lhs.mode == rhs.mode;
}

void test_project_state_defaults_are_stable() {
    project::ProjectState state;

    assert(state.metadata.id[0] == '\0');
    assert(std::strcmp(state.metadata.name.data(), "untitled") == 0);
    assert(!state.metadata.dirty);
    assert(!state.metadata.hasSavedIdentity);
    assert(state.transport.tempoBpm == 120.0f);
    assert(state.transport.swingPercent == 0);
    assert(state.transport.runMode == 0);
    assert(sameScale(state.musical.scale, sequencer::defaultProjectScaleSettings()));
    assert(state.musical.patternsInheritScale);
    assert(state.musical.clipsInheritScale);
    assert(state.editing.stepPasteMode == project::PROJECT_STEP_PASTE_MODE_DEFAULT);
    assert(state.editing.ccLaneDefaultControllers ==
           project::PROJECT_CC_LANE_DEFAULT_CONTROLLERS);

    std::cout << "[PASS] test_project_state_defaults_are_stable\n";
}

void test_snapshot_project_tracks_keep_track_specific_routes() {
    project::ProjectSnapshot snapshot;
    for (uint8_t i = 0; i < snapshot.projectTracks.midiChannels.size(); ++i) {
        assert(snapshot.projectTracks.midiChannels[i] == i);
    }

    std::cout << "[PASS] Project Track snapshot keeps track-specific routes\n";
}

void configureProjectSession(core::state::CoreState& state) {
    std::strncpy(
        state.project.metadata.id.data(),
        "p123",
        state.project.metadata.id.size() - 1
    );
    std::strncpy(
        state.project.metadata.name.data(),
        "p123",
        state.project.metadata.name.size() - 1
    );
    state.project.metadata.modifiedCounter = 42;
    state.project.metadata.dirty = true;
    state.project.metadata.hasSavedIdentity = true;

    state.statusBar.tempo.set(137.0f);
    state.statusBar.tempoDisplay.set(137.0f);
    state.projectNavigation.transportSwingPercent = 23;
    state.projectNavigation.transportRunMode = 2;
    state.projectNavigation.patternsInheritScale = false;
    state.projectNavigation.clipsInheritScale = true;
    state.projectNavigation.stepPasteMode = project::ProjectStepPasteMode::PAGE;
    state.projectNavigation.ccLaneDefaultControllers = {0U, 23U, 99U, 127U};

    StepSequencerScaleSettings scale{
        .root = 2,
        .type = StepSequencerScaleType::WholeTone,
        .mode = StepSequencerScaleConstraintMode::ConstrainDown,
    };
    assert(state.sequencerTracks.setProjectScaleSettings(scale));

    assert(state.setSharedTrackState(0x0003, 1));
    auto& page = state.pages.activePageData();
    std::strncpy(page.name, "Snapshot", sizeof(page.name) - 1);
    page.setMacroActive(0, true);
    page.cc[0] = 74;
    page.values[0] = 0.75f;

    const auto automationAddress = core::state::macro::MacroAutomationSlotAddress{
        .track = state.pages.currentActiveTrack(),
        .page = state.pages.currentActivePage(),
        .macro = 0,
    };
    core::state::macro::MacroAutomationLane automation;
    assert(core::state::macro::macroAutomationAppendPoint(automation, 0.0f, 0.2f));
    assert(core::state::macro::macroAutomationAppendPoint(automation, 2.0f, 0.8f));
    assert(test_support::project_control::assignAutomation(
        state.pages.control,
        automationAddress,
        automation
    ));
    assert(core::state::modulation::setProjectControlAutomationEnabled(
        state.pages.control,
        automationAddress,
        false
    ));

    test_support::project_control::ModulationShape modulation;
    modulation.durationBeats = 2.0f;
    assert(test_support::project_control::appendModulationPoint(
        modulation, 0.0f, -0.4f
    ));
    assert(test_support::project_control::appendModulationPoint(
        modulation, 2.0f, 0.3f
    ));
    assert(test_support::project_control::assignModulation(
        state.pages.control,
        automationAddress,
        modulation,
        0.37f
    ));
    auto authoredSlot = test_support::project_control::readSlot(
        state.pages.control,
        automationAddress
    );
    auto* modulationCurve = test_support::project_control::mutableCurve(
        state.pages.control,
        authoredSlot.primaryModulation.recordedShape.id
    );
    assert(modulationCurve != nullptr);
    modulationCurve->origin =
        core::state::modulation::ProjectCurveOrigin::CONVERTED_MEAN;
    assert(core::state::modulation::setProjectControlModulationEnabled(
        state.pages.control,
        automationAddress,
        false
    ));
    core::state::macro::MacroWorkflow::syncRuntimeFromActivePage(state.macros, state.pages);

    state.sequencer.pattern.setContentLength(15);
    state.sequencer.pattern.stepsPerBeat.set(6);
    assert(project::setProjectTrackMidiChannel(state.projectTracks, 1U, 9U).changed());
    state.sequencer.setStepDataAt(0, 66, 111, 88);
    state.sequencer.pattern.toggle(0);
    assert(sequencer::ensureGraphRoot(state.sequencer.pattern));
    auto* ccLanes = sequencer::ensureSequencerCcLaneBank(state.sequencer.pattern);
    assert(ccLanes != nullptr);
    sequencer::SequencerCcLaneDraft ccLane{};
    ccLane.destination.controller = 74U;
    assert(sequencer::createSequencerCcLane(*ccLanes, 0U, ccLane).changed());
    state.sequencer.focusedStep.set(0);
    state.sequencer.page.set(0);
}

void test_snapshot_capture_apply_restores_project_session() {
    test_support::CoreStorages storages;
    auto state = makeCoreState(storages);
    configureProjectSession(state);

    project::ProjectSnapshot snapshot;
    assert(project::captureProjectSnapshot(state, snapshot));

    const auto capturedProjectAddress = core::state::macro::MacroAutomationSlotAddress{
        .track = state.pages.currentActiveTrack(),
        .page = state.pages.currentActivePage(),
        .macro = 0,
    };
    assert(state.macroUi.manualOverrides.activate(capturedProjectAddress, 0.91f) ==
           core::state::macro::MacroManualOverrideState::ActivateStatus::ACTIVATED);

    assert(state.resetMusicalProject() ==
           core::state::ProjectResetOutcome::Completed);
    assert(state.statusBar.tempo.get() == 120.0f);
    assert(state.sequencer.pattern.length.get() == sequencer::SequencerPatternState::DEFAULT_LENGTH);
    assert(state.sharedTrackActive.get() == 0);
    assert(state.macroUi.manualOverrides.entryCount == 0);

    const auto resetProjectAddress = core::state::macro::MacroAutomationSlotAddress{
        .track = state.pages.currentActiveTrack(),
        .page = state.pages.currentActivePage(),
        .macro = 0,
    };
    assert(state.macroUi.manualOverrides.activate(resetProjectAddress, 0.13f) ==
           core::state::macro::MacroManualOverrideState::ActivateStatus::ACTIVATED);

    assert(project::applyProjectSnapshot(state, snapshot));
    assert(state.macroUi.manualOverrides.entryCount == 0);

    assert(std::strcmp(state.project.metadata.id.data(), "p123") == 0);
    assert(std::strcmp(state.project.metadata.name.data(), "p123") == 0);
    assert(state.project.metadata.modifiedCounter == 42);
    assert(state.project.metadata.dirty);
    assert(state.project.metadata.hasSavedIdentity);

    assert(state.statusBar.tempo.get() == 137.0f);
    assert(state.statusBar.tempoDisplay.get() == 137.0f);
    assert(state.projectNavigation.transportSwingPercent == 23);
    assert(state.projectNavigation.transportRunMode == 2);
    assert(!state.projectNavigation.patternsInheritScale);
    assert(state.projectNavigation.clipsInheritScale);
    assert(state.projectNavigation.stepPasteMode == project::ProjectStepPasteMode::PAGE);
    assert((state.projectNavigation.ccLaneDefaultControllers ==
            std::array<uint8_t, 4>{0U, 23U, 99U, 127U}));

    StepSequencerScaleSettings expectedScale{
        .root = 2,
        .type = StepSequencerScaleType::WholeTone,
        .mode = StepSequencerScaleConstraintMode::ConstrainDown,
    };
    assert(sameScale(state.sequencerTracks.projectScaleSettings(), expectedScale));

    assert(state.sharedTrackEnabledMask.get() == 0x0003);
    assert(state.sharedTrackActive.get() == 1);
    assert(std::strcmp(state.pages.activePageData().name, "Snapshot") == 0);
    assert(state.pages.activePageData().cc[0] == 74);
    assert(state.pages.activePageData().values[0] == 0.75f);
    assert(state.pages.activeConfigs[0].cc == 74);
    assert(state.macros.slots[0].value.get() == 0.75f);

    const auto restoredAutomation = test_support::project_control::readSlot(
        state.pages.control,
        core::state::macro::MacroAutomationSlotAddress{
            .track = state.pages.currentActiveTrack(),
            .page = state.pages.currentActivePage(),
            .macro = 0,
        }
    );
    assert(restoredAutomation.automation.stored());
    assert(!restoredAutomation.automation.enabled);
    assert(restoredAutomation.modulationCount > 0U);
    assert(!restoredAutomation.primaryModulation.enabled);
    assert(restoredAutomation.primaryModulation.recordedShape.spec.origin ==
           core::state::modulation::ProjectCurveOrigin::CONVERTED_MEAN);
    assert(restoredAutomation.primaryModulation.amount > 0.3699f &&
           restoredAutomation.primaryModulation.amount < 0.3701f);

    assert(state.sequencer.pattern.length.get() == 15);
    assert(state.sequencer.pattern.stepsPerBeat.get() == 6);
    assert(state.projectTracks.authored.midiChannels[1] == 9U);
    assert(state.sequencer.pattern.isEnabled(0));
    assert(state.sequencer.pattern.note[0] == 66);
    assert(state.sequencer.pattern.velocity[0] == 111);
    assert(state.sequencer.pattern.gate[0] == 88);

    std::cout << "[PASS] test_snapshot_capture_apply_restores_project_session\n";
}

void test_snapshot_project_track_authority_wins_on_apply() {
    test_support::CoreStorages storages;
    auto state = makeCoreState(storages);
    configureProjectSession(state);

    project::ProjectSnapshot snapshot;
    assert(project::captureProjectSnapshot(state, snapshot));
    snapshot.projectTracks.midiChannels[1] = 12;

    assert(state.resetMusicalProject() ==
           core::state::ProjectResetOutcome::Completed);
    const auto beforeApply = state.projectSessionSaveToken();
    assert(project::applyProjectSnapshot(state, snapshot));

    assert(state.sequencerTracks.activeTrackIndex() == 1);
    assert(state.projectTracks.authored.midiChannels[1] == 12);
    assert(state.projectSessionSaveToken().session != beforeApply.session);

    std::cout << "[PASS] test_snapshot_project_track_authority_wins_on_apply\n";
}

void test_incremental_capture_completes_the_same_snapshot_contract() {
    test_support::CoreStorages storages;
    auto state = makeCoreState(storages);
    configureProjectSession(state);

    project::ProjectSnapshot snapshot;
    project::ProjectSnapshotCapture capture;
    assert(capture.begin(state, snapshot));

    uint16_t advances = 0U;
    uint32_t maxSmallSlice = 0U;
    uint32_t maxSequencerSlice = 0U;
    project::ProjectSnapshotCapture::Progress complete{};
    while (capture.active() && advances < 192U) {
        const auto sliceKind = capture.nextSliceKind();
        const auto progress = capture.advance();
        ++advances;
        assert(progress.modifiedCounter == 42U);
        if (sliceKind == project::ProjectSnapshotCapture::SliceKind::SEQUENCER) {
            maxSequencerSlice = std::max(maxSequencerSlice, progress.workBytes);
            assert(progress.workBytes <= 16384U);
        } else {
            maxSmallSlice = std::max(maxSmallSlice, progress.workBytes);
            assert(progress.workBytes <= 4096U);
        }
        if (progress.status == project::ProjectSnapshotCapture::Status::COMPLETE) {
            complete = progress;
            break;
        }
        assert(progress.status ==
               project::ProjectSnapshotCapture::Status::IN_PROGRESS);
    }

    assert(complete.status == project::ProjectSnapshotCapture::Status::COMPLETE);
    assert(complete.modifiedCounter == 42);
    assert(advances > 64U);
    assert(maxSmallSlice == 4096U);
    assert(maxSequencerSlice ==
           sizeof(oc::note::sequencer::StepSequencerGraph));
    assert(!capture.active());
    assert(capture.complete());
    assert(capture.guard() != nullptr);
    assert(state.projectSessionSaveTokenMatches(capture.guard()->token));
    assert(snapshot.project.metadata.modifiedCounter == 42);
    assert(snapshot.macroTracks[1].pages[0].cc[0] == 74);
    assert(snapshot.sequencer.flat.tracks[1].note[0] == 66);

    std::cout << "[PASS] test_incremental_capture_completes_the_same_snapshot_contract\n";
}

void test_incremental_capture_rejects_a_mixed_revision() {
    test_support::CoreStorages storages;
    auto state = makeCoreState(storages);
    configureProjectSession(state);

    project::ProjectSnapshot snapshot;
    project::ProjectSnapshotCapture capture;
    assert(capture.begin(state, snapshot));
    assert(capture.advance().status ==
           project::ProjectSnapshotCapture::Status::IN_PROGRESS);

    state.sequencer.setStepDataAt(0, 77, 111, 88);
    state.markProjectMutated();

    const auto stale = capture.advance();
    assert(stale.status == project::ProjectSnapshotCapture::Status::STALE);
    assert(stale.modifiedCounter == 42);
    assert(!capture.active());

    std::cout << "[PASS] test_incremental_capture_rejects_a_mixed_revision\n";
}

void test_incremental_capture_rejects_a_same_mutation_second_request() {
    test_support::CoreStorages storages;
    auto state = makeCoreState(storages);
    configureProjectSession(state);
    state.markProjectMutated();
    const auto firstToken = state.projectSessionSaveToken();

    project::ProjectSnapshot snapshot;
    project::ProjectSnapshotCapture capture;
    assert(capture.begin(state, snapshot));
    assert(capture.advance().status ==
           project::ProjectSnapshotCapture::Status::IN_PROGRESS);

    const auto secondToken = state.requestProjectSessionSave();
    assert(secondToken.session == firstToken.session);
    assert(secondToken.mutationEpoch == firstToken.mutationEpoch);
    assert(secondToken.modifiedCounter == firstToken.modifiedCounter);
    assert(secondToken.requestId == firstToken.requestId + 1U);

    const auto stale = capture.advance();
    assert(stale.status == project::ProjectSnapshotCapture::Status::STALE);
    assert(stale.modifiedCounter == firstToken.modifiedCounter);
    assert(!capture.active());
    assert(capture.guard() == nullptr);

    std::cout
        << "[PASS] incremental capture rejects same-mutation request\n";
}

void test_incremental_capture_rejects_an_authored_only_revision_change() {
    test_support::CoreStorages storages;
    auto state = makeCoreState(storages);
    configureProjectSession(state);

    project::ProjectSnapshot snapshot;
    project::ProjectSnapshotCapture capture;
    assert(capture.begin(state, snapshot));
    assert(capture.advance().status ==
           project::ProjectSnapshotCapture::Status::IN_PROGRESS);

    state.pages.control.markAuthoredMutation();
    const auto stale = capture.advance();
    assert(stale.status == project::ProjectSnapshotCapture::Status::STALE);
    assert(stale.modifiedCounter == 42U);
    assert(!capture.active());

    std::cout
        << "[PASS] incremental capture rejects authored-only revision change\n";
}

void test_incremental_capture_rejects_a_project_track_revision_change() {
    test_support::CoreStorages storages;
    auto state = makeCoreState(storages);
    configureProjectSession(state);

    project::ProjectSnapshot snapshot;
    project::ProjectSnapshotCapture capture;
    assert(capture.begin(state, snapshot));
    assert(capture.advance().status ==
           project::ProjectSnapshotCapture::Status::IN_PROGRESS);

    assert(project::setProjectTrackDelayMs(state.projectTracks, 1U, -12).changed());
    const auto stale = capture.advance();
    assert(stale.status == project::ProjectSnapshotCapture::Status::STALE);
    assert(stale.modifiedCounter == 42U);
    assert(!capture.active());

    std::cout
        << "[PASS] incremental capture rejects Project Track revision change\n";
}

void test_snapshot_rejects_invalid_project_tracks_before_live_mutation() {
    test_support::CoreStorages storages;
    auto state = makeCoreState(storages);
    configureProjectSession(state);

    project::ProjectSnapshot invalid;
    assert(project::captureProjectSnapshot(state, invalid));
    invalid.projectTracks.midiChannels[1] = 16U;

    const auto beforeTracks = state.projectTracks.authored;
    const uint8_t beforeNote = state.sequencer.pattern.note[0];
    const uint32_t beforeModified = state.project.metadata.modifiedCounter;
    const auto beforeToken = state.projectSessionSaveToken();
    assert(!project::applyProjectSnapshot(state, invalid));
    assert(project::sameProjectTrackSnapshot(state.projectTracks.authored, beforeTracks));
    assert(state.sequencer.pattern.note[0] == beforeNote);
    assert(state.project.metadata.modifiedCounter == beforeModified);
    assert(state.projectSessionSaveToken() == beforeToken);

    std::cout << "[PASS] invalid Project Tracks fail before live mutation\n";
}

void test_snapshot_rejects_active_project_track_gesture_before_live_mutation() {
    test_support::CoreStorages storages;
    auto state = makeCoreState(storages);
    configureProjectSession(state);

    project::ProjectSnapshot incoming;
    assert(project::captureProjectSnapshot(state, incoming));

    assert(state.resetMusicalProject() ==
           core::state::ProjectResetOutcome::Completed);
    std::strncpy(
        state.project.metadata.name.data(),
        "outgoing",
        state.project.metadata.name.size() - 1U
    );
    state.statusBar.tempo.set(101.0f);
    state.sequencer.pattern.note[0] = 91U;

    auto tracks = project::ProjectTrackDomainServices::fromCoreState(state);
    assert(tracks.beginGesture(project::ProjectTrackHistoryActionKind::Delay, 3U));
    assert(tracks.setDelayMs(3U, 42));
    assert(tracks.hasActiveGesture());

    const auto tracksBefore = state.projectTracks.authored;
    const auto tokenBefore = state.projectSessionSaveToken();
    assert(!project::applyProjectSnapshot(state, incoming));

    assert(tracks.hasActiveGesture());
    assert(std::strcmp(state.project.metadata.name.data(), "outgoing") == 0);
    assert(state.statusBar.tempo.get() == 101.0f);
    assert(state.sequencer.pattern.note[0] == 91U);
    assert(project::sameProjectTrackSnapshot(state.projectTracks.authored, tracksBefore));
    assert(state.projectSessionSaveToken() == tokenBefore);

    assert(tracks.cancelGesture());
    test_support::drainNotifications();
    std::cout << "[PASS] active Project Track gesture rejects load before mutation\n";
}

void test_snapshot_apply_consumes_deferred_mutation_callbacks() {
    test_support::CoreStorages sourceStorages;
    test_support::CoreStorages liveStorages;
    auto source = makeCoreState(sourceStorages);
    auto live = makeCoreState(liveStorages);
    configureProjectSession(source);

    project::ProjectSnapshot incoming;
    assert(project::captureProjectSnapshot(source, incoming));
    incoming.project.metadata.modifiedCounter = 17U;
    incoming.project.metadata.dirty = false;

    test_support::drainNotifications();
    live.flush();
    test_support::drainNotifications();

    assert(project::applyProjectSnapshot(live, incoming));
    const auto appliedToken = live.projectSessionSaveToken();
    assert(live.project.metadata.modifiedCounter == 17U);
    assert(!live.project.metadata.dirty);
    assert(!live.hasPendingProjectSessionSave());

    test_support::drainNotifications();
    assert(!live.hasPendingProjectMutationCoalescing());
    live.flush();
    test_support::drainNotifications();

    assert(live.project.metadata.modifiedCounter == 17U);
    assert(!live.project.metadata.dirty);
    assert(live.projectSessionSaveToken() == appliedToken);
    assert(!live.hasPendingProjectSessionSave());

    std::cout << "[PASS] Project load callbacks do not dirty loaded snapshot\n";
}

void test_snapshot_boundaries_reject_an_active_modulator_audition() {
    test_support::CoreStorages storages;
    auto state = makeCoreState(storages);
    configureProjectSession(state);

    project::ProjectSnapshot baseline;
    assert(project::captureProjectSnapshot(state, baseline));
    const auto authoredBefore = state.pages.control.authored;
    const uint32_t revisionBefore = state.pages.control.authoredRevision;

    const auto begun = beginLfoAudition(state);
    assert(begun.changed());
    assert(state.pages.control.audition.active());
    assert(state.hasPendingProjectTransaction());
    assert(state.pages.control.authoredRevision != revisionBefore);

    project::ProjectSnapshot blockedSnapshot;
    project::ProjectSnapshotCapture blockedCapture;
    assert(!blockedCapture.begin(state, blockedSnapshot));
    assert(!project::captureProjectSnapshot(state, blockedSnapshot));

    const auto auditionAuthored = state.pages.control.authored;
    const uint32_t auditionRevision = state.pages.control.authoredRevision;
    assert(!project::applyProjectSnapshot(state, baseline));
    assert(state.pages.control.audition.active());
    assert(state.pages.control.authoredRevision == auditionRevision);
    assert(std::memcmp(
        &state.pages.control.authored,
        &auditionAuthored,
        sizeof(auditionAuthored)
    ) == 0);

    assert(state.macroHistory.abortPendingModulatorAudition(state.pages));
    assert(!state.pages.control.audition.active());
    assert(!state.hasPendingProjectTransaction());
    assert(state.pages.control.authoredRevision == revisionBefore);
    assert(std::memcmp(
        &state.pages.control.authored,
        &authoredBefore,
        sizeof(authoredBefore)
    ) == 0);

    std::cout
        << "[PASS] snapshot boundaries reject an active modulator audition\n";
}

void test_incremental_capture_becomes_stale_when_an_audition_starts() {
    test_support::CoreStorages storages;
    auto state = makeCoreState(storages);
    configureProjectSession(state);

    project::ProjectSnapshot snapshot;
    project::ProjectSnapshotCapture capture;
    assert(capture.begin(state, snapshot));
    assert(capture.advance().status ==
           project::ProjectSnapshotCapture::Status::IN_PROGRESS);

    const auto begun = beginLfoAudition(state);
    assert(begun.changed());
    const auto stale = capture.advance();
    assert(stale.status == project::ProjectSnapshotCapture::Status::STALE);
    assert(stale.modifiedCounter == 42U);
    assert(!capture.active());

    assert(state.macroHistory.abortPendingModulatorAudition(state.pages));
    assert(!state.hasPendingProjectTransaction());

    std::cout
        << "[PASS] incremental capture becomes stale when audition starts\n";
}

void test_clear_project_history_rolls_back_audition_before_clearing() {
    test_support::CoreStorages storages;
    auto state = makeCoreState(storages);
    configureProjectSession(state);

    const auto authoredBefore = state.pages.control.authored;
    const uint32_t revisionBefore = state.pages.control.authoredRevision;
    const auto begun = beginLfoAudition(state);
    assert(begun.changed());
    assert(state.pages.control.audition.active());

    assert(state.clearProjectHistory());
    assert(!state.pages.control.audition.active());
    assert(!state.hasPendingProjectTransaction());
    assert(state.pages.control.authoredRevision == revisionBefore);
    assert(std::memcmp(
        &state.pages.control.authored,
        &authoredBefore,
        sizeof(authoredBefore)
    ) == 0);
    assert(!state.macroHistory.canUndo());
    assert(!state.macroHistory.canRedo());
    assert(!state.projectHistory.canUndo());
    assert(!state.projectHistory.canRedo());

    std::cout
        << "[PASS] clear Project history rolls back audition before clearing\n";
}

void test_inconsistent_audition_state_is_blocked_fail_closed() {
    test_support::CoreStorages storages;
    auto state = makeCoreState(storages);
    configureProjectSession(state);

    const auto begun = beginLfoAudition(state);
    assert(begun.changed());
    const auto validAudition = state.pages.control.audition;
    state.pages.control.audition = {};

    assert(state.hasPendingProjectTransaction());
    project::ProjectSnapshot snapshot;
    project::ProjectSnapshotCapture capture;
    assert(!capture.begin(state, snapshot));
    assert(!state.clearProjectHistory());

    state.pages.control.audition = validAudition;
    assert(state.macroHistory.abortPendingModulatorAudition(state.pages));

    state.pages.control.audition.mode = static_cast<
        modulation::ProjectModulatorSourceSessionMode
    >(0xFFU);
    assert(state.hasPendingProjectTransaction());
    assert(!capture.begin(state, snapshot));
    assert(!state.clearProjectHistory());

    state.pages.control.audition = {};
    assert(state.clearProjectHistory());
    assert(!state.hasPendingProjectTransaction());

    std::cout << "[PASS] inconsistent audition state is blocked fail-closed\n";
}

void test_snapshot_apply_reconciles_a_removed_modulator_selection() {
    test_support::CoreStorages storages;
    auto state = makeCoreState(storages);
    configureProjectSession(state);

    project::ProjectSnapshot baseline;
    assert(project::captureProjectSnapshot(state, baseline));

    modulation::ModulatorLfoDraft source{};
    source.name = "Removed by load";
    source.parameters.periodTicks = modulation::PROJECT_CONTROL_TICKS_PER_BEAT;
    const auto created = state.macroHistory.createUnassignedLfo(state.pages, source);
    assert(created.changed());
    state.projectNavigation.activeTab.set(project::ProjectTab::MODULATORS);
    state.projectNavigation.currentNode.set(project::ProjectNodeId::MODULATOR_SOURCE_DETAIL);
    state.projectNavigation.depth.set(1U);
    state.projectNavigation.pathStack[0] = project::ProjectNodeId::MODULATORS_ROOT;
    state.projectNavigation.selectedModulator = created.sourceId;

    assert(project::applyProjectSnapshot(state, baseline));
    assert(state.projectNavigation.currentNode.get() ==
           project::ProjectNodeId::MODULATORS_ROOT);
    assert(!modulation::valid(state.projectNavigation.selectedModulator));
    assert(!modulation::valid(state.projectNavigation.selectedModulationBinding));

    std::cout
        << "[PASS] snapshot apply reconciles removed Modulator selection\n";
}

void test_full_project_reset_discards_an_unrecoverable_audition_pair() {
    test_support::CoreStorages storages;
    auto state = makeCoreState(storages);
    configureProjectSession(state);

    assert(beginLfoAudition(state).changed());
    state.pages.control.audition = {};
    assert(state.hasPendingProjectTransaction());

    assert(state.resetMusicalProject() ==
           core::state::ProjectResetOutcome::Completed);
    assert(!state.hasPendingProjectTransaction());
    assert(!state.pages.control.audition.active());
    assert(state.pages.control.authored.modulation.sourceCount == 0U);
    assert(state.pages.control.authored.modulation.outputBindingCount == 0U);
    assert(!state.macroHistory.canUndo());
    assert(!state.sequencerHistory.canUndo());
    assert(!state.projectHistory.canUndo());

    std::cout
        << "[PASS] full Project reset discards unrecoverable audition pair\n";
}

void test_project_load_and_reset_reject_an_active_step_draft() {
    test_support::CoreStorages storages;
    auto state = makeCoreState(storages);

    project::ProjectSnapshot baseline;
    assert(project::captureProjectSnapshot(state, baseline));
    state.sequencer.pattern.note[0] = 93;
    assert(sequencer::beginStepContentDraft(
        state.sequencer,
        sequencer::SequencerStepContentDraftKind::MICRO_SEQUENCE,
        0
    ));

    const auto beforeBlockedTransitions = state.projectSessionSaveToken();
    assert(!project::applyProjectSnapshot(state, baseline));
    assert(state.sequencer.stepContentDraft.active.get());
    assert(state.sequencer.pattern.note[0] == 93);
    assert(state.sequencer.stepContentDraft.failure ==
           sequencer::SequencerStepContentDraftFailure::TRANSITION_BLOCKED);
    assert(state.sequencer.stepContentDraft.blockedTransition ==
           sequencer::SequencerStepContentDraftBlockedTransition::PROJECT_LOAD);

    assert(state.resetMusicalProject() ==
           core::state::ProjectResetOutcome::DraftActive);
    assert(state.sequencer.stepContentDraft.active.get());
    assert(state.sequencer.pattern.note[0] == 93);
    assert(state.sequencer.stepContentDraft.blockedTransition ==
           sequencer::SequencerStepContentDraftBlockedTransition::RESET);
    assert(state.projectSessionSaveToken() == beforeBlockedTransitions);

    std::cout
        << "[PASS] Project load/reset reject an active Step draft\n";
}

}  // namespace

int main() {
    std::cout << "==============================================\n";
    std::cout << "ProjectSnapshot tests\n";
    std::cout << "==============================================\n\n";

    test_project_state_defaults_are_stable();
    test_snapshot_project_tracks_keep_track_specific_routes();
    test_snapshot_capture_apply_restores_project_session();
    test_snapshot_project_track_authority_wins_on_apply();
    test_incremental_capture_completes_the_same_snapshot_contract();
    test_incremental_capture_rejects_a_mixed_revision();
    test_incremental_capture_rejects_a_same_mutation_second_request();
    test_incremental_capture_rejects_an_authored_only_revision_change();
    test_incremental_capture_rejects_a_project_track_revision_change();
    test_snapshot_rejects_invalid_project_tracks_before_live_mutation();
    test_snapshot_rejects_active_project_track_gesture_before_live_mutation();
    test_snapshot_apply_consumes_deferred_mutation_callbacks();
    test_snapshot_boundaries_reject_an_active_modulator_audition();
    test_incremental_capture_becomes_stale_when_an_audition_starts();
    test_clear_project_history_rolls_back_audition_before_clearing();
    test_inconsistent_audition_state_is_blocked_fail_closed();
    test_snapshot_apply_reconciles_a_removed_modulator_selection();
    test_full_project_reset_discards_an_unrecoverable_audition_pair();
    test_project_load_and_reset_reject_an_active_step_draft();

    std::cout << "\n==============================================\n";
    std::cout << "All tests passed\n";
    std::cout << "==============================================\n";
    return 0;
}
