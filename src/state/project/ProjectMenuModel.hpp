#pragma once

#include <array>
#include <cstdint>

#include "state/MidiSyncState.hpp"
#include "state/project/ProjectDomainRules.hpp"
#include "state/project/ProjectNavigationState.hpp"
#include "state/sequencer/SequencerScaleState.hpp"
#include "state/sequencer/SequencerTrackBankState.hpp"

namespace core::state::modulation {
struct ProjectModulationState;
}

namespace core::state::project {

enum class ProjectMenuRowKind : uint8_t {
    Value = 0,
    Folder,
    Action,
    Toggle,
    Disabled,
};

enum class ProjectMenuRowTone : uint8_t {
    Neutral = 0,
    Positive,
    Warning,
    Destructive,
};

enum class ProjectMenuIcon : uint8_t {
    NONE = 0,
    SCALE,
    TEMPO,
    SWING,
    CLOCK_SYNC,
    ROUTING,
    STORAGE,
    PATTERN,
    CLIP,
    ACTION_PASTE,
    MIDI_CC,
    NOTE_PROP_PITCH,
    LOCK,
    TRANSPORT_PLAY,
    SETTINGS_GEAR,
    MIDI_CHANNEL,
    ACTION_SAVE,
    ACTION_RENAME,
    ACTION_NEW_PROJECT,
    ACTION_LOAD,
    VIEW_PROJECT,
    ACTION_CANCEL,
    STATUS_QUEUED,
    STATUS_WARNING,
};

struct ProjectMenuRow {
    static constexpr uint8_t VALUE_TEXT_SIZE = 20;

    const char* label = "";
    const char* value = "";
    ProjectMenuRowKind kind = ProjectMenuRowKind::Value;
    ProjectMenuRowTone tone = ProjectMenuRowTone::Neutral;
    ProjectMenuIcon icon = ProjectMenuIcon::NONE;
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

    const char* title = "Project";
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
    float tempoBpm = PROJECT_TEMPO_DEFAULT_BPM;
    core::state::MidiSyncMode clockMode = core::state::MidiSyncMode::AUTO;
    std::array<char, ProjectMetadata::ID_SIZE> projectId{};
    std::array<char, ProjectMetadata::NAME_SIZE> projectName{};
    bool projectDirty = false;
    bool projectHasSavedIdentity = false;
    std::array<uint8_t, core::state::sequencer::SequencerTrackBankState::TRACK_COUNT>
        outputMidiChannels{};
};

ProjectMenuPage buildProjectMenuPage(const ProjectNavigationState& navigation);
ProjectMenuPage buildProjectMenuPage(const ProjectNavigationState& navigation,
                                     ProjectMenuContext context);

void navigateProjectRows(ProjectNavigationState& navigation,
                         float delta,
                         uint16_t modulatorSourceCount = 0,
                         uint16_t modulatorDetailRowCount = 0);
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
void openProjectRootTab(ProjectNavigationState& navigation, ProjectTab tab);
bool projectNavigationAtRoot(const ProjectNavigationState& navigation);
void reconcileProjectModulatorNavigationAfterHistory(
    ProjectNavigationState& navigation,
    const core::state::modulation::ProjectModulationState& graph,
    bool preserveMissingSelection = true
);
uint16_t projectCurrentRowCount(const ProjectNavigationState& navigation,
                                uint16_t modulatorSourceCount = 0,
                                uint16_t modulatorDetailRowCount = 0);
bool openProjectModulatorDetail(
    ProjectNavigationState& navigation,
    core::state::modulation::ModulatorId sourceId
);
bool openProjectModulatorKindPicker(ProjectNavigationState& navigation);
bool openProjectModulatorWorkspace(
    ProjectNavigationState& navigation,
    core::state::modulation::ModulatorId sourceId
);
bool openProjectModulatorOptions(ProjectNavigationState& navigation);
bool openProjectModulatorDestinations(ProjectNavigationState& navigation);
bool openProjectModulatorTrigger(ProjectNavigationState& navigation);
bool openProjectModulatorDestinationPicker(
    ProjectNavigationState& navigation,
    uint8_t track,
    uint8_t page,
    bool creatingSource
);

}  // namespace core::state::project
