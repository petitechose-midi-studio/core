#include "state/project/ProjectNavigationState.hpp"

#include <cstring>

#include <config/PlatformCompat.hpp>

namespace core::state::project {

FLASHMEM void ProjectBrowserState::clear() {
    entries = {};
    count = 0;
    scanned = false;
    truncated = false;
}

FLASHMEM bool ProjectBrowserState::add(const char* id, uint32_t sizeBytes) {
    if (!id || id[0] == '\0') return false;
    if (count >= entries.size()) {
        truncated = true;
        scanned = true;
        return false;
    }

    auto& entry = entries[count++];
    entry.id.fill('\0');
    std::strncpy(entry.id.data(), id, entry.id.size() - 1U);
    entry.id[entry.id.size() - 1U] = '\0';
    entry.sizeBytes = sizeBytes;
    scanned = true;
    return true;
}

FLASHMEM void ProjectNavigationState::reset() {
    activeTab.set(ProjectTab::OVERVIEW);
    currentNode.set(ProjectNodeId::OVERVIEW_ROOT);
    depth.set(0);
    focusedRow.set(0);
    physicalHoldActive.set(false);
    contentRevision.set(0);
    lifecycleFeedback.set("");
    autosaveEnabled = true;
    scaleConstrainEnabled = true;
    patternsInheritScale = true;
    clipsInheritScale = true;
    transportSwingPercent = 0;
    transportRunMode = 0;
    pendingLoadProjectId = {};
    pendingLoadCanSaveCurrent = false;
    loadProjects.clear();
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

FLASHMEM void ProjectNavigationState::setLifecycleFeedback(const char* message) {
    lifecycleFeedback.set(message ? message : "");
    notifyContentChanged();
}

FLASHMEM void ProjectNavigationState::clearLifecycleFeedback() {
    if (lifecycleFeedback.empty()) return;
    lifecycleFeedback.set("");
    notifyContentChanged();
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
        case ProjectNodeId::LOAD_PROJECT:
        case ProjectNodeId::LOAD_PROJECT_CONFIRM:
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
