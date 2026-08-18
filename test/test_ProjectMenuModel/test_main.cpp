#include <cassert>
#include <cstring>
#include <iostream>
#include <string>

#include "../../src/state/project/ProjectMenuModel.hpp"
#include "../../src/state/project/ProjectModulatorMenuModel.hpp"
#include "../../src/state/sequencer/SequencerScaleCatalog.hpp"

namespace {

using core::state::project::ProjectNodeId;
using core::state::project::ProjectMenuIcon;
using core::state::project::ProjectMenuRowTone;
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
    core::state::project::switchProjectTab(navigation, 4);
}

void test_project_tab_catalog_is_explicit_and_excludes_modulators() {
    using core::state::project::projectRootTabAt;
    using core::state::project::projectRootTabCount;
    using core::state::project::projectRootTabIndex;

    assert(projectRootTabCount() == 5U);
    assert(projectRootTabAt(0) == ProjectTab::OVERVIEW);
    assert(projectRootTabAt(1) == ProjectTab::MUSIC);
    assert(projectRootTabAt(2) == ProjectTab::TRANSPORT);
    assert(projectRootTabAt(3) == ProjectTab::ROUTING);
    assert(projectRootTabAt(4) == ProjectTab::STORAGE);
    assert(projectRootTabIndex(ProjectTab::MODULATORS) < 0);
}

void test_overview_root_is_a_navigable_project_summary() {
    core::state::project::ProjectNavigationState navigation;

    const auto page = core::state::project::buildProjectMenuPage(
        navigation,
        projectContext("p002", "p002", true, true)
    );
    assert(page.rowCount == 5);
    assert(page.selectedIndex == 0);
    assert(std::string(page.meta) == "Overview  p002*");
    assert(std::string(page.rows[0].label) == "Music");
    assert(page.rows[0].kind == core::state::project::ProjectMenuRowKind::Folder);
    assert(page.rows[0].enabled);
    assert(page.rows[0].hasTarget);
    assert(page.rows[0].target == ProjectNodeId::MUSIC_ROOT);
    assert(std::string(page.rows[1].label) == "Tempo");
    assert(std::string(rowValue(page.rows[1])) == "120 BPM");
    assert(page.rows[1].target == ProjectNodeId::TRANSPORT_ROOT);
    assert(std::string(page.rows[2].label) == "Clock");
    assert(std::string(rowValue(page.rows[2])) == "Auto");
    assert(std::string(page.rows[3].label) == "Routing");
    assert(std::string(rowValue(page.rows[3])) == "16 Tracks");
    assert(std::string(page.rows[4].label) == "Storage");
    assert(std::string(rowValue(page.rows[4])) == "Modified");
    assert(page.rows[4].target == ProjectNodeId::STORAGE_ROOT);
    assert(page.rows[0].icon == ProjectMenuIcon::SCALE);
    assert(page.rows[1].icon == ProjectMenuIcon::TEMPO);
    assert(page.rows[2].icon == ProjectMenuIcon::CLOCK_SYNC);
    assert(page.rows[3].icon == ProjectMenuIcon::ROUTING);
    assert(page.rows[4].icon == ProjectMenuIcon::STORAGE);

    assert(core::state::project::enterFocusedProjectRow(navigation));
    assert(navigation.activeTab.get() == ProjectTab::MUSIC);
    assert(navigation.currentNode.get() == ProjectNodeId::MUSIC_ROOT);
    assert(navigation.depth.get() == 0U);

    std::cout << "[PASS] Overview is a navigable project summary\n";
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

void test_project_settings_tab_cycle_excludes_first_rank_modulators() {
    core::state::project::ProjectNavigationState navigation;

    core::state::project::switchProjectTab(navigation, -1);
    assert(navigation.activeTab.get() == ProjectTab::STORAGE);
    assert(navigation.currentNode.get() == ProjectNodeId::STORAGE_ROOT);

    core::state::project::switchProjectTab(navigation, 1);
    assert(navigation.activeTab.get() == ProjectTab::OVERVIEW);
    assert(navigation.currentNode.get() == ProjectNodeId::OVERVIEW_ROOT);

    core::state::project::openProjectRootTab(
        navigation,
        ProjectTab::MODULATORS
    );
    core::state::project::switchProjectTab(navigation, 1);
    assert(navigation.activeTab.get() == ProjectTab::MODULATORS);
    assert(navigation.currentNode.get() == ProjectNodeId::MODULATORS_ROOT);
    std::cout << "[PASS] Project Settings tab cycle excludes Modulators\n";
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
    assert(std::string(page.meta) == "New project?");
    assert(std::string(page.rows[0].label) == "Save as new");
    assert(page.rows[0].enabled);
    assert(std::string(rowValue(page.rows[0])) == "Next");
    assert(std::string(page.rows[1].label) == "Don't save");
    assert(page.rows[1].enabled);
    assert(std::string(rowValue(page.rows[1])) == "Reset");
    assert(page.rows[1].tone == ProjectMenuRowTone::Destructive);
    assert(std::string(page.rows[2].label) == "Cancel");
    assert(page.rows[2].enabled);
    assert(page.rows[0].icon == ProjectMenuIcon::ACTION_NEW_PROJECT);
    assert(page.rows[1].icon == ProjectMenuIcon::ACTION_NEW_PROJECT);
    assert(page.rows[2].icon == ProjectMenuIcon::ACTION_CANCEL);

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
        projectContext("p002", "p002", true, true)
    );

    assert(page.rowCount == 3);
    assert(page.selectedIndex == 0);
    assert(std::string(page.rows[0].label) == "Save & reset");
    assert(page.rows[0].enabled);
    assert(std::string(rowValue(page.rows[0])) == "p002");
    assert(std::string(page.rows[1].label) == "Don't save");
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
    assert(page.rows[0].icon == ProjectMenuIcon::NOTE_PROP_PITCH);
    assert(page.rows[1].icon == ProjectMenuIcon::SCALE);
    assert(page.rows[2].icon == ProjectMenuIcon::LOCK);
    assert(page.rows[3].icon == ProjectMenuIcon::PATTERN);
    assert(page.rows[4].icon == ProjectMenuIcon::CLIP);

    std::cout << "[PASS] test_music_scale_rows_use_project_scale_context\n";
}

void test_music_root_scale_row_summarizes_key_and_folder_target() {
    core::state::project::ProjectNavigationState navigation;
    core::state::project::switchProjectTab(navigation, 1);

    auto context = core::state::project::ProjectMenuContext{};
    context.projectScale.root = 5;
    context.projectScale.type = scale_catalog::StepSequencerScaleType::HarmonicMinor;

    const auto page = core::state::project::buildProjectMenuPage(navigation, context);
    assert(page.rowCount == 5);
    assert(std::string(page.rows[0].label) == "Scale");
    assert(page.rows[0].kind == core::state::project::ProjectMenuRowKind::Folder);
    assert(page.rows[0].hasTarget);
    assert(page.rows[0].target == ProjectNodeId::MUSIC_SCALE);
    assert(std::string(rowValue(page.rows[0])) == "F Harm Minor >");
    assert(std::string(page.rows[3].label) == "Step paste");
    assert(page.rows[3].kind == core::state::project::ProjectMenuRowKind::Value);
    assert(std::string(rowValue(page.rows[3])) == "Extend");
    assert(std::string(page.rows[4].label) == "CC defaults");
    assert(std::string(rowValue(page.rows[4])) == "4 Lanes");
    assert(page.rows[4].kind == core::state::project::ProjectMenuRowKind::Folder);
    assert(page.rows[4].hasTarget);
    assert(page.rows[4].target == ProjectNodeId::MUSIC_CC_DEFAULTS);
    assert(page.rows[0].icon == ProjectMenuIcon::SCALE);
    assert(page.rows[1].icon == ProjectMenuIcon::PATTERN);
    assert(page.rows[2].icon == ProjectMenuIcon::CLIP);
    assert(page.rows[3].icon == ProjectMenuIcon::ACTION_PASTE);
    assert(page.rows[4].icon == ProjectMenuIcon::MIDI_CC);

    navigation.focusedRow.set(4U);
    assert(core::state::project::enterFocusedProjectRow(navigation));
    assert(navigation.currentNode.get() == ProjectNodeId::MUSIC_CC_DEFAULTS);
    assert(navigation.depth.get() == 1U);
    const auto defaultsPage = core::state::project::buildProjectMenuPage(navigation, context);
    assert(defaultsPage.rowCount == 4U);
    assert(std::string(defaultsPage.meta) == "Music > CC defaults");
    assert(std::string(defaultsPage.rows[0].label) == "Lane 1");
    assert(std::string(rowValue(defaultsPage.rows[0])) == "CC 1");
    assert(std::string(rowValue(defaultsPage.rows[1])) == "CC 11");
    assert(std::string(rowValue(defaultsPage.rows[2])) == "CC 74");
    assert(std::string(defaultsPage.rows[3].label) == "Lane 4");
    assert(std::string(rowValue(defaultsPage.rows[3])) == "CC 71");
    assert(defaultsPage.rows[0].icon == ProjectMenuIcon::MIDI_CC);
    assert(core::state::project::backProjectNavigation(navigation));
    assert(navigation.currentNode.get() == ProjectNodeId::MUSIC_ROOT);
    assert(navigation.focusedRow.get() == 4U);

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
    assert(page.rows[0].icon == ProjectMenuIcon::TEMPO);
    assert(page.rows[1].icon == ProjectMenuIcon::SWING);
    assert(page.rows[2].icon == ProjectMenuIcon::CLOCK_SYNC);
    assert(page.rows[3].icon == ProjectMenuIcon::TRANSPORT_PLAY);
    assert(page.rows[4].icon == ProjectMenuIcon::SETTINGS_GEAR);

    std::cout << "[PASS] test_transport_rows_use_runtime_context\n";
}

void test_routing_rows_expose_all_track_output_channels() {
    core::state::project::ProjectNavigationState navigation;
    core::state::project::switchProjectTab(navigation, 3);

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
    assert(page.rows[0].icon == ProjectMenuIcon::MIDI_CHANNEL);
    assert(page.rows[15].icon == ProjectMenuIcon::MIDI_CHANNEL);

    std::cout << "[PASS] test_routing_rows_expose_all_track_output_channels\n";
}

void test_storage_has_six_rows_and_read_only_project_identity() {
    core::state::project::ProjectNavigationState navigation;

    switchToStorage(navigation);

    navigation.focusedRow.set(5);
    const uint8_t revisionBefore = navigation.contentRevision.get();
    assert(!core::state::project::enterFocusedProjectRow(navigation));
    auto page = core::state::project::buildProjectMenuPage(
        navigation,
        projectContext("p002", "p002", true, true)
    );
    assert(page.rowCount == 6);
    assert(std::string(page.meta) == "Storage  p002*");
    for (uint8_t row = 0; row < 5U; ++row) {
        assert(page.rows[row].tone == ProjectMenuRowTone::Neutral);
    }
    assert(std::string(page.rows[5].label) == "Project");
    assert(page.rows[5].kind == core::state::project::ProjectMenuRowKind::Disabled);
    assert(std::string(rowValue(page.rows[5])) == "p002");
    assert(page.rows[0].icon == ProjectMenuIcon::ACTION_SAVE);
    assert(page.rows[1].icon == ProjectMenuIcon::ACTION_SAVE);
    assert(page.rows[2].icon == ProjectMenuIcon::ACTION_RENAME);
    assert(page.rows[3].icon == ProjectMenuIcon::ACTION_NEW_PROJECT);
    assert(page.rows[4].icon == ProjectMenuIcon::ACTION_LOAD);
    assert(page.rows[5].icon == ProjectMenuIcon::VIEW_PROJECT);
    assert(navigation.contentRevision.get() == revisionBefore);

    core::state::project::navigateProjectRows(navigation, 1.0f);
    assert(navigation.focusedRow.get() == 0);
    core::state::project::navigateProjectRows(navigation, -1.0f);
    assert(navigation.focusedRow.get() == 5);

    std::cout << "[PASS] test_storage_has_six_rows_and_read_only_project_identity\n";
}

void test_project_name_editor_exposes_qwerty_entry_state() {
    core::state::project::ProjectNavigationState navigation;

    assert(core::state::project::openProjectNameEditor(
        navigation,
        ProjectNodeId::RENAME_PROJECT_NAME,
        "p042"
    ));
    assert(navigation.currentNode.get() == ProjectNodeId::RENAME_PROJECT_NAME);
    assert(navigation.activeTab.get() == ProjectTab::STORAGE);
    assert(navigation.depth.get() == 1);
    assert(navigation.focusedRow.get() == 1);
    assert(core::state::project::projectNavigationInProjectConfirmation(navigation));

    const auto page = core::state::project::buildProjectMenuPage(navigation);
    assert(page.rowCount == 2);
    assert(std::string(page.meta) == "Rename");
    assert(std::string(page.rows[0].label) == "Name");
    assert(!page.rows[0].enabled);
    assert(std::string(rowValue(page.rows[0])) == "p042");
    assert(page.rows[0].icon == ProjectMenuIcon::ACTION_RENAME);
    assert(std::string(page.rows[1].label) == "Key");
    assert(page.rows[1].kind == core::state::project::ProjectMenuRowKind::Value);
    assert(std::string(rowValue(page.rows[1])) == "q");

    std::cout << "[PASS] test_project_name_editor_exposes_qwerty_entry_state\n";
}

void test_load_project_picker_shows_detected_projects() {
    core::state::project::ProjectNavigationState navigation;

    navigation.loadProjects.clear();
    navigation.loadProjects.scanned = true;
    assert(navigation.loadProjects.add("p001", 100));
    assert(navigation.loadProjects.add("p003", 200));

    assert(core::state::project::openProjectLoadPicker(navigation));
    assert(navigation.currentNode.get() == ProjectNodeId::LOAD_PROJECT);
    assert(navigation.depth.get() == 1);

    auto page = core::state::project::buildProjectMenuPage(navigation);
    assert(page.rowCount == 2);
    assert(std::string(page.meta) == "Load project");
    assert(std::string(page.rows[0].label) == "p001");
    assert(std::string(rowValue(page.rows[0])) == "Load");
    assert(page.rows[0].enabled);
    assert(std::string(page.rows[1].label) == "p003");
    assert(page.rows[0].icon == ProjectMenuIcon::VIEW_PROJECT);
    assert(page.rows[1].icon == ProjectMenuIcon::VIEW_PROJECT);

    assert(core::state::project::backProjectNavigation(navigation));
    assert(navigation.currentNode.get() == ProjectNodeId::OVERVIEW_ROOT);

    std::cout << "[PASS] test_load_project_picker_shows_detected_projects\n";
}

void test_load_project_confirmation_prompts_dirty_session_choice() {
    core::state::project::ProjectNavigationState navigation;

    navigation.loadProjects.clear();
    navigation.loadProjects.scanned = true;
    assert(navigation.loadProjects.add("p003", 200));
    assert(core::state::project::openProjectLoadPicker(navigation));
    assert(core::state::project::openProjectLoadConfirmation(navigation, "p003", true));
    assert(navigation.currentNode.get() == ProjectNodeId::LOAD_PROJECT_CONFIRM);
    assert(navigation.activeTab.get() == ProjectTab::STORAGE);
    assert(navigation.focusedRow.get() == 0);

    const auto page = core::state::project::buildProjectMenuPage(
        navigation,
        projectContext("p002", "p002", true, true)
    );
    assert(page.rowCount == 4);
    assert(std::string(page.meta) == "Load dirty?");
    assert(std::string(page.rows[0].label) == "Save & load");
    assert(page.rows[0].enabled);
    assert(std::string(rowValue(page.rows[0])) == "p002 > p003");
    assert(std::string(page.rows[1].label) == "Save as & load");
    assert(page.rows[1].enabled);
    assert(std::string(rowValue(page.rows[1])) == "New > p003");
    assert(std::string(page.rows[2].label) == "Don't save");
    assert(page.rows[2].enabled);
    assert(std::string(rowValue(page.rows[2])) == "Load p003");
    assert(page.rows[2].tone == ProjectMenuRowTone::Destructive);
    assert(std::string(page.rows[3].label) == "Cancel");
    assert(page.rows[0].icon == ProjectMenuIcon::ACTION_LOAD);
    assert(page.rows[1].icon == ProjectMenuIcon::ACTION_LOAD);
    assert(page.rows[2].icon == ProjectMenuIcon::ACTION_LOAD);
    assert(page.rows[3].icon == ProjectMenuIcon::ACTION_CANCEL);

    assert(core::state::project::backProjectNavigation(navigation));
    assert(navigation.currentNode.get() == ProjectNodeId::LOAD_PROJECT);
    assert(navigation.focusedRow.get() == 0);

    std::cout << "[PASS] test_load_project_confirmation_prompts_dirty_session_choice\n";
}

void test_load_project_confirmation_without_saved_identity_disables_save_choice() {
    core::state::project::ProjectNavigationState navigation;

    assert(core::state::project::openProjectLoadPicker(navigation));
    assert(core::state::project::openProjectLoadConfirmation(navigation, "p004", false));
    assert(navigation.focusedRow.get() == 0);

    const auto page = core::state::project::buildProjectMenuPage(
        navigation,
        projectContext("", "untitled", true, false)
    );
    assert(page.rowCount == 3);
    assert(std::string(page.rows[0].label) == "Save as & load");
    assert(page.rows[0].enabled);
    assert(std::string(rowValue(page.rows[0])) == "untitled > p004");
    assert(std::string(page.rows[1].label) == "Don't save");
    assert(page.rows[1].enabled);
    assert(std::string(rowValue(page.rows[1])) == "Load p004");
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
    assert(navigation.focusedRow.get() == 4);

    core::state::project::navigateProjectRows(navigation, 2.0f);
    assert(navigation.focusedRow.get() == 1);

    core::state::project::navigateProjectRows(navigation, -3.0f);
    assert(navigation.focusedRow.get() == 3);

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

void test_modulator_workspace_layouts_are_semantic_and_bounded() {
    namespace modulators = core::state::project::modulators;
    using core::state::modulation::ModulatorKind;
    using Item = modulators::SourceDetailItem;

    assert(modulators::MODULATOR_SOURCE_KIND_COUNT == 3U);
    const auto lfoTarget = modulators::sourceKindTargetAtRow(0U);
    const auto adsrTarget = modulators::sourceKindTargetAtRow(1U);
    const auto recordedTarget = modulators::sourceKindTargetAtRow(2U);
    assert(lfoTarget.valid && lfoTarget.kind == ModulatorKind::LFO);
    assert(adsrTarget.valid && adsrTarget.kind == ModulatorKind::ADSR);
    assert(
        recordedTarget.valid &&
        recordedTarget.kind == ModulatorKind::RECORDED_SHAPE
    );
    assert(!modulators::sourceKindTargetAtRow(
        modulators::MODULATOR_SOURCE_KIND_COUNT
    ).valid);

    const auto lfo = modulators::sourceWorkspaceLayout(
        ModulatorKind::LFO,
        false,
        false
    );
    assert(lfo.count == 5U);
    assert(lfo.at(0) == Item::SHAPE);
    assert(lfo.at(1) == Item::TIMING);
    assert(lfo.at(2) == Item::RATE);
    assert(lfo.at(3) == Item::OPTIONS);
    assert(lfo.at(4) == Item::DESTINATIONS);

    const auto recorded = modulators::sourceWorkspaceLayout(
        ModulatorKind::RECORDED_SHAPE,
        false,
        false
    );
    assert(recorded.count == 4U);
    assert(recorded.at(0) == Item::RECORD);
    assert(recorded.at(1) == Item::LENGTH);
    assert(recorded.at(2) == Item::OPTIONS);
    assert(recorded.at(3) == Item::DESTINATIONS);

    const auto adsr = modulators::sourceWorkspaceLayout(
        ModulatorKind::ADSR,
        false,
        false
    );
    assert(adsr.count == 7U);
    assert(adsr.at(0) == Item::ATTACK);
    assert(adsr.at(1) == Item::DECAY);
    assert(adsr.at(2) == Item::SUSTAIN);
    assert(adsr.at(3) == Item::RELEASE);
    assert(adsr.at(4) == Item::TRIGGER);
    assert(adsr.at(5) == Item::OPTIONS);
    assert(adsr.at(6) == Item::DESTINATIONS);

    const auto lfoOptions = modulators::sourceWorkspaceLayout(
        ModulatorKind::LFO,
        true,
        false
    );
    assert(lfoOptions.count == 4U);
    assert(lfoOptions.at(0) == Item::PHASE);
    assert(lfoOptions.at(1) == Item::RETRIGGER);
    assert(lfoOptions.at(2) == Item::RENAME);
    assert(lfoOptions.at(3) == Item::DESTINATIONS);

    const auto recordedOptions = modulators::sourceWorkspaceLayout(
        ModulatorKind::RECORDED_SHAPE,
        true,
        false
    );
    assert(recordedOptions.count == 2U);
    assert(recordedOptions.at(0) == Item::RENAME);
    assert(recordedOptions.at(1) == Item::DESTINATIONS);

    const auto adsrOptions = modulators::sourceWorkspaceLayout(
        ModulatorKind::ADSR,
        true,
        false
    );
    assert(adsrOptions.count == 7U);
    assert(adsrOptions.at(0) == Item::DELAY);
    assert(adsrOptions.at(1) == Item::HOLD);
    assert(adsrOptions.at(2) == Item::TIMING);
    assert(adsrOptions.at(3) == Item::SMOOTH);
    assert(adsrOptions.at(4) == Item::RESPONSE);
    assert(adsrOptions.at(5) == Item::RETRIGGER);
    assert(adsrOptions.at(6) == Item::RENAME);

    const auto lfoAudition = modulators::sourceWorkspaceLayout(
        ModulatorKind::LFO,
        false,
        true
    );
    assert(lfoAudition.count == 4U);
    assert(lfoAudition.at(0) == Item::SHAPE);
    assert(lfoAudition.at(1) == Item::RATE);
    assert(lfoAudition.at(2) == Item::DEPTH);
    assert(lfoAudition.at(3) == Item::OPTIONS);

    const auto lfoAuditionOptions = modulators::sourceWorkspaceLayout(
        ModulatorKind::LFO,
        true,
        true
    );
    assert(lfoAuditionOptions.count == 3U);
    assert(lfoAuditionOptions.at(0) == Item::TIMING);
    assert(lfoAuditionOptions.at(1) == Item::PHASE);
    assert(lfoAuditionOptions.at(2) == Item::RETRIGGER);

    const auto adsrAudition = modulators::sourceWorkspaceLayout(
        ModulatorKind::ADSR,
        false,
        true
    );
    assert(adsrAudition.count == 7U);
    assert(adsrAudition.at(4) == Item::TRIGGER);
    assert(adsrAudition.at(5) == Item::OPTIONS);
    assert(adsrAudition.at(6) == Item::DEPTH);
    const auto recordedAudition = modulators::sourceWorkspaceLayout(
        ModulatorKind::RECORDED_SHAPE,
        false,
        true
    );
    assert(recordedAudition.count == 3U);
    assert(recordedAudition.at(0) == Item::RECORD);
    assert(recordedAudition.at(1) == Item::LENGTH);
    assert(recordedAudition.at(2) == Item::DEPTH);
    const auto adsrAuditionOptions = modulators::sourceWorkspaceLayout(
        ModulatorKind::ADSR,
        true,
        true
    );
    assert(adsrAuditionOptions.count == 6U);
    assert(adsrAuditionOptions.at(0) == Item::DELAY);
    assert(adsrAuditionOptions.at(1) == Item::HOLD);
    assert(adsrAuditionOptions.at(2) == Item::TIMING);
    assert(adsrAuditionOptions.at(3) == Item::SMOOTH);
    assert(adsrAuditionOptions.at(4) == Item::RESPONSE);
    assert(adsrAuditionOptions.at(5) == Item::RETRIGGER);
    std::cout << "[PASS] test_modulator_workspace_layouts_are_semantic_and_bounded\n";
}

void test_modulator_workspace_navigation_restores_local_focus() {
    core::state::project::ProjectNavigationState navigation;
    core::state::project::openProjectRootTab(
        navigation,
        ProjectTab::MODULATORS
    );
    const core::state::modulation::ModulatorId sourceId{42U};
    assert(core::state::project::openProjectModulatorDetail(
        navigation,
        sourceId
    ));
    navigation.focusedRow.set(3U);
    assert(core::state::project::openProjectModulatorOptions(navigation));
    assert(navigation.currentNode.get() == ProjectNodeId::MODULATOR_SOURCE_OPTIONS);
    assert(navigation.activeTab.get() == ProjectTab::MODULATORS);

    navigation.focusedRow.set(3U);
    assert(core::state::project::openProjectModulatorDestinations(navigation));
    assert(navigation.currentNode.get() == ProjectNodeId::MODULATOR_DESTINATIONS);
    assert(core::state::project::backProjectNavigation(navigation));
    assert(navigation.currentNode.get() == ProjectNodeId::MODULATOR_SOURCE_OPTIONS);
    assert(navigation.focusedRow.get() == 3U);

    navigation.focusedRow.set(2U);
    assert(core::state::project::openProjectNameEditor(
        navigation,
        ProjectNodeId::MODULATOR_SOURCE_RENAME,
        "Shared LFO"
    ));
    assert(navigation.currentNode.get() == ProjectNodeId::MODULATOR_SOURCE_RENAME);
    assert(navigation.activeTab.get() == ProjectTab::MODULATORS);
    assert(std::string(navigation.editingProjectSlug.data()) == "Shared LFO");
    assert(core::state::project::backProjectNavigation(navigation));
    assert(navigation.currentNode.get() == ProjectNodeId::MODULATOR_SOURCE_OPTIONS);
    assert(navigation.focusedRow.get() == 2U);
    std::cout << "[PASS] test_modulator_workspace_navigation_restores_local_focus\n";
}

void test_modulator_kind_and_trigger_navigation_are_reversible() {
    core::state::project::ProjectNavigationState navigation;
    core::state::project::openProjectRootTab(
        navigation,
        ProjectTab::MODULATORS
    );
    assert(navigation.currentNode.get() == ProjectNodeId::MODULATORS_ROOT);
    assert(core::state::project::openProjectModulatorKindPicker(navigation));
    assert(navigation.currentNode.get() ==
           ProjectNodeId::MODULATOR_SOURCE_KIND_PICKER);
    assert(navigation.depth.get() == 1U);
    navigation.focusedRow.set(1U);
    navigation.creatingModulatorKind =
        core::state::modulation::ModulatorKind::ADSR;
    assert(core::state::project::openProjectModulatorDestinationPicker(
        navigation,
        2U,
        1U,
        true
    ));
    assert(navigation.currentNode.get() ==
           ProjectNodeId::MODULATOR_DESTINATION_PICKER);
    assert(navigation.depth.get() == 2U);
    assert(core::state::project::backProjectNavigation(navigation));
    assert(navigation.currentNode.get() ==
           ProjectNodeId::MODULATOR_SOURCE_KIND_PICKER);
    assert(navigation.focusedRow.get() == 1U);
    assert(core::state::project::backProjectNavigation(navigation));

    const core::state::modulation::ModulatorId sourceId{7U};
    assert(core::state::project::openProjectModulatorDetail(
        navigation,
        sourceId
    ));
    navigation.focusedRow.set(4U);
    assert(core::state::project::openProjectModulatorTrigger(navigation));
    assert(navigation.currentNode.get() == ProjectNodeId::MODULATOR_TRIGGER);
    assert(navigation.depth.get() == 2U);
    assert(core::state::project::backProjectNavigation(navigation));
    assert(navigation.currentNode.get() ==
           ProjectNodeId::MODULATOR_SOURCE_DETAIL);
    assert(navigation.focusedRow.get() == 4U);
    std::cout << "[PASS] kind and Trigger journeys preserve local context\n";
}

void test_destination_picker_exposes_hierarchy_and_sparse_macro_positions() {
    using namespace core::state::project;
    using namespace core::state::project::modulators;
    core::state::macro::MacroPagesState pages;
    ProjectNavigationState navigation;
    navigation.creatingModulatorSource = true;
    navigation.destinationPickerLevel = ModulatorDestinationPickerLevel::TRACK;

    assert(destinationPickerRowCount(pages, navigation) == 3U);
    auto row = destinationPickerTargetAtRow(pages, navigation, 0U);
    assert(row.valid && row.kind == DestinationPickerRowKind::TRACK &&
           row.index == 0U && !row.create);
    row = destinationPickerTargetAtRow(pages, navigation, 1U);
    assert(row.valid && row.kind == DestinationPickerRowKind::TRACK &&
           row.index == 1U && row.create);
    row = destinationPickerTargetAtRow(pages, navigation, 2U);
    assert(row.valid && row.kind == DestinationPickerRowKind::KEEP_UNASSIGNED);

    navigation.destinationPickerTrack = 0U;
    navigation.destinationPickerLevel = ModulatorDestinationPickerLevel::PAGE;
    assert(destinationPickerRowCount(pages, navigation) == 2U);
    row = destinationPickerTargetAtRow(pages, navigation, 1U);
    assert(row.valid && row.kind == DestinationPickerRowKind::PAGE &&
           row.index == 1U && row.create);

    navigation.destinationPickerPage = 0U;
    navigation.destinationPickerLevel = ModulatorDestinationPickerLevel::MACRO;
    assert(destinationPickerRowCount(pages, navigation) ==
           core::state::macro::MACRO_COUNT);
    row = destinationPickerTargetAtRow(pages, navigation, 3U);
    assert(row.valid && row.kind == DestinationPickerRowKind::MACRO &&
           row.index == 3U && row.create);
    assert(core::state::macro::defaultMacroCc(0U, 3U) == 3U);
    std::cout << "[PASS] destination picker is hierarchical and sparse\n";
}

}  // namespace

int main() {
    test_project_tab_catalog_is_explicit_and_excludes_modulators();
    test_overview_root_is_a_navigable_project_summary();
    test_enter_music_then_scale_and_back();
    test_switch_tab_resets_to_target_tab_root();
    test_project_settings_tab_cycle_excludes_first_rank_modulators();
    test_root_section_is_a_navigation_root();
    test_new_project_confirmation_page_defaults_to_save_choice();
    test_new_project_confirmation_uses_current_project_identity();
    test_music_scale_rows_use_project_scale_context();
    test_music_root_scale_row_summarizes_key_and_folder_target();
    test_transport_rows_use_runtime_context();
    test_routing_rows_expose_all_track_output_channels();
    test_storage_has_six_rows_and_read_only_project_identity();
    test_project_name_editor_exposes_qwerty_entry_state();
    test_load_project_picker_shows_detected_projects();
    test_load_project_confirmation_prompts_dirty_session_choice();
    test_load_project_confirmation_without_saved_identity_disables_save_choice();
    test_load_project_picker_empty_state_is_disabled();
    test_navigation_wraps_rows();
    test_focus_changes_selection_without_content_revision_change();
    test_modulator_workspace_layouts_are_semantic_and_bounded();
    test_modulator_workspace_navigation_restores_local_focus();
    test_modulator_kind_and_trigger_navigation_are_reversible();
    test_destination_picker_exposes_hierarchy_and_sparse_macro_positions();

    std::cout << "\nAll ProjectMenuModel tests passed.\n";
    return 0;
}
