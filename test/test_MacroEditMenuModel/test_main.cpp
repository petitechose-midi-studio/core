#include <cassert>
#include <iostream>

#include "state/macro/MacroEditMenuModel.hpp"
#include "ui/macro/MacroSourceDetailLayout.hpp"

namespace {

namespace macro = core::state::macro;
namespace modulation = core::state::modulation;
namespace detail_ui = core::ui::macro;

modulation::ModulationDestination destination(uint8_t macroIndex) {
    return {
        .kind = modulation::ModulationDestinationKind::MACRO_SLOT,
        .track = 1U,
        .page = 2U,
        .macro = macroIndex,
    };
}

void appendBinding(
    modulation::ProjectModulationState& graph,
    uint32_t bindingId,
    const modulation::ModulationDestination& target
) {
    auto& binding = graph.outputBindings[graph.outputBindingCount++];
    binding.id = {bindingId};
    binding.sourceId = {bindingId + 100U};
    binding.destination = target;
}

void test_root_items_are_typed_at_the_ui_boundary() {
    assert(macro::macroRootItemAt(0U) == macro::MacroRootItem::DESTINATION);
    assert(macro::macroRootItemAt(1U) == macro::MacroRootItem::AUTOMATION);
    assert(macro::macroRootItemAt(2U) == macro::MacroRootItem::MODULATION);
    assert(macro::macroRootItemAt(3U) == macro::MacroRootItem::INVALID);
    assert(macro::macroRootRow(macro::MacroRootItem::MODULATION) == 2U);
    assert(macro::macroRootRow(macro::MacroRootItem::INVALID) == 0xFFU);

    std::cout << "[PASS] typed Macro root rows\n";
}

void test_source_detail_layout_indices_fail_closed() {
    detail_ui::AutomationDetailLayout emptyAutomation{};
    detail_ui::ModulationDetailLayout emptyModulation{};
    assert(emptyAutomation.at(0U) ==
           detail_ui::AutomationDetailItem::INVALID);
    assert(emptyModulation.at(0U) ==
           detail_ui::ModulationDetailItem::INVALID);

    detail_ui::MacroSourceDetailContext context{};
    context.automationStored = true;
    context.modulationStored = true;
    context.manualOverride = true;

    const auto automation = detail_ui::buildAutomationDetailLayout(context);
    assert(automation.count == 5U);
    assert(automation.at(0U) == detail_ui::AutomationDetailItem::PLAYBACK);
    assert(automation.at(1U) == detail_ui::AutomationDetailItem::RESUME);
    assert(automation.at(2U) ==
           detail_ui::AutomationDetailItem::CONVERT_TO_MODULATION);
    assert(automation.at(3U) == detail_ui::AutomationDetailItem::LENGTH);
    assert(automation.at(4U) == detail_ui::AutomationDetailItem::OFFSET);
    assert(automation.at(automation.count) ==
           detail_ui::AutomationDetailItem::INVALID);
    assert(automation.at(0xFFU) ==
           detail_ui::AutomationDetailItem::INVALID);

    const auto modulation = detail_ui::buildModulationDetailLayout(context);
    assert(modulation.count == 4U);
    assert(modulation.at(0U) == detail_ui::ModulationDetailItem::PLAYBACK);
    assert(modulation.at(1U) == detail_ui::ModulationDetailItem::DEPTH);
    assert(modulation.at(2U) == detail_ui::ModulationDetailItem::SHAPE);
    assert(modulation.at(3U) == detail_ui::ModulationDetailItem::ORIGIN);
    assert(modulation.at(modulation.count) ==
           detail_ui::ModulationDetailItem::INVALID);
    assert(modulation.at(0xFFU) ==
           detail_ui::ModulationDetailItem::INVALID);

    std::cout << "[PASS] Macro source detail layouts fail closed\n";
}

void test_context_actions_replace_destination_and_automation_ordinals() {
    modulation::ProjectModulationState graph{};
    const auto rows = macro::buildMacroModulationRows(graph, destination(0U));

    assert(macro::macroContextActionCount(
        macro::MacroRootItem::DESTINATION, 0U
    ) == 2U);
    assert(macro::macroContextActionAt(
        graph, rows, macro::MacroRootItem::DESTINATION, 0U
    ).action == macro::MacroContextAction::DESTINATION_CC);
    assert(macro::macroContextActionAt(
        graph, rows, macro::MacroRootItem::DESTINATION, 1U
    ).action == macro::MacroContextAction::DESTINATION_CHANNEL);
    assert(macro::macroContextActionCount(
        macro::MacroRootItem::AUTOMATION, 0U
    ) == 5U);
    assert(macro::macroContextActionAt(
        graph, rows, macro::MacroRootItem::AUTOMATION, 0U
    ).action == macro::MacroContextAction::AUTOMATION_RECORD);
    assert(macro::macroContextActionAt(
        graph, rows, macro::MacroRootItem::AUTOMATION, 4U
    ).action == macro::MacroContextAction::AUTOMATION_CONVERT);
    assert(macro::macroContextActionAt(
        graph, rows, macro::MacroRootItem::DESTINATION, 2U
    ).action == macro::MacroContextAction::NONE);
    assert(macro::macroContextActionAt(
        graph, rows, macro::MacroRootItem::AUTOMATION, 5U
    ).action == macro::MacroContextAction::NONE);

    std::cout << "[PASS] typed Macro contextual actions\n";
}

void test_modulation_rows_carry_stable_binding_ids() {
    modulation::ProjectModulationState graph{};
    const auto target = destination(3U);
    appendBinding(graph, 11U, target);
    appendBinding(graph, 99U, destination(4U));
    appendBinding(graph, 22U, target);

    const auto rows = macro::buildMacroModulationRows(graph, target);
    assert(rows.assignmentCount == 2U);
    assert(rows.rowCount() == 4);
    assert(rows.firstAssignmentRow() == 1);
    assert(rows.addSourceRow() == 3);

    const auto all = macro::macroModulationRowAt(graph, rows, 0);
    const auto first = macro::macroModulationRowAt(graph, rows, 1);
    const auto second = macro::macroModulationRowAt(graph, rows, 2);
    const auto add = macro::macroModulationRowAt(graph, rows, 3);
    assert(all.kind == macro::MacroModulationRowKind::ALL);
    assert(first.kind == macro::MacroModulationRowKind::ASSIGNMENT);
    assert(first.bindingId == modulation::ModulationBindingId{11U});
    assert(second.kind == macro::MacroModulationRowKind::ASSIGNMENT);
    assert(second.bindingId == modulation::ModulationBindingId{22U});
    assert(add.kind == macro::MacroModulationRowKind::ADD_SOURCE);
    assert(macro::macroModulationRowAt(graph, rows, 4).kind ==
           macro::MacroModulationRowKind::INVALID);
    assert(macro::macroModulationBinding(graph, second)->id == second.bindingId);
    auto crossDestination = second;
    crossDestination.destination = destination(7U);
    assert(macro::macroModulationBinding(graph, crossDestination) == nullptr);
    assert(macro::macroModulationRowForBinding(
        graph, rows, second.bindingId
    ) == 2);

    std::cout << "[PASS] stable Macro modulation row identities\n";
}

void test_invalid_and_stale_modulation_rows_fail_closed() {
    modulation::ProjectModulationState graph{};
    const auto target = destination(1U);
    appendBinding(graph, 41U, target);
    const auto rows = macro::buildMacroModulationRows(graph, target);
    const auto captured = macro::macroModulationRowAt(graph, rows, 1);

    assert(captured.kind == macro::MacroModulationRowKind::ASSIGNMENT);
    assert(macro::macroModulationRowAt(graph, rows, -1).kind ==
           macro::MacroModulationRowKind::INVALID);
    assert(macro::macroModulationRowAt(graph, rows, 99).kind ==
           macro::MacroModulationRowKind::INVALID);
    assert(macro::macroModulationBinding(graph, {}) == nullptr);

    // A descriptor is only valid for the exact edge and destination captured
    // at projection time. Replacing the edge must never retarget the action to
    // a newer row that happens to reuse the same ordinal.
    graph.outputBindings[0].id = {42U};
    assert(macro::macroModulationBinding(graph, captured) == nullptr);
    assert(macro::macroContextActionAt(
        graph, rows, macro::MacroRootItem::MODULATION, 0U
    ).action == macro::MacroContextAction::MODULATION_EDGE_DEPTH);
    assert(macro::macroContextActionAt(
        graph, rows, macro::MacroRootItem::MODULATION, 0U
    ).bindingId == modulation::ModulationBindingId{42U});

    graph.outputBindings[0].destination = destination(7U);
    assert(macro::macroModulationBinding(graph, captured) == nullptr);
    assert(macro::macroContextActionAt(
        graph, rows, macro::MacroRootItem::MODULATION, 0U
    ).action == macro::MacroContextAction::NONE);

    std::cout << "[PASS] invalid and stale Macro modulation rows fail closed\n";
}

void test_modulation_context_actions_resolve_depths_before_secondary_record() {
    modulation::ProjectModulationState graph{};
    const auto target = destination(6U);
    appendBinding(graph, 31U, target);
    appendBinding(graph, 32U, target);
    const auto rows = macro::buildMacroModulationRows(graph, target);

    assert(macro::macroContextActionCount(
        macro::MacroRootItem::MODULATION, rows.assignmentCount
    ) == 4U);
    const auto first = macro::macroContextActionAt(
        graph, rows, macro::MacroRootItem::MODULATION, 0U
    );
    const auto second = macro::macroContextActionAt(
        graph, rows, macro::MacroRootItem::MODULATION, 1U
    );
    const auto global = macro::macroContextActionAt(
        graph, rows, macro::MacroRootItem::MODULATION, 2U
    );
    const auto record = macro::macroContextActionAt(
        graph, rows, macro::MacroRootItem::MODULATION, 3U
    );
    assert(record.action ==
           macro::MacroContextAction::MODULATION_RECORD_NEW_SHAPE);
    assert(first.action == macro::MacroContextAction::MODULATION_EDGE_DEPTH);
    assert(first.bindingId == modulation::ModulationBindingId{31U});
    assert(second.bindingId == modulation::ModulationBindingId{32U});
    assert(global.action ==
           macro::MacroContextAction::MODULATION_GLOBAL_DEPTH);
    assert(macro::macroContextActionAt(
        graph, rows, macro::MacroRootItem::MODULATION, 4U
    ).action == macro::MacroContextAction::NONE);
    assert(macro::macroContextActionIndexForBinding(
        graph, rows, second.bindingId
    ) == 1U);
    assert(macro::macroContextActionIndexForBinding(
        graph, rows, modulation::ModulationBindingId{}
    ) == 0U);

    modulation::ProjectModulationState emptyGraph{};
    const auto emptyRows = macro::buildMacroModulationRows(
        emptyGraph, destination(7U)
    );
    assert(macro::macroContextActionCount(
        macro::MacroRootItem::MODULATION, emptyRows.assignmentCount
    ) == 1U);
    assert(macro::macroContextActionAt(
        emptyGraph, emptyRows, macro::MacroRootItem::MODULATION, 0U
    ).action == macro::MacroContextAction::MODULATION_RECORD_NEW_SHAPE);
    assert(macro::macroContextActionAt(
        emptyGraph, emptyRows, macro::MacroRootItem::MODULATION, 1U
    ).action == macro::MacroContextAction::NONE);

    std::cout << "[PASS] stable Macro contextual modulation identities\n";
}

void test_invalid_root_and_context_indices_are_inert() {
    modulation::ProjectModulationState graph{};
    const auto rows = macro::buildMacroModulationRows(graph, destination(0U));

    assert(macro::macroRootItemAt(0xFFU) == macro::MacroRootItem::INVALID);
    assert(macro::macroContextActionCount(
        macro::MacroRootItem::INVALID, 0U
    ) == 0U);
    assert(macro::macroContextActionAt(
        graph, rows, macro::MacroRootItem::INVALID, 0U
    ).action == macro::MacroContextAction::NONE);
    assert(macro::macroContextActionAt(
        graph, rows, macro::MacroRootItem::DESTINATION, 0xFFU
    ).action == macro::MacroContextAction::NONE);
    assert(macro::macroContextActionAt(
        graph, rows, macro::MacroRootItem::AUTOMATION, 0xFFU
    ).action == macro::MacroContextAction::NONE);
    assert(macro::macroContextActionAt(
        graph, rows, macro::MacroRootItem::MODULATION, 0xFFU
    ).action == macro::MacroContextAction::NONE);

    std::cout << "[PASS] invalid Macro root and context indices are inert\n";
}

}  // namespace

int main() {
    test_root_items_are_typed_at_the_ui_boundary();
    test_source_detail_layout_indices_fail_closed();
    test_context_actions_replace_destination_and_automation_ordinals();
    test_modulation_rows_carry_stable_binding_ids();
    test_invalid_and_stale_modulation_rows_fail_closed();
    test_modulation_context_actions_resolve_depths_before_secondary_record();
    test_invalid_root_and_context_indices_are_inert();
    return 0;
}
