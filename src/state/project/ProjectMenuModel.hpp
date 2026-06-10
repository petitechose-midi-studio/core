#pragma once

#include <array>
#include <cstdint>

#include "state/MidiSyncState.hpp"
#include "state/sequencer/SequencerScaleState.hpp"
#include "state/sequencer/SequencerTrackBankState.hpp"
#include "state/project/ProjectNavigationState.hpp"

namespace core::state::project {

enum class ProjectMenuRowKind : uint8_t {
    Value = 0,
    Folder,
    Action,
    Toggle,
    Disabled,
};

struct ProjectMenuRow {
    static constexpr uint8_t VALUE_TEXT_SIZE = 20;

    const char* label = "";
    const char* value = "";
    ProjectMenuRowKind kind = ProjectMenuRowKind::Value;
    bool enabled = true;
    ProjectNodeId target = ProjectNodeId::OVERVIEW_ROOT;
    bool hasTarget = false;
    std::array<char, VALUE_TEXT_SIZE> valueText{};

    const char* displayValue() const {
        return valueText[0] != '\0' ? valueText.data() : value;
    }
};

struct ProjectMenuPage {
    static constexpr uint8_t MAX_ROWS = core::state::sequencer::SequencerTrackBankState::TRACK_COUNT;
    static constexpr uint8_t META_TEXT_SIZE = 32;

    const char* title = "PROJECT";
    const char* meta = "";
    std::array<char, META_TEXT_SIZE> metaText{};
    std::array<ProjectMenuRow, MAX_ROWS> rows{};
    uint8_t rowCount = 0;
    uint8_t selectedIndex = 0;
    uint32_t dataRevision = 0;

    const char* displayMeta() const {
        return metaText[0] != '\0' ? metaText.data() : meta;
    }
};

struct ProjectMenuContext {
    oc::note::sequencer::StepSequencerScaleSettings projectScale =
        core::state::sequencer::defaultProjectScaleSettings();
    float tempoBpm = 120.0f;
    core::state::MidiSyncMode clockMode = core::state::MidiSyncMode::AUTO;
    std::array<char, ProjectMetadata::ID_SIZE> projectId{};
    std::array<char, ProjectMetadata::NAME_SIZE> projectName{};
    bool projectDirty = false;
    bool projectHasSavedIdentity = false;
    bool projectOverwriteSafe = true;
    std::array<uint8_t, core::state::sequencer::SequencerTrackBankState::TRACK_COUNT>
        outputMidiChannels{};
};

ProjectMenuPage buildProjectMenuPage(const ProjectNavigationState& navigation);
ProjectMenuPage buildProjectMenuPage(const ProjectNavigationState& navigation,
                                     ProjectMenuContext context);

void navigateProjectRows(ProjectNavigationState& navigation, float delta);
bool enterFocusedProjectRow(ProjectNavigationState& navigation);
bool backProjectNavigation(ProjectNavigationState& navigation);
bool openNewProjectConfirmation(ProjectNavigationState& navigation);
bool openProjectLoadPicker(ProjectNavigationState& navigation);
bool openProjectLoadConfirmation(ProjectNavigationState& navigation,
                                 const char* projectId,
                                 bool canSaveCurrent);
bool openProjectNameEditor(ProjectNavigationState& navigation,
                           ProjectNodeId editorNode,
                           const char* initialSlug);
bool projectNavigationInNewProjectConfirmation(const ProjectNavigationState& navigation);
bool projectNavigationInProjectConfirmation(const ProjectNavigationState& navigation);
void switchProjectTab(ProjectNavigationState& navigation, int delta);
bool projectNavigationAtRoot(const ProjectNavigationState& navigation);
uint8_t projectCurrentRowCount(const ProjectNavigationState& navigation);

}  // namespace core::state::project
