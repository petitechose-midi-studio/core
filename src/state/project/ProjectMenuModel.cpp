#include "state/project/ProjectMenuModel.hpp"

#include <cstddef>
#include <cstring>

#include <config/PlatformCompat.hpp>
#include <oc/type/TextFormat.hpp>

#include "state/sequencer/SequencerScaleCatalog.hpp"

namespace core::state::project {

namespace {

namespace scale_catalog = core::state::sequencer::scale_catalog;

constexpr ProjectMenuRow row(const char* label,
                             const char* value,
                             ProjectMenuRowKind kind,
                             ProjectNodeId target,
                             bool hasTarget = false,
                             bool enabled = true) {
    return ProjectMenuRow{
        .label = label,
        .value = value,
        .kind = kind,
        .enabled = enabled,
        .target = target,
        .hasTarget = hasTarget,
    };
}

constexpr uint8_t TRANSPORT_SWING_MAX = 75;
constexpr uint8_t TRANSPORT_RUN_MODE_COUNT = 3;
constexpr uint8_t MIDI_CHANNEL_COUNT = 16;

constexpr const char* const ROUTING_TRACK_LABELS[] = {
    "Track 1",
    "Track 2",
    "Track 3",
    "Track 4",
    "Track 5",
    "Track 6",
    "Track 7",
    "Track 8",
    "Track 9",
    "Track 10",
    "Track 11",
    "Track 12",
    "Track 13",
    "Track 14",
    "Track 15",
    "Track 16",
};

FLASHMEM int signedStepCount(float delta) {
    if (delta == 0.0f) return 0;
    const float absolute = delta > 0.0f ? delta : -delta;
    int magnitude = static_cast<int>(absolute);
    if (magnitude < 1) magnitude = 1;
    return delta > 0.0f ? magnitude : -magnitude;
}

FLASHMEM int wrapIndex(int value, int count) {
    if (count <= 0) return 0;
    int wrapped = value % count;
    if (wrapped < 0) wrapped += count;
    return wrapped;
}

FLASHMEM void setRowValue(ProjectMenuRow& target, const char* value) {
    target.value = value ? value : "";
    target.valueText[0] = '\0';
}

FLASHMEM void copyRowValue(ProjectMenuRow& target, const char* value) {
    auto* buffer = target.valueText.data();
    const auto size = target.valueText.size();
    size_t pos = oc::type::text::appendString(buffer, size, 0, value ? value : "");
    oc::type::text::terminate(buffer, size, pos);
    target.value = buffer;
}

FLASHMEM void setRowValue(ProjectMenuRow& target, unsigned value, const char* suffix) {
    auto* buffer = target.valueText.data();
    const auto size = target.valueText.size();
    size_t pos = oc::type::text::appendUnsigned(buffer, size, 0, value);
    if (suffix) {
        pos = oc::type::text::appendString(buffer, size, pos, suffix);
    }
    oc::type::text::terminate(buffer, size, pos);
    target.value = buffer;
}

FLASHMEM void setScaleSummary(ProjectMenuRow& target,
                              oc::note::sequencer::StepSequencerScaleSettings settings) {
    settings.clamp();

    auto* buffer = target.valueText.data();
    const auto size = target.valueText.size();
    size_t pos = oc::type::text::appendString(
        buffer,
        size,
        0,
        scale_catalog::rootLabel(settings.root)
    );
    pos = oc::type::text::appendString(buffer, size, pos, " ");
    pos = oc::type::text::appendString(
        buffer,
        size,
        pos,
        scale_catalog::scaleTypeLabel(settings.type)
    );
    pos = oc::type::text::appendString(buffer, size, pos, " >");
    oc::type::text::terminate(buffer, size, pos);
    target.value = buffer;
}

FLASHMEM void setMidiChannelValue(ProjectMenuRow& target, uint8_t channel0Based) {
    auto* buffer = target.valueText.data();
    const auto size = target.valueText.size();
    size_t pos = oc::type::text::appendString(buffer, size, 0, "MIDI Ch ");
    pos = oc::type::text::appendUnsigned(
        buffer,
        size,
        pos,
        static_cast<unsigned>((channel0Based % MIDI_CHANNEL_COUNT) + 1U)
    );
    oc::type::text::terminate(buffer, size, pos);
    target.value = buffer;
}

FLASHMEM const char* projectIdentityLabel(const ProjectMenuContext& context) {
    if (context.projectHasSavedIdentity && context.projectId[0] != '\0') {
        return context.projectId.data();
    }
    if (context.projectName[0] != '\0') {
        return context.projectName.data();
    }
    return "Untitled";
}

FLASHMEM void setPageMeta(ProjectMenuPage& page,
                          const char* section,
                          const ProjectMenuContext& context) {
    auto* buffer = page.metaText.data();
    const auto size = page.metaText.size();
    size_t pos = oc::type::text::appendString(buffer, size, 0, section ? section : "");
    pos = oc::type::text::appendString(buffer, size, pos, "  ");
    pos = oc::type::text::appendString(buffer, size, pos, projectIdentityLabel(context));
    if (context.projectDirty) {
        pos = oc::type::text::appendString(buffer, size, pos, "*");
    }
    oc::type::text::terminate(buffer, size, pos);
    page.meta = buffer;
}

FLASHMEM void setTransitionValue(ProjectMenuRow& target,
                                 const char* source,
                                 const char* destination) {
    auto* buffer = target.valueText.data();
    const auto size = target.valueText.size();
    size_t pos = oc::type::text::appendString(buffer, size, 0, source ? source : "");
    pos = oc::type::text::appendString(buffer, size, pos, " > ");
    pos = oc::type::text::appendString(buffer, size, pos, destination ? destination : "");
    oc::type::text::terminate(buffer, size, pos);
    target.value = buffer;
}

FLASHMEM const char* clockModeValue(core::state::MidiSyncMode mode) {
    switch (mode) {
        case core::state::MidiSyncMode::MASTER:
            return "Master";
        case core::state::MidiSyncMode::SLAVE:
            return "Slave";
        case core::state::MidiSyncMode::AUTO:
        default:
            return "Auto";
    }
}

FLASHMEM const char* runModeValue(uint8_t index) {
    switch (index % TRANSPORT_RUN_MODE_COUNT) {
        case 1:
            return "Restart";
        case 2:
            return "Stop";
        case 0:
        default:
            return "Continue";
    }
}

FLASHMEM unsigned roundedTempo(float tempoBpm) {
    if (tempoBpm < 0.0f) return 0;
    return static_cast<unsigned>(tempoBpm + 0.5f);
}

FLASHMEM void addRow(ProjectMenuPage& page, ProjectMenuRow next) {
    if (page.rowCount >= page.rows.size()) return;
    page.rows[page.rowCount++] = next;
}

FLASHMEM void buildOverviewRows(ProjectMenuPage& page) {
    addRow(page, row("New Project", "Reset", ProjectMenuRowKind::Action, ProjectNodeId::OVERVIEW_ROOT));
    addRow(page, row("Load Project", "Browse", ProjectMenuRowKind::Action, ProjectNodeId::OVERVIEW_ROOT));
    addRow(page, row("Save", "Current", ProjectMenuRowKind::Action, ProjectNodeId::OVERVIEW_ROOT));
    addRow(page, row("Save As", "P002", ProjectMenuRowKind::Action, ProjectNodeId::OVERVIEW_ROOT, false, false));
}

FLASHMEM void buildNewProjectConfirmRows(ProjectMenuPage& page,
                                         ProjectMenuContext context) {
    if (context.projectHasSavedIdentity) {
        auto saveAndReset = row(
            "Save & Reset",
            "",
            ProjectMenuRowKind::Action,
            ProjectNodeId::NEW_PROJECT_CONFIRM
        );
        copyRowValue(saveAndReset, projectIdentityLabel(context));
        addRow(page, saveAndReset);
    } else {
        addRow(page, row(
            "Save As New",
            "Next",
            ProjectMenuRowKind::Action,
            ProjectNodeId::NEW_PROJECT_CONFIRM
        ));
    }
    addRow(page, row("Don't Save", "Reset", ProjectMenuRowKind::Action, ProjectNodeId::NEW_PROJECT_CONFIRM));
    addRow(page, row("Cancel", "Back", ProjectMenuRowKind::Action, ProjectNodeId::NEW_PROJECT_CONFIRM));
}

FLASHMEM void buildLoadProjectConfirmRows(ProjectMenuPage& page,
                                          const ProjectNavigationState& navigation,
                                          ProjectMenuContext context) {
    const char* projectId = navigation.pendingLoadProjectId.data();
    if (navigation.pendingLoadCanSaveCurrent) {
        auto saveAndLoad = row(
            "Save & Load",
            "",
            ProjectMenuRowKind::Action,
            ProjectNodeId::LOAD_PROJECT_CONFIRM
        );
        setTransitionValue(saveAndLoad, projectIdentityLabel(context), projectId);
        addRow(page, saveAndLoad);
    }
    auto saveAsAndLoad = row(
        "Save As & Load",
        "",
        ProjectMenuRowKind::Action,
        ProjectNodeId::LOAD_PROJECT_CONFIRM
    );
    setTransitionValue(
        saveAsAndLoad,
        navigation.pendingLoadCanSaveCurrent ? "New" : projectIdentityLabel(context),
        projectId
    );
    addRow(page, saveAsAndLoad);

    auto dontSave = row(
        "Don't Save",
        "",
        ProjectMenuRowKind::Action,
        ProjectNodeId::LOAD_PROJECT_CONFIRM
    );
    {
        auto* buffer = dontSave.valueText.data();
        const auto size = dontSave.valueText.size();
        size_t pos = oc::type::text::appendString(buffer, size, 0, "Load ");
        pos = oc::type::text::appendString(buffer, size, pos, projectId);
        oc::type::text::terminate(buffer, size, pos);
        dontSave.value = buffer;
    }
    addRow(page, dontSave);
    addRow(page, row(
        "Cancel",
        "Back",
        ProjectMenuRowKind::Action,
        ProjectNodeId::LOAD_PROJECT_CONFIRM
    ));
}

FLASHMEM void buildMusicRootRows(ProjectMenuPage& page, ProjectMenuContext context) {
    context.projectScale.clamp();
    auto scaleRow = row("Scale", "", ProjectMenuRowKind::Folder, ProjectNodeId::MUSIC_SCALE, true);
    setScaleSummary(scaleRow, context.projectScale);
    addRow(page, scaleRow);
    addRow(page, row("Pattern Default", "Inherit", ProjectMenuRowKind::Value, ProjectNodeId::MUSIC_ROOT));
    addRow(page, row("Clip Default", "Inherit", ProjectMenuRowKind::Value, ProjectNodeId::MUSIC_ROOT));
}

FLASHMEM void buildMusicScaleRows(ProjectMenuPage& page, ProjectMenuContext context) {
    context.projectScale.clamp();
    addRow(page, row("Root", scale_catalog::rootLabel(context.projectScale.root), ProjectMenuRowKind::Value, ProjectNodeId::MUSIC_SCALE));
    addRow(page, row("Scale", scale_catalog::scaleTypeLabel(context.projectScale.type), ProjectMenuRowKind::Value, ProjectNodeId::MUSIC_SCALE));
    addRow(page, row("Constraint", scale_catalog::constraintModeLabel(context.projectScale.mode), ProjectMenuRowKind::Value, ProjectNodeId::MUSIC_SCALE));
    addRow(page, row("Patterns", "Inherit", ProjectMenuRowKind::Toggle, ProjectNodeId::MUSIC_SCALE));
    addRow(page, row("Clips", "Inherit", ProjectMenuRowKind::Toggle, ProjectNodeId::MUSIC_SCALE));
}

FLASHMEM void buildTransportRows(ProjectMenuPage& page, ProjectMenuContext context) {
    addRow(page, row("Tempo", "120 BPM", ProjectMenuRowKind::Value, ProjectNodeId::TRANSPORT_ROOT));
    addRow(page, row("Swing", "0%", ProjectMenuRowKind::Value, ProjectNodeId::TRANSPORT_ROOT));
    addRow(page, row("Clock", clockModeValue(context.clockMode), ProjectMenuRowKind::Value, ProjectNodeId::TRANSPORT_ROOT));
    addRow(page, row("Run Mode", "Continue", ProjectMenuRowKind::Value, ProjectNodeId::TRANSPORT_ROOT));
    addRow(page, row("Sync Settings", "System", ProjectMenuRowKind::Disabled, ProjectNodeId::TRANSPORT_ROOT, false, false));

    if (page.rowCount > 0) {
        setRowValue(page.rows[0], roundedTempo(context.tempoBpm), " BPM");
    }
}

FLASHMEM void buildStorageRows(ProjectMenuPage& page, ProjectMenuContext context) {
    addRow(page, row("Save Project", "Current", ProjectMenuRowKind::Action, ProjectNodeId::STORAGE_ROOT));
    addRow(page, row("New Project", "Reset", ProjectMenuRowKind::Action, ProjectNodeId::STORAGE_ROOT));
    addRow(page, row("Load Project", "Browse", ProjectMenuRowKind::Action, ProjectNodeId::STORAGE_ROOT));
    auto projectRow = row("Project", "", ProjectMenuRowKind::Disabled, ProjectNodeId::STORAGE_ROOT, false, false);
    copyRowValue(projectRow, projectIdentityLabel(context));
    addRow(page, projectRow);
    addRow(page, row("Autosave", "On", ProjectMenuRowKind::Toggle, ProjectNodeId::STORAGE_ROOT));
}

FLASHMEM void buildLoadProjectRows(ProjectMenuPage& page,
                                   const ProjectNavigationState& navigation) {
    if (!navigation.loadProjects.scanned || navigation.loadProjects.count == 0) {
        addRow(page, row(
            "No projects",
            "Save first",
            ProjectMenuRowKind::Disabled,
            ProjectNodeId::LOAD_PROJECT,
            false,
            false
        ));
        return;
    }

    for (uint8_t i = 0; i < navigation.loadProjects.count; ++i) {
        addRow(page, row(
            navigation.loadProjects.entries[i].id.data(),
            "Load",
            ProjectMenuRowKind::Action,
            ProjectNodeId::LOAD_PROJECT
        ));
    }
}

FLASHMEM void buildRoutingRows(ProjectMenuPage& page, ProjectMenuContext context) {
    for (uint8_t i = 0; i < MIDI_CHANNEL_COUNT; ++i) {
        auto routingRow = row(
            ROUTING_TRACK_LABELS[i],
            "",
            ProjectMenuRowKind::Value,
            ProjectNodeId::ROUTING_ROOT
        );
        setMidiChannelValue(routingRow, context.outputMidiChannels[i]);
        addRow(page, routingRow);
    }
}

FLASHMEM void applyPageMeta(ProjectMenuPage& page,
                            ProjectNodeId node,
                            const ProjectMenuContext& context) {
    switch (node) {
        case ProjectNodeId::MUSIC_ROOT:
            page.meta = "MUSIC";
            return;
        case ProjectNodeId::MUSIC_SCALE:
            page.meta = "MUSIC > SCALE";
            return;
        case ProjectNodeId::TRANSPORT_ROOT:
            page.meta = "TRANSPORT";
            return;
        case ProjectNodeId::STORAGE_ROOT:
            setPageMeta(page, "STORAGE", context);
            return;
        case ProjectNodeId::LOAD_PROJECT:
            page.meta = "LOAD PROJECT";
            return;
        case ProjectNodeId::LOAD_PROJECT_CONFIRM:
            page.meta = "LOAD DIRTY?";
            return;
        case ProjectNodeId::ROUTING_ROOT:
            page.meta = "ROUTING";
            return;
        case ProjectNodeId::NEW_PROJECT_CONFIRM:
            page.meta = "NEW PROJECT?";
            return;
        case ProjectNodeId::OVERVIEW_ROOT:
        default:
            setPageMeta(page, "OVERVIEW", context);
            return;
    }
}

FLASHMEM void setNodeRoot(ProjectNavigationState& navigation, ProjectTab tab) {
    const ProjectNodeId root = rootNodeForTab(tab);
    navigation.pathStack[0] = root;
    navigation.focusedRowByDepth = {};
    navigation.activeTab.set(tab);
    navigation.depth.set(0);
    navigation.currentNode.set(root);
    navigation.focusedRow.set(0);
}

constexpr bool isRootNode(ProjectNodeId node) {
    switch (node) {
        case ProjectNodeId::OVERVIEW_ROOT:
        case ProjectNodeId::MUSIC_ROOT:
        case ProjectNodeId::TRANSPORT_ROOT:
        case ProjectNodeId::STORAGE_ROOT:
        case ProjectNodeId::ROUTING_ROOT:
            return true;
        case ProjectNodeId::MUSIC_SCALE:
        default:
            return false;
    }
}

FLASHMEM void pushNode(ProjectNavigationState& navigation, ProjectNodeId target) {
    const uint8_t currentDepth = navigation.depth.get();
    if (currentDepth >= ProjectNavigationState::MAX_DEPTH - 1) return;

    navigation.focusedRowByDepth[currentDepth] = navigation.focusedRow.get();
    const uint8_t nextDepth = static_cast<uint8_t>(currentDepth + 1);
    navigation.pathStack[nextDepth] = target;
    navigation.focusedRowByDepth[nextDepth] = 0;
    navigation.depth.set(nextDepth);
    navigation.currentNode.set(target);
    navigation.activeTab.set(tabForRootNode(target));
    navigation.focusedRow.set(0);
}

FLASHMEM uint32_t revisionFor(const ProjectNavigationState& navigation,
                              ProjectMenuContext context) {
    context.projectScale.clamp();
    const uint32_t flags =
        (navigation.autosaveEnabled ? 0x01u : 0u) |
        (navigation.scaleConstrainEnabled ? 0x02u : 0u) |
        (navigation.patternsInheritScale ? 0x04u : 0u) |
        (navigation.clipsInheritScale ? 0x08u : 0u) |
        (context.projectDirty ? 0x10u : 0u) |
        (context.projectHasSavedIdentity ? 0x20u : 0u);
    const uint32_t scaleBits =
        (static_cast<uint32_t>(context.projectScale.root & 0x0FU) << 8) |
        ((static_cast<uint32_t>(context.projectScale.type) & 0x0FU) << 12) |
        ((static_cast<uint32_t>(context.projectScale.mode) & 0x03U) << 6);

    uint32_t revision =
        (static_cast<uint32_t>(navigation.activeTab.get()) << 28) |
        (static_cast<uint32_t>(navigation.currentNode.get()) << 24) |
        (static_cast<uint32_t>(navigation.depth.get()) << 20) |
        scaleBits |
        flags;
    revision ^= (roundedTempo(context.tempoBpm) & 0x03FFu) * 2654435761u;
    revision ^= static_cast<uint32_t>(navigation.contentRevision.get()) * 2246822519u;
    revision ^= (static_cast<uint32_t>(context.clockMode) & 0x03u) << 14;
    revision ^= static_cast<uint32_t>(navigation.transportSwingPercent & 0x7Fu) << 2;
    revision ^= static_cast<uint32_t>(navigation.transportRunMode & 0x03u) << 10;
    revision ^= static_cast<uint32_t>(navigation.loadProjects.count) << 16;
    revision ^= navigation.loadProjects.truncated ? 0x40000000u : 0u;
    for (uint8_t i = 0; i < context.projectId.size() && context.projectId[i] != '\0'; ++i) {
        revision = (revision * 16777619u) ^ static_cast<uint8_t>(context.projectId[i]);
    }
    for (uint8_t i = 0; i < context.projectName.size() && context.projectName[i] != '\0'; ++i) {
        revision = (revision * 16777619u) ^ static_cast<uint8_t>(context.projectName[i]);
    }
    for (uint8_t i = 0; i < navigation.loadProjects.count; ++i) {
        const char* id = navigation.loadProjects.entries[i].id.data();
        for (uint8_t c = 0; id[c] != '\0'; ++c) {
            revision = (revision * 16777619u) ^ static_cast<uint8_t>(id[c]);
        }
    }
    for (uint8_t i = 0; i < context.outputMidiChannels.size(); ++i) {
        revision = (revision * 16777619u) ^
                   static_cast<uint32_t>((context.outputMidiChannels[i] & 0x0FU) + i + 1U);
    }
    return revision;
}

FLASHMEM const char* boolValue(bool enabled) {
    return enabled ? "On" : "Off";
}

FLASHMEM const char* inheritValue(bool inherit) {
    return inherit ? "Inherit" : "Override";
}

FLASHMEM void applyDynamicValues(ProjectMenuPage& page,
                                 const ProjectNavigationState& navigation) {
    switch (navigation.currentNode.get()) {
        case ProjectNodeId::MUSIC_SCALE:
            if (page.rowCount > 3) page.rows[3].value = inheritValue(navigation.patternsInheritScale);
            if (page.rowCount > 4) page.rows[4].value = inheritValue(navigation.clipsInheritScale);
            break;
        case ProjectNodeId::STORAGE_ROOT:
            if (page.rowCount > 4) page.rows[4].value = boolValue(navigation.autosaveEnabled);
            break;
        case ProjectNodeId::TRANSPORT_ROOT:
            if (page.rowCount > 1) setRowValue(page.rows[1], navigation.transportSwingPercent, "%");
            if (page.rowCount > 3) setRowValue(page.rows[3], runModeValue(navigation.transportRunMode));
            break;
        default:
            break;
    }
}

FLASHMEM bool activateValueRow(ProjectNavigationState& navigation,
                               ProjectNodeId node,
                               uint8_t rowIndex) {
    switch (node) {
        case ProjectNodeId::MUSIC_SCALE:
            if (rowIndex == 3) {
                navigation.patternsInheritScale = !navigation.patternsInheritScale;
                navigation.notifyContentChanged();
                return true;
            }
            if (rowIndex == 4) {
                navigation.clipsInheritScale = !navigation.clipsInheritScale;
                navigation.notifyContentChanged();
                return true;
            }
            return false;
        case ProjectNodeId::STORAGE_ROOT:
            if (rowIndex == 1) {
                return true;
            }
            if (rowIndex == 4) {
                navigation.autosaveEnabled = !navigation.autosaveEnabled;
                navigation.notifyContentChanged();
                return true;
            }
            return false;
        case ProjectNodeId::TRANSPORT_ROOT:
            if (rowIndex == 1) {
                navigation.transportSwingPercent = static_cast<uint8_t>(
                    wrapIndex(navigation.transportSwingPercent + 1, TRANSPORT_SWING_MAX + 1)
                );
                navigation.notifyContentChanged();
                return true;
            }
            if (rowIndex == 3) {
                navigation.transportRunMode = static_cast<uint8_t>(
                    wrapIndex(navigation.transportRunMode + 1, TRANSPORT_RUN_MODE_COUNT)
                );
                navigation.notifyContentChanged();
                return true;
            }
            return false;
        default:
            return false;
    }
}

}  // namespace

FLASHMEM ProjectMenuPage buildProjectMenuPage(const ProjectNavigationState& navigation) {
    return buildProjectMenuPage(
        navigation,
        ProjectMenuContext{core::state::sequencer::defaultProjectScaleSettings()}
    );
}

FLASHMEM ProjectMenuPage buildProjectMenuPage(const ProjectNavigationState& navigation,
                                              ProjectMenuContext context) {
    ProjectMenuPage page{};
    page.title = "PROJECT";
    applyPageMeta(page, navigation.currentNode.get(), context);
    page.selectedIndex = navigation.focusedRow.get();
    page.dataRevision = revisionFor(navigation, context);

    switch (navigation.currentNode.get()) {
        case ProjectNodeId::MUSIC_ROOT:
            buildMusicRootRows(page, context);
            break;
        case ProjectNodeId::MUSIC_SCALE:
            buildMusicScaleRows(page, context);
            break;
        case ProjectNodeId::TRANSPORT_ROOT:
            buildTransportRows(page, context);
            break;
        case ProjectNodeId::STORAGE_ROOT:
            buildStorageRows(page, context);
            break;
        case ProjectNodeId::ROUTING_ROOT:
            buildRoutingRows(page, context);
            break;
        case ProjectNodeId::NEW_PROJECT_CONFIRM:
            buildNewProjectConfirmRows(page, context);
            break;
        case ProjectNodeId::LOAD_PROJECT:
            buildLoadProjectRows(page, navigation);
            break;
        case ProjectNodeId::LOAD_PROJECT_CONFIRM:
            buildLoadProjectConfirmRows(page, navigation, context);
            break;
        case ProjectNodeId::OVERVIEW_ROOT:
        default:
            buildOverviewRows(page);
            break;
    }
    applyDynamicValues(page, navigation);

    if (page.rowCount == 0) {
        page.selectedIndex = 0;
        return page;
    }

    if (page.selectedIndex >= page.rowCount) {
        page.selectedIndex = static_cast<uint8_t>(page.rowCount - 1);
    }
    return page;
}

FLASHMEM uint8_t projectCurrentRowCount(const ProjectNavigationState& navigation) {
    return buildProjectMenuPage(navigation).rowCount;
}

FLASHMEM void navigateProjectRows(ProjectNavigationState& navigation, float delta) {
    if (delta == 0.0f) return;

    const uint8_t rowCount = projectCurrentRowCount(navigation);
    if (rowCount == 0) {
        navigation.focusedRow.set(0);
        return;
    }

    const int current = navigation.focusedRow.get();
    const int next = wrapIndex(current + signedStepCount(delta), rowCount);
    navigation.focusedRow.set(static_cast<uint8_t>(next));
}

FLASHMEM bool enterFocusedProjectRow(ProjectNavigationState& navigation) {
    const auto page = buildProjectMenuPage(navigation);
    if (page.rowCount == 0 || page.selectedIndex >= page.rowCount) return false;

    const auto& selected = page.rows[page.selectedIndex];
    if (!selected.enabled) return false;

    if (!selected.hasTarget) {
        return activateValueRow(navigation, navigation.currentNode.get(), page.selectedIndex);
    }

    if (navigation.depth.get() == 0 && selected.target != navigation.currentNode.get() &&
        isRootNode(selected.target)) {
        setNodeRoot(navigation, tabForRootNode(selected.target));
        return true;
    }

    pushNode(navigation, selected.target);
    return true;
}

FLASHMEM bool backProjectNavigation(ProjectNavigationState& navigation) {
    const uint8_t currentDepth = navigation.depth.get();
    if (currentDepth == 0) {
        return false;
    }

    const uint8_t nextDepth = static_cast<uint8_t>(currentDepth - 1);
    const ProjectNodeId nextNode = navigation.pathStack[nextDepth];
    navigation.depth.set(nextDepth);
    navigation.currentNode.set(nextNode);
    navigation.activeTab.set(tabForRootNode(nextNode));
    navigation.focusedRow.set(navigation.focusedRowByDepth[nextDepth]);
    return true;
}

FLASHMEM bool openNewProjectConfirmation(ProjectNavigationState& navigation) {
    const uint8_t currentDepth = navigation.depth.get();
    if (currentDepth >= ProjectNavigationState::MAX_DEPTH - 1) {
        return false;
    }

    navigation.focusedRowByDepth[currentDepth] = navigation.focusedRow.get();
    const uint8_t nextDepth = static_cast<uint8_t>(currentDepth + 1);
    navigation.pathStack[nextDepth] = ProjectNodeId::NEW_PROJECT_CONFIRM;
    navigation.focusedRowByDepth[nextDepth] = 0;
    navigation.depth.set(nextDepth);
    navigation.currentNode.set(ProjectNodeId::NEW_PROJECT_CONFIRM);
    navigation.focusedRow.set(0);
    return true;
}

FLASHMEM bool openProjectLoadPicker(ProjectNavigationState& navigation) {
    const uint8_t currentDepth = navigation.depth.get();
    if (currentDepth >= ProjectNavigationState::MAX_DEPTH - 1) {
        return false;
    }

    navigation.focusedRowByDepth[currentDepth] = navigation.focusedRow.get();
    const uint8_t nextDepth = static_cast<uint8_t>(currentDepth + 1);
    navigation.pathStack[nextDepth] = ProjectNodeId::LOAD_PROJECT;
    navigation.focusedRowByDepth[nextDepth] = 0;
    navigation.depth.set(nextDepth);
    navigation.currentNode.set(ProjectNodeId::LOAD_PROJECT);
    navigation.focusedRow.set(0);
    return true;
}

FLASHMEM bool openProjectLoadConfirmation(ProjectNavigationState& navigation,
                                          const char* projectId,
                                          bool canSaveCurrent) {
    if (projectId == nullptr || projectId[0] == '\0') return false;

    const uint8_t currentDepth = navigation.depth.get();
    if (currentDepth >= ProjectNavigationState::MAX_DEPTH - 1) {
        return false;
    }

    navigation.pendingLoadProjectId = {};
    std::strncpy(
        navigation.pendingLoadProjectId.data(),
        projectId,
        navigation.pendingLoadProjectId.size() - 1U
    );
    navigation.pendingLoadProjectId[navigation.pendingLoadProjectId.size() - 1U] = '\0';
    navigation.pendingLoadCanSaveCurrent = canSaveCurrent;

    navigation.focusedRowByDepth[currentDepth] = navigation.focusedRow.get();
    const uint8_t nextDepth = static_cast<uint8_t>(currentDepth + 1);
    navigation.pathStack[nextDepth] = ProjectNodeId::LOAD_PROJECT_CONFIRM;
    navigation.focusedRowByDepth[nextDepth] = 0;
    navigation.depth.set(nextDepth);
    navigation.currentNode.set(ProjectNodeId::LOAD_PROJECT_CONFIRM);
    navigation.activeTab.set(ProjectTab::STORAGE);
    navigation.focusedRow.set(0);
    return true;
}

FLASHMEM bool projectNavigationInNewProjectConfirmation(const ProjectNavigationState& navigation) {
    return navigation.currentNode.get() == ProjectNodeId::NEW_PROJECT_CONFIRM;
}

FLASHMEM bool projectNavigationInProjectConfirmation(const ProjectNavigationState& navigation) {
    return navigation.currentNode.get() == ProjectNodeId::NEW_PROJECT_CONFIRM ||
           navigation.currentNode.get() == ProjectNodeId::LOAD_PROJECT_CONFIRM;
}

FLASHMEM void switchProjectTab(ProjectNavigationState& navigation, int delta) {
    if (delta == 0) return;

    const int count = static_cast<int>(projectTabCount());
    const int current = static_cast<int>(navigation.activeTab.get());
    const int next = wrapIndex(current + delta, count);
    setNodeRoot(navigation, static_cast<ProjectTab>(next));
}

FLASHMEM bool projectNavigationAtRoot(const ProjectNavigationState& navigation) {
    return navigation.depth.get() == 0;
}

}  // namespace core::state::project
