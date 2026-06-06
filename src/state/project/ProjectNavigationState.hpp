#pragma once

#include <array>
#include <cstdint>

#include <oc/state/Signal.hpp>

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
};

struct ProjectNavigationState {
    static constexpr uint8_t MAX_DEPTH = 4;

    oc::state::Signal<ProjectTab, 8> activeTab{ProjectTab::OVERVIEW};
    oc::state::Signal<ProjectNodeId, 8> currentNode{ProjectNodeId::OVERVIEW_ROOT};
    oc::state::Signal<uint8_t, 8> depth{0};
    oc::state::Signal<uint8_t, 8> focusedRow{0};
    oc::state::Signal<bool, 8> physicalHoldActive{false};
    oc::state::Signal<uint8_t, 8> contentRevision{0};

    bool autosaveEnabled = true;
    uint8_t storageSlotIndex = 0;
    bool scaleConstrainEnabled = true;
    bool patternsInheritScale = true;
    bool clipsInheritScale = true;
    uint8_t transportSwingPercent = 0;
    uint8_t transportRunMode = 0;

    std::array<ProjectNodeId, MAX_DEPTH> pathStack{
        ProjectNodeId::OVERVIEW_ROOT,
        ProjectNodeId::OVERVIEW_ROOT,
        ProjectNodeId::OVERVIEW_ROOT,
        ProjectNodeId::OVERVIEW_ROOT,
    };
    std::array<uint8_t, MAX_DEPTH> focusedRowByDepth{};

    void reset();
    void notifyContentChanged();
};

constexpr uint8_t projectTabCount() {
    return static_cast<uint8_t>(ProjectTab::COUNT);
}

ProjectNodeId rootNodeForTab(ProjectTab tab);
ProjectTab tabForRootNode(ProjectNodeId node);
const char* projectTabLabel(ProjectTab tab);

}  // namespace core::state::project
