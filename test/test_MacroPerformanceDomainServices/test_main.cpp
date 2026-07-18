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
#include "../support/ProjectControlTestUtils.hpp"

namespace {

uint32_t g_mock_now_ms = 1000;

uint32_t mockTimeMs() {
    return g_mock_now_ms;
}

using test_support::CoreStorages;
using test_support::drainNotifications;

void fillAutomationPointPoolExcept(
    core::state::modulation::ProjectControlState& control,
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
         control.authored.curves.pointCount <
             core::state::modulation::PROJECT_CURVE_POINT_CAPACITY;
         ++track) {
        for (uint8_t macro = 0;
             macro < core::state::macro::MACRO_COUNT &&
             control.authored.curves.pointCount <
                 core::state::modulation::PROJECT_CURVE_POINT_CAPACITY;
             ++macro) {
            const auto address = core::state::macro::MacroAutomationSlotAddress{
                .track = track,
                .page = 0,
                .macro = macro,
            };
            if (core::state::macro::macroAutomationAddressEquals(address, excluded)) continue;
            assert(test_support::project_control::assignAutomation(
                control,
                address,
                lane
            ));
        }
    }
    assert(control.authored.curves.pointCount ==
           core::state::modulation::PROJECT_CURVE_POINT_CAPACITY);
}

core::state::modulation::ProjectControlMacroSlotView configureAutomation(
    core::state::modulation::ProjectControlState& control,
    const core::state::macro::MacroAutomationSlotAddress& address
) {
    core::state::macro::MacroAutomationLane lane;
    assert(core::state::macro::macroAutomationAppendPoint(lane, 0.0f, 0.2f));
    assert(core::state::macro::macroAutomationAppendPoint(lane, 1.0f, 0.8f));
    assert(test_support::project_control::assignAutomation(control, address, lane));
    return test_support::project_control::readSlot(control, address);
}

core::state::modulation::ProjectControlMacroSlotView configureModulation(
    core::state::modulation::ProjectControlState& control,
    const core::state::macro::MacroAutomationSlotAddress& address,
    float depth
) {
    core::state::macro::MacroModulationShape shape;
    assert(core::state::macro::macroModulationAppendPoint(shape, 0.0f, -0.25f));
    assert(core::state::macro::macroModulationAppendPoint(shape, 1.0f, 0.25f));
    assert(test_support::project_control::assignModulation(
        control,
        address,
        shape,
        depth
    ));
    return test_support::project_control::readSlot(control, address);
}

bool recordHoldAutomationTake(
    const core::handler::MacroPerformanceDomainServices& services,
    uint8_t macro,
    uint32_t startedAtMs,
    float firstValue,
    uint32_t movedAtMs,
    float movedValue,
    uint32_t releasedAtMs
) {
    (void)services.setAutomationTakeTiming(
        core::state::macro::MacroAutomationTakeTiming::HOLD
    );
    assert(services.armAutomationTake());
    assert(services.recordAutomationTakeValue(macro, startedAtMs, firstValue));
    assert(services.recordAutomationTakeValue(macro, movedAtMs, movedValue));
    return services.releaseAutomationTake(releasedAtMs);
}

void assertCurvePayloadEquals(
    const core::state::modulation::ProjectControlState& control,
    core::state::modulation::ProjectCurveId expectedId,
    core::state::modulation::ProjectCurveId actualId,
    bool signedValues
) {
    const auto* expected = core::state::modulation::findProjectCurve(
        control.authored.curves,
        expectedId
    );
    const auto* actual = core::state::modulation::findProjectCurve(
        control.authored.curves,
        actualId
    );
    assert(expected != nullptr && actual != nullptr);
    assert(expected->pointCount == actual->pointCount);
    assert(expected->sourceDurationTicks == actual->sourceDurationTicks);
    assert(expected->durationTicks == actual->durationTicks);
    assert(expected->windowOffsetTicks == actual->windowOffsetTicks);
    assert(expected->interpolation == actual->interpolation);
    assert(expected->valueDomain == actual->valueDomain);
    assert(expected->origin == actual->origin);
    for (uint16_t i = 0; i < expected->pointCount; ++i) {
        const auto expectedPoint = test_support::project_control::readCurvePoint(
            control,
            expectedId,
            i,
            signedValues
        );
        const auto actualPoint = test_support::project_control::readCurvePoint(
            control,
            actualId,
            i,
            signedValues
        );
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
    configureAutomation(state.pages.control, firstAddress);
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
    configureAutomation(state.pages.control, secondAddress);
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

void test_macro_slot_activation_is_sparse_and_marks_project_dirty() {
    CoreStorages storage;

    core::state::CoreState state(storage.settings,
                                 storage.macroLibrary,
                                 storage.sequencerPatternLibrary,
                                 storage.sequencerSetLibrary);
    const auto services = core::handler::MacroPerformanceDomainServices::fromCoreState(state);

    assert(services.isMacroSlotActive(0));
    assert(!services.isMacroSlotActive(1));
    assert(services.isMacroAddSlot(1));
    assert(services.isMacroAddSlot(2));

    const uint32_t initialRevision = state.configRevision.get();
    assert(services.activateMacroSlot(5));
    assert(services.isMacroSlotActive(5));
    assert(services.isMacroAddSlot(1));
    assert(state.pages.activePageData().activeMacroMask == 0x21U);
    assert(state.pages.activeConfigs[5].cc == 5);
    assert(state.configRevision.get() ==
           core::state::macro::nextMacroConfigRevision(initialRevision, 5));
    assert(state.project.metadata.dirty);

    std::cout << "[PASS] test_macro_slot_activation_is_sparse_and_marks_project_dirty\n";
}

void test_addressed_macro_slot_activation_preserves_cold_page_cache() {
    CoreStorages storage;
    core::state::CoreState state(storage.settings,
                                 storage.macroLibrary,
                                 storage.sequencerPatternLibrary,
                                 storage.sequencerSetLibrary);

    const auto activeConfigBefore = state.pages.activeConfigs[1];
    const core::state::macro::MacroAutomationSlotAddress coldAddress{0, 1, 1};
    const auto plan =
        core::state::macro::MacroWorkflow::planMacroSlotActivation(
            state.pages,
            coldAddress
        );
    assert(plan.valid);
    assert(plan.cc == 9U);
    assert(plan.baseValue == 0.5f);
    assert(core::state::macro::MacroWorkflow::applyMacroSlotActivation(
        state.pages,
        plan
    ));
    assert(state.pages.pageData(0, 1).isMacroActive(1));
    assert(state.pages.pageData(0, 1).cc[1] == 9U);
    assert(state.pages.activeConfigs[1].cc == activeConfigBefore.cc);
    assert(state.pages.activeConfigs[1].channel == activeConfigBefore.channel);

    const auto stale = plan;
    assert(!core::state::macro::MacroWorkflow::applyMacroSlotActivation(
        state.pages,
        stale
    ));
    state.pages.setActivePage(1);
    assert(state.pages.activeConfigs[1].cc == 9U);

    std::cout << "[PASS] addressed Macro activation preserves cold-page caches\n";
}

void test_destination_activation_keeps_structure_contiguous_and_macros_sparse() {
    CoreStorages storage;
    core::state::CoreState state(storage.settings,
                                 storage.macroLibrary,
                                 storage.sequencerPatternLibrary,
                                 storage.sequencerSetLibrary);
    using core::state::macro::MacroAutomationSlotAddress;
    using core::state::macro::MacroWorkflow;

    const auto sparse = MacroWorkflow::planDestinationActivation(
        state.pages,
        MacroAutomationSlotAddress{0U, 0U, 5U}
    );
    assert(sparse.valid && !sparse.createTrack && !sparse.createPage &&
           sparse.createMacro);
    assert(MacroWorkflow::applyDestinationActivation(state.pages, sparse));
    assert(state.pages.pageData(0U, 0U).activeMacroMask == 0x21U);
    assert(state.pages.pageData(0U, 0U).cc[5] == 5U);

    const auto nextPage = MacroWorkflow::planDestinationActivation(
        state.pages,
        MacroAutomationSlotAddress{0U, 1U, 3U}
    );
    assert(nextPage.valid && !nextPage.createTrack && nextPage.createPage &&
           nextPage.createMacro);
    assert(MacroWorkflow::applyDestinationActivation(state.pages, nextPage));
    assert(state.pages.tracks[0].enabledPageMask == 0x0003U);
    assert(state.pages.pageData(0U, 1U).activeMacroMask == 0x08U);
    assert(state.pages.pageData(0U, 1U).cc[3] == 11U);
    assert(state.pages.currentActivePage() == 0U);

    const auto skippedTrack = MacroWorkflow::planDestinationActivation(
        state.pages,
        MacroAutomationSlotAddress{2U, 0U, 5U}
    );
    assert(!skippedTrack.valid);
    const auto nextTrack = MacroWorkflow::planDestinationActivation(
        state.pages,
        MacroAutomationSlotAddress{1U, 0U, 5U}
    );
    assert(nextTrack.valid && nextTrack.createTrack && nextTrack.createPage &&
           nextTrack.createMacro);
    assert(MacroWorkflow::applyDestinationActivation(state.pages, nextTrack));
    assert(state.pages.currentTrackEnabledMask() == 0x0003U);
    assert(state.pages.pageData(1U, 0U).activeMacroMask == 0x20U);
    assert(state.pages.pageData(1U, 0U).cc[5] == 5U);
    assert(state.pages.currentActiveTrack() == 0U);

    std::cout << "[PASS] destination topology is contiguous with sparse Macros\n";
}

void test_automation_take_commits_to_current_macro_slot() {
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

    (void)services.setAutomationTakeTiming(
        core::state::macro::MacroAutomationTakeTiming::HOLD
    );
    assert(services.armAutomationTake());
    assert(state.macroUi.automationRecordingStatus.get() ==
           core::state::macro::MacroAutomationRecordingStatus::ARMED);
    assert(!state.project.metadata.dirty);

    assert(services.recordAutomationTakeValue(0, 1000, 0.25f));
    assert(services.automationTakeActiveFor(0));
    assert(services.recordAutomationTakeValue(0, 1500, 0.75f));
    assert(services.releaseAutomationTake(2000));
    assert(state.macroUi.automationRecordingStatus.get() ==
           core::state::macro::MacroAutomationRecordingStatus::IDLE);
    assert(!services.automationTakeActiveFor(0));
    assert(state.project.metadata.dirty);
    assert(state.hasPendingProjectSessionSave());

    const auto slot = test_support::project_control::readSlot(
        state.pages.control,
        core::state::macro::MacroAutomationSlotAddress{
            .track = state.pages.currentActiveTrack(),
            .page = state.pages.currentActivePage(),
            .macro = 0,
        }
    );
    assert(slot.automationEnabled);
    assert(core::state::macro::macroAutomationBeatsFromTicks(
        slot.compatibility.automation.durationTicks
    ) == 2.0f);
    assert(slot.compatibility.automation.pointCount >= 2U);
    const auto firstPoint = test_support::project_control::readCurvePoint(
        state.pages.control,
        slot.automationCurveId,
        0,
        false
    );
    const auto lastPoint = test_support::project_control::readCurvePoint(
        state.pages.control,
        slot.automationCurveId,
        static_cast<uint16_t>(slot.compatibility.automation.pointCount - 1U),
        false
    );
    assert(std::fabs(firstPoint.beat - 0.0f) < 0.0001f);
    assert(std::fabs(firstPoint.value - 0.25f) < 0.005f);
    assert(std::fabs(lastPoint.beat - 2.0f) < 0.0001f);
    assert(std::fabs(lastPoint.value - 0.75f) < 0.005f);

    drainNotifications();

    std::cout << "[PASS] test_automation_take_commits_to_current_macro_slot\n";
}

void test_automation_take_cancel_discards_session() {
    CoreStorages storage;

    core::state::CoreState state(storage.settings,
                                 storage.macroLibrary,
                                 storage.sequencerPatternLibrary,
                                 storage.sequencerSetLibrary);
    const auto services = core::handler::MacroPerformanceDomainServices::fromCoreState(state);

    assert(services.armAutomationTake());
    assert(services.recordAutomationTakeValue(0, 1000, 0.8f));
    assert(services.cancelAutomationTake());
    assert(state.macroUi.automationRecordingStatus.get() ==
           core::state::macro::MacroAutomationRecordingStatus::IDLE);
    assert(!services.releaseAutomationTake(1500));

    const auto slot = test_support::project_control::readSlot(
        state.pages.control,
        core::state::macro::MacroAutomationSlotAddress{
            .track = state.pages.currentActiveTrack(),
            .page = state.pages.currentActivePage(),
            .macro = 0,
        }
    );
    assert(!slot.present);
    assert(!state.project.metadata.dirty);

    std::cout << "[PASS] test_automation_take_cancel_discards_session\n";
}

void test_shared_automation_take_records_late_join_and_one_undo() {
    using namespace core::state::macro;
    CoreStorages storage;
    core::state::CoreState state(storage.settings,
                                 storage.macroLibrary,
                                 storage.sequencerPatternLibrary,
                                 storage.sequencerSetLibrary);
    const auto services =
        core::handler::MacroPerformanceDomainServices::fromCoreState(state);
    state.statusBar.tempo.set(120.0f);
    state.pages.activePageData().setMacroActive(1U, true);
    state.pages.activePageData().values[0] = 0.25f;
    state.pages.activePageData().values[1] = 0.5f;
    const MacroAutomationSlotAddress first{0U, 0U, 0U};
    const MacroAutomationSlotAddress second{0U, 0U, 1U};
    configureModulation(state.pages.control, first, 0.4f);
    const auto graphBefore = state.pages.control.authored.modulation;
    const uint8_t undoBefore = state.macroHistory.undoCount();

    assert(services.setAutomationTakeTiming(MacroAutomationTakeTiming::BAR_1));
    assert(services.armAutomationTake());
    assert(services.automationTakeArmed());
    assert(state.macroUi.automationRecordingStatus.get() ==
           MacroAutomationRecordingStatus::ARMED);
    assert(services.recordAutomationTakeValue(0U, 1000U, 0.75f));
    assert(services.automationTakeRecording());
    assert(services.recordAutomationTakeValue(1U, 1250U, 1.0f));
    assert(services.releaseAutomationTake(1300U));
    assert(services.automationTakeRecording());
    assert(services.updateAutomationTake(3000U));
    assert(!services.automationTakeRecording());
    assert(state.macroHistory.undoCount() == undoBefore + 1U);
    assert(std::memcmp(
        &state.pages.control.authored.modulation,
        &graphBefore,
        sizeof(graphBefore)
    ) == 0);

    auto firstSlot = test_support::project_control::readSlot(
        state.pages.control,
        first
    );
    auto secondSlot = test_support::project_control::readSlot(
        state.pages.control,
        second
    );
    assert(firstSlot.automationEnabled && secondSlot.automationEnabled);
    assert(firstSlot.compatibility.automation.durationTicks == 768U);
    assert(secondSlot.compatibility.automation.durationTicks == 768U);
    const auto secondStart = test_support::project_control::readCurvePoint(
        state.pages.control,
        secondSlot.automationCurveId,
        0U,
        false
    );
    assert(std::fabs(secondStart.value - 0.5f) < 0.005f);

    assert(state.macroHistory.undo(state.pages));
    firstSlot = test_support::project_control::readSlot(state.pages.control, first);
    secondSlot = test_support::project_control::readSlot(state.pages.control, second);
    assert(!firstSlot.automationStored);
    assert(!secondSlot.automationStored);
    assert(firstSlot.modulationEnabled);
    assert(state.macroHistory.redo(state.pages));
    assert(test_support::project_control::readSlot(
        state.pages.control,
        first
    ).automationEnabled);
    assert(test_support::project_control::readSlot(
        state.pages.control,
        second
    ).automationEnabled);
    std::cout << "[PASS] shared take has late join and one Undo\n";
}

void test_take_cancel_restores_manual_and_preflight_failure_is_clean() {
    using namespace core::state::macro;
    CoreStorages storage;
    core::state::CoreState state(storage.settings,
                                 storage.macroLibrary,
                                 storage.sequencerPatternLibrary,
                                 storage.sequencerSetLibrary);
    const auto services =
        core::handler::MacroPerformanceDomainServices::fromCoreState(state);
    const MacroAutomationSlotAddress address{0U, 0U, 0U};
    configureAutomation(state.pages.control, address);
    assert(services.takeManualControl(0U, 0.42f));
    assert(services.armAutomationTake());
    assert(services.recordAutomationTakeValue(0U, 1000U, 0.8f));
    assert(!services.manualOverrideActiveFor(0U));
    assert(services.cancelAutomationTake());
    float restored = 0.0f;
    assert(services.manualOverrideValueFor(0U, restored));
    assert(std::fabs(restored - 0.42f) < 0.0001f);

    // A full arena fails before the first authored/manual mutation.
    state.macroUi.manualOverrides.clearProjectRuntime();
    core::state::CoreState full(storage.settings,
                                storage.macroLibrary,
                                storage.sequencerPatternLibrary,
                                storage.sequencerSetLibrary);
    const auto fullServices =
        core::handler::MacroPerformanceDomainServices::fromCoreState(full);
    fillAutomationPointPoolExcept(full.pages.control, address);
    const auto authoredBefore = full.pages.control.authored;
    const uint8_t undoBefore = full.macroHistory.undoCount();
    assert(fullServices.armAutomationTake());
    assert(!fullServices.recordAutomationTakeValue(0U, 1000U, 0.7f));
    assert(full.macroUi.automationTake.phase == MacroAutomationTakePhase::IDLE);
    assert(full.macroHistory.undoCount() == undoBefore);
    assert(std::memcmp(
        &full.pages.control.authored,
        &authoredBefore,
        sizeof(authoredBefore)
    ) == 0);
    std::cout << "[PASS] take Cancel and preflight failure are exact\n";
}

void test_armed_automation_take_without_motion_does_not_create_slot() {
    CoreStorages storage;

    core::state::CoreState state(storage.settings,
                                 storage.macroLibrary,
                                 storage.sequencerPatternLibrary,
                                 storage.sequencerSetLibrary);
    const auto services = core::handler::MacroPerformanceDomainServices::fromCoreState(state);

    assert(services.armAutomationTake());
    assert(services.releaseAutomationTake(1500));
    assert(state.macroUi.automationRecordingStatus.get() ==
           core::state::macro::MacroAutomationRecordingStatus::IDLE);
    assert(!services.automationTakeRecording());

    const auto slot = test_support::project_control::readSlot(
        state.pages.control,
        core::state::macro::MacroAutomationSlotAddress{
            .track = state.pages.currentActiveTrack(),
            .page = state.pages.currentActivePage(),
            .macro = 0,
        }
    );
    assert(!slot.present);
    assert(!state.project.metadata.dirty);

    std::cout << "[PASS] armed take without motion is a clean no-op\n";
}

void test_cancelled_automation_take_restores_previous_manual_state() {
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
    configureAutomation(state.pages.control, address);
    assert(services.takeManualControl(0, 0.42f));

    assert(services.armAutomationTake());
    assert(services.recordAutomationTakeValue(0, 1000, 0.9f));
    assert(!services.manualOverrideActiveFor(0));
    assert(services.cancelAutomationTake());
    float restored = 0.0f;
    assert(services.manualOverrideValueFor(0, restored));
    assert(std::fabs(restored - 0.42f) < 0.0001f);

    assert(services.armAutomationTake());
    assert(services.releaseAutomationTake(1400));
    assert(services.manualOverrideValueFor(0, restored));
    assert(std::fabs(restored - 0.42f) < 0.0001f);

    std::cout << "[PASS] cancelled take restores previous Manual state\n";
}

void test_automation_take_preserves_active_modulation_without_resume() {
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
    auto slot = configureModulation(state.pages.control, address, 0.37f);
    const auto modulationBefore = slot.compatibility.modulation;
    const auto firstBefore = test_support::project_control::readCurvePoint(
        state.pages.control,
        slot.modulationCurveId,
        0,
        true
    );
    const auto secondBefore = test_support::project_control::readCurvePoint(
        state.pages.control,
        slot.modulationCurveId,
        1,
        true
    );

    assert(services.computedSourcePlaybackActiveFor(0));
    assert(recordHoldAutomationTake(
        services, 0U, 1000U, 0.2f, 1500U, 0.8f, 2000U
    ));

    slot = test_support::project_control::readSlot(state.pages.control, address);
    assert(slot.automationEnabled);
    assert(slot.modulationEnabled);
    assert(std::fabs(slot.compatibility.modulationDepth - 0.37f) < 0.0001f);
    assert(slot.compatibility.modulation.pointCount == modulationBefore.pointCount);
    assert(slot.compatibility.modulation.durationTicks == modulationBefore.durationTicks);
    assert(slot.compatibility.modulation.modulationOrigin ==
           modulationBefore.modulationOrigin);
    const auto firstAfter = test_support::project_control::readCurvePoint(
        state.pages.control,
        slot.modulationCurveId,
        0,
        true
    );
    const auto secondAfter = test_support::project_control::readCurvePoint(
        state.pages.control,
        slot.modulationCurveId,
        1,
        true
    );
    assert(std::fabs(firstAfter.value - firstBefore.value) < 0.0001f);
    assert(std::fabs(secondAfter.value - secondBefore.value) < 0.0001f);
    assert(!services.manualOverrideActiveFor(0));

    const uint32_t modifiedBeforeResume = state.project.metadata.modifiedCounter;
    assert(!services.resumeComputedSources(0));
    slot = test_support::project_control::readSlot(state.pages.control, address);
    assert(slot.automationEnabled);
    assert(slot.modulationEnabled);
    assert(state.project.metadata.modifiedCounter == modifiedBeforeResume);
    assert(state.project.metadata.dirty);
    assert(state.hasPendingProjectSessionSave());

    std::cout
        << "[PASS] Automation take preserves active Modulation without Resume\n";
}

void test_automation_take_preserves_shared_lfos_through_undo_redo() {
    using namespace core::state::modulation;
    CoreStorages storage;
    core::state::CoreState state(storage.settings,
                                 storage.macroLibrary,
                                 storage.sequencerPatternLibrary,
                                 storage.sequencerSetLibrary);
    const auto services =
        core::handler::MacroPerformanceDomainServices::fromCoreState(state);
    const auto address = core::state::macro::MacroAutomationSlotAddress{
        .track = state.pages.currentActiveTrack(),
        .page = state.pages.currentActivePage(),
        .macro = 0,
    };
    const auto otherAddress = core::state::macro::MacroAutomationSlotAddress{
        .track = address.track,
        .page = address.page,
        .macro = 1,
    };
    state.pages.setMacroSlotActive(1, true);

    auto& graph = state.pages.control.authored.modulation;
    ModulatorLfoDraft sharedDraft{};
    sharedDraft.name = "Shared LFO";
    sharedDraft.parameters.shape = ModulatorLfoShape::TRIANGLE;
    sharedDraft.parameters.retrigger = ModulatorRetriggerPolicy::TRANSPORT;
    const auto shared = createLfoModulator(graph, sharedDraft);
    assert(shared.changed());

    ModulatorLfoDraft localDraft = sharedDraft;
    localDraft.name = "Second LFO";
    localDraft.parameters.shape = ModulatorLfoShape::SAW_UP;
    const auto local = createLfoModulator(graph, localDraft);
    assert(local.changed());

    ModulationBindingDraft binding{};
    binding.sourceId = shared.sourceId;
    binding.destination = projectControlDestination(address);
    binding.amountQ15 = 8192;
    assert(addProjectModulationBinding(graph, binding).changed());
    binding.destination = projectControlDestination(otherAddress);
    binding.amountQ15 = -4096;
    assert(addProjectModulationBinding(graph, binding).changed());
    binding.sourceId = local.sourceId;
    binding.destination = projectControlDestination(address);
    binding.amountQ15 = 12288;
    assert(addProjectModulationBinding(graph, binding).changed());
    state.pages.control.markAuthoredMutation();

    services.setManualValue(0, 0.3f);
    const auto graphBefore = graph;
    const auto runtimeBefore = state.pages.control.runtime;
    assert(state.macroHistory.undoCount() == 0U);
    assert(recordHoldAutomationTake(
        services, 0U, 1000U, 0.3f, 1500U, 0.8f, 2000U
    ));
    assert(state.macroUi.automationRecordingStatus.get() ==
           core::state::macro::MacroAutomationRecordingStatus::IDLE);
    assert(state.macroHistory.undoCount() == 1U);
    assert(std::memcmp(&graph, &graphBefore, sizeof(graph)) == 0);
    assert(std::memcmp(
        &state.pages.control.runtime,
        &runtimeBefore,
        sizeof(runtimeBefore)
    ) == 0);
    auto slot = test_support::project_control::readSlot(
        state.pages.control,
        address
    );
    assert(slot.automationEnabled);
    assert(graph.sourceCount == 2U);
    assert(graph.outputBindingCount == 3U);

    assert(state.macroHistory.undo(state.pages));
    assert(std::memcmp(&graph, &graphBefore, sizeof(graph)) == 0);
    assert(std::memcmp(
        &state.pages.control.runtime,
        &runtimeBefore,
        sizeof(runtimeBefore)
    ) == 0);
    slot = test_support::project_control::readSlot(state.pages.control, address);
    assert(!slot.automationStored);
    assert(slot.modulationStored);

    assert(state.macroHistory.redo(state.pages));
    assert(std::memcmp(&graph, &graphBefore, sizeof(graph)) == 0);
    assert(std::memcmp(
        &state.pages.control.runtime,
        &runtimeBefore,
        sizeof(runtimeBefore)
    ) == 0);
    slot = test_support::project_control::readSlot(state.pages.control, address);
    assert(slot.automationEnabled);
    assert(slot.compatibility.automation.pointCount >= 2U);
    const auto first = test_support::project_control::readCurvePoint(
        state.pages.control,
        slot.automationCurveId,
        0,
        false
    );
    const auto second = test_support::project_control::readCurvePoint(
        state.pages.control,
        slot.automationCurveId,
        static_cast<uint16_t>(slot.compatibility.automation.pointCount - 1U),
        false
    );
    assert(std::fabs(first.value - 0.3f) < 0.005f);
    assert(std::fabs(second.value - 0.8f) < 0.005f);

    services.setManualValue(0, 0.6f);
    assert(recordHoldAutomationTake(
        services, 0U, 3000U, 0.6f, 3500U, 0.1f, 4000U
    ));
    assert(state.macroHistory.undoCount() == 2U);
    assert(std::memcmp(&graph, &graphBefore, sizeof(graph)) == 0);
    slot = test_support::project_control::readSlot(state.pages.control, address);
    auto replacementFirst = test_support::project_control::readCurvePoint(
        state.pages.control,
        slot.automationCurveId,
        0,
        false
    );
    auto replacementSecond = test_support::project_control::readCurvePoint(
        state.pages.control,
        slot.automationCurveId,
        static_cast<uint16_t>(slot.compatibility.automation.pointCount - 1U),
        false
    );
    assert(std::fabs(replacementFirst.value - 0.6f) < 0.005f);
    assert(std::fabs(replacementSecond.value - 0.1f) < 0.005f);

    assert(state.macroHistory.undo(state.pages));
    assert(std::memcmp(&graph, &graphBefore, sizeof(graph)) == 0);
    slot = test_support::project_control::readSlot(state.pages.control, address);
    replacementFirst = test_support::project_control::readCurvePoint(
        state.pages.control,
        slot.automationCurveId,
        0,
        false
    );
    replacementSecond = test_support::project_control::readCurvePoint(
        state.pages.control,
        slot.automationCurveId,
        static_cast<uint16_t>(slot.compatibility.automation.pointCount - 1U),
        false
    );
    assert(std::fabs(replacementFirst.value - first.value) < 0.0001f);
    assert(std::fabs(replacementSecond.value - second.value) < 0.0001f);

    assert(state.macroHistory.redo(state.pages));
    assert(std::memcmp(&graph, &graphBefore, sizeof(graph)) == 0);
    slot = test_support::project_control::readSlot(state.pages.control, address);
    replacementFirst = test_support::project_control::readCurvePoint(
        state.pages.control,
        slot.automationCurveId,
        0,
        false
    );
    replacementSecond = test_support::project_control::readCurvePoint(
        state.pages.control,
        slot.automationCurveId,
        static_cast<uint16_t>(slot.compatibility.automation.pointCount - 1U),
        false
    );
    assert(std::fabs(replacementFirst.value - 0.6f) < 0.005f);
    assert(std::fabs(replacementSecond.value - 0.1f) < 0.005f);
    std::cout
        << "[PASS] Automation take preserves shared LFO graph through Undo/Redo\n";
}

void test_failed_first_automation_take_does_not_leave_an_empty_slot() {
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
    fillAutomationPointPoolExcept(state.pages.control, address);
    const uint16_t entryCountBefore =
        state.pages.control.authored.automation.entryCount;

    assert(services.armAutomationTake());
    assert(!services.recordAutomationTakeValue(0U, 1000U, 0.25f));
    assert(state.macroUi.automationRecordingStatus.get() ==
           core::state::macro::MacroAutomationRecordingStatus::COMMIT_FAILED);

    assert(!services.automationTakeRecording());
    assert(!test_support::project_control::readSlot(
        state.pages.control,
        address
    ).present);
    assert(state.pages.control.authored.automation.entryCount == entryCountBefore);
    assert(state.pages.control.authored.curves.pointCount ==
           core::state::modulation::PROJECT_CURVE_POINT_CAPACITY);
    assert(!state.project.metadata.dirty);

    std::cout << "[PASS] failed first take leaves no empty slot\n";
}

void test_macro_edit_automation_lifecycle_actions() {
    CoreStorages storage;

    core::state::CoreState state(storage.settings,
                                 storage.macroLibrary,
                                 storage.sequencerPatternLibrary,
                                 storage.sequencerSetLibrary);
    const auto performance = core::handler::MacroPerformanceDomainServices::fromCoreState(state);
    const auto edit = core::handler::MacroEditDomainServices::fromCoreState(state);

    assert(recordHoldAutomationTake(
        performance, 0U, 1000U, 0.25f, 1500U, 0.75f, 2000U
    ));

    assert(edit.copyAutomation(0));
    assert(state.structureClipboard.hasMacroAutomation());
    assert(edit.removeAutomation(0));
    assert(edit.automationSlot(0) == nullptr);

    assert(edit.pasteAutomation(0));
    const auto* pasted = edit.automationSlot(0);
    assert(pasted != nullptr);
    assert(pasted->automation.active);
    assert(pasted->automation.pointCount >= 2U);

    assert(edit.clearAutomation(0));
    const auto* cleared = edit.automationSlot(0);
    assert(cleared == nullptr);
    assert(state.pages.isMacroSlotActive(0));
    assert(!test_support::project_control::readSlot(
        state.pages.control,
        {state.pages.currentActiveTrack(), state.pages.currentActivePage(), 0}
    ).automationStored);

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
    configureAutomation(state.pages.control, sourceAddress);
    auto source = configureModulation(state.pages.control, sourceAddress, 0.37f);
    assert(core::state::modulation::setProjectControlModulationEnabled(
        state.pages.control,
        sourceAddress,
        false
    ));
    source = test_support::project_control::readSlot(
        state.pages.control,
        sourceAddress
    );
    auto* sourceCurve = test_support::project_control::mutableCurve(
        state.pages.control,
        source.modulationCurveId
    );
    assert(sourceCurve != nullptr);
    sourceCurve->origin =
        core::state::modulation::ProjectCurveOrigin::CONVERTED_FIRST;

    auto target = configureAutomation(state.pages.control, targetAddress);
    core::state::macro::MacroAutomationLane distinctTargetAutomation;
    assert(core::state::macro::macroAutomationAppendPoint(
        distinctTargetAutomation, 0.0f, 0.1f
    ));
    assert(core::state::macro::macroAutomationAppendPoint(
        distinctTargetAutomation, 1.0f, 0.9f
    ));
    assert(test_support::project_control::assignAutomation(
        state.pages.control,
        targetAddress,
        distinctTargetAutomation
    ));
    target = configureModulation(state.pages.control, targetAddress, 0.82f);
    const auto targetAutomationBefore = target.automationCurveId;
    const auto targetModulationBefore = target.modulationCurveId;
    std::array<core::state::macro::MacroCurvePoint, 2> targetAutomationPoints{};
    for (uint16_t i = 0; i < targetAutomationPoints.size(); ++i) {
        targetAutomationPoints[i] = test_support::project_control::readCurvePoint(
            state.pages.control,
            targetAutomationBefore,
            i,
            false
        );
    }

    const auto sourceBindingBefore =
        state.pages.control.authored.modulation.outputBindings[0];
    const auto targetBindingBefore =
        state.pages.control.authored.modulation.outputBindings[1];
    const uint16_t sourceCountBefore =
        state.pages.control.authored.modulation.sourceCount;
    assert(edit.copyModulation(0));
    assert(state.structureClipboard.hasMacroModulationAssignment());
    const auto plan = edit.preflightModulationPaste(1);
    assert(plan.actionable());
    assert(!plan.requiresOverwrite());
    assert(edit.pasteModulation(1, false));

    source = test_support::project_control::readSlot(
        state.pages.control,
        sourceAddress
    );
    target = test_support::project_control::readSlot(
        state.pages.control,
        targetAddress
    );
    assert(page.cc[1] == 11);
    assert(std::fabs(page.values[1] - 0.66f) < 0.0001f);
    const auto& graph = state.pages.control.authored.modulation;
    assert(graph.sourceCount == sourceCountBefore);
    assert(graph.outputBindingCount == 3U);
    assert(std::memcmp(
        &graph.outputBindings[0],
        &sourceBindingBefore,
        sizeof(sourceBindingBefore)
    ) == 0);
    assert(std::memcmp(
        &graph.outputBindings[1],
        &targetBindingBefore,
        sizeof(targetBindingBefore)
    ) == 0);
    const auto& pastedBinding = graph.outputBindings[2];
    assert(pastedBinding.sourceId == source.modulationSourceId);
    assert(pastedBinding.destination ==
           core::state::modulation::projectControlDestination(targetAddress));
    assert(pastedBinding.amountQ15 == sourceBindingBefore.amountQ15);
    assert(target.automationCurveId == targetAutomationBefore);
    assert(target.compatibility.automation.pointCount == targetAutomationPoints.size());
    for (uint16_t i = 0; i < targetAutomationPoints.size(); ++i) {
        const auto actual = test_support::project_control::readCurvePoint(
            state.pages.control,
            target.automationCurveId,
            i,
            false
        );
        assert(std::fabs(actual.beat - targetAutomationPoints[i].beat) < 0.0001f);
        assert(std::fabs(actual.value - targetAutomationPoints[i].value) < 0.0001f);
    }
    assert(target.modulationCount == 2U);
    assert(target.modulationCurveId == targetModulationBefore);
    assert(std::fabs(target.compatibility.modulationDepth - 0.82f) < 0.0001f);

    std::cout
        << "[PASS] modulation assignment Paste preserves target and shares source\n";
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
    configureAutomation(state.pages.control, sourceAddress);
    auto source = configureModulation(state.pages.control, sourceAddress, 0.43f);
    assert(core::state::modulation::setProjectControlAutomationEnabled(
        state.pages.control,
        sourceAddress,
        false
    ));
    source = test_support::project_control::readSlot(
        state.pages.control,
        sourceAddress
    );
    auto* sourceModulationCurve = test_support::project_control::mutableCurve(
        state.pages.control,
        source.modulationCurveId
    );
    assert(sourceModulationCurve != nullptr);
    sourceModulationCurve->origin =
        core::state::modulation::ProjectCurveOrigin::CONVERTED_MIN;
    configureAutomation(state.pages.control, targetAddress);
    configureModulation(state.pages.control, targetAddress, 0.9f);

    assert(edit.copySlot(0));
    const auto plan = edit.preflightSlotPaste(2);
    assert(plan.actionable());
    assert(plan.requiresOverwrite());
    assert(edit.pasteSlot(2, true));

    source = test_support::project_control::readSlot(
        state.pages.control,
        sourceAddress
    );
    const auto target = test_support::project_control::readSlot(
        state.pages.control,
        targetAddress
    );
    assert(page.isMacroActive(2));
    assert(page.cc[2] == 74);
    assert(std::fabs(page.values[2] - 0.42f) < 0.0001f);
    assertCurvePayloadEquals(
        state.pages.control,
        source.automationCurveId,
        target.automationCurveId,
        false
    );
    assertCurvePayloadEquals(
        state.pages.control,
        source.modulationCurveId,
        target.modulationCurveId,
        true
    );
    assert(std::fabs(target.compatibility.modulationDepth - 0.43f) < 0.0001f);

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
    state.pages.pageData(0, 0).setMacroActive(3, true);
    state.pages.pageData(0, 0).setMacroActive(5, true);
    state.pages.pageData(0, 0).cc[0] = 74;
    state.pages.pageData(0, 0).values[0] = 0.36f;
    state.pages.pageData(0, 0).cc[3] = 33;
    state.pages.pageData(0, 0).values[3] = 0.44f;
    state.pages.pageData(0, 0).cc[5] = 55;
    state.pages.pageData(0, 0).values[5] = 0.66f;
    configureAutomation(state.pages.control, source);
    auto sourceSlot = configureModulation(state.pages.control, source, 0.33f);
    assert(core::state::modulation::setProjectControlAutomationEnabled(
        state.pages.control,
        source,
        false
    ));
    sourceSlot = test_support::project_control::readSlot(
        state.pages.control,
        source
    );
    auto* sourceCurve = test_support::project_control::mutableCurve(
        state.pages.control,
        sourceSlot.modulationCurveId
    );
    assert(sourceCurve != nullptr);
    sourceCurve->origin =
        core::state::modulation::ProjectCurveOrigin::CONVERTED_MEAN;

    assert(state.structureClipboard.storeMacroPage(
        state.pages.pageData(0, 0),
        state.pages.control,
        0,
        0
    ));
    assert(structure.pastePage(
        1,
        state.structureClipboard.macroPage,
        state.structureClipboard.macroAutomationSet.get()
    ));
    sourceSlot = test_support::project_control::readSlot(
        state.pages.control,
        source
    );
    auto targetSlot = test_support::project_control::readSlot(
        state.pages.control,
        pageTarget
    );
    assertCurvePayloadEquals(
        state.pages.control,
        sourceSlot.automationCurveId,
        targetSlot.automationCurveId,
        false
    );
    assertCurvePayloadEquals(
        state.pages.control,
        sourceSlot.modulationCurveId,
        targetSlot.modulationCurveId,
        true
    );
    assert(std::fabs(targetSlot.compatibility.modulationDepth - 0.33f) < 0.0001f);
    assert(state.pages.pageData(0, 1).cc[0] == 74);
    assert(std::fabs(state.pages.pageData(0, 1).values[0] - 0.36f) < 0.0001f);
    assert(state.pages.pageData(0, 1).activeMacroMask == 0x29U);
    assert(state.pages.pageData(0, 1).cc[3] == 33U);
    assert(std::fabs(state.pages.pageData(0, 1).values[3] - 0.44f) < 0.0001f);
    assert(state.pages.pageData(0, 1).cc[5] == 55U);
    assert(std::fabs(state.pages.pageData(0, 1).values[5] - 0.66f) < 0.0001f);

    assert(state.structureClipboard.storeMacroTrack(
        state.pages.tracks[0],
        state.pages.control,
        0
    ));
    assert(structure.pasteTrack(
        1,
        state.structureClipboard.macroTrack,
        state.structureClipboard.macroAutomationSet.get()
    ));
    sourceSlot = test_support::project_control::readSlot(
        state.pages.control,
        source
    );
    targetSlot = test_support::project_control::readSlot(
        state.pages.control,
        trackTarget
    );
    assertCurvePayloadEquals(
        state.pages.control,
        sourceSlot.automationCurveId,
        targetSlot.automationCurveId,
        false
    );
    assertCurvePayloadEquals(
        state.pages.control,
        sourceSlot.modulationCurveId,
        targetSlot.modulationCurveId,
        true
    );
    assert(std::fabs(targetSlot.compatibility.modulationDepth - 0.33f) < 0.0001f);
    assert(state.pages.pageData(1, 0).cc[0] == 74);
    assert(std::fabs(state.pages.pageData(1, 0).values[0] - 0.36f) < 0.0001f);
    assert(state.pages.pageData(1, 0).activeMacroMask == 0x29U);
    assert(state.pages.pageData(1, 0).cc[3] == 33U);
    assert(std::fabs(state.pages.pageData(1, 0).values[3] - 0.44f) < 0.0001f);
    assert(state.pages.pageData(1, 0).cc[5] == 55U);
    assert(std::fabs(state.pages.pageData(1, 0).values[5] - 0.66f) < 0.0001f);

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
    configureAutomation(state.pages.control, source);
    configureAutomation(state.pages.control, pageTarget);
    configureAutomation(state.pages.control, trackTarget);
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
        state.pages.control,
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
        state.pages.control,
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

    configureAutomation(state.pages.control, source);
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
    test_macro_slot_activation_is_sparse_and_marks_project_dirty();
    test_addressed_macro_slot_activation_preserves_cold_page_cache();
    test_destination_activation_keeps_structure_contiguous_and_macros_sparse();
    test_automation_take_commits_to_current_macro_slot();
    test_automation_take_cancel_discards_session();
    test_shared_automation_take_records_late_join_and_one_undo();
    test_take_cancel_restores_manual_and_preflight_failure_is_clean();
    test_armed_automation_take_without_motion_does_not_create_slot();
    test_cancelled_automation_take_restores_previous_manual_state();
    test_automation_take_preserves_active_modulation_without_resume();
    test_automation_take_preserves_shared_lfos_through_undo_redo();
    test_failed_first_automation_take_does_not_leave_an_empty_slot();
    test_macro_edit_automation_lifecycle_actions();
    test_modulation_copy_paste_preserves_target_and_exact_payload();
    test_typed_slot_copy_paste_preserves_automation_and_modulation();
    test_page_and_track_copy_preserve_automation_and_modulation();
    test_slot_page_and_track_replacement_invalidate_only_targeted_manual_entries();
    std::cout << "\nAll MacroPerformanceDomainServices tests passed.\n";
    return 0;
}
