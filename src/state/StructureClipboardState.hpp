#pragma once

#include <array>
#include <cstdint>

#include <oc/state/Signal.hpp>

#include "app/ExtmemAllocator.hpp"
#include "state/macro/MacroPagesState.hpp"
#include "state/sequencer/SequencerGraphOps.hpp"
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
};

enum class SequencerStepContentClipboardKind : uint8_t {
    NONE = 0,
    ALL = 1,
    MICRO_SEQUENCE = 2,
    CYCLE_STATES = 3,
};

bool cloneSequencerGraph(
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
    uint8_t offset = 0;
    core::state::sequencer::SequencerPatternSnapshot snapshot{};
    core::app::ExtmemUniquePtr<oc::note::sequencer::StepSequencerGraph> graph;
};

struct SequencerTrackSelectionClipboard {
    static constexpr uint8_t MAX_ENTRIES = core::state::sequencer::SequencerTrackBankState::TRACK_COUNT;

    bool valid = false;
    uint8_t count = 0;
    std::array<SequencerTrackSelectionClipboardEntry, MAX_ENTRIES> tracks{};

    void reset();
};

struct StructureClipboardState {
    oc::state::Signal<StructureClipboardKind, 4> kind{StructureClipboardKind::NONE};
    oc::state::Signal<uint32_t, 8> revision{0};

    core::state::macro::MacroPageData macroPage{};
    core::state::macro::MacroTrackData macroTrack{};
    core::state::SequencerPageClipboard sequencerPage{};
    core::state::SequencerStepsClipboard sequencerSteps{};
    core::state::SequencerPageSelectionClipboard sequencerPageSelection{};
    core::state::sequencer::SequencerPatternSnapshot sequencerTrack{};
    core::app::ExtmemUniquePtr<core::state::SequencerTrackSelectionClipboard> sequencerTrackSelection;
    core::app::ExtmemUniquePtr<oc::note::sequencer::StepSequencerGraph> sequencerGraph;
    core::state::sequencer::SequencerGraphNodeId sequencerStepContentNodeId =
        oc::note::sequencer::StepSequencerGraphLimits::INVALID_ID;
    SequencerStepContentClipboardKind sequencerStepContentKind =
        SequencerStepContentClipboardKind::NONE;

    void clear();

    void storeMacroPage(const core::state::macro::MacroPageData& page);

    void storeMacroTrack(const core::state::macro::MacroTrackData& track);

    bool storeSequencerPage(
        const core::state::SequencerPageClipboard& page,
        const oc::note::sequencer::StepSequencerGraph* graph
    );

    bool storeSequencerTrack(
        const core::state::sequencer::SequencerPatternSnapshot& track,
        const oc::note::sequencer::StepSequencerGraph* graph
    );

    bool storeSequencerStepContent(
        const oc::note::sequencer::StepSequencerGraph& graph,
        core::state::sequencer::SequencerGraphNodeId nodeId,
        SequencerStepContentClipboardKind contentKind = SequencerStepContentClipboardKind::ALL
    );

    bool storeSequencerSteps(
        const core::state::SequencerStepsClipboard& steps,
        const oc::note::sequencer::StepSequencerGraph* graph
    );

    bool storeSequencerPageSelection(
        const core::state::SequencerPageSelectionClipboard& pages,
        const oc::note::sequencer::StepSequencerGraph* graph
    );

    bool storeSequencerTrackSelection(
        core::app::ExtmemUniquePtr<core::state::SequencerTrackSelectionClipboard> tracks
    );

    bool hasMacroPage() const { return kind.get() == StructureClipboardKind::MACRO_PAGE; }
    bool hasMacroTrack() const { return kind.get() == StructureClipboardKind::MACRO_TRACK; }
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
