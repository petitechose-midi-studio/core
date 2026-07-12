#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>

#include <oc/time/Time.hpp>

#include "../../src/handler/macro/MacroEditDomainServices.hpp"
#include "../../src/handler/macro/MacroPerformanceDomainServices.hpp"
#include "../../src/state/CoreState.hpp"
#include "../../src/state/macro/MacroWorkflow.hpp"
#include "../support/CoreStorages.hpp"
#include "../support/NotificationTestUtils.hpp"

namespace {

uint32_t g_mock_now_ms = 1000;

uint32_t mockTimeMs() {
    return g_mock_now_ms;
}

using test_support::CoreStorages;
using test_support::drainNotifications;

void fillAutomationPointPoolExcept(
    core::state::macro::MacroAutomationBankState& bank,
    const core::state::macro::MacroAutomationSlotAddress& excluded
) {
    core::state::macro::MacroAutomationLane lane;
    lane.active = true;
    lane.durationBeats = 256.0f;
    for (uint16_t i = 0;
         i < core::state::macro::MACRO_AUTOMATION_RECORDING_MAX_POINTS;
         ++i) {
        assert(core::state::macro::macroAutomationAppendPoint(
            lane,
            static_cast<float>(i) * 0.125f,
            (i & 1U) == 0U ? 0.25f : 0.75f
        ));
    }

    for (uint8_t track = 0;
         track < core::state::macro::TRACK_COUNT &&
         bank.pointPool.used < core::state::macro::MACRO_AUTOMATION_POINT_POOL_CAPACITY;
         ++track) {
        for (uint8_t macro = 0;
             macro < core::state::macro::MACRO_COUNT &&
             bank.pointPool.used < core::state::macro::MACRO_AUTOMATION_POINT_POOL_CAPACITY;
             ++macro) {
            const auto address = core::state::macro::MacroAutomationSlotAddress{
                .track = track,
                .page = 0,
                .macro = macro,
            };
            if (core::state::macro::macroAutomationAddressEquals(address, excluded)) continue;
            auto* slot = core::state::macro::macroAutomationGetOrCreateSlot(bank, address);
            assert(slot != nullptr);
            assert(core::state::macro::macroAutomationAssignAutomation(bank, *slot, lane));
        }
    }
    assert(bank.pointPool.used == core::state::macro::MACRO_AUTOMATION_POINT_POOL_CAPACITY);
}

void test_runtime_values_are_forwarded_and_clamped() {
    CoreStorages storage;

    core::state::CoreState state(storage.settings,
                                 storage.macroLibrary,
                                 storage.sequencerPatternLibrary,
                                 storage.sequencerSetLibrary);
    const auto services = core::handler::MacroPerformanceDomainServices::fromCoreState(state);

    services.setResolvedValue(0, 1.5f);
    services.setResolvedValue(1, -0.5f);

    assert(std::fabs(services.runtimeValue(0) - 1.0f) < 0.0001f);
    assert(std::fabs(services.runtimeValue(1) - 0.0f) < 0.0001f);

    drainNotifications();
    state.flush();

    std::cout << "[PASS] test_runtime_values_are_forwarded_and_clamped\n";
}

void test_manual_value_updates_base_and_stages_project_mutation() {
    CoreStorages storage;

    core::state::CoreState state(storage.settings,
                                 storage.macroLibrary,
                                 storage.sequencerPatternLibrary,
                                 storage.sequencerSetLibrary);
    const auto services = core::handler::MacroPerformanceDomainServices::fromCoreState(state);

    services.setManualValue(0, 0.75f);

    assert(std::fabs(services.runtimeValue(0) - 0.75f) < 0.0001f);
    assert(std::fabs(state.pages.activePageData().values[0] - 0.75f) < 0.0001f);
    assert(state.hasPendingProjectMutationCoalescing());
    assert(!state.project.metadata.dirty);

    state.flushProjectMutationCoalescing();
    assert(!state.hasPendingProjectMutationCoalescing());
    assert(state.project.metadata.dirty);

    drainNotifications();

    std::cout << "[PASS] test_manual_value_updates_base_and_stages_project_mutation\n";
}

void test_config_changes_mark_project_dirty_and_bump_revision() {
    CoreStorages storage;

    uint8_t updatedChannel = 0;
    uint8_t updatedCc = 0;

    core::state::CoreState state(storage.settings,
                                 storage.macroLibrary,
                                 storage.sequencerPatternLibrary,
                                 storage.sequencerSetLibrary);
    const auto services = core::handler::MacroPerformanceDomainServices::fromCoreState(state);

    const auto initialConfig = services.activeConfig(0);
    const uint32_t initialRevision = state.configRevision.get();

    assert(!services.setConfig(0, initialConfig.channel, initialConfig.cc));
    assert(state.configRevision.get() == initialRevision);
    assert(!state.project.metadata.dirty);
    assert(!state.hasPendingProjectSessionSave());

    updatedChannel = static_cast<uint8_t>((initialConfig.channel + 1U) % 16U);
    updatedCc = static_cast<uint8_t>((initialConfig.cc < 127U) ? (initialConfig.cc + 1U)
                                                               : (initialConfig.cc - 1U));

    assert(services.setConfig(0, updatedChannel, updatedCc));
    assert(
        state.configRevision.get() ==
        core::state::macro::nextMacroConfigRevision(
            initialRevision,
            core::state::macro::kMacroConfigDirtyAll
        )
    );

    const auto updatedConfig = services.activeConfig(0);
    assert(updatedConfig.channel == updatedChannel);
    assert(updatedConfig.cc == updatedCc);
    assert(state.project.metadata.dirty);
    assert(state.hasPendingProjectSessionSave());

    core::state::CoreState restored(storage.settings,
                                    storage.macroLibrary,
                                    storage.sequencerPatternLibrary,
                                    storage.sequencerSetLibrary);
    const auto restoredServices = core::handler::MacroPerformanceDomainServices::fromCoreState(
        restored
    );
    const auto restoredConfig = restoredServices.activeConfig(0);
    assert(restoredConfig.channel != updatedChannel || restoredConfig.cc != updatedCc);

    drainNotifications();

    std::cout << "[PASS] test_config_changes_mark_project_dirty_and_bump_revision\n";
}

void test_switch_to_page_updates_runtime_status_and_marks_project_dirty() {
    CoreStorages storage;
    storage.initAll();

    core::state::CoreState state(storage.settings,
                                 storage.macroLibrary,
                                 storage.sequencerPatternLibrary,
                                 storage.sequencerSetLibrary);
    const auto services = core::handler::MacroPerformanceDomainServices::fromCoreState(state);

    std::strncpy(state.pages.activeTrackData().pages[2].name,
                 "Mix Bus",
                 core::state::macro::PAGE_NAME_SIZE - 1);
    state.pages.activeTrackData().pages[2].name[core::state::macro::PAGE_NAME_SIZE - 1] = '\0';
    state.pages.activeTrackData().pages[2].values[0] = 0.23f;

    assert(!state.project.metadata.dirty);
    assert(!state.hasPendingProjectSessionSave());
    services.switchToPage(2);

    assert(state.pages.currentActivePage() == 2);
    assert(std::strcmp(state.statusBar.pageName.get(), "Mix Bus") == 0);
    assert(std::fabs(services.runtimeValue(0) - 0.23f) < 0.0001f);
    assert(state.project.metadata.dirty);
    assert(state.hasPendingProjectSessionSave());

    drainNotifications();

    std::cout << "[PASS] test_switch_to_page_updates_runtime_status_and_marks_project_dirty\n";
}

void test_track_config_batch_requires_shared_channel_and_marks_project_dirty_when_valid() {
    CoreStorages storage;

    std::array<core::state::macro::MacroConfig, core::state::macro::MACRO_COUNT> updatedConfigs{};
    uint8_t updatedChannel = 0;

    core::state::CoreState state(storage.settings,
                                 storage.macroLibrary,
                                 storage.sequencerPatternLibrary,
                                 storage.sequencerSetLibrary);
    const auto services = core::handler::MacroPerformanceDomainServices::fromCoreState(state);
    const uint32_t initialRevision = state.configRevision.get();

    for (uint8_t i = 0; i < core::state::macro::MACRO_COUNT; ++i) {
        updatedConfigs[i] = services.activeConfig(i);
    }

    updatedConfigs[0].channel = 4;
    updatedConfigs[1].channel = 5;
    assert(!services.setTrackConfigs(updatedConfigs));
    assert(state.configRevision.get() == initialRevision);
    assert(!state.project.metadata.dirty);
    assert(!state.hasPendingProjectSessionSave());

    updatedChannel = 11;
    for (uint8_t i = 0; i < core::state::macro::MACRO_COUNT; ++i) {
        updatedConfigs[i].channel = updatedChannel;
        updatedConfigs[i].cc = static_cast<uint8_t>((32U + i) % 128U);
    }

    assert(services.setTrackConfigs(updatedConfigs));
    assert(state.configRevision.get() ==
           core::state::macro::nextMacroConfigRevision(initialRevision));
    assert(state.pages.activeTrackChannel() == updatedChannel);
    for (uint8_t i = 0; i < core::state::macro::MACRO_COUNT; ++i) {
        const auto config = services.activeConfig(i);
        assert(config.channel == updatedChannel);
        assert(config.cc == updatedConfigs[i].cc);
    }
    assert(state.project.metadata.dirty);
    assert(state.hasPendingProjectSessionSave());

    core::state::CoreState restored(storage.settings,
                                    storage.macroLibrary,
                                    storage.sequencerPatternLibrary,
                                    storage.sequencerSetLibrary);
    const auto restoredServices = core::handler::MacroPerformanceDomainServices::fromCoreState(
        restored
    );
    bool restoredAnyUpdated = false;
    for (uint8_t i = 0; i < core::state::macro::MACRO_COUNT; ++i) {
        const auto config = restoredServices.activeConfig(i);
        restoredAnyUpdated =
            restoredAnyUpdated ||
            (config.channel == updatedChannel && config.cc == updatedConfigs[i].cc);
    }
    assert(!restoredAnyUpdated);

    std::cout << "[PASS] test_track_config_batch_requires_shared_channel_and_marks_project_dirty_when_valid\n";
}

void test_status_bar_pulses_are_forwarded() {
    CoreStorages storage;
    storage.initAll();

    core::state::CoreState state(storage.settings,
                                 storage.macroLibrary,
                                 storage.sequencerPatternLibrary,
                                 storage.sequencerSetLibrary);
    const auto services = core::handler::MacroPerformanceDomainServices::fromCoreState(state);

    services.pulseCcIn();
    services.pulseCcOut();
    services.pulseNoteIn();

    assert(state.statusBar.ccInActive.get());
    assert(state.statusBar.ccOutActive.get());
    assert(state.statusBar.noteInActive.get());

    drainNotifications();

    std::cout << "[PASS] test_status_bar_pulses_are_forwarded\n";
}

void test_macro_slot_activation_is_sequential_and_marks_project_dirty() {
    CoreStorages storage;

    core::state::CoreState state(storage.settings,
                                 storage.macroLibrary,
                                 storage.sequencerPatternLibrary,
                                 storage.sequencerSetLibrary);
    const auto services = core::handler::MacroPerformanceDomainServices::fromCoreState(state);

    assert(services.isMacroSlotActive(0));
    assert(!services.isMacroSlotActive(1));
    assert(services.isMacroAddSlot(1));
    assert(!services.activateMacroSlot(2));

    const uint32_t initialRevision = state.configRevision.get();
    assert(services.activateMacroSlot(1));
    assert(services.isMacroSlotActive(1));
    assert(services.isMacroAddSlot(2));
    assert(state.pages.activePageData().activeMacroCount() == 2);
    assert(state.pages.activeConfigs[1].cc == 1);
    assert(state.configRevision.get() ==
           core::state::macro::nextMacroConfigRevision(initialRevision, 1));
    assert(state.project.metadata.dirty);

    std::cout << "[PASS] test_macro_slot_activation_is_sequential_and_marks_project_dirty\n";
}

void test_automation_recording_commits_to_current_macro_slot() {
    CoreStorages storage;

    core::state::CoreState state(storage.settings,
                                 storage.macroLibrary,
                                 storage.sequencerPatternLibrary,
                                 storage.sequencerSetLibrary);
    const auto services = core::handler::MacroPerformanceDomainServices::fromCoreState(state);

    state.statusBar.tempo.set(120.0f);
    services.setResolvedValue(0, 0.25f);

    assert(services.beginAutomationRecording(0, 1000));
    assert(state.macroUi.automationRecordingStatus.get() ==
           core::state::macro::MacroAutomationRecordingStatus::RECORDING);
    assert(services.automationRecordingActiveFor(0));
    assert(!services.beginAutomationRecording(1, 1000));
    assert(!state.project.metadata.dirty);

    assert(services.recordAutomationPoint(0, 1500, 0.75f));
    assert(services.commitAutomationRecording(2000));
    assert(state.macroUi.automationRecordingStatus.get() ==
           core::state::macro::MacroAutomationRecordingStatus::IDLE);
    assert(!services.automationRecordingActiveFor(0));
    assert(state.project.metadata.dirty);
    assert(state.hasPendingProjectSessionSave());

    const auto* slot = core::state::macro::macroAutomationFindSlot(
        state.pages.automation,
        core::state::macro::MacroAutomationSlotAddress{
            .track = state.pages.currentActiveTrack(),
            .page = state.pages.currentActivePage(),
            .macro = 0,
        }
    );
    assert(slot != nullptr);
    assert(slot->automation.active);
    assert(core::state::macro::macroAutomationBeatsFromTicks(slot->automation.durationTicks) == 2.0f);
    assert(slot->automation.pointCount == 2);
    core::state::macro::MacroCurvePoint firstPoint{};
    core::state::macro::MacroCurvePoint secondPoint{};
    assert(core::state::macro::macroAutomationReadPoint(
        slot->automation,
        state.pages.automation.pointPool,
        0,
        false,
        firstPoint
    ));
    assert(core::state::macro::macroAutomationReadPoint(
        slot->automation,
        state.pages.automation.pointPool,
        1,
        false,
        secondPoint
    ));
    assert(std::fabs(firstPoint.beat - 0.0f) < 0.0001f);
    assert(std::fabs(firstPoint.value - 0.25f) < 0.0001f);
    assert(std::fabs(secondPoint.beat - 1.0f) < 0.0001f);
    assert(std::fabs(secondPoint.value - 0.75f) < 0.0001f);

    drainNotifications();

    std::cout << "[PASS] test_automation_recording_commits_to_current_macro_slot\n";
}

void test_automation_recording_cancel_discards_session() {
    CoreStorages storage;

    core::state::CoreState state(storage.settings,
                                 storage.macroLibrary,
                                 storage.sequencerPatternLibrary,
                                 storage.sequencerSetLibrary);
    const auto services = core::handler::MacroPerformanceDomainServices::fromCoreState(state);

    assert(services.beginAutomationRecording(0, 1000));
    assert(services.recordAutomationPoint(0, 1250, 0.8f));
    assert(services.cancelAutomationRecording());
    assert(state.macroUi.automationRecordingStatus.get() ==
           core::state::macro::MacroAutomationRecordingStatus::IDLE);
    assert(!services.commitAutomationRecording(1500));

    const auto* slot = core::state::macro::macroAutomationFindSlot(
        state.pages.automation,
        core::state::macro::MacroAutomationSlotAddress{
            .track = state.pages.currentActiveTrack(),
            .page = state.pages.currentActivePage(),
            .macro = 0,
        }
    );
    assert(slot == nullptr);
    assert(!state.project.metadata.dirty);

    std::cout << "[PASS] test_automation_recording_cancel_discards_session\n";
}

void test_automation_recording_without_motion_does_not_create_slot() {
    CoreStorages storage;

    core::state::CoreState state(storage.settings,
                                 storage.macroLibrary,
                                 storage.sequencerPatternLibrary,
                                 storage.sequencerSetLibrary);
    const auto services = core::handler::MacroPerformanceDomainServices::fromCoreState(state);

    assert(services.beginAutomationRecording(0, 1000));
    assert(!services.commitAutomationRecording(1500));
    assert(state.macroUi.automationRecordingStatus.get() ==
           core::state::macro::MacroAutomationRecordingStatus::TOO_SHORT);
    assert(!services.automationRecordingActiveFor(0));

    const auto* slot = core::state::macro::macroAutomationFindSlot(
        state.pages.automation,
        core::state::macro::MacroAutomationSlotAddress{
            .track = state.pages.currentActiveTrack(),
            .page = state.pages.currentActivePage(),
            .macro = 0,
        }
    );
    assert(slot == nullptr);
    assert(!state.project.metadata.dirty);

    std::cout << "[PASS] test_automation_recording_without_motion_does_not_create_slot\n";
}

void test_failed_first_recording_does_not_leave_an_empty_slot() {
    CoreStorages storage;

    core::state::CoreState state(storage.settings,
                                 storage.macroLibrary,
                                 storage.sequencerPatternLibrary,
                                 storage.sequencerSetLibrary);
    const auto services = core::handler::MacroPerformanceDomainServices::fromCoreState(state);
    const auto address = core::state::macro::MacroAutomationSlotAddress{
        .track = state.pages.currentActiveTrack(),
        .page = state.pages.currentActivePage(),
        .macro = 0,
    };
    fillAutomationPointPoolExcept(state.pages.automation, address);
    const uint8_t entryCountBefore = state.pages.automation.entryCount;

    assert(services.beginAutomationRecording(0, 1000));
    assert(services.recordAutomationPoint(0, 1500, 0.75f));
    assert(!services.commitAutomationRecording(2000));
    assert(state.macroUi.automationRecordingStatus.get() ==
           core::state::macro::MacroAutomationRecordingStatus::COMMIT_FAILED);

    assert(!services.automationRecordingActiveFor(0));
    assert(core::state::macro::macroAutomationFindSlot(state.pages.automation, address) == nullptr);
    assert(state.pages.automation.entryCount == entryCountBefore);
    assert(state.pages.automation.pointPool.used ==
           core::state::macro::MACRO_AUTOMATION_POINT_POOL_CAPACITY);
    assert(!state.project.metadata.dirty);

    std::cout << "[PASS] test_failed_first_recording_does_not_leave_an_empty_slot\n";
}

void test_macro_edit_automation_lifecycle_actions() {
    CoreStorages storage;

    core::state::CoreState state(storage.settings,
                                 storage.macroLibrary,
                                 storage.sequencerPatternLibrary,
                                 storage.sequencerSetLibrary);
    const auto performance = core::handler::MacroPerformanceDomainServices::fromCoreState(state);
    const auto edit = core::handler::MacroEditDomainServices::fromCoreState(state);

    assert(performance.beginAutomationRecording(0, 1000));
    assert(performance.recordAutomationPoint(0, 1500, 0.75f));
    assert(performance.commitAutomationRecording(2000));

    assert(edit.copyAutomation(0));
    assert(state.structureClipboard.hasMacroAutomation());
    assert(edit.removeAutomation(0));
    assert(edit.automationSlot(0) == nullptr);

    assert(edit.pasteAutomation(0));
    const auto* pasted = edit.automationSlot(0);
    assert(pasted != nullptr);
    assert(pasted->automation.active);
    assert(pasted->automation.pointCount == 2);

    assert(edit.clearAutomation(0));
    const auto* cleared = edit.automationSlot(0);
    assert(cleared != nullptr);
    assert(!cleared->automation.active);

    std::cout << "[PASS] test_macro_edit_automation_lifecycle_actions\n";
}

}  // namespace

int main() {
    oc::time::setProvider(mockTimeMs);
    test_runtime_values_are_forwarded_and_clamped();
    test_manual_value_updates_base_and_stages_project_mutation();
    test_config_changes_mark_project_dirty_and_bump_revision();
    test_switch_to_page_updates_runtime_status_and_marks_project_dirty();
    test_track_config_batch_requires_shared_channel_and_marks_project_dirty_when_valid();
    test_status_bar_pulses_are_forwarded();
    test_macro_slot_activation_is_sequential_and_marks_project_dirty();
    test_automation_recording_commits_to_current_macro_slot();
    test_automation_recording_cancel_discards_session();
    test_automation_recording_without_motion_does_not_create_slot();
    test_failed_first_recording_does_not_leave_an_empty_slot();
    test_macro_edit_automation_lifecycle_actions();
    std::cout << "\nAll MacroPerformanceDomainServices tests passed.\n";
    return 0;
}
