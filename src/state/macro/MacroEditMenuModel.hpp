#pragma once

#include <cstdint>
#include <type_traits>

#include "state/modulation/ProjectModulationState.hpp"

namespace core::state::macro {

/** Stable semantic rows exposed by the Macro editor root. */
enum class MacroRootItem : uint8_t {
    DESTINATION = 0,
    AUTOMATION,
    MODULATION,
    INVALID = 0xFF,
};

inline constexpr uint8_t MACRO_ROOT_ITEM_COUNT = 3U;

/** Actions exposed by the temporary Macro contextual-property selector. */
enum class MacroContextAction : uint8_t {
    NONE = 0,
    DESTINATION_CC,
    DESTINATION_CHANNEL,
    AUTOMATION_RECORD,
    AUTOMATION_PLAYBACK,
    AUTOMATION_LENGTH,
    AUTOMATION_OFFSET,
    AUTOMATION_CONVERT,
    MODULATION_RECORD_NEW_SHAPE,
    MODULATION_EDGE_DEPTH,
    MODULATION_GLOBAL_DEPTH,
};

/** Semantic rows exposed by the Modulation assignment detail. */
enum class MacroModulationRowKind : uint8_t {
    INVALID = 0,
    ALL,
    ASSIGNMENT,
    ADD_SOURCE,
    RECORD_SHAPE,
};

/**
 * Resolved row identity.
 *
 * A binding row always carries its stable domain identifier. UI ordinals are
 * therefore confined to the projection boundary and never become an action
 * target.
 */
struct MacroModulationRowDescriptor {
    MacroModulationRowKind kind = MacroModulationRowKind::INVALID;
    core::state::modulation::ModulationBindingId bindingId{};
    core::state::modulation::ModulationDestination destination{};
};

/** Resolved contextual action, including its stable edge when applicable. */
struct MacroContextActionDescriptor {
    MacroContextAction action = MacroContextAction::NONE;
    core::state::modulation::ModulationBindingId bindingId{};
};

/** Compact facts shared by handlers and presenters for one destination. */
struct MacroModulationRows {
    core::state::modulation::ModulationDestination destination{};
    uint16_t assignmentCount = 0U;

    [[nodiscard]] int rowCount() const;
    [[nodiscard]] int firstAssignmentRow() const;
    [[nodiscard]] int addSourceRow() const;
    [[nodiscard]] int recordShapeRow() const;
};

[[nodiscard]] MacroRootItem macroRootItemAt(uint8_t row);
[[nodiscard]] uint8_t macroRootRow(MacroRootItem item);

[[nodiscard]] MacroModulationRows buildMacroModulationRows(
    const core::state::modulation::ProjectModulationState& graph,
    const core::state::modulation::ModulationDestination& destination
);

[[nodiscard]] MacroModulationRowDescriptor macroModulationRowAt(
    const core::state::modulation::ProjectModulationState& graph,
    const MacroModulationRows& rows,
    int row
);

[[nodiscard]] int macroModulationRowForBinding(
    const core::state::modulation::ProjectModulationState& graph,
    const MacroModulationRows& rows,
    core::state::modulation::ModulationBindingId bindingId
);

[[nodiscard]] const core::state::modulation::ModulationBindingState*
macroModulationBinding(
    const core::state::modulation::ProjectModulationState& graph,
    const MacroModulationRowDescriptor& row
);

[[nodiscard]] uint8_t macroContextActionCount(
    MacroRootItem item,
    uint16_t modulationAssignmentCount
);

[[nodiscard]] MacroContextActionDescriptor macroContextActionAt(
    const core::state::modulation::ProjectModulationState& graph,
    const MacroModulationRows& modulationRows,
    MacroRootItem item,
    uint8_t propertyIndex
);

[[nodiscard]] uint8_t macroContextActionIndexForBinding(
    const core::state::modulation::ProjectModulationState& graph,
    const MacroModulationRows& rows,
    core::state::modulation::ModulationBindingId bindingId
);

static_assert(sizeof(MacroModulationRowDescriptor) == 12U);
static_assert(sizeof(MacroContextActionDescriptor) == 8U);
static_assert(sizeof(MacroModulationRows) <= 8U);
static_assert(std::is_trivially_copyable_v<MacroModulationRowDescriptor>);
static_assert(std::is_trivially_copyable_v<MacroContextActionDescriptor>);
static_assert(std::is_trivially_copyable_v<MacroModulationRows>);

}  // namespace core::state::macro
