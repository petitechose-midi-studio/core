#include "state/macro/MacroEditMenuModel.hpp"

#include <config/PlatformCompat.hpp>

namespace core::state::macro {

namespace modulation = core::state::modulation;

FLASHMEM int MacroModulationRows::rowCount() const {
    return assignmentCount == 0U
        ? 3
        : static_cast<int>(assignmentCount) + 3;
}

FLASHMEM int MacroModulationRows::firstAssignmentRow() const {
    return assignmentCount > 0U ? 1 : 0;
}

FLASHMEM int MacroModulationRows::addSourceRow() const {
    return assignmentCount == 0U
        ? 1
        : firstAssignmentRow() + static_cast<int>(assignmentCount);
}

FLASHMEM int MacroModulationRows::recordShapeRow() const {
    return addSourceRow() + 1;
}

FLASHMEM MacroRootItem macroRootItemAt(uint8_t row) {
    if (row == macroRootRow(MacroRootItem::DESTINATION)) {
        return MacroRootItem::DESTINATION;
    }
    if (row == macroRootRow(MacroRootItem::AUTOMATION)) {
        return MacroRootItem::AUTOMATION;
    }
    if (row == macroRootRow(MacroRootItem::MODULATION)) {
        return MacroRootItem::MODULATION;
    }
    return MacroRootItem::INVALID;
}

FLASHMEM uint8_t macroRootRow(MacroRootItem item) {
    switch (item) {
        case MacroRootItem::DESTINATION: return 0U;
        case MacroRootItem::AUTOMATION: return 1U;
        case MacroRootItem::MODULATION: return 2U;
        case MacroRootItem::INVALID: return 0xFFU;
    }
    return 0xFFU;
}

FLASHMEM MacroModulationRows buildMacroModulationRows(
    const modulation::ProjectModulationState& graph,
    const modulation::ModulationDestination& destination
) {
    MacroModulationRows rows{};
    rows.destination = destination;
    for (uint16_t index = 0U; index < graph.outputBindingCount; ++index) {
        if (graph.outputBindings[index].destination == destination) {
            ++rows.assignmentCount;
        }
    }
    return rows;
}

FLASHMEM MacroModulationRowDescriptor macroModulationRowAt(
    const modulation::ProjectModulationState& graph,
    const MacroModulationRows& rows,
    int row
) {
    if (rows.assignmentCount > 0U && row == 0) {
        return {MacroModulationRowKind::ALL, {}, rows.destination};
    }
    if (row == rows.addSourceRow()) {
        return {MacroModulationRowKind::ADD_SOURCE, {}, rows.destination};
    }
    if (row == rows.recordShapeRow()) {
        return {MacroModulationRowKind::RECORD_SHAPE, {}, rows.destination};
    }

    const int targetOrdinal = row - rows.firstAssignmentRow();
    if (targetOrdinal < 0 ||
        targetOrdinal >= static_cast<int>(rows.assignmentCount)) {
        return {};
    }
    uint8_t ordinal = 0U;
    for (uint16_t index = 0U; index < graph.outputBindingCount; ++index) {
        const auto& binding = graph.outputBindings[index];
        if (binding.destination != rows.destination) continue;
        if (ordinal++ == targetOrdinal) {
            return {
                MacroModulationRowKind::ASSIGNMENT,
                binding.id,
                rows.destination,
            };
        }
    }
    return {};
}

FLASHMEM int macroModulationRowForBinding(
    const modulation::ProjectModulationState& graph,
    const MacroModulationRows& rows,
    modulation::ModulationBindingId bindingId
) {
    uint8_t ordinal = 0U;
    for (uint16_t index = 0U; index < graph.outputBindingCount; ++index) {
        const auto& binding = graph.outputBindings[index];
        if (binding.destination != rows.destination) continue;
        if (binding.id == bindingId) {
            return rows.firstAssignmentRow() + ordinal;
        }
        ++ordinal;
    }
    return rows.assignmentCount > 0U
        ? rows.firstAssignmentRow()
        : rows.addSourceRow();
}

FLASHMEM const modulation::ModulationBindingState* macroModulationBinding(
    const modulation::ProjectModulationState& graph,
    const MacroModulationRowDescriptor& row
) {
    if (row.kind != MacroModulationRowKind::ASSIGNMENT ||
        !modulation::valid(row.bindingId)) {
        return nullptr;
    }
    for (uint16_t index = 0U; index < graph.outputBindingCount; ++index) {
        if (graph.outputBindings[index].id == row.bindingId) {
            return graph.outputBindings[index].destination == row.destination
                ? &graph.outputBindings[index]
                : nullptr;
        }
    }
    return nullptr;
}

FLASHMEM uint8_t macroContextActionCount(
    MacroRootItem item,
    uint16_t modulationAssignmentCount
) {
    switch (item) {
        case MacroRootItem::DESTINATION:
            return 2U;
        case MacroRootItem::AUTOMATION:
            return 5U;
        case MacroRootItem::MODULATION:
            return modulationAssignmentCount == 0U
                ? 1U
                : static_cast<uint8_t>(modulationAssignmentCount + 2U);
        case MacroRootItem::INVALID:
            return 0U;
    }
    return 0U;
}

FLASHMEM MacroContextActionDescriptor macroContextActionAt(
    const modulation::ProjectModulationState& graph,
    const MacroModulationRows& modulationRows,
    MacroRootItem item,
    uint8_t propertyIndex
) {
    if (item == MacroRootItem::DESTINATION) {
        if (propertyIndex >= 2U) return {};
        return {
            propertyIndex == 0U
                ? MacroContextAction::DESTINATION_CC
                : MacroContextAction::DESTINATION_CHANNEL,
            {},
        };
    }
    if (item == MacroRootItem::AUTOMATION) {
        if (propertyIndex >= 5U) return {};
        constexpr MacroContextAction actions[] = {
            MacroContextAction::AUTOMATION_RECORD,
            MacroContextAction::AUTOMATION_PLAYBACK,
            MacroContextAction::AUTOMATION_LENGTH,
            MacroContextAction::AUTOMATION_OFFSET,
            MacroContextAction::AUTOMATION_CONVERT,
        };
        return {
            actions[propertyIndex],
            {},
        };
    }
    if (item != MacroRootItem::MODULATION) return {};
    if (modulationRows.assignmentCount == 0U) {
        return propertyIndex == 0U
            ? MacroContextActionDescriptor{
                  MacroContextAction::MODULATION_RECORD_NEW_SHAPE,
                  {},
              }
            : MacroContextActionDescriptor{};
    }
    if (propertyIndex == modulationRows.assignmentCount + 1U) {
        return {MacroContextAction::MODULATION_RECORD_NEW_SHAPE, {}};
    }
    if (propertyIndex == modulationRows.assignmentCount) {
        return {MacroContextAction::MODULATION_GLOBAL_DEPTH, {}};
    }
    if (propertyIndex > modulationRows.assignmentCount + 1U) return {};

    uint8_t ordinal = 0U;
    for (uint16_t index = 0U; index < graph.outputBindingCount; ++index) {
        const auto& binding = graph.outputBindings[index];
        if (binding.destination != modulationRows.destination) continue;
        if (ordinal++ == propertyIndex) {
            return {
                MacroContextAction::MODULATION_EDGE_DEPTH,
                binding.id,
            };
        }
    }
    return {};
}

FLASHMEM uint8_t macroContextActionIndexForBinding(
    const modulation::ProjectModulationState& graph,
    const MacroModulationRows& rows,
    modulation::ModulationBindingId bindingId
) {
    uint16_t ordinal = 0U;
    for (uint16_t index = 0U; index < graph.outputBindingCount; ++index) {
        const auto& binding = graph.outputBindings[index];
        if (binding.destination != rows.destination) continue;
        if (binding.id == bindingId) {
            return static_cast<uint8_t>(ordinal);
        }
        ++ordinal;
    }
    // A stale/absent focus starts on the first, always-safe action. This also
    // gives an empty destination a useful entry point without inventing an edge.
    return 0U;
}

}  // namespace core::state::macro
