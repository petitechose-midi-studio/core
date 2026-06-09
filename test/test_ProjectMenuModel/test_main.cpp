#include <cassert>
#include <cstring>
#include <iostream>
#include <string>

#include "../../src/state/project/ProjectMenuModel.hpp"
#include "../../src/state/sequencer/SequencerScaleCatalog.hpp"

namespace {

using core::state::project::ProjectNodeId;
using core::state::project::ProjectTab;
namespace scale_catalog = core::state::sequencer::scale_catalog;

const char* rowValue(const core::state::project::ProjectMenuRow& row) {
    return row.displayValue();
}

core::state::project::ProjectMenuContext projectContext(const char* id,
                                                        const char* name,
                                                        bool dirty,
                                                        bool hasSavedIdentity) {
    auto context = core::state::project::ProjectMenuContext{};
    if (id) {
        std::strncpy(context.projectId.data(), id, context.projectId.size() - 1U);
        context.projectId[context.projectId.size() - 1U] = '\0';
    }
    if (name) {
        std::strncpy(context.projectName.data(), name, context.projectName.size() - 1U);
        context.projectName[context.projectName.size() - 1U] = '\0';
    }
    context.projectDirty = dirty;
    context.projectHasSavedIdentity = hasSavedIdentity;
    return context;
}

void switchToStorage(core::state::project::ProjectNavigationState& navigation) {
    core::state::project::switchProjectTab(navigation, 1);
    core::state::project::switchProjectTab(navigation, 1);
    core::state::project::switchProjectTab(navigation, 1);
}

void test_overview_root_exposes_project_actions() {
    core::state::project::ProjectNavigationState navigation;

    const auto page = core::state::project::buildProjectMenuPage(
        navigation,
        projectContext("P002", "Project 002", true, true)
    );
    assert(page.rowCount == 4);
    assert(page.selectedIndex == 0);
    assert(std::string(page.meta) == "OVERVIEW  P002*");
    assert(std::string(page.rows[0].label) == "New Project");
    assert(page.rows[0].kind == core::state::project::ProjectMenuRowKind::Action);
    assert(page.rows[0].enabled);
    assert(std::string(rowValue(page.rows[0])) == "Reset");

    std::cout << "[PASS] test_overview_root_exposes_project_actions\n";
}

void test_enter_music_then_scale_and_back() {
    core::state::project::ProjectNavigationState navigation;

    core::state::project::switchProjectTab(navigation, 1);
    assert(navigation.activeTab.get() == ProjectTab::MUSIC);
    assert(navigation.currentNode.get() == ProjectNodeId::MUSIC_ROOT);
    assert(navigation.depth.get() == 0);

    assert(core::state::project::enterFocusedProjectRow(navigation));
    assert(navigation.activeTab.get() == ProjectTab::MUSIC);
    assert(navigation.currentNode.get() == ProjectNodeId::MUSIC_SCALE);
    assert(navigation.depth.get() == 1);

    core::state::project::navigateProjectRows(navigation, 1.0f);
    assert(navigation.focusedRow.get() == 1);

    assert(core::state::project::backProjectNavigation(navigation));
    assert(navigation.currentNode.get() == ProjectNodeId::MUSIC_ROOT);
    assert(navigation.depth.get() == 0);
    assert(navigation.focusedRow.get() == 0);

    std::cout << "[PASS] test_enter_music_then_scale_and_back\n";
}

void test_switch_tab_resets_to_target_tab_root() {
    core::state::project::ProjectNavigationState navigation;

    core::state::project::switchProjectTab(navigation, 2);
    assert(navigation.activeTab.get() == ProjectTab::TRANSPORT);
    assert(navigation.currentNode.get() == ProjectNodeId::TRANSPORT_ROOT);
    assert(navigation.depth.get() == 0);

    std::cout << "[PASS] test_switch_tab_resets_to_target_tab_root\n";
}

void test_root_section_is_a_navigation_root() {
    core::state::project::ProjectNavigationState navigation;

    switchToStorage(navigation);
    assert(navigation.activeTab.get() == ProjectTab::STORAGE);
    assert(navigation.currentNode.get() == ProjectNodeId::STORAGE_ROOT);
    assert(core::state::project::projectNavigationAtRoot(navigation));

    assert(!core::state::project::backProjectNavigation(navigation));
    assert(navigation.activeTab.get() == ProjectTab::STORAGE);
    assert(navigation.currentNode.get() == ProjectNodeId::STORAGE_ROOT);

    std::cout << "[PASS] test_root_section_is_a_navigation_root\n";
}

void test_new_project_confirmation_page_defaults_to_save_choice() {
    core::state::project::ProjectNavigationState navigation;

    assert(core::state::project::openNewProjectConfirmation(navigation));
    assert(core::state::project::projectNavigationInNewProjectConfirmation(navigation));
    assert(navigation.currentNode.get() == ProjectNodeId::NEW_PROJECT_CONFIRM);
    assert(navigation.depth.get() == 1);
    assert(navigation.focusedRow.get() == 0);

    const auto page = core::state::project::buildProjectMenuPage(navigation);
    assert(page.rowCount == 3);
    assert(std::string(page.meta) == "NEW PROJECT?");
    assert(std::string(page.rows[0].label) == "Save As New");
    assert(page.rows[0].enabled);
    assert(std::string(rowValue(page.rows[0])) == "Next");
    assert(std::string(page.rows[1].label) == "Don't Save");
    assert(page.rows[1].enabled);
    assert(std::string(rowValue(page.rows[1])) == "Reset");
    assert(std::string(page.rows[2].label) == "Cancel");
    assert(page.rows[2].enabled);

    assert(core::state::project::backProjectNavigation(navigation));
    assert(navigation.currentNode.get() == ProjectNodeId::OVERVIEW_ROOT);
    assert(navigation.depth.get() == 0);

    std::cout << "[PASS] test_new_project_confirmation_page_defaults_to_save_choice\n";
}

void test_new_project_confirmation_uses_current_project_identity() {
    core::state::project::ProjectNavigationState navigation;

    assert(core::state::project::openNewProjectConfirmation(navigation));
    const auto page = core::state::project::buildProjectMenuPage(
        navigation,
        projectContext("P002", "Project 002", true, true)
    );

    assert(page.rowCount == 3);
    assert(page.selectedIndex == 0);
    assert(std::string(page.rows[0].label) == "Save & Reset");
    assert(page.rows[0].enabled);
    assert(std::string(rowValue(page.rows[0])) == "P002");
    assert(std::string(page.rows[1].label) == "Don't Save");
    assert(std::string(rowValue(page.rows[1])) == "Reset");

    std::cout << "[PASS] test_new_project_confirmation_uses_current_project_identity\n";
}

void test_music_scale_rows_use_project_scale_context() {
    core::state::project::ProjectNavigationState navigation;
    core::state::project::switchProjectTab(navigation, 1);
    assert(core::state::project::enterFocusedProjectRow(navigation));

    auto context = core::state::project::ProjectMenuContext{};
    context.projectScale.root = 6;
    context.projectScale.type = scale_catalog::StepSequencerScaleType::WholeTone;
    context.projectScale.mode = scale_catalog::StepSequencerScaleConstraintMode::ConstrainDown;

    const auto page = core::state::project::buildProjectMenuPage(navigation, context);
    assert(page.rowCount == 5);
    assert(std::string(rowValue(page.rows[0])) == "F#");
    assert(std::string(rowValue(page.rows[1])) == "Whole Tone");
    assert(std::string(rowValue(page.rows[2])) == "Down");

    std::cout << "[PASS] test_music_scale_rows_use_project_scale_context\n";
}

void test_music_root_scale_row_summarizes_key_and_folder_target() {
    core::state::project::ProjectNavigationState navigation;
    core::state::project::switchProjectTab(navigation, 1);

    auto context = core::state::project::ProjectMenuContext{};
    context.projectScale.root = 5;
    context.projectScale.type = scale_catalog::StepSequencerScaleType::HarmonicMinor;

    const auto page = core::state::project::buildProjectMenuPage(navigation, context);
    assert(page.rowCount == 3);
    assert(std::string(page.rows[0].label) == "Scale");
    assert(page.rows[0].kind == core::state::project::ProjectMenuRowKind::Folder);
    assert(page.rows[0].hasTarget);
    assert(page.rows[0].target == ProjectNodeId::MUSIC_SCALE);
    assert(std::string(rowValue(page.rows[0])) == "F Harm Minor >");

    std::cout << "[PASS] test_music_root_scale_row_summarizes_key_and_folder_target\n";
}

void test_transport_rows_use_runtime_context() {
    core::state::project::ProjectNavigationState navigation;
    core::state::project::switchProjectTab(navigation, 2);
    navigation.transportSwingPercent = 12;
    navigation.transportRunMode = 1;

    auto context = core::state::project::ProjectMenuContext{};
    context.tempoBpm = 128.0f;
    context.clockMode = core::state::MidiSyncMode::MASTER;

    const auto page = core::state::project::buildProjectMenuPage(navigation, context);
    assert(page.rowCount == 5);
    assert(std::string(rowValue(page.rows[0])) == "128 BPM");
    assert(std::string(rowValue(page.rows[1])) == "12%");
    assert(std::string(rowValue(page.rows[2])) == "Master");
    assert(std::string(rowValue(page.rows[3])) == "Restart");

    std::cout << "[PASS] test_transport_rows_use_runtime_context\n";
}

void test_routing_rows_expose_all_track_output_channels() {
    core::state::project::ProjectNavigationState navigation;
    core::state::project::switchProjectTab(navigation, 4);

    auto context = core::state::project::ProjectMenuContext{};
    for (uint8_t i = 0; i < context.outputMidiChannels.size(); ++i) {
        context.outputMidiChannels[i] = i;
    }
    context.outputMidiChannels[3] = 9;

    const auto page = core::state::project::buildProjectMenuPage(navigation, context);
    assert(page.rowCount == 16);
    assert(std::string(page.rows[0].label) == "Track 1");
    assert(std::string(rowValue(page.rows[0])) == "MIDI Ch 1");
    assert(std::string(page.rows[3].label) == "Track 4");
    assert(std::string(rowValue(page.rows[3])) == "MIDI Ch 10");
    assert(std::string(page.rows[15].label) == "Track 16");
    assert(std::string(rowValue(page.rows[15])) == "MIDI Ch 16");

    std::cout << "[PASS] test_routing_rows_expose_all_track_output_channels\n";
}

void test_storage_project_identity_is_read_only_and_autosave_is_activable() {
    core::state::project::ProjectNavigationState navigation;

    switchToStorage(navigation);

    navigation.focusedRow.set(3);
    assert(!core::state::project::enterFocusedProjectRow(navigation));
    auto page = core::state::project::buildProjectMenuPage(
        navigation,
        projectContext("P002", "Project 002", true, true)
    );
    assert(std::string(page.meta) == "STORAGE  P002*");
    assert(std::string(page.rows[3].label) == "Project");
    assert(page.rows[3].kind == core::state::project::ProjectMenuRowKind::Disabled);
    assert(std::string(rowValue(page.rows[3])) == "P002");

    navigation.focusedRow.set(4);
    assert(core::state::project::enterFocusedProjectRow(navigation));
    assert(!navigation.autosaveEnabled);
    page = core::state::project::buildProjectMenuPage(navigation);
    assert(std::string(rowValue(page.rows[4])) == "Off");

    std::cout << "[PASS] test_storage_project_identity_is_read_only_and_autosave_is_activable\n";
}

void test_load_project_picker_shows_detected_projects() {
    core::state::project::ProjectNavigationState navigation;

    navigation.loadProjects.clear();
    navigation.loadProjects.scanned = true;
    assert(navigation.loadProjects.add("P001", 100));
    assert(navigation.loadProjects.add("P003", 200));

    assert(core::state::project::openProjectLoadPicker(navigation));
    assert(navigation.currentNode.get() == ProjectNodeId::LOAD_PROJECT);
    assert(navigation.depth.get() == 1);

    auto page = core::state::project::buildProjectMenuPage(navigation);
    assert(page.rowCount == 2);
    assert(std::string(page.meta) == "LOAD PROJECT");
    assert(std::string(page.rows[0].label) == "P001");
    assert(std::string(rowValue(page.rows[0])) == "Load");
    assert(page.rows[0].enabled);
    assert(std::string(page.rows[1].label) == "P003");

    assert(core::state::project::backProjectNavigation(navigation));
    assert(navigation.currentNode.get() == ProjectNodeId::OVERVIEW_ROOT);

    std::cout << "[PASS] test_load_project_picker_shows_detected_projects\n";
}

void test_load_project_confirmation_prompts_dirty_session_choice() {
    core::state::project::ProjectNavigationState navigation;

    navigation.loadProjects.clear();
    navigation.loadProjects.scanned = true;
    assert(navigation.loadProjects.add("P003", 200));
    assert(core::state::project::openProjectLoadPicker(navigation));
    assert(core::state::project::openProjectLoadConfirmation(navigation, "P003", true));
    assert(navigation.currentNode.get() == ProjectNodeId::LOAD_PROJECT_CONFIRM);
    assert(navigation.activeTab.get() == ProjectTab::STORAGE);
    assert(navigation.focusedRow.get() == 0);

    const auto page = core::state::project::buildProjectMenuPage(
        navigation,
        projectContext("P002", "Project 002", true, true)
    );
    assert(page.rowCount == 4);
    assert(std::string(page.meta) == "LOAD DIRTY?");
    assert(std::string(page.rows[0].label) == "Save & Load");
    assert(page.rows[0].enabled);
    assert(std::string(rowValue(page.rows[0])) == "P002 > P003");
    assert(std::string(page.rows[1].label) == "Save As & Load");
    assert(page.rows[1].enabled);
    assert(std::string(rowValue(page.rows[1])) == "New > P003");
    assert(std::string(page.rows[2].label) == "Don't Save");
    assert(page.rows[2].enabled);
    assert(std::string(rowValue(page.rows[2])) == "Load P003");
    assert(std::string(page.rows[3].label) == "Cancel");

    assert(core::state::project::backProjectNavigation(navigation));
    assert(navigation.currentNode.get() == ProjectNodeId::LOAD_PROJECT);
    assert(navigation.focusedRow.get() == 0);

    std::cout << "[PASS] test_load_project_confirmation_prompts_dirty_session_choice\n";
}

void test_load_project_confirmation_without_saved_identity_disables_save_choice() {
    core::state::project::ProjectNavigationState navigation;

    assert(core::state::project::openProjectLoadPicker(navigation));
    assert(core::state::project::openProjectLoadConfirmation(navigation, "P004", false));
    assert(navigation.focusedRow.get() == 0);

    const auto page = core::state::project::buildProjectMenuPage(
        navigation,
        projectContext("", "Untitled", true, false)
    );
    assert(page.rowCount == 3);
    assert(std::string(page.rows[0].label) == "Save As & Load");
    assert(page.rows[0].enabled);
    assert(std::string(rowValue(page.rows[0])) == "Untitled > P004");
    assert(std::string(page.rows[1].label) == "Don't Save");
    assert(page.rows[1].enabled);
    assert(std::string(rowValue(page.rows[1])) == "Load P004");
    assert(std::string(page.rows[2].label) == "Cancel");

    std::cout << "[PASS] test_load_project_confirmation_without_saved_identity_disables_save_choice\n";
}

void test_load_project_picker_empty_state_is_disabled() {
    core::state::project::ProjectNavigationState navigation;

    navigation.loadProjects.clear();
    navigation.loadProjects.scanned = true;
    assert(core::state::project::openProjectLoadPicker(navigation));

    const auto page = core::state::project::buildProjectMenuPage(navigation);
    assert(page.rowCount == 1);
    assert(std::string(page.rows[0].label) == "No projects");
    assert(!page.rows[0].enabled);
    assert(std::string(rowValue(page.rows[0])) == "Save first");

    std::cout << "[PASS] test_load_project_picker_empty_state_is_disabled\n";
}

void test_navigation_wraps_rows() {
    core::state::project::ProjectNavigationState navigation;

    core::state::project::navigateProjectRows(navigation, -1.0f);
    assert(navigation.focusedRow.get() == 3);

    core::state::project::navigateProjectRows(navigation, 2.0f);
    assert(navigation.focusedRow.get() == 1);

    core::state::project::navigateProjectRows(navigation, -3.0f);
    assert(navigation.focusedRow.get() == 2);

    std::cout << "[PASS] test_navigation_wraps_rows\n";
}

void test_focus_changes_selection_without_content_revision_change() {
    core::state::project::ProjectNavigationState navigation;
    core::state::project::switchProjectTab(navigation, 2);

    const auto before = core::state::project::buildProjectMenuPage(navigation);
    assert(before.selectedIndex == 0);

    core::state::project::navigateProjectRows(navigation, 1.0f);
    const auto after = core::state::project::buildProjectMenuPage(navigation);

    assert(after.selectedIndex == 1);
    assert(after.dataRevision == before.dataRevision);

    std::cout << "[PASS] test_focus_changes_selection_without_content_revision_change\n";
}

}  // namespace

int main() {
    test_overview_root_exposes_project_actions();
    test_enter_music_then_scale_and_back();
    test_switch_tab_resets_to_target_tab_root();
    test_root_section_is_a_navigation_root();
    test_new_project_confirmation_page_defaults_to_save_choice();
    test_new_project_confirmation_uses_current_project_identity();
    test_music_scale_rows_use_project_scale_context();
    test_music_root_scale_row_summarizes_key_and_folder_target();
    test_transport_rows_use_runtime_context();
    test_routing_rows_expose_all_track_output_channels();
    test_storage_project_identity_is_read_only_and_autosave_is_activable();
    test_load_project_picker_shows_detected_projects();
    test_load_project_confirmation_prompts_dirty_session_choice();
    test_load_project_confirmation_without_saved_identity_disables_save_choice();
    test_load_project_picker_empty_state_is_disabled();
    test_navigation_wraps_rows();
    test_focus_changes_selection_without_content_revision_change();

    std::cout << "\nAll ProjectMenuModel tests passed.\n";
    return 0;
}
