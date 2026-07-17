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
    telemetryRevision.set(0);
    lifecycleFeedback.set("");
    selectedModulator = {};
    selectedModulationBinding = {};
    modulatorReturn = {};
    guardedModulator = {};
    guardedModulationBinding = {};
    guardedClipboardModulator = {};
    modulatorClipboardGuard.set({});
    modulatorClipboardPasteAvailable = false;
    creatingModulatorSource = false;
    destinationPickerTrack = 0;
    destinationPickerPage = 0;
    modulatorGuard.set({});
    autosaveEnabled = true;
    scaleConstrainEnabled = true;
    patternsInheritScale = true;
    clipsInheritScale = true;
    stepPasteMode = PROJECT_STEP_PASTE_MODE_DEFAULT;
    transportSwingPercent = 0;
    transportRunMode = 0;
    pendingLoadProjectId = {};
    editingProjectSlug = {};
    projectNameKeyIndex = PROJECT_NAME_KEYBOARD_DEFAULT_INDEX;
    projectNameOptRawPosition = 0.0f;
    projectNameOptRowAccumulator = 0.0f;
    projectNameShiftActive = false;
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

FLASHMEM void ProjectNavigationState::notifyTelemetryChanged() {
    telemetryRevision.set(static_cast<uint8_t>(telemetryRevision.get() + 1));
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
        case ProjectTab::MODULATORS:
            return ProjectNodeId::MODULATORS_ROOT;
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
        case ProjectNodeId::SAVE_AS_PROJECT_NAME:
        case ProjectNodeId::RENAME_PROJECT_NAME:
            return ProjectTab::STORAGE;
        case ProjectNodeId::ROUTING_ROOT:
            return ProjectTab::ROUTING;
        case ProjectNodeId::MODULATOR_SOURCE_DETAIL:
        case ProjectNodeId::MODULATOR_REACH:
        case ProjectNodeId::MODULATOR_DESTINATIONS:
        case ProjectNodeId::MODULATOR_DESTINATION_PICKER:
        case ProjectNodeId::MODULATORS_ROOT:
            return ProjectTab::MODULATORS;
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
        case ProjectTab::MODULATORS:
            return "Modulators";
        case ProjectTab::OVERVIEW:
        default:
            return "Overview";
    }
}

}  // namespace core::state::project
