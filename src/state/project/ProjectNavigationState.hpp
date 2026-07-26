#pragma once

#include <array>
#include <cstdint>

#include <oc/state/Signal.hpp>
#include <oc/state/SignalString.hpp>

#include "state/project/ProjectNameKeyboard.hpp"
#include "state/project/ProjectState.hpp"
#include "state/contextual/GuardedActionState.hpp"
#include "state/macro/MacroAutomationAddress.hpp"
#include "state/modulation/ModulationIds.hpp"
#include "state/modulation/ProjectModulationState.hpp"

namespace core::state::project {

enum class ProjectTab : uint8_t {
    OVERVIEW = 0,
    MUSIC,
    TRANSPORT,
    STORAGE,
    ROUTING,
    MODULATORS,
    COUNT,
};

enum class ProjectNodeId : uint8_t {
    OVERVIEW_ROOT = 0,
    MUSIC_ROOT,
    MUSIC_SCALE,
    TRANSPORT_ROOT,
    STORAGE_ROOT,
    ROUTING_ROOT,
    MODULATORS_ROOT,
    MODULATOR_SOURCE_DETAIL,
    MODULATOR_DESTINATIONS,
    MODULATOR_DESTINATION_PICKER,
    NEW_PROJECT_CONFIRM,
    LOAD_PROJECT,
    LOAD_PROJECT_CONFIRM,
    SAVE_AS_PROJECT_NAME,
    RENAME_PROJECT_NAME,
    MODULATOR_SOURCE_OPTIONS,
    MODULATOR_SOURCE_RENAME,
    MODULATOR_SOURCE_KIND_PICKER,
    MODULATOR_TRIGGER,
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

enum class ModulatorNavigationCaller : uint8_t {
    NONE = 0,
    MACRO_ASSIGNMENT,
    MACRO_AUDITION,
};

/** Exact Macro surface restored when a Project Source session exits. */
enum class ModulatorMacroReturnTarget : uint8_t {
    MODULATION_ASSIGNMENT = 0,
    MODULATOR_CREATE,
    MODULATOR_PICKER,
};

enum class ModulatorDestinationPickerLevel : uint8_t {
    TRACK = 0,
    PAGE,
    MACRO,
};

/**
 * Session-only return address for a Macro -> Project Modulator deep-link.
 *
 * Stable graph IDs are retained instead of row ordinals so a source edit can
 * reorder the registry without making Back land on another assignment.
 */
struct ModulatorReturnContext {
    core::state::modulation::ModulatorId sourceId{};
    core::state::modulation::ModulationBindingId bindingId{};
    core::state::macro::MacroAutomationSlotAddress macroAddress{};
    ModulatorNavigationCaller caller = ModulatorNavigationCaller::NONE;
    ModulatorMacroReturnTarget target =
        ModulatorMacroReturnTarget::MODULATION_ASSIGNMENT;
    uint8_t focusedRow = 0;

    [[nodiscard]] constexpr bool active() const {
        return caller != ModulatorNavigationCaller::NONE;
    }
};

static_assert(sizeof(ModulatorReturnContext) == 16U);

struct ProjectNavigationState {
    static constexpr uint8_t MAX_DEPTH = 4;

    oc::state::Signal<ProjectTab, 8> activeTab{ProjectTab::OVERVIEW};
    oc::state::Signal<ProjectNodeId, 8> currentNode{ProjectNodeId::OVERVIEW_ROOT};
    oc::state::Signal<uint8_t, 8> depth{0};
    oc::state::Signal<uint8_t, 8> focusedRow{0};
    oc::state::Signal<bool, 8> physicalHoldActive{false};
    oc::state::Signal<uint8_t, 8> contentRevision{0};
    oc::state::SignalLabel lifecycleFeedback;

    core::state::modulation::ModulatorId selectedModulator{};
    core::state::modulation::ModulationBindingId selectedModulationBinding{};
    ModulatorReturnContext modulatorReturn{};
    core::state::modulation::ModulatorId guardedModulator{};
    oc::state::Signal<core::state::contextual::GuardedActionState, 6>
        modulatorGuard{};
    core::state::modulation::ModulationBindingId guardedModulationBinding{};
    core::state::modulation::ModulatorId guardedClipboardModulator{};
    oc::state::Signal<core::state::contextual::GuardedActionState, 6>
        modulatorClipboardGuard{};
    bool modulatorClipboardPasteAvailable = false;
    bool creatingModulatorSource = false;
    core::state::modulation::ModulatorKind creatingModulatorKind =
        core::state::modulation::ModulatorKind::LFO;
    uint8_t destinationPickerTrack = 0;
    uint8_t destinationPickerPage = 0;
    ModulatorDestinationPickerLevel destinationPickerLevel =
        ModulatorDestinationPickerLevel::TRACK;

    bool autosaveEnabled = true;
    bool scaleConstrainEnabled = true;
    bool patternsInheritScale = true;
    bool clipsInheritScale = true;
    ProjectStepPasteMode stepPasteMode = PROJECT_STEP_PASTE_MODE_DEFAULT;
    std::array<uint8_t, PROJECT_CC_LANE_DEFAULT_COUNT>
        ccLaneDefaultControllers = PROJECT_CC_LANE_DEFAULT_CONTROLLERS;
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

    ~ProjectNavigationState();

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
