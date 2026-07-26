#include "state/project/ProjectMenuModel.hpp"

#include <algorithm>
#include <cstring>

#include <config/PlatformCompat.hpp>

#include "state/macro/MacroConstants.hpp"
#include "state/project/ProjectModulatorMenuModel.hpp"

namespace core::state::project {

namespace {

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

constexpr bool isRootNode(ProjectNodeId node) {
    switch (node) {
        case ProjectNodeId::OVERVIEW_ROOT:
        case ProjectNodeId::MUSIC_ROOT:
        case ProjectNodeId::TRANSPORT_ROOT:
        case ProjectNodeId::STORAGE_ROOT:
        case ProjectNodeId::ROUTING_ROOT:
        case ProjectNodeId::MODULATORS_ROOT:
            return true;
        case ProjectNodeId::MUSIC_SCALE:
        case ProjectNodeId::SAVE_AS_PROJECT_NAME:
        case ProjectNodeId::RENAME_PROJECT_NAME:
        default:
            return false;
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

FLASHMEM bool activateValueRow(ProjectNavigationState& navigation,
                               ProjectNodeId node,
                               uint8_t rowIndex) {
    switch (node) {
        case ProjectNodeId::MUSIC_ROOT:
            if (rowIndex == 3) {
                navigation.stepPasteMode = sanitizeProjectStepPasteMode(
                    static_cast<uint8_t>(navigation.stepPasteMode) + 1U
                );
                navigation.notifyContentChanged();
                return true;
            }
            if (rowIndex >= 4U &&
                rowIndex < 4U + PROJECT_CC_LANE_DEFAULT_COUNT) {
                const uint8_t lane = static_cast<uint8_t>(rowIndex - 4U);
                navigation.ccLaneDefaultControllers[lane] =
                    static_cast<uint8_t>(
                        (navigation.ccLaneDefaultControllers[lane] + 1U) %
                        PROJECT_MIDI_CC_COUNT
                    );
                navigation.notifyContentChanged();
                return true;
            }
            return false;
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
            if (rowIndex == 6) {
                navigation.autosaveEnabled = !navigation.autosaveEnabled;
                navigation.notifyContentChanged();
                return true;
            }
            return false;
        case ProjectNodeId::TRANSPORT_ROOT:
            if (rowIndex == 1) {
                navigation.transportSwingPercent = static_cast<uint8_t>(
                    wrapIndex(navigation.transportSwingPercent + 1, PROJECT_SWING_STEPS)
                );
                navigation.notifyContentChanged();
                return true;
            }
            if (rowIndex == 3) {
                navigation.transportRunMode = static_cast<uint8_t>(
                    wrapIndex(navigation.transportRunMode + 1, PROJECT_RUN_MODE_COUNT)
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

FLASHMEM void navigateProjectRows(ProjectNavigationState& navigation,
                                  float delta,
                                  uint16_t modulatorSourceCount,
                                  uint16_t modulatorDetailRowCount) {
    if (delta == 0.0f) return;

    const uint16_t rowCount = projectCurrentRowCount(
        navigation,
        modulatorSourceCount,
        modulatorDetailRowCount
    );
    if (rowCount == 0) {
        navigation.focusedRow.set(0);
        return;
    }

    const int current = navigation.focusedRow.get();
    const int next = wrapIndex(current + signedStepCount(delta), rowCount);
    navigation.focusedRow.set(static_cast<uint8_t>(next));
}

FLASHMEM bool openProjectModulatorDetail(
    ProjectNavigationState& navigation,
    core::state::modulation::ModulatorId sourceId
) {
    if (!core::state::modulation::valid(sourceId) ||
        navigation.currentNode.get() != ProjectNodeId::MODULATORS_ROOT) {
        return false;
    }
    const uint8_t currentDepth = navigation.depth.get();
    if (currentDepth >= ProjectNavigationState::MAX_DEPTH - 1U) return false;

    navigation.focusedRowByDepth[currentDepth] = navigation.focusedRow.get();
    const uint8_t nextDepth = static_cast<uint8_t>(currentDepth + 1U);
    navigation.pathStack[nextDepth] = ProjectNodeId::MODULATOR_SOURCE_DETAIL;
    navigation.focusedRowByDepth[nextDepth] = 0;
    navigation.selectedModulator = sourceId;
    navigation.selectedModulationBinding = {};
    navigation.depth.set(nextDepth);
    navigation.currentNode.set(ProjectNodeId::MODULATOR_SOURCE_DETAIL);
    navigation.activeTab.set(ProjectTab::MODULATORS);
    navigation.focusedRow.set(0);
    navigation.notifyContentChanged();
    return true;
}

FLASHMEM bool openProjectModulatorKindPicker(
    ProjectNavigationState& navigation
) {
    if (navigation.currentNode.get() != ProjectNodeId::MODULATORS_ROOT) {
        return false;
    }
    const uint8_t depth = navigation.depth.get();
    pushNode(navigation, ProjectNodeId::MODULATOR_SOURCE_KIND_PICKER);
    if (navigation.depth.get() == depth) return false;
    navigation.creatingModulatorKind =
        core::state::modulation::ModulatorKind::LFO;
    navigation.notifyContentChanged();
    return true;
}

FLASHMEM bool openProjectModulatorWorkspace(
    ProjectNavigationState& navigation,
    core::state::modulation::ModulatorId sourceId
) {
    if (!core::state::modulation::valid(sourceId)) return false;
    setNodeRoot(navigation, ProjectTab::MODULATORS);
    navigation.physicalHoldActive.set(false);
    navigation.projectNameShiftActive = false;
    navigation.clearLifecycleFeedback();
    return openProjectModulatorDetail(navigation, sourceId);
}

FLASHMEM bool openProjectModulatorOptions(
    ProjectNavigationState& navigation
) {
    if (!core::state::modulation::valid(navigation.selectedModulator) ||
        navigation.currentNode.get() !=
            ProjectNodeId::MODULATOR_SOURCE_DETAIL) {
        return false;
    }
    const uint8_t depth = navigation.depth.get();
    pushNode(navigation, ProjectNodeId::MODULATOR_SOURCE_OPTIONS);
    if (navigation.depth.get() == depth) return false;
    navigation.notifyContentChanged();
    return true;
}

FLASHMEM bool openProjectModulatorDestinations(
    ProjectNavigationState& navigation
) {
    const auto node = navigation.currentNode.get();
    if (!core::state::modulation::valid(navigation.selectedModulator) ||
        (node != ProjectNodeId::MODULATOR_SOURCE_DETAIL &&
         node != ProjectNodeId::MODULATOR_SOURCE_OPTIONS)) {
        return false;
    }
    const uint8_t currentDepth = navigation.depth.get();
    if (currentDepth >= ProjectNavigationState::MAX_DEPTH - 1U) return false;
    navigation.focusedRowByDepth[currentDepth] = navigation.focusedRow.get();
    const uint8_t nextDepth = static_cast<uint8_t>(currentDepth + 1U);
    navigation.pathStack[nextDepth] = ProjectNodeId::MODULATOR_DESTINATIONS;
    navigation.focusedRowByDepth[nextDepth] = 0;
    navigation.selectedModulationBinding = {};
    navigation.depth.set(nextDepth);
    navigation.currentNode.set(ProjectNodeId::MODULATOR_DESTINATIONS);
    navigation.activeTab.set(ProjectTab::MODULATORS);
    navigation.focusedRow.set(0);
    navigation.notifyContentChanged();
    return true;
}

FLASHMEM bool openProjectModulatorTrigger(
    ProjectNavigationState& navigation
) {
    if (!core::state::modulation::valid(navigation.selectedModulator) ||
        navigation.currentNode.get() !=
            ProjectNodeId::MODULATOR_SOURCE_DETAIL) {
        return false;
    }
    const uint8_t depth = navigation.depth.get();
    pushNode(navigation, ProjectNodeId::MODULATOR_TRIGGER);
    if (navigation.depth.get() == depth) return false;
    navigation.notifyContentChanged();
    return true;
}

FLASHMEM bool openProjectModulatorDestinationPicker(
    ProjectNavigationState& navigation,
    uint8_t track,
    uint8_t page,
    bool creatingSource
) {
    if (track >= core::state::macro::TRACK_COUNT ||
        page >= core::state::macro::PAGE_COUNT) {
        return false;
    }
    const auto node = navigation.currentNode.get();
    if ((creatingSource &&
         node != ProjectNodeId::MODULATOR_SOURCE_KIND_PICKER) ||
        (!creatingSource &&
         (node != ProjectNodeId::MODULATOR_DESTINATIONS ||
          !core::state::modulation::valid(navigation.selectedModulator)))) {
        return false;
    }
    const uint8_t currentDepth = navigation.depth.get();
    if (currentDepth >= ProjectNavigationState::MAX_DEPTH - 1U) return false;
    navigation.focusedRowByDepth[currentDepth] = navigation.focusedRow.get();
    const uint8_t nextDepth = static_cast<uint8_t>(currentDepth + 1U);
    navigation.pathStack[nextDepth] =
        ProjectNodeId::MODULATOR_DESTINATION_PICKER;
    navigation.focusedRowByDepth[nextDepth] = 0;
    navigation.creatingModulatorSource = creatingSource;
    navigation.destinationPickerTrack = track;
    navigation.destinationPickerPage = page;
    navigation.destinationPickerLevel = ModulatorDestinationPickerLevel::TRACK;
    navigation.depth.set(nextDepth);
    navigation.currentNode.set(ProjectNodeId::MODULATOR_DESTINATION_PICKER);
    navigation.activeTab.set(ProjectTab::MODULATORS);
    navigation.focusedRow.set(0);
    navigation.notifyContentChanged();
    return true;
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

    const bool leavingDestinationPicker = navigation.currentNode.get() ==
        ProjectNodeId::MODULATOR_DESTINATION_PICKER;
    const uint8_t nextDepth = static_cast<uint8_t>(currentDepth - 1);
    const ProjectNodeId nextNode = navigation.pathStack[nextDepth];
    navigation.depth.set(nextDepth);
    navigation.currentNode.set(nextNode);
    navigation.activeTab.set(tabForRootNode(nextNode));
    navigation.focusedRow.set(navigation.focusedRowByDepth[nextDepth]);
    navigation.projectNameShiftActive = false;
    if (leavingDestinationPicker) {
        navigation.creatingModulatorSource = false;
    }
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

FLASHMEM bool openProjectNameEditor(ProjectNavigationState& navigation,
                                    ProjectNodeId editorNode,
                                    const char* initialSlug) {
    if (editorNode != ProjectNodeId::SAVE_AS_PROJECT_NAME &&
        editorNode != ProjectNodeId::RENAME_PROJECT_NAME &&
        editorNode != ProjectNodeId::MODULATOR_SOURCE_RENAME) {
        return false;
    }

    const uint8_t currentDepth = navigation.depth.get();
    if (currentDepth >= ProjectNavigationState::MAX_DEPTH - 1) {
        return false;
    }

    navigation.editingProjectSlug = {};
    if (initialSlug != nullptr && initialSlug[0] != '\0') {
        std::strncpy(
            navigation.editingProjectSlug.data(),
            initialSlug,
            navigation.editingProjectSlug.size() - 1U
        );
        navigation.editingProjectSlug[navigation.editingProjectSlug.size() - 1U] = '\0';
    }
    navigation.projectNameKeyIndex = PROJECT_NAME_KEYBOARD_DEFAULT_INDEX;
    navigation.projectNameOptRawPosition = 0.0f;
    navigation.projectNameOptRowAccumulator = 0.0f;
    navigation.projectNameShiftActive = false;

    navigation.focusedRowByDepth[currentDepth] = navigation.focusedRow.get();
    const uint8_t nextDepth = static_cast<uint8_t>(currentDepth + 1);
    navigation.pathStack[nextDepth] = editorNode;
    navigation.focusedRowByDepth[nextDepth] = 1;
    navigation.depth.set(nextDepth);
    navigation.currentNode.set(editorNode);
    navigation.activeTab.set(tabForRootNode(editorNode));
    navigation.focusedRow.set(1);
    navigation.notifyContentChanged();
    return true;
}

FLASHMEM bool projectNavigationInNewProjectConfirmation(const ProjectNavigationState& navigation) {
    return navigation.currentNode.get() == ProjectNodeId::NEW_PROJECT_CONFIRM;
}

FLASHMEM bool projectNavigationInProjectConfirmation(const ProjectNavigationState& navigation) {
    return navigation.currentNode.get() == ProjectNodeId::NEW_PROJECT_CONFIRM ||
           navigation.currentNode.get() == ProjectNodeId::LOAD_PROJECT_CONFIRM ||
           navigation.currentNode.get() == ProjectNodeId::SAVE_AS_PROJECT_NAME ||
           navigation.currentNode.get() == ProjectNodeId::RENAME_PROJECT_NAME;
}

FLASHMEM void switchProjectTab(ProjectNavigationState& navigation, int delta) {
    if (delta == 0) return;

    // Modulators is a first-rank musical destination, not a Settings tab.
    // Its workspace never leaks into the internal Settings carousel.
    if (navigation.activeTab.get() == ProjectTab::MODULATORS) return;

    const int count = static_cast<int>(ProjectTab::MODULATORS);
    const int current = static_cast<int>(navigation.activeTab.get());
    const int next = wrapIndex(current + delta, count);
    setNodeRoot(navigation, static_cast<ProjectTab>(next));
}

FLASHMEM void openProjectRootTab(
    ProjectNavigationState& navigation,
    ProjectTab tab
) {
    setNodeRoot(navigation, tab);
    navigation.physicalHoldActive.set(false);
    navigation.projectNameShiftActive = false;
    navigation.clearLifecycleFeedback();
    navigation.notifyContentChanged();
}

FLASHMEM bool projectNavigationAtRoot(const ProjectNavigationState& navigation) {
    return navigation.depth.get() == 0;
}

FLASHMEM void reconcileProjectModulatorNavigationAfterHistory(
    ProjectNavigationState& navigation,
    const core::state::modulation::ProjectModulationState& graph,
    bool preserveMissingSelection
) {
    const bool ownsModulatorContext =
        navigation.activeTab.get() == ProjectTab::MODULATORS ||
        core::state::modulation::valid(navigation.selectedModulator) ||
        core::state::modulation::valid(navigation.selectedModulationBinding) ||
        navigation.modulatorReturn.active() ||
        navigation.creatingModulatorSource;
    if (!ownsModulatorContext) {
        // A Project load can occur inside Storage navigation. Reconciliation
        // must refresh content without stealing that unrelated caller path.
        navigation.notifyContentChanged();
        return;
    }

    const bool destinations = navigation.currentNode.get() ==
        ProjectNodeId::MODULATOR_DESTINATIONS;
    auto* selected = core::state::modulation::findProjectModulator(
        graph,
        navigation.selectedModulator
    );

    if ((selected == nullptr || destinations) &&
        core::state::modulation::valid(navigation.selectedModulationBinding)) {
        const auto* binding =
            core::state::modulation::findProjectModulationBinding(
                graph,
                navigation.selectedModulationBinding
            );
        selected = binding
            ? core::state::modulation::findProjectModulator(
                  graph,
                  binding->sourceId
              )
            : nullptr;
        if (selected != nullptr) navigation.selectedModulator = selected->id;
    }

    if (selected == nullptr) {
        while (navigation.depth.get() > 0U) {
            (void)backProjectNavigation(navigation);
        }
        navigation.focusedRow.set(static_cast<uint8_t>(graph.sourceCount));
        navigation.selectedModulationBinding = {};
        if (!preserveMissingSelection) {
            navigation.selectedModulator = {};
            navigation.modulatorReturn = {};
            navigation.guardedModulator = {};
            navigation.guardedModulationBinding = {};
            navigation.creatingModulatorSource = false;
        }
        navigation.notifyContentChanged();
        return;
    }

    if (navigation.currentNode.get() == ProjectNodeId::MODULATORS_ROOT) {
        for (uint16_t index = 0U; index < graph.sourceCount; ++index) {
            if (graph.sources[index].id != selected->id) continue;
            navigation.focusedRow.set(static_cast<uint8_t>(index));
            navigation.notifyContentChanged();
            return;
        }
    }

    if (destinations) {
        const uint16_t count = modulators::sourceDestinationCount(
            graph,
            selected->id
        );
        uint16_t row = std::min<uint16_t>(navigation.focusedRow.get(), count);
        if (core::state::modulation::valid(
                navigation.selectedModulationBinding
            )) {
            uint16_t ordinal = 0U;
            for (uint16_t index = 0U; index < graph.outputBindingCount; ++index) {
                const auto& binding = graph.outputBindings[index];
                if (binding.sourceId != selected->id) continue;
                if (binding.id == navigation.selectedModulationBinding) {
                    row = ordinal;
                    break;
                }
                ++ordinal;
            }
        }
        navigation.focusedRow.set(static_cast<uint8_t>(row));
        const auto* binding = row < count
            ? modulators::sourceBindingAtOrdinal(graph, selected->id, row)
            : nullptr;
        navigation.selectedModulationBinding = binding
            ? binding->id
            : core::state::modulation::ModulationBindingId{};
    }

    navigation.notifyContentChanged();
}

}  // namespace core::state::project
