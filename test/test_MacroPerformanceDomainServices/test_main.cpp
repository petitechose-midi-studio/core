#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>

#include <oc/time/Time.hpp>

#include "../../src/handler/macro/MacroEditDomainServices.hpp"
#include "../../src/handler/macro/MacroPerformanceDomainServices.hpp"
#include "../../src/handler/macro/MacroStructureDomainServices.hpp"
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

core::state::macro::MacroAutomationSlotState* configureAutomation(
    core::state::macro::MacroAutomationBankState& bank,
    const core::state::macro::MacroAutomationSlotAddress& address
) {
    auto* slot = core::state::macro::macroAutomationGetOrCreateSlot(bank, address);
    assert(slot != nullptr);
    core::state::macro::MacroAutomationLane lane;
    assert(core::state::macro::macroAutomationAppendPoint(lane, 0.0f, 0.2f));
    assert(core::state::macro::macroAutomationAppendPoint(lane, 1.0f, 0.8f));
    assert(core::state::macro::macroAutomationAssignAutomation(bank, *slot, lane));
    return slot;
}

core::state::macro::MacroAutomationSlotState* configureModulation(
    core::state::macro::MacroAutomationBankState& bank,
    const core::state::macro::MacroAutomationSlotAddress& address,
    float depth
) {
    auto* slot = core::state::macro::macroAutomationGetOrCreateSlot(bank, address);
    assert(slot != nullptr);
    core::state::macro::MacroModulationShape shape;
    assert(core::state::macro::macroModulationAppendPoint(shape, 0.0f, -0.25f));
    assert(core::state::macro::macroModulationAppendPoint(shape, 1.0f, 0.25f));
    assert(core::state::macro::macroAutomationAssignModulation(bank, *slot, shape));
    slot->modulationDepth = depth;
    return slot;
}

void assertCurvePayloadEquals(
    const core::state::macro::MacroAutomationCurveRef& expected,
    const core::state::macro::MacroAutomationPointPool& expectedPool,
    const core::state::macro::MacroAutomationCurveRef& actual,
    const core::state::macro::MacroAutomationPointPool& actualPool,
    bool signedValues
) {
    assert(expected.active == actual.active);
    assert(expected.playbackState == actual.playbackState);
    assert(expected.pointCount == actual.pointCount);
    assert(expected.sourceDurationTicks == actual.sourceDurationTicks);
    assert(expected.durationTicks == actual.durationTicks);
    assert(expected.windowOffsetTicks == actual.windowOffsetTicks);
    assert(expected.interpolation == actual.interpolation);
    assert(expected.modulationOrigin == actual.modulationOrigin);
    for (uint16_t i = 0; i < expected.pointCount; ++i) {
        core::state::macro::MacroCurvePoint expectedPoint{};
        core::state::macro::MacroCurvePoint actualPoint{};
        assert(core::state::macro::macroAutomationReadPoint(
            expected,
            expectedPool,
            i,
            signedValues,
            expectedPoint
        ));
        assert(core::state::macro::macroAutomationReadPoint(
            actual,
            actualPool,
            i,
            signedValues,
            actualPoint
        ));
        assert(std::fabs(expectedPoint.beat - actualPoint.beat) < 0.0001f);
        assert(std::fabs(expectedPoint.value - actualPoint.value) < 0.0001f);
    }
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

void test_manual_override_persists_absolute_base_and_is_addressed_by_slot() {
    CoreStorages storage;
    core::state::CoreState state(storage.settings,
                                 storage.macroLibrary,
                                 storage.sequencerPatternLibrary,
                                 storage.sequencerSetLibrary);
    const auto services = core::handler::MacroPerformanceDomainServices::fromCoreState(state);
    const auto firstAddress = core::state::macro::MacroAutomationSlotAddress{
        .track = state.pages.currentActiveTrack(),
        .page = state.pages.currentActivePage(),
        .macro = 0,
    };
    configureAutomation(state.pages.automation, firstAddress);
    state.pages.activePageData().values[0] = 0.25f;
    services.setResolvedValue(0, 0.25f);

    assert(services.takeManualControl(0, 0.75f));
    assert(services.manualOverrideActiveFor(0));
    float manualValue = 0.0f;
    assert(services.manualOverrideValueFor(0, manualValue));
    assert(std::fabs(manualValue - 0.75f) < 0.0001f);
    assert(std::fabs(state.pages.activePageData().values[0] - 0.75f) < 0.0001f);
    assert(state.hasPendingProjectMutationCoalescing());
    assert(!state.project.metadata.dirty);

    services.switchToPage(1);
    const auto secondAddress = core::state::macro::MacroAutomationSlotAddress{
        .track = state.pages.currentActiveTrack(),
        .page = state.pages.currentActivePage(),
        .macro = 0,
    };
    configureAutomation(state.pages.automation, secondAddress);
    assert(!services.manualOverrideActiveFor(0));
    assert((state.macroUi.automationManualOverrideMask.get() & 0x0001U) == 0);
    services.setResolvedValue(0, 0.1f);
    assert(services.takeManualControl(0, 0.4f));
    assert((state.macroUi.automationManualOverrideMask.get() & 0x0001U) != 0);
    assert(std::fabs(state.pages.activePageData().values[0] - 0.4f) < 0.0001f);

    services.switchToPage(0);
    assert(services.manualOverrideValueFor(0, manualValue));
    assert(std::fabs(manualValue - 0.75f) < 0.0001f);
    assert(std::fabs(services.runtimeValue(0) - 0.75f) < 0.0001f);
    assert((state.macroUi.automationManualOverrideMask.get() & 0x0001U) != 0);
    assert(state.macroUi.manualOverrides.activeFor(secondAddress));

    std::cout << "[PASS] test_manual_override_persists_absolute_base_and_is_addressed_by_slot\n";
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
    // Recording captures the authored absolute Base, never a potentially
    // modulated runtime Out projection.
    services.setManualValue(0, 0.25f);

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

void test_failed_or_cancelled_recording_restores_previous_manual_state() {
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
    configureAutomation(state.pages.automation, address);
    assert(services.takeManualControl(0, 0.42f));

    assert(services.beginAutomationRecording(0, 1000));
    assert(!services.manualOverrideActiveFor(0));
    assert(!services.commitAutomationRecording(1100));
    assert(state.macroUi.automationRecordingStatus.get() ==
           core::state::macro::MacroAutomationRecordingStatus::TOO_SHORT);
    float restored = 0.0f;
    assert(services.manualOverrideValueFor(0, restored));
    assert(std::fabs(restored - 0.42f) < 0.0001f);

    assert(services.beginAutomationRecording(0, 1200));
    assert(services.recordAutomationPoint(0, 1400, 0.9f));
    assert(services.cancelAutomationRecording());
    assert(services.manualOverrideValueFor(0, restored));
    assert(std::fabs(restored - 0.42f) < 0.0001f);

    std::cout << "[PASS] test_failed_or_cancelled_recording_restores_previous_manual_state\n";
}

void test_recording_preserves_active_modulation_without_resume() {
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
    auto* slot = configureModulation(state.pages.automation, address, 0.37f);
    const auto modulationBefore = slot->modulation;
    core::state::macro::MacroCurvePoint firstBefore{};
    core::state::macro::MacroCurvePoint secondBefore{};
    assert(core::state::macro::macroAutomationReadPoint(
        slot->modulation,
        state.pages.automation.pointPool,
        0,
        true,
        firstBefore
    ));
    assert(core::state::macro::macroAutomationReadPoint(
        slot->modulation,
        state.pages.automation.pointPool,
        1,
        true,
        secondBefore
    ));

    assert(services.computedSourcePlaybackActiveFor(0));
    assert(services.beginAutomationRecording(0, 1000));
    assert(services.recordAutomationPoint(0, 1500, 0.8f));
    assert(services.commitAutomationRecording(2000));

    slot = core::state::macro::macroAutomationFindMutableSlot(
        state.pages.automation,
        address
    );
    assert(slot != nullptr);
    assert(core::state::macro::macroCurvePlaybackActive(slot->automation));
    assert(core::state::macro::macroCurvePlaybackActive(slot->modulation));
    assert(!core::state::macro::macroCurveSuspendedAfterRecord(slot->modulation));
    assert(std::fabs(slot->modulationDepth - 0.37f) < 0.0001f);
    assert(slot->modulation.pointCount == modulationBefore.pointCount);
    assert(slot->modulation.durationTicks == modulationBefore.durationTicks);
    assert(slot->modulation.modulationOrigin == modulationBefore.modulationOrigin);
    core::state::macro::MacroCurvePoint firstAfter{};
    core::state::macro::MacroCurvePoint secondAfter{};
    assert(core::state::macro::macroAutomationReadPoint(
        slot->modulation,
        state.pages.automation.pointPool,
        0,
        true,
        firstAfter
    ));
    assert(core::state::macro::macroAutomationReadPoint(
        slot->modulation,
        state.pages.automation.pointPool,
        1,
        true,
        secondAfter
    ));
    assert(std::fabs(firstAfter.value - firstBefore.value) < 0.0001f);
    assert(std::fabs(secondAfter.value - secondBefore.value) < 0.0001f);
    assert(!services.manualOverrideActiveFor(0));

    const uint32_t modifiedBeforeResume = state.project.metadata.modifiedCounter;
    assert(!services.resumeComputedSources(0));
    assert(core::state::macro::macroCurvePlaybackActive(slot->automation));
    assert(core::state::macro::macroCurvePlaybackActive(slot->modulation));
    assert(state.project.metadata.modifiedCounter == modifiedBeforeResume);
    assert(state.project.metadata.dirty);
    assert(state.hasPendingProjectSessionSave());

    std::cout
        << "[PASS] test_recording_preserves_active_modulation_without_resume\n";
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

void test_modulation_copy_paste_preserves_target_and_exact_payload() {
    CoreStorages storage;
    core::state::CoreState state(storage.settings,
                                 storage.macroLibrary,
                                 storage.sequencerPatternLibrary,
                                 storage.sequencerSetLibrary);
    const auto edit = core::handler::MacroEditDomainServices::fromCoreState(state);
    auto& page = state.pages.activePageData();
    page.setMacroActive(0, true);
    page.setMacroActive(1, true);
    page.cc[0] = 74;
    page.values[0] = 0.21f;
    page.cc[1] = 11;
    page.values[1] = 0.66f;

    const auto sourceAddress = core::state::macro::MacroAutomationSlotAddress{
        .track = state.pages.currentActiveTrack(),
        .page = state.pages.currentActivePage(),
        .macro = 0,
    };
    const auto targetAddress = core::state::macro::MacroAutomationSlotAddress{
        .track = state.pages.currentActiveTrack(),
        .page = state.pages.currentActivePage(),
        .macro = 1,
    };
    configureAutomation(state.pages.automation, sourceAddress);
    auto* source = configureModulation(state.pages.automation, sourceAddress, 0.37f);
    source->modulation.playbackState =
        core::state::macro::MacroCurvePlaybackState::SUSPENDED_AFTER_RECORD;
    source->modulation.modulationOrigin =
        core::state::macro::MacroModulationOrigin::CONVERTED_FIRST;

    auto* target = configureAutomation(state.pages.automation, targetAddress);
    core::state::macro::MacroAutomationLane distinctTargetAutomation;
    assert(core::state::macro::macroAutomationAppendPoint(
        distinctTargetAutomation, 0.0f, 0.1f
    ));
    assert(core::state::macro::macroAutomationAppendPoint(
        distinctTargetAutomation, 1.0f, 0.9f
    ));
    assert(core::state::macro::macroAutomationAssignAutomation(
        state.pages.automation,
        *target,
        distinctTargetAutomation
    ));
    target = configureModulation(state.pages.automation, targetAddress, 0.82f);
    const auto targetAutomationBefore = target->automation;
    std::array<core::state::macro::MacroCurvePoint, 2> targetAutomationPoints{};
    for (uint16_t i = 0; i < targetAutomationPoints.size(); ++i) {
        assert(core::state::macro::macroAutomationReadPoint(
            targetAutomationBefore,
            state.pages.automation.pointPool,
            i,
            false,
            targetAutomationPoints[i]
        ));
    }

    assert(edit.copyModulation(0));
    assert(state.structureClipboard.hasMacroModulation());
    const auto plan = edit.preflightModulationPaste(1);
    assert(plan.actionable());
    assert(plan.requiresOverwrite());
    assert(!edit.pasteModulation(1, false));
    assert(std::fabs(edit.modulationDepth(1) - 0.82f) < 0.0001f);
    assert(edit.pasteModulation(1, true));

    source = core::state::macro::macroAutomationFindMutableSlot(
        state.pages.automation,
        sourceAddress
    );
    target = core::state::macro::macroAutomationFindMutableSlot(
        state.pages.automation,
        targetAddress
    );
    assert(source != nullptr && target != nullptr);
    assert(page.cc[1] == 11);
    assert(std::fabs(page.values[1] - 0.66f) < 0.0001f);
    assert(target->automation.pointCount == targetAutomationPoints.size());
    for (uint16_t i = 0; i < targetAutomationPoints.size(); ++i) {
        core::state::macro::MacroCurvePoint actual{};
        assert(core::state::macro::macroAutomationReadPoint(
            target->automation,
            state.pages.automation.pointPool,
            i,
            false,
            actual
        ));
        assert(std::fabs(actual.beat - targetAutomationPoints[i].beat) < 0.0001f);
        assert(std::fabs(actual.value - targetAutomationPoints[i].value) < 0.0001f);
    }
    assertCurvePayloadEquals(
        source->modulation,
        state.pages.automation.pointPool,
        target->modulation,
        state.pages.automation.pointPool,
        true
    );
    assert(std::fabs(target->modulationDepth - 0.37f) < 0.0001f);

    std::cout
        << "[PASS] test_modulation_copy_paste_preserves_target_and_exact_payload\n";
}

void test_typed_slot_copy_paste_preserves_automation_and_modulation() {
    CoreStorages storage;
    core::state::CoreState state(storage.settings,
                                 storage.macroLibrary,
                                 storage.sequencerPatternLibrary,
                                 storage.sequencerSetLibrary);
    const auto edit = core::handler::MacroEditDomainServices::fromCoreState(state);
    auto& page = state.pages.activePageData();
    page.setMacroActive(0, true);
    page.setMacroActive(2, true);
    page.cc[0] = 74;
    page.values[0] = 0.42f;
    page.cc[2] = 7;
    page.values[2] = 0.91f;
    const auto sourceAddress = core::state::macro::MacroAutomationSlotAddress{
        .track = state.pages.currentActiveTrack(),
        .page = state.pages.currentActivePage(),
        .macro = 0,
    };
    const auto targetAddress = core::state::macro::MacroAutomationSlotAddress{
        .track = state.pages.currentActiveTrack(),
        .page = state.pages.currentActivePage(),
        .macro = 2,
    };
    configureAutomation(state.pages.automation, sourceAddress);
    auto* source = configureModulation(state.pages.automation, sourceAddress, 0.43f);
    source->automation.playbackState = core::state::macro::MacroCurvePlaybackState::OFF;
    source->modulation.modulationOrigin =
        core::state::macro::MacroModulationOrigin::CONVERTED_MIN;
    configureAutomation(state.pages.automation, targetAddress);
    configureModulation(state.pages.automation, targetAddress, 0.9f);

    assert(edit.copySlot(0));
    const auto plan = edit.preflightSlotPaste(2);
    assert(plan.actionable());
    assert(plan.requiresOverwrite());
    assert(edit.pasteSlot(2, true));

    source = core::state::macro::macroAutomationFindMutableSlot(
        state.pages.automation,
        sourceAddress
    );
    auto* target = core::state::macro::macroAutomationFindMutableSlot(
        state.pages.automation,
        targetAddress
    );
    assert(source != nullptr && target != nullptr);
    assert(page.isMacroActive(2));
    assert(page.cc[2] == 74);
    assert(std::fabs(page.values[2] - 0.42f) < 0.0001f);
    assertCurvePayloadEquals(
        source->automation,
        state.pages.automation.pointPool,
        target->automation,
        state.pages.automation.pointPool,
        false
    );
    assertCurvePayloadEquals(
        source->modulation,
        state.pages.automation.pointPool,
        target->modulation,
        state.pages.automation.pointPool,
        true
    );
    assert(std::fabs(target->modulationDepth - 0.43f) < 0.0001f);

    std::cout
        << "[PASS] test_typed_slot_copy_paste_preserves_automation_and_modulation\n";
}

void test_page_and_track_copy_preserve_automation_and_modulation() {
    CoreStorages storage;
    core::state::CoreState state(storage.settings,
                                 storage.macroLibrary,
                                 storage.sequencerPatternLibrary,
                                 storage.sequencerSetLibrary);
    const auto structure = core::handler::MacroStructureDomainServices::fromCoreState(state);
    const auto source = core::state::macro::MacroAutomationSlotAddress{
        .track = 0,
        .page = 0,
        .macro = 0,
    };
    const auto pageTarget = core::state::macro::MacroAutomationSlotAddress{
        .track = 0,
        .page = 1,
        .macro = 0,
    };
    const auto trackTarget = core::state::macro::MacroAutomationSlotAddress{
        .track = 1,
        .page = 0,
        .macro = 0,
    };
    state.pages.pageData(0, 0).setMacroActive(0, true);
    state.pages.pageData(0, 0).cc[0] = 74;
    state.pages.pageData(0, 0).values[0] = 0.36f;
    configureAutomation(state.pages.automation, source);
    auto* sourceSlot = configureModulation(state.pages.automation, source, 0.33f);
    sourceSlot->automation.playbackState =
        core::state::macro::MacroCurvePlaybackState::OFF;
    sourceSlot->modulation.modulationOrigin =
        core::state::macro::MacroModulationOrigin::CONVERTED_MEAN;

    assert(state.structureClipboard.storeMacroPage(
        state.pages.pageData(0, 0),
        state.pages.automation,
        0,
        0
    ));
    assert(structure.pastePage(
        1,
        state.structureClipboard.macroPage,
        state.structureClipboard.macroAutomationSet.get()
    ));
    sourceSlot = core::state::macro::macroAutomationFindMutableSlot(
        state.pages.automation,
        source
    );
    auto* targetSlot = core::state::macro::macroAutomationFindMutableSlot(
        state.pages.automation,
        pageTarget
    );
    assert(sourceSlot != nullptr && targetSlot != nullptr);
    assertCurvePayloadEquals(
        sourceSlot->automation,
        state.pages.automation.pointPool,
        targetSlot->automation,
        state.pages.automation.pointPool,
        false
    );
    assertCurvePayloadEquals(
        sourceSlot->modulation,
        state.pages.automation.pointPool,
        targetSlot->modulation,
        state.pages.automation.pointPool,
        true
    );
    assert(std::fabs(targetSlot->modulationDepth - 0.33f) < 0.0001f);
    assert(state.pages.pageData(0, 1).cc[0] == 74);
    assert(std::fabs(state.pages.pageData(0, 1).values[0] - 0.36f) < 0.0001f);

    assert(state.structureClipboard.storeMacroTrack(
        state.pages.tracks[0],
        state.pages.automation,
        0
    ));
    assert(structure.pasteTrack(
        1,
        state.structureClipboard.macroTrack,
        state.structureClipboard.macroAutomationSet.get()
    ));
    sourceSlot = core::state::macro::macroAutomationFindMutableSlot(
        state.pages.automation,
        source
    );
    targetSlot = core::state::macro::macroAutomationFindMutableSlot(
        state.pages.automation,
        trackTarget
    );
    assert(sourceSlot != nullptr && targetSlot != nullptr);
    assertCurvePayloadEquals(
        sourceSlot->automation,
        state.pages.automation.pointPool,
        targetSlot->automation,
        state.pages.automation.pointPool,
        false
    );
    assertCurvePayloadEquals(
        sourceSlot->modulation,
        state.pages.automation.pointPool,
        targetSlot->modulation,
        state.pages.automation.pointPool,
        true
    );
    assert(std::fabs(targetSlot->modulationDepth - 0.33f) < 0.0001f);
    assert(state.pages.pageData(1, 0).cc[0] == 74);
    assert(std::fabs(state.pages.pageData(1, 0).values[0] - 0.36f) < 0.0001f);

    std::cout
        << "[PASS] test_page_and_track_copy_preserve_automation_and_modulation\n";
}

void test_slot_page_and_track_replacement_invalidate_only_targeted_manual_entries() {
    CoreStorages storage;
    core::state::CoreState state(storage.settings,
                                 storage.macroLibrary,
                                 storage.sequencerPatternLibrary,
                                 storage.sequencerSetLibrary);
    const auto performance = core::handler::MacroPerformanceDomainServices::fromCoreState(state);
    const auto edit = core::handler::MacroEditDomainServices::fromCoreState(state);
    const auto structure = core::handler::MacroStructureDomainServices::fromCoreState(state);
    const auto source = core::state::macro::MacroAutomationSlotAddress{
        .track = 0,
        .page = 0,
        .macro = 0,
    };
    const auto pageTarget = core::state::macro::MacroAutomationSlotAddress{
        .track = 0,
        .page = 1,
        .macro = 0,
    };
    const auto trackTarget = core::state::macro::MacroAutomationSlotAddress{
        .track = 1,
        .page = 0,
        .macro = 0,
    };
    const auto unrelated = core::state::macro::MacroAutomationSlotAddress{
        .track = 2,
        .page = 3,
        .macro = 4,
    };
    configureAutomation(state.pages.automation, source);
    configureAutomation(state.pages.automation, pageTarget);
    configureAutomation(state.pages.automation, trackTarget);
    assert(state.macroUi.manualOverrides.activate(source, 0.1f) ==
           core::state::macro::MacroManualOverrideState::ActivateStatus::ACTIVATED);
    assert(state.macroUi.manualOverrides.activate(pageTarget, 0.2f) ==
           core::state::macro::MacroManualOverrideState::ActivateStatus::ACTIVATED);
    assert(state.macroUi.manualOverrides.activate(trackTarget, 0.3f) ==
           core::state::macro::MacroManualOverrideState::ActivateStatus::ACTIVATED);
    assert(state.macroUi.manualOverrides.activate(unrelated, 0.4f) ==
           core::state::macro::MacroManualOverrideState::ActivateStatus::ACTIVATED);

    assert(state.structureClipboard.storeMacroPage(
        state.pages.pageData(0, 0),
        state.pages.automation,
        0,
        0
    ));
    assert(structure.pastePage(
        1,
        state.structureClipboard.macroPage,
        state.structureClipboard.macroAutomationSet.get()
    ));
    assert(state.macroUi.manualOverrides.activeFor(source));
    assert(!state.macroUi.manualOverrides.activeFor(pageTarget));
    assert(state.macroUi.manualOverrides.activeFor(trackTarget));
    assert(state.macroUi.manualOverrides.activeFor(unrelated));

    assert(state.structureClipboard.storeMacroTrack(
        state.pages.tracks[0],
        state.pages.automation,
        0
    ));
    assert(structure.pasteTrack(
        1,
        state.structureClipboard.macroTrack,
        state.structureClipboard.macroAutomationSet.get()
    ));
    assert(state.macroUi.manualOverrides.activeFor(source));
    assert(!state.macroUi.manualOverrides.activeFor(trackTarget));
    assert(state.macroUi.manualOverrides.activeFor(unrelated));

    structure.switchToTrack(0);
    structure.switchToPage(0);
    assert(performance.manualOverrideActiveFor(0));
    const uint32_t revisionBeforeSamePayloadPaste =
        state.project.metadata.modifiedCounter;
    const uint8_t undoBeforeSamePayloadPaste = state.macroHistory.undoCount();
    assert(edit.copyAutomation(0));
    assert(edit.pasteAutomation(0));
    assert(!state.macroUi.manualOverrides.activeFor(source));
    assert(state.macroUi.manualOverrides.activeFor(unrelated));
    assert(state.project.metadata.modifiedCounter ==
           revisionBeforeSamePayloadPaste);
    assert(state.macroHistory.undoCount() == undoBeforeSamePayloadPaste);

    assert(performance.takeManualControl(0, 0.6f));
    assert(edit.clearAutomation(0));
    assert(!state.macroUi.manualOverrides.activeFor(source));
    assert(state.macroUi.manualOverrides.activeFor(unrelated));

    configureAutomation(state.pages.automation, source);
    assert(performance.takeManualControl(0, 0.7f));
    assert(edit.removeAutomation(0));
    assert(!state.macroUi.manualOverrides.activeFor(source));
    assert(state.macroUi.manualOverrides.activeFor(unrelated));

    std::cout
        << "[PASS] test_slot_page_and_track_replacement_invalidate_only_targeted_manual_entries\n";
}

}  // namespace

int main() {
    oc::time::setProvider(mockTimeMs);
    test_runtime_values_are_forwarded_and_clamped();
    test_manual_value_updates_base_and_stages_project_mutation();
    test_manual_override_persists_absolute_base_and_is_addressed_by_slot();
    test_config_changes_mark_project_dirty_and_bump_revision();
    test_switch_to_page_updates_runtime_status_and_marks_project_dirty();
    test_track_config_batch_requires_shared_channel_and_marks_project_dirty_when_valid();
    test_status_bar_pulses_are_forwarded();
    test_macro_slot_activation_is_sequential_and_marks_project_dirty();
    test_automation_recording_commits_to_current_macro_slot();
    test_automation_recording_cancel_discards_session();
    test_automation_recording_without_motion_does_not_create_slot();
    test_failed_or_cancelled_recording_restores_previous_manual_state();
    test_recording_preserves_active_modulation_without_resume();
    test_failed_first_recording_does_not_leave_an_empty_slot();
    test_macro_edit_automation_lifecycle_actions();
    test_modulation_copy_paste_preserves_target_and_exact_payload();
    test_typed_slot_copy_paste_preserves_automation_and_modulation();
    test_page_and_track_copy_preserve_automation_and_modulation();
    test_slot_page_and_track_replacement_invalidate_only_targeted_manual_entries();
    std::cout << "\nAll MacroPerformanceDomainServices tests passed.\n";
    return 0;
}
