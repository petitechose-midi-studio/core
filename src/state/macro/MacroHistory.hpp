#pragma once

#include <array>
#include <cstdint>

#include "app/ExtmemAllocator.hpp"
#include "state/macro/MacroAutomationState.hpp"
#include "state/macro/MacroPagesState.hpp"

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

struct MacroHistoryChange {
    MacroHistoryActionKind kind = MacroHistoryActionKind::SOURCE_STATE;
    MacroAutomationSlotAddress address{};
    MacroSlotHistorySnapshot before{};
    MacroSlotHistorySnapshot after{};
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
