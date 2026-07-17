#pragma once

#include <array>
#include <cstdint>

#include <oc/state/Signal.hpp>

#include "app/ExtmemAllocator.hpp"
#include "state/macro/MacroAutomationState.hpp"
#include "state/macro/MacroPagesState.hpp"
#include "state/modulation/ProjectControlState.hpp"
#include "state/sequencer/SequencerGraphOps.hpp"
#include "state/sequencer/SequencerCcLanePatternOps.hpp"
#include "state/sequencer/SequencerSnapshots.hpp"

namespace core::state {

/**
 * Cross-domain clipboard for page and track structure operations.
 *
 * The clipboard stores detached value snapshots plus a revision signal so views
 * can react without owning macro or sequencer domain mutation.
 */
enum class StructureClipboardKind : uint8_t {
    NONE = 0,
    MACRO_PAGE = 1,
    MACRO_TRACK = 2,
    SEQUENCER_PAGE = 3,
    SEQUENCER_TRACK = 4,
    SEQUENCER_STEP_CONTENT = 5,
    SEQUENCER_STEPS = 6,
    SEQUENCER_PAGE_SELECTION = 7,
    SEQUENCER_TRACK_SELECTION = 8,
    MACRO_AUTOMATION = 9,
    MACRO_SLOT = 10,
    MACRO_MODULATION = 11,
    MACRO_DESTINATION = 12,
    MACRO_MODULATION_ASSIGNMENT = 13,
    PROJECT_MODULATOR_SOURCE = 14,
};

enum class MacroClipboardPayloadKind : uint8_t {
    AUTOMATION = 0,
    SLOT,
    MODULATION,
    DESTINATION,
};

enum class SequencerStepContentClipboardKind : uint8_t {
    NONE = 0,
    ALL = 1,
    MICRO_SEQUENCE = 2,
    CYCLE_STATES = 3,
};

[[nodiscard]] bool cloneSequencerGraph(
    core::app::ExtmemUniquePtr<oc::note::sequencer::StepSequencerGraph>& target,
    const oc::note::sequencer::StepSequencerGraph* source
);

struct SequencerPageClipboard {
    static constexpr uint8_t STEP_COUNT = core::state::sequencer::SequencerPatternState::STEPS_PER_PAGE;

    bool valid = false;
    uint8_t sourcePage = core::state::sequencer::SequencerPatternState::PAGE_COUNT;
    uint8_t count = 0;
    uint8_t enabledMask = 0;
    std::array<uint8_t, STEP_COUNT> note{};
    std::array<uint8_t, STEP_COUNT> velocity{};
    std::array<uint16_t, STEP_COUNT> gate{};
    std::array<int8_t, STEP_COUNT> nudge{};
    std::array<uint8_t, STEP_COUNT> probability{};

    void reset();

    bool isEnabled(uint8_t index) const {
        if (index >= count) return false;
        return (enabledMask & static_cast<uint8_t>(1U << index)) != 0;
    }
};

struct SequencerStepClipboardEntry {
    bool valid = false;
    uint8_t offset = 0;
    bool enabled = false;
    uint8_t note = core::state::sequencer::SequencerPatternState::DEFAULT_NOTE;
    uint8_t velocity = core::state::sequencer::SequencerPatternState::DEFAULT_VELOCITY;
    uint16_t gate = core::state::sequencer::SequencerPatternState::DEFAULT_GATE_PERCENT;
    int8_t nudge = 0;
    uint8_t probability = core::state::sequencer::SequencerPatternState::DEFAULT_PROBABILITY;
    core::state::sequencer::SequencerGraphNodeId sourceNodeId =
        oc::note::sequencer::StepSequencerGraphLimits::INVALID_ID;
};

struct SequencerStepsClipboard {
    static constexpr uint8_t MAX_ENTRIES =
        core::state::sequencer::SequencerPatternState::MAX_STEPS;

    bool valid = false;
    bool rootContext = true;
    uint8_t count = 0;
    uint8_t span = 0;
    std::array<SequencerStepClipboardEntry, MAX_ENTRIES> entries{};

    void reset();
};

struct SequencerPageSelectionClipboard {
    static constexpr uint8_t MAX_ENTRIES = core::state::sequencer::SequencerPatternState::PAGE_COUNT;

    bool valid = false;
    uint8_t sourceFirstPage = core::state::sequencer::SequencerPatternState::PAGE_COUNT;
    uint8_t count = 0;
    std::array<SequencerPageClipboard, MAX_ENTRIES> pages{};

    void reset();
};

struct SequencerTrackSelectionClipboardEntry {
    bool valid = false;
    uint8_t sourceTrack = core::state::sequencer::SequencerTrackBankState::TRACK_COUNT;
    core::state::sequencer::SequencerPatternSnapshot snapshot{};
    core::app::ExtmemUniquePtr<oc::note::sequencer::StepSequencerGraph> graph;
    core::state::sequencer::SequencerCcLaneBankPtr ccLanes;
};

struct SequencerTrackSelectionClipboard {
    static constexpr uint8_t MAX_ENTRIES = core::state::sequencer::SequencerTrackBankState::TRACK_COUNT;

    bool valid = false;
    uint8_t count = 0;
    std::array<SequencerTrackSelectionClipboardEntry, MAX_ENTRIES> tracks{};

    void reset();
};

struct MacroAutomationClipboardEntry {
    bool valid = false;
    uint8_t sourcePage = 0;
    uint8_t sourceMacro = 0;
    core::state::macro::MacroAutomationSlotState state{};
    uint16_t destinationScaleQ15 =
        core::state::modulation::PROJECT_MODULATION_DESTINATION_SCALE_ONE_Q15;
};

struct MacroAutomationClipboard {
    bool valid = false;
    bool trackScope = false;
    MacroClipboardPayloadKind payloadKind =
        MacroClipboardPayloadKind::AUTOMATION;
    uint8_t sourceTrack = core::state::macro::TRACK_COUNT;
    uint8_t sourcePage = core::state::macro::PAGE_COUNT;
    uint8_t sourceMacro = core::state::macro::MACRO_COUNT;
    bool sourceMacroActive = false;
    bool sourceSlotPresent = false;
    uint8_t sourceCc = 0;
    float sourceStaticValue = 0.0f;
    uint8_t count = 0;
    core::state::macro::MacroAutomationPointPool pointPool{};
    std::array<
        MacroAutomationClipboardEntry,
        core::state::macro::MACRO_AUTOMATION_SLOT_CAPACITY> entries{};

    MacroAutomationClipboard();
    bool append(uint8_t sourcePage,
                uint8_t sourceMacro,
                const core::state::modulation::ProjectControlState& control,
                const core::state::macro::MacroAutomationSlotAddress& address,
                bool includeAutomation = true,
                bool includeModulation = true);
};

/**
 * Detached edge payload. The Project source remains shared and is referenced
 * by stable ID; Paste only creates or updates an assignment on the target.
 */
struct MacroModulationAssignmentClipboard {
    bool valid = false;
    core::state::modulation::ModulatorId sourceId{};
    core::state::modulation::ModulationBindingState binding{};
    std::array<
        char,
        core::state::modulation::PROJECT_MODULATOR_NAME_CAPACITY
    > sourceName{};
};

/** Stable shared-source reference used by Project registry Copy/Paste. */
struct ProjectModulatorSourceClipboard {
    bool valid = false;
    core::state::modulation::ModulatorId sourceId{};
    core::state::modulation::ModulatorKind kind =
        core::state::modulation::ModulatorKind::LFO;
    std::array<
        char,
        core::state::modulation::PROJECT_MODULATOR_NAME_CAPACITY
    > sourceName{};
};

struct StructureClipboardState {
    oc::state::Signal<StructureClipboardKind, 4> kind{StructureClipboardKind::NONE};
    oc::state::Signal<uint32_t, 8> revision{0};

    core::state::macro::MacroPageData macroPage{};
    core::state::macro::MacroTrackData macroTrack{};
    core::app::ExtmemUniquePtr<core::state::MacroAutomationClipboard> macroAutomationSet;
    core::app::ExtmemUniquePtr<
        core::state::MacroModulationAssignmentClipboard
    > macroModulationAssignment;
    ProjectModulatorSourceClipboard projectModulatorSource{};
    core::state::SequencerPageClipboard sequencerPage{};
    core::state::SequencerStepsClipboard sequencerSteps{};
    core::state::SequencerPageSelectionClipboard sequencerPageSelection{};
    core::state::sequencer::SequencerPatternSnapshot sequencerTrack{};
    uint8_t sequencerTrackSource =
        core::state::sequencer::SequencerTrackBankState::TRACK_COUNT;
    core::app::ExtmemUniquePtr<core::state::SequencerTrackSelectionClipboard> sequencerTrackSelection;
    core::app::ExtmemUniquePtr<oc::note::sequencer::StepSequencerGraph> sequencerGraph;
    core::state::sequencer::SequencerCcLaneBankPtr sequencerCcLanes;
    core::state::sequencer::SequencerGraphNodeId sequencerStepContentNodeId =
        oc::note::sequencer::StepSequencerGraphLimits::INVALID_ID;
    SequencerStepContentClipboardKind sequencerStepContentKind =
        SequencerStepContentClipboardKind::NONE;

    void clear();

    [[nodiscard]] bool storeMacroPage(
        const core::state::macro::MacroPageData& page,
        const core::state::modulation::ProjectControlState& control,
        uint8_t sourceTrack,
        uint8_t sourcePage
    );

    [[nodiscard]] bool storeMacroTrack(
        const core::state::macro::MacroTrackData& track,
        const core::state::modulation::ProjectControlState& control,
        uint8_t sourceTrack
    );

    [[nodiscard]] bool storeMacroAutomation(
        const core::state::modulation::ProjectControlState& control,
        const core::state::macro::MacroAutomationSlotAddress& address
    );

    /** Stores only the destination CC. Track/channel ownership stays external. */
    [[nodiscard]] bool storeMacroDestination(
        const core::state::macro::MacroPagesState& pages,
        const core::state::macro::MacroAutomationSlotAddress& address
    );

    /** Stores the complete typed Slot: destination, base and both sources. */
    [[nodiscard]] bool storeMacroSlot(
        const core::state::macro::MacroPagesState& pages,
        const core::state::macro::MacroAutomationSlotAddress& address
    );

    /** Stores only Modulation shape/timing/depth for target-preserving paste. */
    [[nodiscard]] bool storeMacroModulation(
        const core::state::modulation::ProjectControlState& control,
        const core::state::macro::MacroAutomationSlotAddress& address
    );

    /** Stores one focused edge without copying its shared Project source. */
    [[nodiscard]] bool storeMacroModulationAssignment(
        const core::state::modulation::ProjectControlState& control,
        const core::state::macro::MacroAutomationSlotAddress& address,
        core::state::modulation::ModulationBindingId bindingId
    );
    [[nodiscard]] bool storeProjectModulatorSource(
        const core::state::modulation::ProjectControlState& control,
        core::state::modulation::ModulatorId sourceId
    );

    [[nodiscard]] bool storeSequencerPage(
        const core::state::SequencerPageClipboard& page,
        const oc::note::sequencer::StepSequencerGraph* graph
    );

    [[nodiscard]] bool storeSequencerTrack(
        const core::state::sequencer::SequencerPatternSnapshot& track,
        const oc::note::sequencer::StepSequencerGraph* graph,
        uint8_t sourceTrack = core::state::sequencer::SequencerTrackBankState::TRACK_COUNT,
        const core::state::sequencer::SequencerCcLaneBank* ccLanes = nullptr
    );

    [[nodiscard]] bool storeSequencerStepContent(
        const oc::note::sequencer::StepSequencerGraph& graph,
        core::state::sequencer::SequencerGraphNodeId nodeId,
        SequencerStepContentClipboardKind contentKind = SequencerStepContentClipboardKind::ALL
    );

    [[nodiscard]] bool storeSequencerSteps(
        const core::state::SequencerStepsClipboard& steps,
        const oc::note::sequencer::StepSequencerGraph* graph
    );

    [[nodiscard]] bool storeSequencerPageSelection(
        const core::state::SequencerPageSelectionClipboard& pages,
        const oc::note::sequencer::StepSequencerGraph* graph
    );

    [[nodiscard]] bool storeSequencerTrackSelection(
        core::app::ExtmemUniquePtr<core::state::SequencerTrackSelectionClipboard> tracks
    );

    bool hasMacroPage() const { return kind.get() == StructureClipboardKind::MACRO_PAGE; }
    bool hasMacroTrack() const { return kind.get() == StructureClipboardKind::MACRO_TRACK; }
    bool hasMacroAutomation() const {
        return kind.get() == StructureClipboardKind::MACRO_AUTOMATION &&
               macroAutomationSet &&
               macroAutomationSet->valid &&
               macroAutomationSet->count > 0 &&
               macroAutomationSet->payloadKind == MacroClipboardPayloadKind::AUTOMATION &&
               macroAutomationSet->entries[0].state.automation.active;
    }
    bool hasMacroDestination() const {
        return kind.get() == StructureClipboardKind::MACRO_DESTINATION &&
               macroAutomationSet && macroAutomationSet->valid &&
               macroAutomationSet->payloadKind == MacroClipboardPayloadKind::DESTINATION &&
               macroAutomationSet->sourceMacroActive &&
               macroAutomationSet->sourceCc <= 127;
    }
    bool hasMacroSlot() const {
        return kind.get() == StructureClipboardKind::MACRO_SLOT &&
               macroAutomationSet && macroAutomationSet->valid &&
               macroAutomationSet->payloadKind == MacroClipboardPayloadKind::SLOT &&
               macroAutomationSet->count == 1;
    }
    bool hasMacroModulation() const {
        return kind.get() == StructureClipboardKind::MACRO_MODULATION &&
               macroAutomationSet && macroAutomationSet->valid &&
               macroAutomationSet->payloadKind == MacroClipboardPayloadKind::MODULATION &&
               macroAutomationSet->count == 1 &&
               macroAutomationSet->entries[0].valid &&
               macroAutomationSet->entries[0].state.modulation.active;
    }
    bool hasMacroModulationAssignment() const {
        return kind.get() ==
                   StructureClipboardKind::MACRO_MODULATION_ASSIGNMENT &&
               macroModulationAssignment &&
               macroModulationAssignment->valid &&
               core::state::modulation::valid(
                   macroModulationAssignment->sourceId
               ) &&
               core::state::modulation::valid(
                   macroModulationAssignment->binding.id
               );
    }
    bool hasProjectModulatorSource() const {
        return kind.get() == StructureClipboardKind::PROJECT_MODULATOR_SOURCE &&
               projectModulatorSource.valid &&
               core::state::modulation::valid(projectModulatorSource.sourceId);
    }
    bool hasSequencerPage() const {
        return kind.get() == StructureClipboardKind::SEQUENCER_PAGE && sequencerPage.valid;
    }
    bool hasSequencerTrack() const { return kind.get() == StructureClipboardKind::SEQUENCER_TRACK; }
    bool hasSequencerStepContent() const {
        return kind.get() == StructureClipboardKind::SEQUENCER_STEP_CONTENT &&
               sequencerGraph.get() != nullptr &&
               sequencerStepContentNodeId !=
                   oc::note::sequencer::StepSequencerGraphLimits::INVALID_ID;
    }
    bool hasSequencerStepContent(SequencerStepContentClipboardKind requiredKind) const {
        return hasSequencerStepContent() && sequencerStepContentKind == requiredKind;
    }
    bool hasSequencerSteps() const {
        return kind.get() == StructureClipboardKind::SEQUENCER_STEPS &&
               sequencerSteps.valid &&
               sequencerSteps.count > 0;
    }
    bool hasSequencerPageSelection() const {
        return kind.get() == StructureClipboardKind::SEQUENCER_PAGE_SELECTION &&
               sequencerPageSelection.valid &&
               sequencerPageSelection.count > 0;
    }
    bool hasSequencerTrackSelection() const {
        return kind.get() == StructureClipboardKind::SEQUENCER_TRACK_SELECTION &&
               sequencerTrackSelection &&
               sequencerTrackSelection->valid &&
               sequencerTrackSelection->count > 0;
    }
};

}  // namespace core::state
