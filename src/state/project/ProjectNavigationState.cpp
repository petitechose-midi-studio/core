#include "state/project/ProjectNavigationState.hpp"

#include <config/PlatformCompat.hpp>

namespace core::state::project {

FLASHMEM void ProjectNavigationState::reset() {
    activeTab.set(ProjectTab::OVERVIEW);
    currentNode.set(ProjectNodeId::OVERVIEW_ROOT);
    depth.set(0);
    focusedRow.set(0);
    physicalHoldActive.set(false);
    contentRevision.set(0);
    autosaveEnabled = true;
    storageSlotIndex = 0;
    scaleConstrainEnabled = true;
    patternsInheritScale = true;
    clipsInheritScale = true;
    transportSwingPercent = 0;
    transportRunMode = 0;
    pathStack = {
        ProjectNodeId::OVERVIEW_ROOT,
        ProjectNodeId::OVERVIEW_ROOT,
        ProjectNodeId::OVERVIEW_ROOT,
        ProjectNodeId::OVERVIEW_ROOT,
    };
    focusedRowByDepth = {};
}

FLASHMEM void ProjectNavigationState::notifyContentChanged() {
    contentRevision.set(static_cast<uint8_t>(contentRevision.get() + 1));
}

FLASHMEM ProjectNodeId rootNodeForTab(ProjectTab tab) {
    switch (tab) {
        case ProjectTab::MUSIC:
            return ProjectNodeId::MUSIC_ROOT;
        case ProjectTab::TRANSPORT:
            return ProjectNodeId::TRANSPORT_ROOT;
        case ProjectTab::STORAGE:
            return ProjectNodeId::STORAGE_ROOT;
        case ProjectTab::ROUTING:
            return ProjectNodeId::ROUTING_ROOT;
        case ProjectTab::OVERVIEW:
        default:
            return ProjectNodeId::OVERVIEW_ROOT;
    }
}

FLASHMEM ProjectTab tabForRootNode(ProjectNodeId node) {
    switch (node) {
        case ProjectNodeId::MUSIC_ROOT:
        case ProjectNodeId::MUSIC_SCALE:
            return ProjectTab::MUSIC;
        case ProjectNodeId::TRANSPORT_ROOT:
            return ProjectTab::TRANSPORT;
        case ProjectNodeId::STORAGE_ROOT:
            return ProjectTab::STORAGE;
        case ProjectNodeId::ROUTING_ROOT:
            return ProjectTab::ROUTING;
        case ProjectNodeId::NEW_PROJECT_CONFIRM:
        case ProjectNodeId::OVERVIEW_ROOT:
        default:
            return ProjectTab::OVERVIEW;
    }
}

FLASHMEM const char* projectTabLabel(ProjectTab tab) {
    switch (tab) {
        case ProjectTab::MUSIC:
            return "Music";
        case ProjectTab::TRANSPORT:
            return "Transport";
        case ProjectTab::STORAGE:
            return "Storage";
        case ProjectTab::ROUTING:
            return "Routing";
        case ProjectTab::OVERVIEW:
        default:
            return "Overview";
    }
}

}  // namespace core::state::project
