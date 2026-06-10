#pragma once

#include <array>
#include <cstdint>

#include <oc/state/Signal.hpp>
#include <oc/state/SignalString.hpp>

#include "state/project/ProjectNameKeyboard.hpp"
#include "state/project/ProjectState.hpp"

namespace core::state::project {

enum class ProjectTab : uint8_t {
    OVERVIEW = 0,
    MUSIC,
    TRANSPORT,
    STORAGE,
    ROUTING,
    COUNT,
};

enum class ProjectNodeId : uint8_t {
    OVERVIEW_ROOT = 0,
    MUSIC_ROOT,
    MUSIC_SCALE,
    TRANSPORT_ROOT,
    STORAGE_ROOT,
    ROUTING_ROOT,
    NEW_PROJECT_CONFIRM,
    LOAD_PROJECT,
    LOAD_PROJECT_CONFIRM,
    SAVE_AS_PROJECT_NAME,
    RENAME_PROJECT_NAME,
};

struct ProjectBrowserEntry {
    std::array<char, ProjectMetadata::ID_SIZE> id{};
    uint32_t sizeBytes = 0;
};

struct ProjectBrowserState {
    static constexpr uint8_t MAX_PROJECTS = 16;

    std::array<ProjectBrowserEntry, MAX_PROJECTS> entries{};
    uint8_t count = 0;
    bool scanned = false;
    bool truncated = false;

    void clear();
    bool add(const char* id, uint32_t sizeBytes);
};

struct ProjectNavigationState {
    static constexpr uint8_t MAX_DEPTH = 4;

    oc::state::Signal<ProjectTab, 8> activeTab{ProjectTab::OVERVIEW};
    oc::state::Signal<ProjectNodeId, 8> currentNode{ProjectNodeId::OVERVIEW_ROOT};
    oc::state::Signal<uint8_t, 8> depth{0};
    oc::state::Signal<uint8_t, 8> focusedRow{0};
    oc::state::Signal<bool, 8> physicalHoldActive{false};
    oc::state::Signal<uint8_t, 8> contentRevision{0};
    oc::state::SignalLabel lifecycleFeedback;

    bool autosaveEnabled = true;
    bool scaleConstrainEnabled = true;
    bool patternsInheritScale = true;
    bool clipsInheritScale = true;
    uint8_t transportSwingPercent = 0;
    uint8_t transportRunMode = 0;
    std::array<char, ProjectMetadata::ID_SIZE> pendingLoadProjectId{};
    std::array<char, ProjectMetadata::ID_SIZE> editingProjectSlug{};
    uint8_t projectNameKeyIndex = PROJECT_NAME_KEYBOARD_DEFAULT_INDEX;
    float projectNameOptRawPosition = 0.0f;
    float projectNameOptRowAccumulator = 0.0f;
    bool projectNameShiftActive = false;
    bool pendingLoadCanSaveCurrent = false;
    ProjectBrowserState loadProjects;

    std::array<ProjectNodeId, MAX_DEPTH> pathStack{
        ProjectNodeId::OVERVIEW_ROOT,
        ProjectNodeId::OVERVIEW_ROOT,
        ProjectNodeId::OVERVIEW_ROOT,
        ProjectNodeId::OVERVIEW_ROOT,
    };
    std::array<uint8_t, MAX_DEPTH> focusedRowByDepth{};

    void reset();
    void notifyContentChanged();
    void setLifecycleFeedback(const char* message);
    void clearLifecycleFeedback();
};

constexpr uint8_t projectTabCount() {
    return static_cast<uint8_t>(ProjectTab::COUNT);
}

ProjectNodeId rootNodeForTab(ProjectTab tab);
ProjectTab tabForRootNode(ProjectNodeId node);
const char* projectTabLabel(ProjectTab tab);

}  // namespace core::state::project
