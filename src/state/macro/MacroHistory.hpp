#pragma once

#include <array>
#include <cstdint>

#include "app/ExtmemAllocator.hpp"
#include "state/macro/MacroAutomationState.hpp"
#include "state/macro/MacroPagesState.hpp"
#include "state/modulation/ProjectModulationDomainOps.hpp"

namespace core::state::macro {

/**
 * Bounded, Slot-scoped Macro history.
 *
 * A history entry stores only the addressed Slot and its packed curve points,
 * never the 128 KiB project point pool. The 4096-point admission limit covers
 * the maximum authored Automation plus Modulation pair (2 x 2048 points) and
 * makes memory use deterministic on the controller.
 */
inline constexpr uint16_t MACRO_HISTORY_POINT_CAPACITY =
    static_cast<uint16_t>(MACRO_AUTOMATION_RECORDING_MAX_POINTS * 2U);

enum class MacroHistoryActionKind : uint8_t {
    CONVERT_AUTOMATION = 0,
    PASTE_SLOT,
    PASTE_DESTINATION,
    PASTE_AUTOMATION,
    PASTE_MODULATION,
    CLEAR_AUTOMATION,
    CLEAR_MODULATION,
    REMOVE_SLOT,
    DEPTH_EDIT,
    SOURCE_STATE,
    CREATE_MODULATOR_ASSIGNMENT,
};

struct MacroSlotHistorySnapshot {
    MacroAutomationSlotAddress address{};
    bool macroActive = false;
    uint8_t cc = 0;
    float staticValue = 0.0f;
    bool slotPresent = false;
    MacroAutomationSlotState slot{};
    uint16_t automationPointCount = 0;
    uint16_t modulationPointCount = 0;
    std::array<MacroPackedCurvePoint, MACRO_HISTORY_POINT_CAPACITY> points{};
};

struct MacroSlotHistoryChangePayload {
    MacroSlotHistorySnapshot before{};
    MacroSlotHistorySnapshot after{};
};

/**
 * Small graph delta for destination-first source creation.
 *
 * The before-tail values make Cancel byte-stable even when a dense directory
 * slot was previously used. The committed after objects are sufficient for
 * stable-ID Undo/Redo and avoid retaining the complete Project graph.
 */
struct MacroModulatorCreationHistoryPayload {
    core::state::modulation::ModulatorSourceState beforeSourceTail{};
    core::state::modulation::ModulationBindingState beforeBindingTail{};
    core::state::modulation::ModulatorSourceState source{};
    core::state::modulation::ModulationBindingState binding{};
    uint32_t beforeNextSourceId = 1;
    uint32_t beforeNextBindingId = 1;
    uint32_t afterNextSourceId = 1;
    uint32_t afterNextBindingId = 1;
    uint32_t beforeAuthoredRevision = 1;
    uint32_t generation = 0;
    uint16_t beforeSourceCount = 0;
    uint16_t beforeBindingCount = 0;
    bool pending = false;
    std::array<uint8_t, 3> reserved{};
};

struct MacroHistoryChange {
    MacroHistoryActionKind kind = MacroHistoryActionKind::SOURCE_STATE;
    MacroAutomationSlotAddress address{};
    core::app::ExtmemUniquePtr<MacroSlotHistoryChangePayload> slot{};
    MacroModulatorCreationHistoryPayload modulator{};
};

using MacroHistoryChangePtr = core::app::ExtmemUniquePtr<MacroHistoryChange>;

[[nodiscard]] bool captureMacroSlotHistorySnapshot(
    const MacroPagesState& pages,
    const MacroAutomationSlotAddress& address,
    MacroSlotHistorySnapshot& out
);

[[nodiscard]] bool sameMacroSlotHistorySnapshot(
    const MacroSlotHistorySnapshot& lhs,
    const MacroSlotHistorySnapshot& rhs
);

[[nodiscard]] bool liveMacroSlotMatchesHistorySnapshot(
    const MacroPagesState& pages,
    const MacroSlotHistorySnapshot& snapshot
);

/** Applies a validated Slot snapshot without partially mutating on failure. */
[[nodiscard]] bool applyMacroSlotHistorySnapshot(
    MacroPagesState& pages,
    const MacroSlotHistorySnapshot& snapshot
);

class MacroHistoryService {
public:
    static constexpr uint8_t ENTRY_LIMIT = 8;

    MacroHistoryService();
    ~MacroHistoryService();
    MacroHistoryService(const MacroHistoryService&) = delete;
    MacroHistoryService& operator=(const MacroHistoryService&) = delete;

    [[nodiscard]] MacroHistoryChangePtr prepare(
        const MacroPagesState& pages,
        const MacroAutomationSlotAddress& address,
        MacroHistoryActionKind kind
    ) const;

    /**
     * Captures the post-state and records one action. On capture/admission
     * failure the pre-state is restored before returning false.
     */
    [[nodiscard]] bool commitPrepared(
        MacroPagesState& pages,
        MacroHistoryChangePtr change,
        bool coalesce = false
    );

    /**
     * Reserves history before publishing one provisional LFO + assignment.
     * The returned IDs are also projected through ProjectControlState::audition.
     */
    [[nodiscard]] core::state::modulation::ProjectModulationResult
        beginLfoModulatorAudition(
            MacroPagesState& pages,
            const MacroAutomationSlotAddress& address,
            const core::state::modulation::ModulatorLfoDraft& sourceDraft,
            const core::state::modulation::ModulationBindingDraft& bindingDraft
        );

    /** Exact rollback with no Undo entry and no authored ID/capacity residue. */
    [[nodiscard]] bool cancelModulatorAudition(
        MacroPagesState& pages,
        const MacroAutomationSlotAddress& address
    );

    /** Publishes the reserved delta as one stable-ID Undo action. */
    [[nodiscard]] bool commitModulatorAudition(
        MacroPagesState& pages,
        const MacroAutomationSlotAddress& address
    );

    [[nodiscard]] bool modulatorAuditionPending(
        const MacroAutomationSlotAddress& address
    ) const;

    /** Depth edit fast path: one allocation on first turn, none while coalescing. */
    [[nodiscard]] bool setModulationDepthCoalesced(
        MacroPagesState& pages,
        const MacroAutomationSlotAddress& address,
        float depth
    );

    void endCoalescing();
    [[nodiscard]] bool undo(
        MacroPagesState& pages,
        MacroAutomationSlotAddress* appliedAddress = nullptr
    );
    [[nodiscard]] bool redo(
        MacroPagesState& pages,
        MacroAutomationSlotAddress* appliedAddress = nullptr
    );
    void clear();

    [[nodiscard]] bool canUndo() const { return undo_count_ > 0; }
    [[nodiscard]] bool canRedo() const { return redo_count_ > 0; }
    [[nodiscard]] uint8_t undoCount() const { return undo_count_; }
    [[nodiscard]] uint8_t redoCount() const { return redo_count_; }

private:
    [[nodiscard]] MacroHistoryChangePtr* pendingModulatorSlot_();
    [[nodiscard]] const MacroHistoryChangePtr* pendingModulatorSlot_() const;
    [[nodiscard]] bool parkPending_(MacroHistoryChangePtr change);
    [[nodiscard]] MacroHistoryChangePtr takePending_();
    static void push_(
        std::array<MacroHistoryChangePtr, ENTRY_LIMIT>& stack,
        uint8_t& count,
        MacroHistoryChangePtr change
    );
    void clearRedo_();

    std::array<MacroHistoryChangePtr, ENTRY_LIMIT> undo_{};
    std::array<MacroHistoryChangePtr, ENTRY_LIMIT> redo_{};
    uint8_t undo_count_ = 0;
    uint8_t redo_count_ = 0;
    bool coalescing_ = false;
    MacroHistoryActionKind coalesced_kind_ = MacroHistoryActionKind::SOURCE_STATE;
    MacroAutomationSlotAddress coalesced_address_{};
};

}  // namespace core::state::macro
