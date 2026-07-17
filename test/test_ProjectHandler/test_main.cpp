#include <cassert>
#include <array>
#include <cmath>
#include <cstdio>
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
#include "../../src/persistence/ProjectFileContainer.hpp"
#include "../../src/persistence/ProjectFileStore.hpp"
#include "../../src/persistence/ProjectSnapshotPersistenceCodec.hpp"
#include "../../src/persistence/ProjectStatePersistenceCodec.hpp"
#include "../../src/persistence/ProductFileService.hpp"
#include "../../src/state/CoreState.hpp"
#include "../../src/state/macro/MacroWorkflow.hpp"
#include "../../src/state/modulation/ProjectControlMacroOps.hpp"
#include "../../src/state/project/ProjectMenuModel.hpp"
#include "../../src/state/project/ProjectNameKeyboard.hpp"
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

constexpr float PROJECT_NAME_TEST_OPT_TICKS_PER_ROW =
    (600.0f * 4.0f) /
    static_cast<float>(core::state::project::PROJECT_NAME_KEYBOARD_ROW_COUNT);

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
                storages.macroLibrary,
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
                      state.activeView,
                      state.projectNavigation,
                      state.sequencer,
                      state.sequencerTracks,
                      state.statusBar,
                       state.midiSync,
                       state.pages,
                       state.macros,
                       state.macroEdit,
                       state.configRevision,
                       state.macroHistory,
                       state.structureClipboard,
                       core::handler::SequencerHistoryDomainServices::fromCoreState(state),
                      core::handler::ProjectLifecycleDomainServices::fromCoreState(
                          state,
                          productFiles
                      ),
                  },
                  sequencerSettings,
                  encoders,
                  buttons,
                  PROJECT_SCOPE,
                  mockTimeMs) {
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
                storages.macroLibrary,
                storages.sequencerPatternLibrary,
                storages.sequencerSetLibrary) {
        assert(core::state::project::applyProjectSnapshot(state, snapshot));
    }
};

void saveCurrentProjectSnapshot(ProjectHandlerHarness& h, const char* id) {
    h.state.project.metadata.id.fill('\0');
    std::strncpy(h.state.project.metadata.id.data(), id, h.state.project.metadata.id.size() - 1U);
    h.state.project.metadata.name.fill('\0');
    std::strncpy(h.state.project.metadata.name.data(), id, h.state.project.metadata.name.size() - 1U);
    h.state.project.metadata.hasSavedIdentity = true;
    core::state::project::ProjectSnapshot snapshot;
    assert(core::state::project::captureProjectSnapshot(h.state, snapshot));
    core::persistence::ProjectFileStore store(h.productFiles);
    assert(store.save(snapshot));
}

void writeFutureSequencerProjectFile(ProjectHandlerHarness& h, const char* id) {
    namespace project_file = core::persistence::project_file;
    namespace project_snapshot_codec = core::persistence::project_snapshot_codec;
    namespace project_state_codec = core::persistence::project_state_codec;

    core::state::project::ProjectMetadata metadata{};
    metadata.id.fill('\0');
    metadata.name.fill('\0');
    std::strncpy(metadata.id.data(), id, metadata.id.size() - 1U);
    std::strncpy(metadata.name.data(), id, metadata.name.size() - 1U);
    metadata.hasSavedIdentity = true;
    metadata.dirty = false;

    project_state_codec::ProjectMetaPayload meta{};
    project_state_codec::fillMetaPayload(metadata, meta);
    std::array<uint8_t, project_state_codec::PROJECT_META_PAYLOAD_SIZE> metaBytes{};
    assert(project_state_codec::encodeMetaPayload(
        meta,
        metaBytes.data(),
        static_cast<uint32_t>(metaBytes.size())
    ));
    const uint8_t futureSequencerPayload[] = {1, 2, 3};

    const project_file::ChunkView chunks[] = {
        {
            .id = project_file::chunkIdValue(project_file::ChunkId::PROJECT_META),
            .versionMajor = project_snapshot_codec::PROJECT_SNAPSHOT_CHUNK_VERSION_MAJOR,
            .versionMinor = project_snapshot_codec::PROJECT_SNAPSHOT_CHUNK_VERSION_MINOR,
            .flags = 0,
            .data = metaBytes.data(),
            .size = project_state_codec::PROJECT_META_PAYLOAD_SIZE,
        },
        {
            .id = project_file::chunkIdValue(project_file::ChunkId::SEQUENCER_STATE),
            .versionMajor = static_cast<uint8_t>(
                project_snapshot_codec::PROJECT_SNAPSHOT_CHUNK_VERSION_MAJOR + 1
            ),
            .versionMinor = 0,
            .flags = 0,
            .data = futureSequencerPayload,
            .size = sizeof(futureSequencerPayload),
        },
    };

    uint8_t bytes[512] = {};
    const auto encoded = project_file::encode(
        chunks,
        static_cast<uint16_t>(sizeof(chunks) / sizeof(chunks[0])),
        0,
        bytes,
        sizeof(bytes)
    );
    assert(encoded.status == project_file::Status::OK);

    char path[96] = {};
    const int pathLength = std::snprintf(path, sizeof(path), "projects/%s.mspj", id);
    assert(pathLength > 0 && static_cast<size_t>(pathLength) < sizeof(path));
    const auto written = h.productFiles.write(path, 0, bytes, encoded.bytesWritten);
    assert(written);
    assert(written.value() == encoded.bytesWritten);
    assert(h.productFiles.flush(path));
}

void moveProjectNameKeyboardRows(ProjectHandlerHarness& h, int rowsDown) {
    const float raw =
        h.state.projectNavigation.projectNameOptRawPosition -
        static_cast<float>(rowsDown) * PROJECT_NAME_TEST_OPT_TICKS_PER_ROW;
    h.turn(Config::EncoderID::OPT, raw);
}

void clearProjectNameEditor(ProjectHandlerHarness& h) {
    h.tap(Config::ButtonID::LEFT_BOTTOM);
}

void validateProjectNameEditor(ProjectHandlerHarness& h) {
    h.tap(Config::ButtonID::BOTTOM_RIGHT);
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

    h.turn(Config::EncoderID::NAV, 5.0f);
    assert(h.state.projectNavigation.focusedRow.get() == 5);

    h.tap(Config::ButtonID::NAV);
    assert(h.state.projectNavigation.focusedRow.get() == 5);
    assert(h.state.projectNavigation.autosaveEnabled);

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.projectNavigation.focusedRow.get() == 6);

    h.tap(Config::ButtonID::NAV);
    assert(!h.state.projectNavigation.autosaveEnabled);

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.projectNavigation.focusedRow.get() == 0);

    h.tap(Config::ButtonID::NAV);
    assert(h.state.projectNavigation.currentNode.get() == ProjectNodeId::STORAGE_ROOT);

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

    h.turn(Config::EncoderID::NAV, 6.0f);
    assert(h.state.projectNavigation.focusedRow.get() == 6);

    h.turn(Config::EncoderID::OPT, 1.0f);
    assert(h.state.projectNavigation.autosaveEnabled);

    h.turn(Config::EncoderID::OPT, 0.0f);
    assert(!h.state.projectNavigation.autosaveEnabled);

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.projectNavigation.focusedRow.get() == 0);

    h.turn(Config::EncoderID::OPT, 1.0f);
    assert(!h.state.projectNavigation.autosaveEnabled);

    std::cout << "[PASS] test_storage_autosave_is_editable_with_opt\n";
}

void test_project_name_editor_uses_physical_action_buttons() {
    ProjectHandlerHarness h;

    h.turn(Config::EncoderID::NAV, 3.0f);
    h.tap(Config::ButtonID::NAV);
    assert(h.state.projectNavigation.currentNode.get() == ProjectNodeId::SAVE_AS_PROJECT_NAME);

    h.tap(Config::ButtonID::NAV);  // q
    assert(std::strcmp(h.state.projectNavigation.editingProjectSlug.data(), "q") == 0);

    h.tap(Config::ButtonID::BOTTOM_LEFT);
    assert(h.state.projectNavigation.editingProjectSlug[0] == '\0');
    assert(!h.state.projectNavigation.physicalHoldActive.get());

    h.press(Config::ButtonID::LEFT_CENTER);
    assert(h.state.projectNavigation.projectNameShiftActive);
    h.tap(Config::ButtonID::NAV);  // shifted q
    assert(std::strcmp(h.state.projectNavigation.editingProjectSlug.data(), "Q") == 0);
    h.release(Config::ButtonID::LEFT_CENTER);
    assert(!h.state.projectNavigation.projectNameShiftActive);

    h.tap(Config::ButtonID::BOTTOM_CENTER);
    assert(std::strcmp(h.state.projectNavigation.editingProjectSlug.data(), "Q ") == 0);

    h.tap(Config::ButtonID::NAV);  // q
    h.tap(Config::ButtonID::LEFT_BOTTOM);
    assert(h.state.projectNavigation.editingProjectSlug[0] == '\0');

    h.tap(Config::ButtonID::LEFT_TOP);
    assert(h.state.projectNavigation.currentNode.get() == ProjectNodeId::OVERVIEW_ROOT);

    h.tap(Config::ButtonID::NAV);
    h.tap(Config::ButtonID::NAV);  // q
    h.tap(Config::ButtonID::BOTTOM_RIGHT);
    assert(h.state.projectNavigation.currentNode.get() == ProjectNodeId::OVERVIEW_ROOT);
    assert(std::strcmp(h.state.project.metadata.id.data(), "q") == 0);

    std::cout << "[PASS] test_project_name_editor_uses_physical_action_buttons\n";
}

void test_overview_save_as_name_editor_persists_named_project() {
    ProjectHandlerHarness h;

    h.state.statusBar.tempo.set(151.0f);
    h.state.statusBar.tempoDisplay.set(151.0f);
    h.state.markProjectMutated();

    h.turn(Config::EncoderID::NAV, 3.0f);
    assert(h.state.projectNavigation.focusedRow.get() == 3);
    h.tap(Config::ButtonID::NAV);
    assert(h.state.projectNavigation.currentNode.get() == ProjectNodeId::SAVE_AS_PROJECT_NAME);
    assert(h.state.projectNavigation.focusedRow.get() == 1);

    h.press(Config::ButtonID::LEFT_CENTER);
    h.tap(Config::ButtonID::NAV);  // shifted default key Q
    h.release(Config::ButtonID::LEFT_CENTER);
    h.tap(Config::ButtonID::BOTTOM_CENTER);
    h.tap(Config::ButtonID::NAV);  // q
    assert(std::strcmp(h.state.projectNavigation.editingProjectSlug.data(), "Q q") == 0);

    validateProjectNameEditor(h);

    assert(h.state.projectNavigation.currentNode.get() == ProjectNodeId::OVERVIEW_ROOT);
    assert(std::strcmp(h.state.projectNavigation.lifecycleFeedback.get(), "Saved Q q") == 0);
    assert(std::strcmp(h.state.project.metadata.id.data(), "Q q") == 0);
    assert(std::strcmp(h.state.project.metadata.name.data(), "Q q") == 0);
    assert(h.state.project.metadata.hasSavedIdentity);
    assert(!h.state.project.metadata.dirty);

    core::persistence::ProjectFileStore store(h.productFiles);
    core::state::project::ProjectSnapshot saved;
    assert(store.load("Q q", saved));
    RestoredProjectHarness restored{saved};
    assert(restored.state.statusBar.tempo.get() == 151.0f);

    std::cout << "[PASS] test_overview_save_as_name_editor_persists_named_project\n";
}

void test_overview_save_as_name_editor_rejects_duplicate_project() {
    ProjectHandlerHarness h;
    saveCurrentProjectSnapshot(h, "q");

    h.turn(Config::EncoderID::NAV, 3.0f);
    h.tap(Config::ButtonID::NAV);
    assert(h.state.projectNavigation.currentNode.get() == ProjectNodeId::SAVE_AS_PROJECT_NAME);

    h.tap(Config::ButtonID::NAV);  // q
    validateProjectNameEditor(h);

    assert(h.state.projectNavigation.currentNode.get() == ProjectNodeId::SAVE_AS_PROJECT_NAME);
    assert(std::strcmp(h.state.projectNavigation.lifecycleFeedback.get(), "Name exists q") == 0);
    assert(std::strcmp(h.state.project.metadata.id.data(), "q") == 0);

    std::cout << "[PASS] test_overview_save_as_name_editor_rejects_duplicate_project\n";
}

void test_project_name_editor_opt_requires_full_row_threshold() {
    ProjectHandlerHarness h;

    h.turn(Config::EncoderID::NAV, 3.0f);
    h.tap(Config::ButtonID::NAV);
    assert(h.state.projectNavigation.currentNode.get() == ProjectNodeId::SAVE_AS_PROJECT_NAME);
    assert(std::strcmp(
               core::state::project::projectNameKeyboardCellAt(
                   h.state.projectNavigation.projectNameKeyIndex
               ).label,
               "q"
           ) == 0);

    h.turn(Config::EncoderID::OPT, -PROJECT_NAME_TEST_OPT_TICKS_PER_ROW * 0.5f);
    assert(std::strcmp(
               core::state::project::projectNameKeyboardCellAt(
                   h.state.projectNavigation.projectNameKeyIndex
               ).label,
               "q"
           ) == 0);

    h.turn(Config::EncoderID::OPT, -PROJECT_NAME_TEST_OPT_TICKS_PER_ROW);
    assert(std::strcmp(
               core::state::project::projectNameKeyboardCellAt(
                   h.state.projectNavigation.projectNameKeyIndex
               ).label,
               "a"
           ) == 0);

    std::cout << "[PASS] test_project_name_editor_opt_requires_full_row_threshold\n";
}

void test_overview_rename_name_editor_moves_project_file() {
    ProjectHandlerHarness h;
    saveCurrentProjectSnapshot(h, "p002");

    h.turn(Config::EncoderID::NAV, 4.0f);
    assert(h.state.projectNavigation.focusedRow.get() == 4);
    h.tap(Config::ButtonID::NAV);
    assert(h.state.projectNavigation.currentNode.get() == ProjectNodeId::RENAME_PROJECT_NAME);
    assert(std::strcmp(h.state.projectNavigation.editingProjectSlug.data(), "p002") == 0);

    clearProjectNameEditor(h);
    assert(h.state.projectNavigation.editingProjectSlug[0] == '\0');

    h.tap(Config::ButtonID::NAV);  // q
    validateProjectNameEditor(h);

    assert(h.state.projectNavigation.currentNode.get() == ProjectNodeId::OVERVIEW_ROOT);
    assert(std::strcmp(h.state.projectNavigation.lifecycleFeedback.get(), "Renamed q") == 0);
    assert(std::strcmp(h.state.project.metadata.id.data(), "q") == 0);
    assert(std::strcmp(h.state.project.metadata.name.data(), "q") == 0);

    core::persistence::ProjectFileStore store(h.productFiles);
    core::state::project::ProjectSnapshot snapshot;
    assert(store.load("q", snapshot));
    assert(!store.load("p002", snapshot));

    std::cout << "[PASS] test_overview_rename_name_editor_moves_project_file\n";
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
    assert(std::strcmp(h.state.projectNavigation.lifecycleFeedback.get(), "Saved p001") == 0);
    assert(!h.state.project.metadata.hasSavedIdentity);
    assert(h.state.project.metadata.id[0] == '\0');
    assert(std::strcmp(h.state.project.metadata.name.data(), "untitled") == 0);
    assert(h.state.statusBar.tempo.get() == 120.0f);
    assert(h.state.projectNavigation.transportSwingPercent == 0);
    assert(h.state.sequencer.pattern.length.get() == core::state::sequencer::SequencerPatternState::DEFAULT_LENGTH);
    assert(!h.state.sequencer.pattern.isEnabled(2));
    assert(near(h.state.macros.slots[0].value.get(), 0.5f));

    core::persistence::ProjectFileStore store(h.productFiles);
    core::state::project::ProjectSnapshot saved;
    assert(store.load("p001", saved));

    RestoredProjectHarness restored{saved};
    assert(std::strcmp(restored.state.project.metadata.id.data(), "p001") == 0);
    assert(std::strcmp(restored.state.project.metadata.name.data(), "p001") == 0);
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
    saveCurrentProjectSnapshot(h, "p002");
    assert(h.state.project.metadata.hasSavedIdentity);
    assert(std::strcmp(h.state.project.metadata.id.data(), "p002") == 0);

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
    assert(std::strcmp(h.state.projectNavigation.lifecycleFeedback.get(), "Saved p002") == 0);
    assert(!h.state.project.metadata.hasSavedIdentity);
    assert(h.state.project.metadata.id[0] == '\0');
    assert(h.state.statusBar.tempo.get() == 120.0f);
    assert(h.state.sequencer.pattern.length.get() == core::state::sequencer::SequencerPatternState::DEFAULT_LENGTH);
    assert(!h.state.sequencer.pattern.isEnabled(7));

    core::persistence::ProjectFileStore store(h.productFiles);
    core::state::project::ProjectSnapshot saved;
    assert(store.load("p002", saved));

    RestoredProjectHarness restored{saved};
    assert(std::strcmp(restored.state.project.metadata.id.data(), "p002") == 0);
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
        "draft",
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
    assert(std::strcmp(h.state.projectNavigation.lifecycleFeedback.get(), "Saved p001") == 0);

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
    assert(std::strcmp(h.state.projectNavigation.loadProjects.entries[0].id.data(), "p001") == 0);

    h.tap(Config::ButtonID::NAV);

    assert(std::strcmp(h.state.projectNavigation.lifecycleFeedback.get(), "Loaded p001") == 0);
    assert(std::strcmp(h.state.project.metadata.name.data(), "p001") == 0);
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
    saveCurrentProjectSnapshot(h, "p001");

    h.state.statusBar.tempo.set(153.0f);
    h.state.statusBar.tempoDisplay.set(153.0f);
    saveCurrentProjectSnapshot(h, "p003");

    h.state.statusBar.tempo.set(66.0f);
    h.state.statusBar.tempoDisplay.set(66.0f);

    h.turn(Config::EncoderID::NAV, 1.0f);
    h.tap(Config::ButtonID::NAV);
    assert(h.state.projectNavigation.currentNode.get() == ProjectNodeId::LOAD_PROJECT);
    assert(h.state.projectNavigation.loadProjects.count == 2);
    assert(std::strcmp(h.state.projectNavigation.loadProjects.entries[0].id.data(), "p001") == 0);
    assert(std::strcmp(h.state.projectNavigation.loadProjects.entries[1].id.data(), "p003") == 0);

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.projectNavigation.focusedRow.get() == 1);
    h.tap(Config::ButtonID::NAV);

    assert(std::strcmp(h.state.projectNavigation.lifecycleFeedback.get(), "Loaded p003") == 0);
    assert(std::strcmp(h.state.project.metadata.id.data(), "p003") == 0);
    assert(h.state.statusBar.tempo.get() == 153.0f);

    std::cout << "[PASS] test_load_project_picker_selects_detected_project\n";
}

void test_future_project_load_reports_read_only_and_blocks_direct_save() {
    using Status = core::handler::ProjectLifecycleDomainServices::Status;
    ProjectHandlerHarness h;
    writeFutureSequencerProjectFile(h, "future-project");

    h.turn(Config::EncoderID::NAV, 1.0f);
    h.tap(Config::ButtonID::NAV);
    assert(h.state.projectNavigation.currentNode.get() == ProjectNodeId::LOAD_PROJECT);
    assert(h.state.projectNavigation.loadProjects.count == 1);
    assert(std::strcmp(
               h.state.projectNavigation.loadProjects.entries[0].id.data(),
               "future-project"
           ) == 0);

    h.tap(Config::ButtonID::NAV);
    assert(std::strcmp(
               h.state.projectNavigation.lifecycleFeedback.get(),
               "Loaded read-only future-project"
           ) == 0);
    assert(std::strcmp(h.state.project.metadata.id.data(), "future-project") == 0);
    assert(h.state.project.metadata.hasSavedIdentity);
    assert(!h.state.project.metadata.overwriteSafe);

    h.tap(Config::ButtonID::LEFT_TOP);
    assert(h.state.projectNavigation.currentNode.get() == ProjectNodeId::OVERVIEW_ROOT);
    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.projectNavigation.focusedRow.get() == 2);
    h.tap(Config::ButtonID::NAV);
    assert(std::strcmp(
               h.state.projectNavigation.lifecycleFeedback.get(),
               "Save As required future-project"
           ) == 0);
    assert(!h.state.project.metadata.overwriteSafe);

    auto lifecycle = core::handler::ProjectLifecycleDomainServices::fromCoreState(
        h.state,
        h.productFiles
    );
    const auto directSave = lifecycle.saveCurrentProject();
    assert(directSave.status == Status::UNSAFE_OVERWRITE);
    assert(std::strcmp(h.state.project.metadata.id.data(), "future-project") == 0);
    assert(!h.state.project.metadata.overwriteSafe);

    const auto saveAs = lifecycle.saveAsNextProject();
    assert(saveAs.success());
    assert(std::strcmp(h.state.project.metadata.id.data(), "p001") == 0);
    assert(h.state.project.metadata.overwriteSafe);
    assert(h.productFiles.stat("projects/future-project.mspj"));
    assert(h.productFiles.stat("projects/p001.mspj"));

    std::cout << "[PASS] test_future_project_load_reports_read_only_and_blocks_direct_save\n";
}

void test_dirty_read_only_project_load_confirmation_forces_save_as() {
    ProjectHandlerHarness h;

    h.state.statusBar.tempo.set(121.0f);
    h.state.statusBar.tempoDisplay.set(121.0f);
    saveCurrentProjectSnapshot(h, "p002");
    writeFutureSequencerProjectFile(h, "future-project");

    auto lifecycle = core::handler::ProjectLifecycleDomainServices::fromCoreState(
        h.state,
        h.productFiles
    );
    const auto loadedFuture = lifecycle.loadProject("future-project");
    assert(loadedFuture.success());
    assert(!h.state.project.metadata.overwriteSafe);

    h.state.statusBar.tempo.set(199.0f);
    h.state.statusBar.tempoDisplay.set(199.0f);
    h.state.markProjectMutated();
    assert(h.state.project.metadata.dirty);

    h.turn(Config::EncoderID::NAV, 1.0f);
    h.tap(Config::ButtonID::NAV);
    assert(h.state.projectNavigation.currentNode.get() == ProjectNodeId::LOAD_PROJECT);
    assert(h.state.projectNavigation.loadProjects.count == 2);

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(std::strcmp(
               h.state.projectNavigation.loadProjects.entries[
                   h.state.projectNavigation.focusedRow.get()
               ].id.data(),
               "p002"
           ) == 0);
    h.tap(Config::ButtonID::NAV);
    assert(h.state.projectNavigation.currentNode.get() == ProjectNodeId::LOAD_PROJECT_CONFIRM);
    assert(!h.state.projectNavigation.pendingLoadCanSaveCurrent);

    h.tap(Config::ButtonID::NAV);
    assert(h.state.projectNavigation.currentNode.get() == ProjectNodeId::LOAD_PROJECT);
    assert(std::strcmp(h.state.projectNavigation.lifecycleFeedback.get(), "Loaded p002") == 0);
    assert(std::strcmp(h.state.project.metadata.id.data(), "p002") == 0);
    assert(h.state.project.metadata.overwriteSafe);
    assert(h.state.statusBar.tempo.get() == 121.0f);

    core::persistence::ProjectFileStore store(h.productFiles);
    core::state::project::ProjectSnapshot savedReadOnlyAsNew;
    assert(store.load("p001", savedReadOnlyAsNew));
    RestoredProjectHarness restored{savedReadOnlyAsNew};
    assert(restored.state.statusBar.tempo.get() == 199.0f);

    std::cout << "[PASS] test_dirty_read_only_project_load_confirmation_forces_save_as\n";
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
    assert(std::strcmp(h.state.projectNavigation.lifecycleFeedback.get(), "Saved p001") == 0);
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
    assert(std::strcmp(h.state.projectNavigation.lifecycleFeedback.get(), "Loaded p001") == 0);
    assert(!h.state.project.metadata.dirty);
    assert(h.state.statusBar.tempo.get() == 166.0f);
    assert(h.state.sequencer.pattern.isEnabled(5));
    assert(h.state.sequencer.pattern.note[5] == 74);
    assert(h.state.sequencer.pattern.velocity[5] == 111);

    core::persistence::ProjectFileStore store(h.productFiles);
    core::state::project::ProjectSnapshot loaded;
    assert(store.load("p001", loaded));

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
    saveCurrentProjectSnapshot(h, "p002");

    h.state.resetMusicalProject();
    assert(!h.state.project.metadata.hasSavedIdentity);
    assert(h.state.project.metadata.id[0] == '\0');
    assert(std::strcmp(h.state.project.metadata.name.data(), "untitled") == 0);

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
    assert(std::strcmp(h.state.projectNavigation.loadProjects.entries[0].id.data(), "p002") == 0);

    h.tap(Config::ButtonID::NAV);
    assert(h.state.projectNavigation.currentNode.get() == ProjectNodeId::LOAD_PROJECT_CONFIRM);
    assert(h.state.projectNavigation.focusedRow.get() == 0);

    h.tap(Config::ButtonID::NAV);
    assert(h.state.projectNavigation.currentNode.get() == ProjectNodeId::LOAD_PROJECT);
    assert(std::strcmp(h.state.projectNavigation.lifecycleFeedback.get(), "Loaded p002") == 0);
    assert(std::strcmp(h.state.project.metadata.id.data(), "p002") == 0);
    assert(h.state.statusBar.tempo.get() == 144.0f);

    core::persistence::ProjectFileStore store(h.productFiles);
    core::state::project::ProjectSnapshot saveduntitled;
    assert(store.load("p001", saveduntitled));

    RestoredProjectHarness restored{saveduntitled};
    assert(std::strcmp(restored.state.project.metadata.id.data(), "p001") == 0);
    assert(std::strcmp(restored.state.project.metadata.name.data(), "p001") == 0);
    assert(!restored.state.project.metadata.dirty);
    assert(restored.state.project.metadata.hasSavedIdentity);
    assert(restored.state.statusBar.tempo.get() == 177.0f);
    assert(restored.state.sequencer.pattern.isEnabled(3));
    assert(restored.state.sequencer.pattern.note[3] == 71);

    std::cout << "[PASS] test_untitled_dirty_load_prompts_save_as_and_then_loads_target\n";
}

void test_manual_save_as_rejects_invalid_and_duplicate_slugs() {
    using Status = core::handler::ProjectLifecycleDomainServices::Status;
    ProjectHandlerHarness h;
    auto lifecycle = core::handler::ProjectLifecycleDomainServices::fromCoreState(
        h.state,
        h.productFiles
    );

    h.state.statusBar.tempo.set(132.0f);
    h.state.statusBar.tempoDisplay.set(132.0f);
    auto saved = lifecycle.saveAsProject("live-set.01");
    assert(saved.success());
    assert(std::strcmp(h.state.project.metadata.id.data(), "live-set.01") == 0);
    assert(std::strcmp(h.state.project.metadata.name.data(), "live-set.01") == 0);

    core::persistence::ProjectFileStore store(h.productFiles);
    core::state::project::ProjectSnapshot snapshot;
    assert(store.load("live-set.01", snapshot));
    RestoredProjectHarness restored{snapshot};
    assert(restored.state.statusBar.tempo.get() == 132.0f);

    auto duplicate = lifecycle.saveAsProject("live-set.01");
    assert(duplicate.status == Status::ALREADY_EXISTS);

    auto invalid = lifecycle.saveAsProject("Live_Set");
    assert(invalid.status == Status::INVALID_ARGUMENT);

    std::cout << "[PASS] test_manual_save_as_rejects_invalid_and_duplicate_slugs\n";
}

void test_rename_current_project_moves_catalogue_file() {
    using Status = core::handler::ProjectLifecycleDomainServices::Status;
    ProjectHandlerHarness h;
    auto lifecycle = core::handler::ProjectLifecycleDomainServices::fromCoreState(
        h.state,
        h.productFiles
    );

    h.state.statusBar.tempo.set(126.0f);
    h.state.statusBar.tempoDisplay.set(126.0f);
    assert(lifecycle.saveAsProject("original.project").success());

    h.state.statusBar.tempo.set(158.0f);
    h.state.statusBar.tempoDisplay.set(158.0f);
    h.state.markProjectMutated();

    auto renamed = lifecycle.renameCurrentProject("renamed-project");
    assert(renamed.success());
    assert(std::strcmp(h.state.project.metadata.id.data(), "renamed-project") == 0);
    assert(std::strcmp(h.state.project.metadata.name.data(), "renamed-project") == 0);
    assert(!h.state.project.metadata.dirty);

    assert(!h.productFiles.stat("projects/original.project.mspj"));
    assert(h.productFiles.stat("projects/renamed-project.mspj"));

    core::persistence::ProjectFileStore store(h.productFiles);
    core::state::project::ProjectSnapshot snapshot;
    assert(store.load("renamed-project", snapshot));
    RestoredProjectHarness restored{snapshot};
    assert(restored.state.statusBar.tempo.get() == 158.0f);

    auto invalid = lifecycle.renameCurrentProject("bad_name");
    assert(invalid.status == Status::INVALID_ARGUMENT);

    std::cout << "[PASS] test_rename_current_project_moves_catalogue_file\n";
}

void test_rename_current_project_rejects_existing_target_without_mutating_state() {
    using Status = core::handler::ProjectLifecycleDomainServices::Status;
    ProjectHandlerHarness h;
    auto lifecycle = core::handler::ProjectLifecycleDomainServices::fromCoreState(
        h.state,
        h.productFiles
    );

    assert(lifecycle.saveAsProject("p001").success());

    h.state.resetMusicalProject();
    h.state.statusBar.tempo.set(141.0f);
    h.state.statusBar.tempoDisplay.set(141.0f);
    assert(lifecycle.saveAsProject("p002").success());

    h.state.statusBar.tempo.set(177.0f);
    h.state.statusBar.tempoDisplay.set(177.0f);
    h.state.markProjectMutated();

    auto duplicate = lifecycle.renameCurrentProject("p001");
    assert(duplicate.status == Status::ALREADY_EXISTS);
    assert(std::strcmp(h.state.project.metadata.id.data(), "p002") == 0);
    assert(h.state.project.metadata.dirty);

    assert(h.productFiles.stat("projects/p001.mspj"));
    assert(h.productFiles.stat("projects/p002.mspj"));

    core::persistence::ProjectFileStore store(h.productFiles);
    core::state::project::ProjectSnapshot p002;
    assert(store.load("p002", p002));
    RestoredProjectHarness restored{p002};
    assert(restored.state.statusBar.tempo.get() == 141.0f);

    std::cout << "[PASS] test_rename_current_project_rejects_existing_target_without_mutating_state\n";
}

void enterModulatorsRoot(ProjectHandlerHarness& h) {
    using namespace core::state::project;
    auto& navigation = h.state.projectNavigation;
    navigation.activeTab.set(ProjectTab::MODULATORS);
    navigation.currentNode.set(ProjectNodeId::MODULATORS_ROOT);
    navigation.depth.set(0);
    navigation.focusedRow.set(0);
    navigation.pathStack[0] = ProjectNodeId::MODULATORS_ROOT;
    h.state.activeView.set(core::ui::ViewType::PROJECT);
}

void projectModulatorUndo(ProjectHandlerHarness& h) {
    h.press(Config::ButtonID::LEFT_CENTER);
    h.tap(Config::ButtonID::LEFT_TOP);
    h.release(Config::ButtonID::LEFT_CENTER);
}

void projectModulatorRedo(ProjectHandlerHarness& h) {
    h.press(Config::ButtonID::LEFT_CENTER);
    h.tap(Config::ButtonID::LEFT_BOTTOM);
    h.release(Config::ButtonID::LEFT_CENTER);
}

void test_project_modulator_creation_and_destination_workflow() {
    using namespace core::state::modulation;
    using core::state::project::ProjectNodeId;
    ProjectHandlerHarness h;
    enterModulatorsRoot(h);

    h.tap(Config::ButtonID::NAV);
    assert(h.state.projectNavigation.currentNode.get() ==
           ProjectNodeId::MODULATOR_DESTINATION_PICKER);
    assert(h.state.projectNavigation.creatingModulatorSource);
    h.tap(Config::ButtonID::NAV);
    assert(h.state.pages.control.audition.active);
    assert(h.state.macroHistory.undoCount() == 0U);
    h.tap(Config::ButtonID::BOTTOM_RIGHT);

    auto& graph = h.state.pages.control.authored.modulation;
    assert(graph.sourceCount == 1U);
    assert(graph.outputBindingCount == 1U);
    assert(graph.sources[0].reach.kind == ModulatorReachKind::MACRO);
    assert(graph.outputBindings[0].amountQ15 == 8192);
    assert(h.state.projectNavigation.currentNode.get() ==
           ProjectNodeId::MODULATOR_DESTINATIONS);
    assert(h.state.macroHistory.undoCount() == 1U);

    h.turn(Config::EncoderID::NAV, 1.0f);  // + Destination
    h.tap(Config::ButtonID::NAV);
    assert(h.state.projectNavigation.currentNode.get() ==
           ProjectNodeId::MODULATOR_DESTINATION_PICKER);
    h.turn(Config::EncoderID::NAV, 1.0f);  // Macro 2
    h.tap(Config::ButtonID::NAV);
    assert(h.state.pages.control.audition.active);
    assert(h.state.pages.pageData(0, 0).isMacroActive(1));
    assert(h.state.macroHistory.undoCount() == 1U);
    h.tap(Config::ButtonID::BOTTOM_RIGHT);
    assert(graph.outputBindingCount == 2U);
    assert(graph.sources[0].reach.kind == ModulatorReachKind::TRACK_SET);
    assert(graph.sources[0].reach.trackMask == 1U);
    assert(h.state.macroHistory.undoCount() == 2U);

    projectModulatorUndo(h);
    assert(graph.outputBindingCount == 1U);
    assert(graph.sources[0].reach.kind == ModulatorReachKind::MACRO);
    assert(!h.state.pages.pageData(0, 0).isMacroActive(1));
    projectModulatorRedo(h);
    assert(graph.outputBindingCount == 2U);
    assert(h.state.pages.pageData(0, 0).isMacroActive(1));
    std::cout << "[PASS] Project creates and assigns LFOs destination-first\n";
}

void test_project_macro_destination_audition_cancel_is_exact_and_clean() {
    using namespace core::state::modulation;
    using core::state::project::ProjectNodeId;
    ProjectHandlerHarness h;
    enterModulatorsRoot(h);
    const auto pageBefore = h.state.pages.pageData(0, 0);
    const auto graphBefore = h.state.pages.control.authored.modulation;
    const bool dirtyBefore = h.state.project.metadata.dirty;

    h.tap(Config::ButtonID::NAV);         // New LFO destination picker
    h.turn(Config::EncoderID::NAV, 1.0f); // legal + Macro 2
    h.tap(Config::ButtonID::NAV);         // audible create-and-bind preview
    assert(h.state.pages.control.audition.active);
    assert(h.state.pages.pageData(0, 0).isMacroActive(1));
    assert(h.state.pages.control.authored.modulation.sourceCount == 1U);
    assert(h.state.pages.control.authored.modulation.outputBindingCount == 1U);
    assert(h.state.project.metadata.dirty == dirtyBefore);

    h.turn(Config::EncoderID::OPT, 0.75f);
    assert(h.state.pages.control.authored.modulation
               .outputBindings[0].amountQ15 > 0);
    assert(h.state.project.metadata.dirty == dirtyBefore);
    h.tap(Config::ButtonID::LEFT_TOP);     // Cancel preview, keep picker

    assert(h.state.projectNavigation.currentNode.get() ==
           ProjectNodeId::MODULATOR_DESTINATION_PICKER);
    assert(!h.state.pages.control.audition.active);
    assert(h.state.macroHistory.undoCount() == 0U);
    assert(h.state.project.metadata.dirty == dirtyBefore);
    assert(std::memcmp(
        &h.state.pages.pageData(0, 0),
        &pageBefore,
        sizeof(pageBefore)
    ) == 0);
    assert(std::memcmp(
        &h.state.pages.control.authored.modulation,
        &graphBefore,
        sizeof(graphBefore)
    ) == 0);
    std::cout << "[PASS] Project Macro destination Cancel is exact and not dirty\n";
}

void test_project_created_source_undo_returns_to_registry_and_redo_restores() {
    using core::state::project::ProjectNodeId;
    ProjectHandlerHarness h;
    enterModulatorsRoot(h);
    h.tap(Config::ButtonID::NAV);
    h.turn(Config::EncoderID::NAV, 1.0f);
    h.tap(Config::ButtonID::NAV);
    h.tap(Config::ButtonID::BOTTOM_RIGHT);
    const auto sourceId = h.state.projectNavigation.selectedModulator;
    assert(h.state.projectNavigation.currentNode.get() ==
           ProjectNodeId::MODULATOR_DESTINATIONS);

    projectModulatorUndo(h);
    assert(h.state.projectNavigation.currentNode.get() ==
           ProjectNodeId::MODULATORS_ROOT);
    assert(h.state.projectNavigation.focusedRow.get() == 0U);
    assert(h.state.pages.control.authored.modulation.sourceCount == 0U);
    assert(!h.state.pages.pageData(0, 0).isMacroActive(1));

    projectModulatorRedo(h);
    assert(h.state.projectNavigation.currentNode.get() ==
           ProjectNodeId::MODULATORS_ROOT);
    assert(h.state.projectNavigation.focusedRow.get() == 0U);
    assert(h.state.pages.control.authored.modulation.sourceCount == 1U);
    assert(h.state.pages.control.authored.modulation.sources[0].id == sourceId);
    assert(h.state.pages.pageData(0, 0).isMacroActive(1));
    std::cout << "[PASS] created Source Undo/Redo keeps Project navigation usable\n";
}

void test_project_modulator_explicit_unassigned_creation() {
    using namespace core::state::modulation;
    using core::state::project::ProjectNodeId;
    ProjectHandlerHarness h;
    enterModulatorsRoot(h);
    h.tap(Config::ButtonID::NAV);
    h.turn(Config::EncoderID::NAV, 8.0f);
    h.tap(Config::ButtonID::NAV);

    auto& graph = h.state.pages.control.authored.modulation;
    assert(h.state.projectNavigation.currentNode.get() ==
           ProjectNodeId::MODULATORS_ROOT);
    assert(graph.sourceCount == 1U);
    assert(graph.outputBindingCount == 0U);
    assert(graph.sources[0].reach.kind == ModulatorReachKind::DETACHED);
    assert(h.state.macroHistory.undoCount() == 1U);
    projectModulatorUndo(h);
    assert(graph.sourceCount == 0U);
    std::cout << "[PASS] Unassigned source creation stays explicit and undoable\n";
}

void test_project_modulator_source_copy_and_guarded_paste() {
    using namespace core::state::modulation;
    ProjectHandlerHarness h;
    enterModulatorsRoot(h);

    ModulatorLfoDraft draft{};
    draft.name = "LFO 1";
    draft.reach = {.kind = ModulatorReachKind::PROJECT};
    draft.parameters.periodTicks = PROJECT_CONTROL_TICKS_PER_BEAT;
    auto& graph = h.state.pages.control.authored.modulation;
    const auto created = createLfoModulator(graph, draft);
    assert(created.changed());
    h.state.projectNavigation.notifyContentChanged();

    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.handler.update(g_now_ms);
    h.release(Config::ButtonID::BOTTOM_RIGHT);
    h.handler.update(g_now_ms);
    assert(h.state.structureClipboard.hasProjectModulatorSource());
    assert(h.state.structureClipboard.projectModulatorSource.sourceId ==
           created.sourceId);
    assert(graph.sourceCount == 1U);

    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.handler.update(g_now_ms);
    g_now_ms += Config::Timing::LATCH_THRESHOLD_MS +
                Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS;
    h.handler.update(g_now_ms);
    h.release(Config::ButtonID::BOTTOM_RIGHT);
    h.handler.update(g_now_ms);

    assert(graph.sourceCount == 2U);
    assert(std::strcmp(graph.sources[1].name.data(), "LFO 2") == 0);
    assert(h.state.projectNavigation.selectedModulator == graph.sources[1].id);
    assert(h.state.macroHistory.undoCount() == 1U);
    projectModulatorUndo(h);
    assert(graph.sourceCount == 1U);
    assert(graph.sources[0].id == created.sourceId);
    std::cout << "[PASS] Project Source Copy/Paste is typed and guarded\n";
}

void test_project_modulator_reach_page_splits_one_track_with_one_undo() {
    using namespace core::state::modulation;
    using core::state::project::ProjectNodeId;
    ProjectHandlerHarness h;
    enterModulatorsRoot(h);

    ModulatorLfoDraft draft{};
    draft.name = "Slow Tide";
    draft.reach = {.kind = ModulatorReachKind::PROJECT};
    draft.parameters.periodTicks = PROJECT_CONTROL_TICKS_PER_BEAT;
    auto& graph = h.state.pages.control.authored.modulation;
    const auto created = createLfoModulator(graph, draft);
    assert(created.changed());
    ModulationBindingDraft binding{};
    binding.sourceId = created.sourceId;
    binding.destination = {
        .kind = ModulationDestinationKind::MACRO_SLOT,
        .track = 0,
        .page = 0,
        .macro = 0,
    };
    binding.amountQ15 = 8192;
    assert(addProjectModulationBinding(graph, binding).changed());
    binding.destination.track = 1;
    binding.destination.macro = 1;
    const auto moved = addProjectModulationBinding(graph, binding);
    assert(moved.changed());
    h.state.projectNavigation.notifyContentChanged();

    h.tap(Config::ButtonID::NAV);          // Source workspace
    h.turn(Config::EncoderID::NAV, 3.0f);  // Details
    h.tap(Config::ButtonID::NAV);
    assert(h.state.projectNavigation.currentNode.get() ==
           ProjectNodeId::MODULATOR_SOURCE_OPTIONS);
    h.turn(Config::EncoderID::NAV, 2.0f);  // Available in
    h.tap(Config::ButtonID::NAV);
    assert(h.state.projectNavigation.currentNode.get() ==
           ProjectNodeId::MODULATOR_REACH);
    h.turn(Config::EncoderID::NAV, 3.0f);  // Split T2
    h.tap(Config::ButtonID::NAV);

    assert(h.state.projectNavigation.currentNode.get() ==
           ProjectNodeId::MODULATORS_ROOT);
    assert(graph.sourceCount == 2U);
    assert(graph.outputBindingCount == 2U);
    assert(graph.outputBindings[0].sourceId == created.sourceId);
    assert(graph.outputBindings[1].sourceId != created.sourceId);
    assert(graph.sources[0].reach.kind == ModulatorReachKind::MACRO);
    assert(graph.sources[1].reach.kind == ModulatorReachKind::MACRO);
    assert(graph.sources[1].reach.track == 1U);
    assert(std::strcmp(graph.sources[1].name.data(), "T2 Slow Tide") == 0);
    assert(h.state.macroHistory.undoCount() == 1U);

    projectModulatorUndo(h);
    assert(graph.sourceCount == 1U);
    assert(graph.outputBindingCount == 2U);
    assert(graph.outputBindings[0].sourceId == created.sourceId);
    assert(graph.outputBindings[1].sourceId == created.sourceId);
    assert(graph.sources[0].reach.kind == ModulatorReachKind::PROJECT);
    std::cout << "[PASS] Reach page Split is explicit and one exact Undo\n";
}

void test_macro_deep_link_back_restores_exact_assignment() {
    using namespace core::state::modulation;
    ProjectHandlerHarness h;
    enterModulatorsRoot(h);
    auto& graph = h.state.pages.control.authored.modulation;

    ModulatorLfoDraft firstDraft{};
    firstDraft.name = "LFO 1";
    firstDraft.reach = {.kind = ModulatorReachKind::PROJECT};
    const auto firstSource = createLfoModulator(graph, firstDraft);
    assert(firstSource.changed());
    ModulatorLfoDraft secondDraft = firstDraft;
    secondDraft.name = "LFO 2";
    const auto secondSource = createLfoModulator(graph, secondDraft);
    assert(secondSource.changed());

    const auto destination = projectControlDestination({0, 0, 0});
    ModulationBindingDraft binding{};
    binding.sourceId = firstSource.sourceId;
    binding.destination = destination;
    binding.amountQ15 = 4096;
    assert(addProjectModulationBinding(graph, binding).changed());
    binding.sourceId = secondSource.sourceId;
    binding.amountQ15 = 12288;
    const auto selected = addProjectModulationBinding(graph, binding);
    assert(selected.changed());

    h.state.macroEdit.openEditor(0, 0, 0, 0);
    h.state.macroEdit.openModulation(2);
    assert(core::state::project::openProjectModulatorWorkspace(
        h.state.projectNavigation,
        secondSource.sourceId
    ));
    h.state.projectNavigation.modulatorReturn = {
        .sourceId = secondSource.sourceId,
        .bindingId = selected.bindingId,
        .macroAddress = {0, 0, 0},
        .caller = core::state::project::
            ModulatorNavigationCaller::MACRO_ASSIGNMENT,
        .focusedRow = 2,
    };
    h.state.activeView.set(core::ui::ViewType::PROJECT);

    h.tap(Config::ButtonID::LEFT_TOP);

    assert(h.state.activeView.get() == core::ui::ViewType::MACRO);
    assert(h.state.overlays.current() ==
           core::ui::OverlayType::MACRO_AUTOMATION);
    assert(h.state.macroEdit.visible.get());
    assert(h.state.macroEdit.automationVisible.get());
    assert(h.state.macroEdit.flowPhase.get() ==
           core::state::MacroEditFlowPhase::MODULATION);
    assert(h.state.macroEdit.modulationFocusedRow.get() == 2U);
    assert(projectControlFocusedModulationBinding(
               h.state.pages.control,
               {0, 0, 0}
           ) == selected.bindingId);
    assert(!h.state.projectNavigation.modulatorReturn.active());
    assert(h.state.macroEdit.modulatorNavigationFeedback.get() ==
           core::state::MacroModulatorNavigationFeedback::NONE);

    std::cout << "[PASS] Back restores the exact Macro assignment by stable ID\n";
}

void test_macro_deep_link_deleted_source_returns_with_explicit_fallback() {
    using namespace core::state::modulation;
    ProjectHandlerHarness h;
    enterModulatorsRoot(h);
    auto& authored = h.state.pages.control.authored;

    ModulatorLfoDraft draft{};
    draft.name = "Transient LFO";
    draft.reach = {.kind = ModulatorReachKind::PROJECT};
    const auto source = createLfoModulator(authored.modulation, draft);
    assert(source.changed());
    ModulationBindingDraft binding{};
    binding.sourceId = source.sourceId;
    binding.destination = projectControlDestination({0, 0, 0});
    const auto assigned = addProjectModulationBinding(
        authored.modulation,
        binding
    );
    assert(assigned.changed());

    h.state.macroEdit.openEditor(0, 0, 0, 0);
    h.state.macroEdit.openModulation(0);
    assert(core::state::project::openProjectModulatorWorkspace(
        h.state.projectNavigation,
        source.sourceId
    ));
    h.state.projectNavigation.modulatorReturn = {
        .sourceId = source.sourceId,
        .bindingId = assigned.bindingId,
        .macroAddress = {0, 0, 0},
        .caller = core::state::project::
            ModulatorNavigationCaller::MACRO_ASSIGNMENT,
        .focusedRow = 0,
    };
    assert(deleteProjectModulator(
        authored.modulation,
        authored.curves,
        source.sourceId
    ).changed());
    assert(core::state::project::backProjectNavigation(
        h.state.projectNavigation
    ));
    assert(h.state.projectNavigation.depth.get() == 0U);
    h.state.activeView.set(core::ui::ViewType::PROJECT);

    h.tap(Config::ButtonID::LEFT_TOP);

    assert(h.state.activeView.get() == core::ui::ViewType::MACRO);
    assert(h.state.overlays.current() ==
           core::ui::OverlayType::MACRO_AUTOMATION);
    assert(h.state.macroEdit.visible.get());
    assert(h.state.macroEdit.automationVisible.get());
    assert(h.state.macroEdit.modulationFocusedRow.get() == 0U);
    assert(h.state.macroEdit.modulatorNavigationFeedback.get() ==
           core::state::MacroModulatorNavigationFeedback::SOURCE_UNAVAILABLE);
    assert(!h.state.projectNavigation.modulatorReturn.active());

    std::cout << "[PASS] Deleted source returns to a visible deterministic fallback\n";
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
    test_project_name_editor_uses_physical_action_buttons();
    test_overview_save_as_name_editor_persists_named_project();
    test_overview_save_as_name_editor_rejects_duplicate_project();
    test_project_name_editor_opt_requires_full_row_threshold();
    test_overview_rename_name_editor_moves_project_file();
    test_new_project_resets_musical_project_state();
    test_new_project_confirmation_cancel_preserves_state();
    test_new_project_save_as_new_persists_then_resets();
    test_new_project_save_current_persists_saved_identity_then_resets();
    test_routing_output_channels_are_editable();
    test_overview_save_and_load_roundtrip_project_file();
    test_overview_load_missing_project_reports_failure();
    test_load_project_picker_selects_detected_project();
    test_future_project_load_reports_read_only_and_blocks_direct_save();
    test_dirty_read_only_project_load_confirmation_forces_save_as();
    test_dirty_project_load_prompts_save_and_preserves_latest_edits();
    test_untitled_dirty_load_prompts_save_as_and_then_loads_target();
    test_manual_save_as_rejects_invalid_and_duplicate_slugs();
    test_rename_current_project_moves_catalogue_file();
    test_rename_current_project_rejects_existing_target_without_mutating_state();
    test_project_modulator_creation_and_destination_workflow();
    test_project_macro_destination_audition_cancel_is_exact_and_clean();
    test_project_created_source_undo_returns_to_registry_and_redo_restores();
    test_project_modulator_explicit_unassigned_creation();
    test_project_modulator_source_copy_and_guarded_paste();
    test_project_modulator_reach_page_splits_one_track_with_one_undo();
    test_macro_deep_link_back_restores_exact_assignment();
    test_macro_deep_link_deleted_source_returns_with_explicit_fallback();

    std::cout << "\nAll ProjectHandler tests passed.\n";
    return 0;
}
