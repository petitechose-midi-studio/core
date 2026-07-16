#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
#include <utility>

#include "../../src/state/macro/MacroHistory.hpp"
#include "../support/ProjectControlTestUtils.hpp"

namespace {

namespace macro = core::state::macro;

constexpr macro::MacroAutomationSlotAddress kAddress{
    .track = 0,
    .page = 0,
    .macro = 1,
};

void seedCurves(macro::MacroPagesState& pages) {
    macro::MacroAutomationLane automation{};
    assert(macro::macroAutomationAppendPoint(automation, 0.0f, 0.2f));
    assert(macro::macroAutomationAppendPoint(automation, 1.0f, 0.8f));
    assert(test_support::project_control::assignAutomation(
        pages.control,
        kAddress,
        automation
    ));

    macro::MacroModulationShape modulation{};
    assert(macro::macroModulationAppendPoint(modulation, 0.0f, -0.2f));
    assert(macro::macroModulationAppendPoint(modulation, 1.0f, 0.3f));
    assert(test_support::project_control::assignModulation(
        pages.control,
        kAddress,
        modulation,
        0.65f
    ));

    auto& page = pages.pageData(kAddress.track, kAddress.page);
    page.setMacroActive(kAddress.macro, true);
    page.cc[kAddress.macro] = 74;
    page.values[kAddress.macro] = 0.42f;
    pages.updateActiveConfigs();
}

void test_snapshot_roundtrip_restores_exact_slot() {
    macro::MacroPagesState pages;
    seedCurves(pages);
    macro::MacroSlotHistorySnapshot expected{};
    assert(macro::captureMacroSlotHistorySnapshot(pages, kAddress, expected));

    auto& page = pages.pageData(0, 0);
    page.setMacroActive(1, false);
    page.cc[1] = 7;
    page.values[1] = 0.9f;
    assert(core::state::modulation::clearProjectControlAutomation(
        pages.control,
        kAddress
    ));
    assert(core::state::modulation::clearProjectControlModulation(
        pages.control,
        kAddress
    ));
    assert(macro::applyMacroSlotHistorySnapshot(pages, expected));
    assert(macro::liveMacroSlotMatchesHistorySnapshot(pages, expected));

    macro::MacroSlotHistorySnapshot restored{};
    assert(macro::captureMacroSlotHistorySnapshot(pages, kAddress, restored));
    assert(macro::sameMacroSlotHistorySnapshot(expected, restored));
    std::cout << "[PASS] snapshot roundtrip restores exact Slot\n";
}

void test_clear_is_one_undo_redo_action() {
    macro::MacroPagesState pages;
    seedCurves(pages);
    macro::MacroHistoryService history;
    auto change = history.prepare(
        pages,
        kAddress,
        macro::MacroHistoryActionKind::CLEAR_MODULATION
    );
    assert(change);
    assert(core::state::modulation::clearProjectControlModulation(
        pages.control,
        kAddress
    ));
    assert(history.commitPrepared(pages, std::move(change)));
    assert(history.undoCount() == 1);
    assert(history.undo(pages));
    auto slot = test_support::project_control::readSlot(pages.control, kAddress);
    assert(slot.modulationEnabled);
    assert(slot.automationEnabled);
    assert(std::fabs(slot.legacy.modulationDepth - 0.65f) < 0.0001f);
    assert(history.redo(pages));
    slot = test_support::project_control::readSlot(pages.control, kAddress);
    assert(!slot.modulationStored);
    assert(slot.automationEnabled);
    std::cout << "[PASS] clear is one exact Undo/Redo action\n";
}

void test_depth_turns_coalesce_without_extra_entries() {
    macro::MacroPagesState pages;
    seedCurves(pages);
    macro::MacroHistoryService history;
    assert(history.setModulationDepthCoalesced(pages, kAddress, 0.5f));
    assert(history.setModulationDepthCoalesced(pages, kAddress, 0.25f));
    assert(history.setModulationDepthCoalesced(pages, kAddress, 0.0f));
    assert(history.undoCount() == 1);
    assert(history.undo(pages));
    auto slot = test_support::project_control::readSlot(pages.control, kAddress);
    assert(std::fabs(slot.legacy.modulationDepth - 0.65f) < 0.0001f);
    assert(history.redo(pages));
    slot = test_support::project_control::readSlot(pages.control, kAddress);
    assert(slot.legacy.modulationDepth == 0.0f);
    std::cout << "[PASS] Depth turns coalesce to one action\n";
}

void test_stale_live_state_blocks_undo_without_overwrite() {
    macro::MacroPagesState pages;
    seedCurves(pages);
    macro::MacroHistoryService history;
    assert(history.setModulationDepthCoalesced(pages, kAddress, 0.4f));
    history.endCoalescing();
    pages.pageData(0, 0).cc[1] = 99;
    assert(!history.undo(pages));
    assert(pages.pageData(0, 0).cc[1] == 99);
    const auto slot = test_support::project_control::readSlot(pages.control, kAddress);
    assert(std::fabs(slot.legacy.modulationDepth - 0.4f) < 0.0001f);
    std::cout << "[PASS] stale live state blocks unsafe Undo\n";
}

void test_history_admission_rejects_oversized_slot() {
    macro::MacroPagesState pages;
    auto& domain = pages.control.authored;
    const uint16_t pointCount = static_cast<uint16_t>(
        macro::MACRO_HISTORY_POINT_CAPACITY + 1U
    );
    domain.curves.nextCurveId = 2;
    domain.curves.recordCount = 1;
    domain.curves.pointCount = pointCount;
    domain.curves.records[0] = {
        .id = core::state::modulation::ProjectCurveId{1},
        .pointOffset = 0,
        .pointCount = pointCount,
        .sourceDurationTicks = pointCount,
        .durationTicks = pointCount,
        .windowOffsetTicks = 0,
        .referenceCount = 1,
        .interpolation = core::state::modulation::ProjectCurveInterpolation::LINEAR,
        .valueDomain = core::state::modulation::ProjectCurveValueDomain::ABSOLUTE_UNIPOLAR,
        .flags = 0,
        .origin = core::state::modulation::ProjectCurveOrigin::NATIVE,
    };
    for (uint16_t i = 0; i < pointCount; ++i) {
        domain.curves.points[i] = {.tick = i, .value = 0};
    }
    domain.automation.entryCount = 1;
    domain.automation.entries[0] = {
        .destination = core::state::modulation::projectControlDestination(kAddress),
        .curveId = core::state::modulation::ProjectCurveId{1},
        .flags = core::state::modulation::PROJECT_AUTOMATION_CURVE_FLAG_ENABLED,
    };
    macro::MacroHistoryService history;
    assert(!history.prepare(
        pages,
        kAddress,
        macro::MacroHistoryActionKind::REMOVE_SLOT
    ));
    assert(test_support::project_control::readSlot(pages.control, kAddress).present);
    std::cout << "[PASS] oversized Slot is rejected before mutation\n";
}

void test_history_evicts_oldest_entry_at_fixed_limit() {
    macro::MacroPagesState pages;
    seedCurves(pages);
    macro::MacroHistoryService history;

    for (uint8_t i = 0; i < 10; ++i) {
        assert(history.setModulationDepthCoalesced(
            pages,
            kAddress,
            static_cast<float>(i) / 10.0f
        ));
        history.endCoalescing();
    }

    assert(history.undoCount() == macro::MacroHistoryService::ENTRY_LIMIT);
    for (uint8_t i = 0; i < macro::MacroHistoryService::ENTRY_LIMIT; ++i) {
        assert(history.undo(pages));
    }
    assert(!history.undo(pages));
    const auto slot = test_support::project_control::readSlot(pages.control, kAddress);
    assert(std::fabs(slot.legacy.modulationDepth - 0.1f) < 0.0001f);
    assert(history.redoCount() == macro::MacroHistoryService::ENTRY_LIMIT);
    std::cout << "[PASS] history evicts oldest entry at fixed limit\n";
}

void test_new_mutation_after_undo_clears_redo_stack() {
    macro::MacroPagesState pages;
    seedCurves(pages);
    macro::MacroHistoryService history;

    assert(history.setModulationDepthCoalesced(pages, kAddress, 0.5f));
    history.endCoalescing();
    assert(history.undo(pages));
    assert(history.redoCount() == 1);

    assert(history.setModulationDepthCoalesced(pages, kAddress, 0.3f));
    history.endCoalescing();
    assert(history.redoCount() == 0);
    assert(!history.redo(pages));
    const auto slot = test_support::project_control::readSlot(pages.control, kAddress);
    assert(std::fabs(slot.legacy.modulationDepth - 0.3f) < 0.0001f);
    std::cout << "[PASS] new mutation after Undo clears Redo\n";
}

core::state::modulation::ModulatorLfoDraft defaultLfoDraft() {
    using namespace core::state::modulation;
    ModulatorLfoDraft draft{};
    draft.name = "LFO 1";
    draft.reach = {
        .kind = ModulatorReachKind::MACRO,
        .track = kAddress.track,
        .page = kAddress.page,
        .macro = kAddress.macro,
    };
    draft.parameters.periodTicks = 384;
    draft.parameters.shape = ModulatorLfoShape::SINE;
    draft.parameters.retrigger = ModulatorRetriggerPolicy::TRANSPORT;
    draft.parameters.timing = ModulatorTimingMode::SYNC;
    return draft;
}

core::state::modulation::ModulationBindingDraft defaultBindingDraft() {
    using namespace core::state::modulation;
    ModulationBindingDraft draft{};
    draft.destination = projectControlDestination(kAddress);
    draft.amountQ15 = 8192;
    draft.inputRange = ModulationInputRange::BIPOLAR;
    return draft;
}

void test_lfo_audition_cancel_is_byte_stable_and_history_free() {
    using namespace core::state::modulation;
    macro::MacroPagesState pages;
    macro::MacroHistoryService history;
    const auto before = pages.control.authored.modulation;
    const uint32_t beforeRevision = pages.control.authoredRevision;

    const auto begun = history.beginLfoModulatorAudition(
        pages,
        kAddress,
        defaultLfoDraft(),
        defaultBindingDraft()
    );
    assert(begun.changed());
    assert(history.modulatorAuditionPending(kAddress));
    assert(pages.control.audition.active);
    assert(history.undoCount() == 0);

    auto* source = findProjectModulator(
        pages.control.authored.modulation,
        begun.sourceId
    );
    assert(source != nullptr);
    source->parameters.lfo.shape = ModulatorLfoShape::TRIANGLE;
    auto& binding = pages.control.authored.modulation.outputBindings[0];
    binding.amountQ15 = -12288;
    pages.control.markAuthoredMutation();

    assert(history.cancelModulatorAudition(pages, kAddress));
    assert(!pages.control.audition.active);
    assert(!history.modulatorAuditionPending(kAddress));
    assert(history.undoCount() == 0);
    assert(pages.control.authoredRevision == beforeRevision);
    assert(std::memcmp(
        &pages.control.authored.modulation,
        &before,
        sizeof(before)
    ) == 0);
    std::cout << "[PASS] LFO audition Cancel is byte-stable and history-free\n";
}

void test_lfo_audition_apply_is_one_compact_undo_redo_action() {
    using namespace core::state::modulation;
    macro::MacroPagesState pages;
    macro::MacroHistoryService history;
    const auto begun = history.beginLfoModulatorAudition(
        pages,
        kAddress,
        defaultLfoDraft(),
        defaultBindingDraft()
    );
    assert(begun.changed());
    auto* source = findProjectModulator(
        pages.control.authored.modulation,
        begun.sourceId
    );
    assert(source != nullptr);
    source->parameters.lfo.shape = ModulatorLfoShape::SAW_DOWN;
    pages.control.authored.modulation.outputBindings[0].amountQ15 = -16384;
    pages.control.markAuthoredMutation();

    assert(history.commitModulatorAudition(pages, kAddress));
    assert(history.undoCount() == 1);
    assert(!history.modulatorAuditionPending(kAddress));
    assert(!pages.control.audition.active);
    const auto committedSource = pages.control.authored.modulation.sources[0];
    const auto committedBinding =
        pages.control.authored.modulation.outputBindings[0];

    assert(history.undo(pages));
    assert(pages.control.authored.modulation.sourceCount == 0);
    assert(pages.control.authored.modulation.outputBindingCount == 0);
    assert(pages.control.authored.modulation.nextSourceId == 1);
    assert(pages.control.authored.modulation.nextBindingId == 1);

    assert(history.redo(pages));
    assert(pages.control.authored.modulation.sourceCount == 1);
    assert(pages.control.authored.modulation.outputBindingCount == 1);
    assert(std::memcmp(
        &pages.control.authored.modulation.sources[0],
        &committedSource,
        sizeof(committedSource)
    ) == 0);
    assert(std::memcmp(
        &pages.control.authored.modulation.outputBindings[0],
        &committedBinding,
        sizeof(committedBinding)
    ) == 0);
    assert(sizeof(macro::MacroHistoryChange) < 512U);
    std::cout << "[PASS] LFO Apply is one compact stable-ID Undo/Redo action\n";
}

void test_lfo_audition_capacity_failure_has_no_partial_state() {
    using namespace core::state::modulation;
    macro::MacroPagesState pages;
    macro::MacroHistoryService history;
    pages.control.authored.modulation.sourceCount = PROJECT_MODULATOR_CAPACITY;
    const auto before = pages.control.authored.modulation;

    const auto result = history.beginLfoModulatorAudition(
        pages,
        kAddress,
        defaultLfoDraft(),
        defaultBindingDraft()
    );
    assert(result.status == ProjectModulationStatus::SOURCE_CAPACITY_EXCEEDED);
    assert(!pages.control.audition.active);
    assert(!history.modulatorAuditionPending(kAddress));
    assert(history.undoCount() == 0);
    assert(std::memcmp(
        &pages.control.authored.modulation,
        &before,
        sizeof(before)
    ) == 0);
    std::cout << "[PASS] LFO capacity failure is an exact no-op\n";
}

}  // namespace

int main() {
    test_snapshot_roundtrip_restores_exact_slot();
    test_clear_is_one_undo_redo_action();
    test_depth_turns_coalesce_without_extra_entries();
    test_stale_live_state_blocks_undo_without_overwrite();
    test_history_admission_rejects_oversized_slot();
    test_history_evicts_oldest_entry_at_fixed_limit();
    test_new_mutation_after_undo_clears_redo_stack();
    test_lfo_audition_cancel_is_byte_stable_and_history_free();
    test_lfo_audition_apply_is_one_compact_undo_redo_action();
    test_lfo_audition_capacity_failure_has_no_partial_state();
    std::cout << "All MacroHistory tests passed\n";
    return 0;
}
