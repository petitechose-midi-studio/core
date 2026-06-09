#include <cassert>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <iostream>

#include <oc/api/ButtonAPI.hpp>
#include <oc/api/EncoderAPI.hpp>
#include <oc/context/OverlayManager.hpp>
#include <oc/core/event/EventBus.hpp>
#include <oc/core/event/Events.hpp>
#include <oc/core/input/InputBinding.hpp>
#include <oc/impl/HostFileSystem.hpp>

#include "../../src/handler/project/ProjectHandler.hpp"
#include "../../src/handler/sequencer/SequencerHistoryDomainServices.hpp"
#include "../../src/handler/settings/SequencerSettingsDomainServices.hpp"
#include "../../src/persistence/ProjectFileStore.hpp"
#include "../../src/persistence/ProductFileService.hpp"
#include "../../src/state/CoreState.hpp"
#include "../../src/state/macro/MacroWorkflow.hpp"
#include "../../src/state/project/ProjectSnapshot.hpp"
#include "../support/CoreStorages.hpp"
#include "../support/InputTestHardware.hpp"

namespace {

uint32_t g_now_ms = 0;

uint32_t mockTimeMs() {
    return g_now_ms;
}

bool near(float actual, float expected, float epsilon = 0.001f) {
    return std::fabs(actual - expected) <= epsilon;
}

std::filesystem::path projectHandlerFsRoot() {
    return std::filesystem::temp_directory_path() / "midi-studio-core-project-handler-test";
}

using test_support::TestButtonHardware;
using test_support::TestEncoderHardware;
using core::state::project::ProjectNodeId;
using core::state::project::ProjectTab;
using core::state::sequencer::SequencerHistoryScope;

struct ProjectHandlerHarness {
    static constexpr oc::type::ScopeID PROJECT_SCOPE = 901;

    std::string projectFsRootPath;
    oc::impl::HostFileSystem projectFilesystem;
    core::persistence::ProductFileService productFiles;
    test_support::CoreStorages storages;
    core::state::CoreState state;

    oc::core::event::EventBus eventBus;
    oc::core::input::InputBinding inputBinding;
    TestButtonHardware buttonHw;
    TestEncoderHardware encoderHw;
    oc::api::ButtonAPI buttons;
    oc::api::EncoderAPI encoders;
    oc::context::OverlayManager<core::ui::OverlayType> overlays;
    core::handler::SequencerSettingsDomainServices sequencerSettings;
    core::handler::ProjectHandler handler;

    ProjectHandlerHarness()
        : projectFsRootPath(projectHandlerFsRoot().string())
        , projectFilesystem(projectFsRootPath.c_str())
        , productFiles(projectFilesystem)
        , state(storages.settings,
                storages.macroWorkspace,
                storages.macroLibrary,
                storages.sequencerWorkspace,
                storages.sequencerPatternLibrary,
                storages.sequencerSetLibrary)
        , inputBinding(eventBus, mockTimeMs)
        , buttons(inputBinding, buttonHw)
        , encoders(inputBinding, encoderHw)
        , overlays(state.overlays, buttons)
        , sequencerSettings(core::handler::SequencerSettingsDomainServices::StateRefs{
              state.sequencer,
              state.sequencerTracks,
          })
        , handler(core::handler::ProjectHandler::StateRefs{
                      state.overlays,
                      state.projectNavigation,
                      state.sequencer,
                      state.sequencerTracks,
                      state.statusBar,
                      state.midiSync,
                      core::handler::SequencerHistoryDomainServices::fromCoreState(state),
                      core::handler::ProjectLifecycleDomainServices::fromCoreState(
                          state,
                          productFiles
                      ),
                  },
                  sequencerSettings,
                  encoders,
                  buttons,
                  PROJECT_SCOPE) {
        std::error_code ec;
        std::filesystem::remove_all(projectHandlerFsRoot(), ec);
        assert(productFiles.init());
        overlays.setActiveViewProvider([]() { return PROJECT_SCOPE; });
        g_now_ms = 0;
    }

    void press(Config::ButtonID id) {
        const auto buttonId = static_cast<oc::type::ButtonID>(id);
        buttonHw.setPressed(buttonId, true);
        eventBus.emit(oc::core::event::ButtonPressEvent(buttonId, true));
    }

    void release(Config::ButtonID id) {
        const auto buttonId = static_cast<oc::type::ButtonID>(id);
        buttonHw.setPressed(buttonId, false);
        eventBus.emit(oc::core::event::ButtonReleaseEvent(buttonId));
    }

    void tap(Config::ButtonID id) {
        press(id);
        release(id);
    }

    void advance(uint32_t ms) {
        g_now_ms += ms;
        inputBinding.processTick();
    }

    void turn(Config::EncoderID id, float value) {
        const auto encoderId = static_cast<oc::type::EncoderID>(id);
        encoderHw.setPosition(encoderId, value);
        eventBus.emit(oc::core::event::EncoderChangedEvent(encoderId, value));
    }
};

struct RestoredProjectHarness {
    test_support::CoreStorages storages;
    core::state::CoreState state;

    explicit RestoredProjectHarness(const core::state::project::ProjectSnapshot& snapshot)
        : state(storages.settings,
                storages.macroWorkspace,
                storages.macroLibrary,
                storages.sequencerWorkspace,
                storages.sequencerPatternLibrary,
                storages.sequencerSetLibrary) {
        assert(core::state::project::applyProjectSnapshot(state, snapshot));
    }
};

void saveCurrentProjectSnapshot(ProjectHandlerHarness& h, const char* id) {
    h.state.project.metadata.id.fill('\0');
    std::strncpy(h.state.project.metadata.id.data(), id, h.state.project.metadata.id.size() - 1U);
    h.state.project.metadata.hasSavedIdentity = true;
    core::state::project::ProjectSnapshot snapshot;
    assert(core::state::project::captureProjectSnapshot(h.state, snapshot));
    core::persistence::ProjectFileStore store(h.productFiles);
    assert(store.save(snapshot));
}

void test_nav_turn_on_overview_actions() {
    ProjectHandlerHarness h;

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.projectNavigation.focusedRow.get() == 1);

    h.turn(Config::EncoderID::NAV, -1.0f);
    assert(h.state.projectNavigation.focusedRow.get() == 0);

    h.tap(Config::ButtonID::NAV);
    assert(h.state.projectNavigation.activeTab.get() == ProjectTab::OVERVIEW);
    assert(h.state.projectNavigation.currentNode.get() == ProjectNodeId::NEW_PROJECT_CONFIRM);
    assert(h.state.projectNavigation.focusedRow.get() == 0);

    h.tap(Config::ButtonID::LEFT_TOP);
    assert(h.state.projectNavigation.currentNode.get() == ProjectNodeId::OVERVIEW_ROOT);

    std::cout << "[PASS] test_nav_turn_on_overview_actions\n";
}

void test_left_top_backs_out_of_nested_project_folder() {
    ProjectHandlerHarness h;

    h.press(Config::ButtonID::LEFT_CENTER);
    h.advance(600);
    h.turn(Config::EncoderID::NAV, 1.0f);
    h.release(Config::ButtonID::LEFT_CENTER);
    h.tap(Config::ButtonID::NAV);
    assert(h.state.projectNavigation.currentNode.get() == ProjectNodeId::MUSIC_SCALE);
    assert(h.state.projectNavigation.depth.get() == 1);

    h.tap(Config::ButtonID::LEFT_TOP);
    assert(h.state.projectNavigation.currentNode.get() == ProjectNodeId::MUSIC_ROOT);
    assert(h.state.projectNavigation.depth.get() == 0);

    std::cout << "[PASS] test_left_top_backs_out_of_nested_project_folder\n";
}

void test_left_top_does_not_back_at_project_tab_root() {
    ProjectHandlerHarness h;

    h.press(Config::ButtonID::LEFT_CENTER);
    h.advance(600);
    h.turn(Config::EncoderID::NAV, 1.0f);
    h.turn(Config::EncoderID::NAV, 1.0f);
    h.turn(Config::EncoderID::NAV, 1.0f);
    h.release(Config::ButtonID::LEFT_CENTER);
    assert(h.state.projectNavigation.currentNode.get() == ProjectNodeId::STORAGE_ROOT);
    assert(h.state.projectNavigation.depth.get() == 0);

    h.tap(Config::ButtonID::LEFT_TOP);
    assert(h.state.projectNavigation.currentNode.get() == ProjectNodeId::STORAGE_ROOT);
    assert(h.state.projectNavigation.depth.get() == 0);

    std::cout << "[PASS] test_left_top_does_not_back_at_project_tab_root\n";
}

void test_nav_press_activates_storage_autosave_only() {
    ProjectHandlerHarness h;

    h.press(Config::ButtonID::LEFT_CENTER);
    h.advance(600);
    h.turn(Config::EncoderID::NAV, 1.0f);
    h.turn(Config::EncoderID::NAV, 1.0f);
    h.turn(Config::EncoderID::NAV, 1.0f);
    h.release(Config::ButtonID::LEFT_CENTER);
    assert(h.state.projectNavigation.currentNode.get() == ProjectNodeId::STORAGE_ROOT);

    h.turn(Config::EncoderID::NAV, 1.0f);
    h.turn(Config::EncoderID::NAV, 1.0f);
    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.projectNavigation.focusedRow.get() == 3);

    h.tap(Config::ButtonID::NAV);
    assert(h.state.projectNavigation.focusedRow.get() == 3);
    assert(h.state.projectNavigation.autosaveEnabled);

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.projectNavigation.focusedRow.get() == 4);

    h.tap(Config::ButtonID::NAV);
    assert(!h.state.projectNavigation.autosaveEnabled);

    std::cout << "[PASS] test_nav_press_activates_storage_autosave_only\n";
}

void test_music_scale_root_is_wired_and_undoable() {
    ProjectHandlerHarness h;

    assert(h.state.sequencerTracks.projectScaleSettings().root == 5);

    h.press(Config::ButtonID::LEFT_CENTER);
    h.advance(600);
    h.turn(Config::EncoderID::NAV, 1.0f);
    h.release(Config::ButtonID::LEFT_CENTER);
    assert(h.state.projectNavigation.currentNode.get() == ProjectNodeId::MUSIC_ROOT);

    h.tap(Config::ButtonID::NAV);
    assert(h.state.projectNavigation.currentNode.get() == ProjectNodeId::MUSIC_SCALE);
    assert(h.state.projectNavigation.focusedRow.get() == 0);

    h.tap(Config::ButtonID::NAV);
    assert(h.state.sequencerTracks.projectScaleSettings().root == 6);
    assert(h.state.sequencerHistory.undoCount(SequencerHistoryScope::FullBank) == 1);

    h.press(Config::ButtonID::LEFT_CENTER);
    h.advance(600);
    h.tap(Config::ButtonID::LEFT_TOP);
    h.release(Config::ButtonID::LEFT_CENTER);
    assert(h.state.sequencerTracks.projectScaleSettings().root == 5);

    std::cout << "[PASS] test_music_scale_root_is_wired_and_undoable\n";
}

void test_left_center_hold_switches_tabs() {
    ProjectHandlerHarness h;

    h.press(Config::ButtonID::LEFT_CENTER);
    assert(h.state.projectNavigation.physicalHoldActive.get());

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.projectNavigation.activeTab.get() == ProjectTab::MUSIC);
    assert(h.state.projectNavigation.currentNode.get() == ProjectNodeId::MUSIC_ROOT);

    h.release(Config::ButtonID::LEFT_CENTER);
    assert(!h.state.projectNavigation.physicalHoldActive.get());

    std::cout << "[PASS] test_left_center_hold_switches_tabs\n";
}

void test_left_center_hold_respects_fast_tab_delta() {
    ProjectHandlerHarness h;

    h.press(Config::ButtonID::LEFT_CENTER);
    assert(h.state.projectNavigation.physicalHoldActive.get());

    h.turn(Config::EncoderID::NAV, 2.0f);
    assert(h.state.projectNavigation.activeTab.get() == ProjectTab::TRANSPORT);
    assert(h.state.projectNavigation.currentNode.get() == ProjectNodeId::TRANSPORT_ROOT);

    h.release(Config::ButtonID::LEFT_CENTER);

    std::cout << "[PASS] test_left_center_hold_respects_fast_tab_delta\n";
}

void test_transport_values_are_editable_from_project() {
    ProjectHandlerHarness h;
    constexpr auto OPT_ID = static_cast<oc::type::EncoderID>(Config::EncoderID::OPT);

    assert(h.state.statusBar.tempo.get() == 120.0f);
    assert(h.state.midiSync.mode.get() == core::state::MidiSyncMode::AUTO);

    h.press(Config::ButtonID::LEFT_CENTER);
    h.turn(Config::EncoderID::NAV, 2.0f);
    h.release(Config::ButtonID::LEFT_CENTER);
    assert(h.state.projectNavigation.currentNode.get() == ProjectNodeId::TRANSPORT_ROOT);
    assert(h.state.projectNavigation.focusedRow.get() == 0);
    assert(near(h.encoderHw.getPosition(OPT_ID), 100.0f / 280.0f));
    assert(near(h.encoderHw.getNormalizedTurns(OPT_ID), 280.0f / 24.0f));

    h.turn(Config::EncoderID::OPT, 1.0f);
    assert(h.state.statusBar.tempo.get() == 300.0f);
    assert(h.state.statusBar.tempoDisplay.get() == 300.0f);

    h.turn(Config::EncoderID::OPT, 0.0f);
    assert(h.state.statusBar.tempo.get() == 20.0f);
    assert(h.state.statusBar.tempoDisplay.get() == 20.0f);

    h.tap(Config::ButtonID::NAV);
    assert(h.state.statusBar.tempo.get() == 21.0f);

    h.turn(Config::EncoderID::NAV, 2.0f);
    assert(h.state.projectNavigation.focusedRow.get() == 2);
    h.turn(Config::EncoderID::NAV, -1.0f);
    assert(h.state.projectNavigation.focusedRow.get() == 1);
    assert(h.encoderHw.getDiscreteSteps(OPT_ID) == 76);
    assert(near(h.encoderHw.getNormalizedTurns(OPT_ID), 75.0f / 18.0f));

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.projectNavigation.focusedRow.get() == 2);

    h.turn(Config::EncoderID::OPT, 0.0f);
    assert(h.state.midiSync.mode.get() == core::state::MidiSyncMode::MASTER);

    std::cout << "[PASS] test_transport_values_are_editable_from_project\n";
}

void test_storage_autosave_is_editable_with_opt() {
    ProjectHandlerHarness h;

    h.press(Config::ButtonID::LEFT_CENTER);
    h.turn(Config::EncoderID::NAV, 3.0f);
    h.release(Config::ButtonID::LEFT_CENTER);
    assert(h.state.projectNavigation.currentNode.get() == ProjectNodeId::STORAGE_ROOT);

    h.turn(Config::EncoderID::NAV, 3.0f);
    assert(h.state.projectNavigation.focusedRow.get() == 3);

    h.turn(Config::EncoderID::OPT, 1.0f);
    assert(h.state.projectNavigation.autosaveEnabled);

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.projectNavigation.focusedRow.get() == 4);

    h.turn(Config::EncoderID::OPT, 0.0f);
    assert(!h.state.projectNavigation.autosaveEnabled);

    std::cout << "[PASS] test_storage_autosave_is_editable_with_opt\n";
}

void test_new_project_resets_musical_project_state() {
    ProjectHandlerHarness h;

    h.state.sequencer.pattern.length.set(16);
    h.state.sequencer.pattern.midiChannel.set(8);
    h.state.sequencer.setStepNoteAt(0, 72);
    h.state.macros.slots[0].value.set(0.91f);
    h.state.pages.activePageData().cc[0] = 99;
    h.state.pages.activePageData().values[0] = 0.91f;
    h.state.pages.setActiveTrackChannel(7);
    h.state.configRevision.set(0x1200FF);
    h.state.statusBar.tempo.set(144.0f);
    h.state.statusBar.tempoDisplay.set(144.0f);
    h.state.midiSync.mode.set(core::state::MidiSyncMode::SLAVE);
    h.state.projectNavigation.transportSwingPercent = 33;

    auto settings = h.state.sequencerTracks.projectScaleSettings();
    settings.root = 6;
    h.state.sequencerTracks.setProjectScaleSettings(settings);
    assert(h.state.sequencerTracks.projectScaleSettings().root == 6);

    h.tap(Config::ButtonID::NAV);
    assert(h.state.projectNavigation.currentNode.get() == ProjectNodeId::NEW_PROJECT_CONFIRM);
    assert(h.state.projectNavigation.focusedRow.get() == 0);

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.projectNavigation.focusedRow.get() == 1);
    h.tap(Config::ButtonID::NAV);

    assert(h.state.projectNavigation.currentNode.get() == ProjectNodeId::OVERVIEW_ROOT);
    assert(h.state.projectNavigation.focusedRow.get() == 0);
    assert(h.state.projectNavigation.transportSwingPercent == 0);
    assert(h.state.sequencer.pattern.length.get() == core::state::sequencer::SequencerPatternState::DEFAULT_LENGTH);
    assert(h.state.sequencer.pattern.midiChannel.get() == 0);
    assert(h.state.sequencer.pattern.note[0] == core::state::sequencer::SequencerState::DEFAULT_NOTE);
    assert(near(h.state.macros.slots[0].value.get(), 0.5f));
    assert(h.state.pages.activePageData().cc[0] == 0);
    assert(near(h.state.pages.activePageData().values[0], 0.5f));
    assert(h.state.pages.activeConfigs[0].channel == 0);
    assert(h.state.configRevision.get() != 0x1200FF);
    assert(h.state.sequencerTracks.projectScaleSettings().root == 5);
    assert(h.state.sequencerTracks.track(0).midiChannel.get() == 0);
    assert(h.state.sequencerTracks.track(15).midiChannel.get() == 15);
    assert(h.state.statusBar.tempo.get() == 120.0f);
    assert(h.state.statusBar.tempoDisplay.get() == 120.0f);
    assert(h.state.midiSync.mode.get() == core::state::MidiSyncMode::SLAVE);
    assert(h.state.sequencerHistory.undoCount(SequencerHistoryScope::FullBank) == 0);

    std::cout << "[PASS] test_new_project_resets_musical_project_state\n";
}

void test_new_project_confirmation_cancel_preserves_state() {
    ProjectHandlerHarness h;

    h.state.sequencer.pattern.length.set(24);
    h.state.macros.slots[0].value.set(0.77f);
    h.state.statusBar.tempo.set(132.0f);

    h.tap(Config::ButtonID::NAV);
    assert(h.state.projectNavigation.currentNode.get() == ProjectNodeId::NEW_PROJECT_CONFIRM);
    assert(h.state.projectNavigation.focusedRow.get() == 0);

    h.turn(Config::EncoderID::NAV, 2.0f);
    assert(h.state.projectNavigation.focusedRow.get() == 2);
    h.tap(Config::ButtonID::NAV);

    assert(h.state.projectNavigation.currentNode.get() == ProjectNodeId::OVERVIEW_ROOT);
    assert(h.state.projectNavigation.focusedRow.get() == 0);
    assert(h.state.sequencer.pattern.length.get() == 24);
    assert(near(h.state.macros.slots[0].value.get(), 0.77f));
    assert(h.state.statusBar.tempo.get() == 132.0f);

    std::cout << "[PASS] test_new_project_confirmation_cancel_preserves_state\n";
}

void test_new_project_save_as_new_persists_then_resets() {
    ProjectHandlerHarness h;

    h.state.statusBar.tempo.set(171.0f);
    h.state.statusBar.tempoDisplay.set(171.0f);
    h.state.projectNavigation.transportSwingPercent = 24;
    h.state.sequencer.pattern.length.set(9);
    h.state.sequencer.setStepDataAt(2, 75, 99, 64);
    h.state.sequencer.pattern.toggle(2);
    h.state.pages.activePageData().cc[0] = 88;
    h.state.pages.activePageData().values[0] = 0.66f;
    core::state::macro::MacroWorkflow::syncRuntimeFromActivePage(h.state.macros, h.state.pages);
    h.state.markProjectMutated();
    assert(!h.state.project.metadata.hasSavedIdentity);

    h.tap(Config::ButtonID::NAV);
    assert(h.state.projectNavigation.currentNode.get() == ProjectNodeId::NEW_PROJECT_CONFIRM);
    assert(h.state.projectNavigation.focusedRow.get() == 0);
    h.tap(Config::ButtonID::NAV);

    assert(h.state.projectNavigation.currentNode.get() == ProjectNodeId::OVERVIEW_ROOT);
    assert(std::strcmp(h.state.projectNavigation.lifecycleFeedback.get(), "Saved P001") == 0);
    assert(!h.state.project.metadata.hasSavedIdentity);
    assert(h.state.project.metadata.id[0] == '\0');
    assert(std::strcmp(h.state.project.metadata.name.data(), "Untitled") == 0);
    assert(h.state.statusBar.tempo.get() == 120.0f);
    assert(h.state.projectNavigation.transportSwingPercent == 0);
    assert(h.state.sequencer.pattern.length.get() == core::state::sequencer::SequencerPatternState::DEFAULT_LENGTH);
    assert(!h.state.sequencer.pattern.isEnabled(2));
    assert(near(h.state.macros.slots[0].value.get(), 0.5f));

    core::persistence::ProjectFileStore store(h.productFiles);
    core::state::project::ProjectSnapshot saved;
    assert(store.load("P001", saved));

    RestoredProjectHarness restored{saved};
    assert(std::strcmp(restored.state.project.metadata.id.data(), "P001") == 0);
    assert(std::strcmp(restored.state.project.metadata.name.data(), "Project 001") == 0);
    assert(restored.state.project.metadata.hasSavedIdentity);
    assert(!restored.state.project.metadata.dirty);
    assert(restored.state.statusBar.tempo.get() == 171.0f);
    assert(restored.state.projectNavigation.transportSwingPercent == 24);
    assert(restored.state.sequencer.pattern.length.get() == 9);
    assert(restored.state.sequencer.pattern.isEnabled(2));
    assert(restored.state.sequencer.pattern.note[2] == 75);
    assert(restored.state.sequencer.pattern.velocity[2] == 99);
    assert(restored.state.sequencer.pattern.gate[2] == 64);
    assert(restored.state.pages.activePageData().cc[0] == 88);
    assert(near(restored.state.macros.slots[0].value.get(), 0.66f));

    std::cout << "[PASS] test_new_project_save_as_new_persists_then_resets\n";
}

void test_new_project_save_current_persists_saved_identity_then_resets() {
    ProjectHandlerHarness h;

    h.state.statusBar.tempo.set(111.0f);
    h.state.statusBar.tempoDisplay.set(111.0f);
    saveCurrentProjectSnapshot(h, "P002");
    assert(h.state.project.metadata.hasSavedIdentity);
    assert(std::strcmp(h.state.project.metadata.id.data(), "P002") == 0);

    h.state.statusBar.tempo.set(188.0f);
    h.state.statusBar.tempoDisplay.set(188.0f);
    h.state.sequencer.pattern.length.set(13);
    h.state.sequencer.setStepDataAt(7, 82, 115, 71);
    h.state.sequencer.pattern.toggle(7);
    h.state.markProjectMutated();
    assert(h.state.project.metadata.dirty);

    h.tap(Config::ButtonID::NAV);
    assert(h.state.projectNavigation.currentNode.get() == ProjectNodeId::NEW_PROJECT_CONFIRM);
    assert(h.state.projectNavigation.focusedRow.get() == 0);
    h.tap(Config::ButtonID::NAV);

    assert(h.state.projectNavigation.currentNode.get() == ProjectNodeId::OVERVIEW_ROOT);
    assert(std::strcmp(h.state.projectNavigation.lifecycleFeedback.get(), "Saved P002") == 0);
    assert(!h.state.project.metadata.hasSavedIdentity);
    assert(h.state.project.metadata.id[0] == '\0');
    assert(h.state.statusBar.tempo.get() == 120.0f);
    assert(h.state.sequencer.pattern.length.get() == core::state::sequencer::SequencerPatternState::DEFAULT_LENGTH);
    assert(!h.state.sequencer.pattern.isEnabled(7));

    core::persistence::ProjectFileStore store(h.productFiles);
    core::state::project::ProjectSnapshot saved;
    assert(store.load("P002", saved));

    RestoredProjectHarness restored{saved};
    assert(std::strcmp(restored.state.project.metadata.id.data(), "P002") == 0);
    assert(restored.state.statusBar.tempo.get() == 188.0f);
    assert(restored.state.sequencer.pattern.length.get() == 13);
    assert(restored.state.sequencer.pattern.isEnabled(7));
    assert(restored.state.sequencer.pattern.note[7] == 82);
    assert(restored.state.sequencer.pattern.velocity[7] == 115);
    assert(restored.state.sequencer.pattern.gate[7] == 71);

    std::cout << "[PASS] test_new_project_save_current_persists_saved_identity_then_resets\n";
}

void test_routing_output_channels_are_editable() {
    ProjectHandlerHarness h;
    constexpr auto OPT_ID = static_cast<oc::type::EncoderID>(Config::EncoderID::OPT);

    h.press(Config::ButtonID::LEFT_CENTER);
    h.turn(Config::EncoderID::NAV, 4.0f);
    h.release(Config::ButtonID::LEFT_CENTER);
    assert(h.state.projectNavigation.currentNode.get() == ProjectNodeId::ROUTING_ROOT);
    assert(h.state.projectNavigation.focusedRow.get() == 0);
    assert(h.encoderHw.getDiscreteSteps(OPT_ID) == 16);

    h.tap(Config::ButtonID::NAV);
    assert(h.state.sequencer.pattern.midiChannel.get() == 1);
    assert(h.state.sequencerTracks.track(0).midiChannel.get() == 1);

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.projectNavigation.focusedRow.get() == 1);

    h.turn(Config::EncoderID::OPT, 1.0f);
    assert(h.state.sequencer.pattern.midiChannel.get() == 1);
    assert(h.state.sequencerTracks.track(1).midiChannel.get() == 15);

    h.turn(Config::EncoderID::OPT, 0.0f);
    assert(h.state.sequencerTracks.track(1).midiChannel.get() == 0);

    std::cout << "[PASS] test_routing_output_channels_are_editable\n";
}

void test_overview_save_and_load_roundtrip_project_file() {
    ProjectHandlerHarness h;

    std::strncpy(
        h.state.project.metadata.name.data(),
        "HandlerSave",
        h.state.project.metadata.name.size() - 1U
    );
    h.state.statusBar.tempo.set(149.0f);
    h.state.statusBar.tempoDisplay.set(149.0f);
    h.state.projectNavigation.transportSwingPercent = 19;
    h.state.sequencer.pattern.length.set(11);
    h.state.sequencer.setStepDataAt(2, 67, 101, 75);
    h.state.sequencer.pattern.toggle(2);
    h.state.pages.activePageData().cc[0] = 81;
    h.state.pages.activePageData().values[0] = 0.63f;
    core::state::macro::MacroWorkflow::syncRuntimeFromActivePage(h.state.macros, h.state.pages);

    h.turn(Config::EncoderID::NAV, 2.0f);
    assert(h.state.projectNavigation.focusedRow.get() == 2);
    h.tap(Config::ButtonID::NAV);
    assert(h.state.project.metadata.hasSavedIdentity);
    assert(!h.state.project.metadata.dirty);
    assert(std::strcmp(h.state.projectNavigation.lifecycleFeedback.get(), "Saved P001") == 0);

    h.state.statusBar.tempo.set(88.0f);
    h.state.statusBar.tempoDisplay.set(88.0f);
    h.state.projectNavigation.transportSwingPercent = 0;
    h.state.sequencer.pattern.length.set(4);
    h.state.sequencer.setStepDataAt(2, 40, 1, 1);
    if (h.state.sequencer.pattern.isEnabled(2)) {
        h.state.sequencer.pattern.toggle(2);
    }
    h.state.pages.activePageData().cc[0] = 1;
    h.state.pages.activePageData().values[0] = 0.01f;
    core::state::macro::MacroWorkflow::syncRuntimeFromActivePage(h.state.macros, h.state.pages);

    h.turn(Config::EncoderID::NAV, -1.0f);
    assert(h.state.projectNavigation.focusedRow.get() == 1);
    assert(h.state.projectNavigation.lifecycleFeedback.empty());
    h.tap(Config::ButtonID::NAV);
    assert(h.state.projectNavigation.currentNode.get() == ProjectNodeId::LOAD_PROJECT);
    assert(h.state.projectNavigation.loadProjects.count == 1);
    assert(std::strcmp(h.state.projectNavigation.loadProjects.entries[0].id.data(), "P001") == 0);

    h.tap(Config::ButtonID::NAV);

    assert(std::strcmp(h.state.projectNavigation.lifecycleFeedback.get(), "Loaded P001") == 0);
    assert(std::strcmp(h.state.project.metadata.name.data(), "HandlerSave") == 0);
    assert(h.state.statusBar.tempo.get() == 149.0f);
    assert(h.state.projectNavigation.transportSwingPercent == 19);
    assert(h.state.sequencer.pattern.length.get() == 11);
    assert(h.state.sequencer.pattern.note[2] == 67);
    assert(h.state.sequencer.pattern.velocity[2] == 101);
    assert(h.state.sequencer.pattern.gate[2] == 75);
    assert(h.state.sequencer.pattern.isEnabled(2));
    assert(h.state.pages.activePageData().cc[0] == 81);
    assert(near(h.state.macros.slots[0].value.get(), 0.63f));

    std::cout << "[PASS] test_overview_save_and_load_roundtrip_project_file\n";
}

void test_overview_load_missing_project_reports_failure() {
    ProjectHandlerHarness h;
    h.state.statusBar.tempo.set(133.0f);

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.projectNavigation.focusedRow.get() == 1);
    h.tap(Config::ButtonID::NAV);

    assert(h.state.projectNavigation.currentNode.get() == ProjectNodeId::LOAD_PROJECT);
    assert(h.state.projectNavigation.loadProjects.count == 0);
    assert(std::strcmp(h.state.projectNavigation.lifecycleFeedback.get(), "No projects") == 0);
    h.tap(Config::ButtonID::NAV);
    assert(std::strcmp(h.state.projectNavigation.lifecycleFeedback.get(), "No projects") == 0);
    assert(h.state.statusBar.tempo.get() == 133.0f);

    std::cout << "[PASS] test_overview_load_missing_project_reports_failure\n";
}

void test_load_project_picker_selects_detected_project() {
    ProjectHandlerHarness h;

    h.state.statusBar.tempo.set(101.0f);
    h.state.statusBar.tempoDisplay.set(101.0f);
    saveCurrentProjectSnapshot(h, "P001");

    h.state.statusBar.tempo.set(153.0f);
    h.state.statusBar.tempoDisplay.set(153.0f);
    saveCurrentProjectSnapshot(h, "P003");

    h.state.statusBar.tempo.set(66.0f);
    h.state.statusBar.tempoDisplay.set(66.0f);

    h.turn(Config::EncoderID::NAV, 1.0f);
    h.tap(Config::ButtonID::NAV);
    assert(h.state.projectNavigation.currentNode.get() == ProjectNodeId::LOAD_PROJECT);
    assert(h.state.projectNavigation.loadProjects.count == 2);
    assert(std::strcmp(h.state.projectNavigation.loadProjects.entries[0].id.data(), "P001") == 0);
    assert(std::strcmp(h.state.projectNavigation.loadProjects.entries[1].id.data(), "P003") == 0);

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.projectNavigation.focusedRow.get() == 1);
    h.tap(Config::ButtonID::NAV);

    assert(std::strcmp(h.state.projectNavigation.lifecycleFeedback.get(), "Loaded P003") == 0);
    assert(std::strcmp(h.state.project.metadata.id.data(), "P003") == 0);
    assert(h.state.statusBar.tempo.get() == 153.0f);

    std::cout << "[PASS] test_load_project_picker_selects_detected_project\n";
}

void test_dirty_project_load_prompts_save_and_preserves_latest_edits() {
    ProjectHandlerHarness h;

    h.state.statusBar.tempo.set(121.0f);
    h.state.statusBar.tempoDisplay.set(121.0f);
    h.state.sequencer.pattern.length.set(8);
    h.state.sequencer.setStepDataAt(4, 60, 90, 70);
    h.state.sequencer.pattern.toggle(4);

    h.turn(Config::EncoderID::NAV, 2.0f);
    assert(h.state.projectNavigation.focusedRow.get() == 2);
    h.tap(Config::ButtonID::NAV);
    assert(std::strcmp(h.state.projectNavigation.lifecycleFeedback.get(), "Saved P001") == 0);
    assert(h.state.project.metadata.hasSavedIdentity);
    assert(!h.state.project.metadata.dirty);

    h.state.statusBar.tempo.set(166.0f);
    h.state.statusBar.tempoDisplay.set(166.0f);
    h.state.sequencer.setStepDataAt(5, 74, 111, 82);
    h.state.sequencer.pattern.toggle(5);
    h.state.markProjectMutated();
    assert(h.state.project.metadata.dirty);

    h.turn(Config::EncoderID::NAV, -1.0f);
    assert(h.state.projectNavigation.focusedRow.get() == 1);
    h.tap(Config::ButtonID::NAV);
    assert(h.state.projectNavigation.currentNode.get() == ProjectNodeId::LOAD_PROJECT);
    assert(h.state.projectNavigation.loadProjects.count == 1);

    h.tap(Config::ButtonID::NAV);
    assert(h.state.projectNavigation.currentNode.get() == ProjectNodeId::LOAD_PROJECT_CONFIRM);
    assert(h.state.projectNavigation.focusedRow.get() == 0);

    h.tap(Config::ButtonID::NAV);
    assert(h.state.projectNavigation.currentNode.get() == ProjectNodeId::LOAD_PROJECT);
    assert(std::strcmp(h.state.projectNavigation.lifecycleFeedback.get(), "Loaded P001") == 0);
    assert(!h.state.project.metadata.dirty);
    assert(h.state.statusBar.tempo.get() == 166.0f);
    assert(h.state.sequencer.pattern.isEnabled(5));
    assert(h.state.sequencer.pattern.note[5] == 74);
    assert(h.state.sequencer.pattern.velocity[5] == 111);

    core::persistence::ProjectFileStore store(h.productFiles);
    core::state::project::ProjectSnapshot loaded;
    assert(store.load("P001", loaded));

    RestoredProjectHarness restored{loaded};
    assert(restored.state.statusBar.tempo.get() == 166.0f);
    assert(restored.state.sequencer.pattern.isEnabled(5));
    assert(restored.state.sequencer.pattern.note[5] == 74);
    assert(restored.state.sequencer.pattern.velocity[5] == 111);

    std::cout << "[PASS] test_dirty_project_load_prompts_save_and_preserves_latest_edits\n";
}

void test_untitled_dirty_load_prompts_save_as_and_then_loads_target() {
    ProjectHandlerHarness h;

    h.state.statusBar.tempo.set(144.0f);
    h.state.statusBar.tempoDisplay.set(144.0f);
    saveCurrentProjectSnapshot(h, "P002");

    h.state.resetMusicalProject();
    assert(!h.state.project.metadata.hasSavedIdentity);
    assert(h.state.project.metadata.id[0] == '\0');
    assert(std::strcmp(h.state.project.metadata.name.data(), "Untitled") == 0);

    h.state.statusBar.tempo.set(177.0f);
    h.state.statusBar.tempoDisplay.set(177.0f);
    h.state.sequencer.setStepDataAt(3, 71, 100, 76);
    h.state.sequencer.pattern.toggle(3);
    h.state.markProjectMutated();
    assert(h.state.project.metadata.dirty);

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.projectNavigation.focusedRow.get() == 1);
    h.tap(Config::ButtonID::NAV);
    assert(h.state.projectNavigation.currentNode.get() == ProjectNodeId::LOAD_PROJECT);
    assert(h.state.projectNavigation.loadProjects.count == 1);
    assert(std::strcmp(h.state.projectNavigation.loadProjects.entries[0].id.data(), "P002") == 0);

    h.tap(Config::ButtonID::NAV);
    assert(h.state.projectNavigation.currentNode.get() == ProjectNodeId::LOAD_PROJECT_CONFIRM);
    assert(h.state.projectNavigation.focusedRow.get() == 0);

    h.tap(Config::ButtonID::NAV);
    assert(h.state.projectNavigation.currentNode.get() == ProjectNodeId::LOAD_PROJECT);
    assert(std::strcmp(h.state.projectNavigation.lifecycleFeedback.get(), "Loaded P002") == 0);
    assert(std::strcmp(h.state.project.metadata.id.data(), "P002") == 0);
    assert(h.state.statusBar.tempo.get() == 144.0f);

    core::persistence::ProjectFileStore store(h.productFiles);
    core::state::project::ProjectSnapshot savedUntitled;
    assert(store.load("P001", savedUntitled));

    RestoredProjectHarness restored{savedUntitled};
    assert(std::strcmp(restored.state.project.metadata.id.data(), "P001") == 0);
    assert(std::strcmp(restored.state.project.metadata.name.data(), "Project 001") == 0);
    assert(!restored.state.project.metadata.dirty);
    assert(restored.state.project.metadata.hasSavedIdentity);
    assert(restored.state.statusBar.tempo.get() == 177.0f);
    assert(restored.state.sequencer.pattern.isEnabled(3));
    assert(restored.state.sequencer.pattern.note[3] == 71);

    std::cout << "[PASS] test_untitled_dirty_load_prompts_save_as_and_then_loads_target\n";
}

}  // namespace

int main() {
    test_nav_turn_on_overview_actions();
    test_left_top_backs_out_of_nested_project_folder();
    test_left_top_does_not_back_at_project_tab_root();
    test_nav_press_activates_storage_autosave_only();
    test_music_scale_root_is_wired_and_undoable();
    test_left_center_hold_switches_tabs();
    test_left_center_hold_respects_fast_tab_delta();
    test_transport_values_are_editable_from_project();
    test_storage_autosave_is_editable_with_opt();
    test_new_project_resets_musical_project_state();
    test_new_project_confirmation_cancel_preserves_state();
    test_new_project_save_as_new_persists_then_resets();
    test_new_project_save_current_persists_saved_identity_then_resets();
    test_routing_output_channels_are_editable();
    test_overview_save_and_load_roundtrip_project_file();
    test_overview_load_missing_project_reports_failure();
    test_load_project_picker_selects_detected_project();
    test_dirty_project_load_prompts_save_and_preserves_latest_edits();
    test_untitled_dirty_load_prompts_save_as_and_then_loads_target();

    std::cout << "\nAll ProjectHandler tests passed.\n";
    return 0;
}
