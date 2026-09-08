#include <cassert>
#include <cstdint>
#include <iostream>

#include "../../src/state/CoreState.hpp"
#include "../../src/state/macro/MacroWorkflow.hpp"
#include "../../src/state/project/ProjectTrackDomainOps.hpp"
#include "../support/CoreStorages.hpp"
#include "../support/NotificationTestUtils.hpp"

namespace {

using test_support::CoreStorages;
using test_support::drainNotifications;

void test_core_state_owns_only_settings_storage() {
    CoreStorages storage;

    {
        core::state::CoreState state(storage.settings);
        const auto& initial =
            core::state::macro::MacroWorkflow::activeConfig(state.pages, 0U);
        const uint8_t nextCc =
            initial.cc < 127U ? static_cast<uint8_t>(initial.cc + 1U)
                              : static_cast<uint8_t>(initial.cc - 1U);
        assert(core::state::macro::MacroWorkflow::setConfig(
            state,
            0U,
            state.projectTracks.authored.midiChannels[0],
            nextCc
        ));
        state.sequencer.pattern().setContentLength(8U);
        assert(state.sequencer.setStepDataAt(0U, 72U, 111U, 75U));
        state.sequencer.pattern().setEnabled(0U, true);
        state.flush();
    }

    core::state::CoreState restored(storage.settings);
    assert(restored.sequencer.pattern().note[0] != 72U ||
           restored.sequencer.pattern().velocity[0] != 111U ||
           restored.sequencer.pattern().gate[0] != 75U ||
           !restored.sequencer.pattern().isEnabled(0U));

    drainNotifications();
    std::cout << "[PASS] CoreState persists settings only; projects/presets are file based\n";
}

void test_macro_config_change_marks_project_and_revision() {
    CoreStorages storage;
    core::state::CoreState state(storage.settings);

    const auto& initial =
        core::state::macro::MacroWorkflow::activeConfig(state.pages, 0U);
    const uint8_t activeTrack = state.pages.currentActiveTrack();
    const uint8_t initialChannel =
        state.projectTracks.authored.midiChannels[activeTrack];
    const uint32_t initialRevision = state.configRevision.get();
    const uint8_t nextChannel = static_cast<uint8_t>((initialChannel + 1U) % 16U);
    const uint8_t nextCc =
        initial.cc < 127U ? static_cast<uint8_t>(initial.cc + 1U)
                          : static_cast<uint8_t>(initial.cc - 1U);

    assert(core::state::macro::MacroWorkflow::setConfig(
        state,
        0U,
        nextChannel,
        nextCc
    ));
    assert(state.configRevision.get() ==
           core::state::macro::nextMacroConfigRevision(
               initialRevision,
               core::state::macro::kMacroConfigDirtyAll
           ));
    assert(state.project.metadata.dirty);
    assert(state.hasPendingProjectSessionSave());

    drainNotifications();
    std::cout << "[PASS] Macro edits remain Project mutations without a slot store\n";
}

void test_device_settings_recovery_does_not_persist_project_track_state() {
    CoreStorages storage;

    {
        core::state::CoreState state(storage.settings);
        storage.settings.setAvailable(false);
        state.midiSync.mode.set(core::state::MidiSyncMode::SLAVE);
        state.midiSync.followTransport.set(false);
        state.midiSync.autoFallbackMs.set(750U);
        state.midiSync.autoLockClockCount.set(12U);
        assert(state.setSharedTrackState(0x0003U, 1U));
        state.flush();

        storage.settings.setAvailable(true);
        assert(state.recoverSettingsFromRamAfterStorageReopen() ==
               core::persistence::PersistenceWriteStatus::OK);
    }

    core::state::CoreState restored(storage.settings);
    assert(restored.midiSync.mode.get() == core::state::MidiSyncMode::SLAVE);
    assert(!restored.midiSync.followTransport.get());
    assert(restored.midiSync.autoFallbackMs.get() == 750U);
    assert(restored.midiSync.autoLockClockCount.get() == 12U);
    assert(restored.currentSharedTrackEnabledMask() == 0x0001U);
    assert(restored.currentSharedActiveTrack() == 0U);

    drainNotifications();
    std::cout
        << "[PASS] settings recovery persists only device settings; Track stays Project-owned\n";
}

void test_new_project_boundary_resets_runtime_activation() {
    CoreStorages storage;
    core::state::CoreState state(storage.settings);
    core::state::sequencer::SequencerTrackActivationBatch activation;
    assert(state.sequencerTrackActivations.prepare(
        0x0001U,
        core::state::project::audibleMask(
            state.projectTracks,
            state.currentSharedTrackEnabledMask()
        ),
        true,
        activation
    ));
    assert(state.sequencerTrackActivations.armPrepared(activation));
    state.sequencerTrackActivations.publishPrepared(activation);
    const uint32_t revisionBefore = state.sequencerRuntimeProjectRevision.get();

    assert(state.resetMusicalProject() ==
           core::state::ProjectResetOutcome::Completed);
    assert(state.sequencerRuntimeProjectRevision.get() != revisionBefore);
    assert(state.sequencerTrackActivations.pendingTrackMask() == 0U);
    assert(state.sequencerTrackActivations.telemetry(0U).status ==
           core::state::sequencer::SequencerTrackActivationStatus::IDLE);

    drainNotifications();
    std::cout << "[PASS] new Project remains a clean runtime boundary\n";
}

void test_queued_project_changes_survive_pattern_selection() {
    CoreStorages storage;
    core::state::CoreState state(storage.settings);
    auto& queue = oc::state::NotificationQueue::instance();
    queue.setDeferredMode(true);
    drainNotifications();
    state.flushProjectMutationCoalescing();
    drainNotifications();

    // Select directly to isolate rebinding from navigation's own mutations.
    // Every stable source must retain an already queued save notification.
    for (unsigned source = 0; source < 8; ++source) {
        const auto before = state.project.metadata.modifiedCounter;
        switch (source) {
        case 0: state.sequencer.page.notify(); break;
        case 1: state.sequencer.focusedStep.notify(); break;
        case 2: state.sequencer.activeStepProperty.notify(); break;
        case 3: state.sequencerTracks.activeTrackSignal().notify(); break;
        case 4: state.sequencerTracks.enabledMaskSignal().notify(); break;
        case 5: state.sequencerTracks.projectScaleRevisionSignal().notify(); break;
        case 6: state.sequencerTracks.drumRevisionSignal().notify(); break;
        case 7: state.sequencerClips.revisionSignal().notify(); break;
        }
        const uint8_t track = static_cast<uint8_t>((source + 1) % 2);
        state.sequencer.selectPattern(
            state.sequencerTracks.track(track), state.sequencerTracks.clip(track));
        drainNotifications();
        state.flushProjectMutationCoalescing();
        drainNotifications();
        assert(state.project.metadata.modifiedCounter == before + 1);
    }
    std::cout << "[PASS] all stable autosave sources survive selection while queued\n";
}

}  // namespace

int main() {
    test_core_state_owns_only_settings_storage();
    test_macro_config_change_marks_project_and_revision();
    test_device_settings_recovery_does_not_persist_project_track_state();
    test_new_project_boundary_resets_runtime_activation();
    test_queued_project_changes_survive_pattern_selection();
    return 0;
}
