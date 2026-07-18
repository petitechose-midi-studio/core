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
    assert(core::state::modulation::setProjectModulationDestinationScale(
        pages.control.authored.modulation,
        core::state::modulation::projectControlDestination(kAddress),
        49152U
    ).changed());

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
    assert(std::fabs(slot.compatibility.modulationDepth - 0.65f) < 0.0001f);
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
    assert(std::fabs(slot.compatibility.modulationDepth - 0.65f) < 0.0001f);
    assert(history.redo(pages));
    slot = test_support::project_control::readSlot(pages.control, kAddress);
    assert(slot.compatibility.modulationDepth == 0.0f);
    std::cout << "[PASS] Depth turns coalesce to one action\n";
}

void test_global_depth_is_compact_coalesced_and_independent_from_bypass() {
    using namespace core::state::modulation;
    macro::MacroPagesState pages;
    seedCurves(pages);
    macro::MacroHistoryService history;
    const auto destination = projectControlDestination(kAddress);
    const auto bindingId = pages.control.authored.modulation.outputBindings[0].id;

    assert(history.setModulationDestinationScaleCoalesced(
        pages,
        kAddress,
        32768U
    ));
    assert(history.setModulationDestinationScaleCoalesced(
        pages,
        kAddress,
        16384U
    ));
    assert(history.undoCount() == 1U);
    assert(projectModulationDestinationScaleQ15(
        pages.control.authored.modulation,
        destination
    ) == 16384U);
    assert((findProjectModulationBinding(
        pages.control.authored.modulation,
        bindingId
    )->flags & PROJECT_MODULATION_BINDING_FLAG_ENABLED) != 0U);

    assert(history.undo(pages));
    assert(projectModulationDestinationScaleQ15(
        pages.control.authored.modulation,
        destination
    ) == 49152U);
    assert(history.redo(pages));
    assert(projectModulationDestinationScaleQ15(
        pages.control.authored.modulation,
        destination
    ) == 16384U);
    std::cout << "[PASS] Global Depth coalesces independently from bypass\n";
}

void test_assignment_undo_preserves_unrelated_macro_fields() {
    macro::MacroPagesState pages;
    seedCurves(pages);
    macro::MacroHistoryService history;
    assert(history.setModulationDepthCoalesced(pages, kAddress, 0.4f));
    history.endCoalescing();
    pages.pageData(0, 0).cc[1] = 99;
    assert(history.undo(pages));
    assert(pages.pageData(0, 0).cc[1] == 99);
    const auto slot = test_support::project_control::readSlot(pages.control, kAddress);
    assert(std::fabs(slot.compatibility.modulationDepth - 0.65f) < 0.0001f);
    std::cout << "[PASS] assignment Undo preserves unrelated Macro fields\n";
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
    assert(std::fabs(slot.compatibility.modulationDepth - 0.1f) < 0.0001f);
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
    assert(std::fabs(slot.compatibility.modulationDepth - 0.3f) < 0.0001f);
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

core::state::modulation::ModulatorAdsrDraft defaultAdsrDraft() {
    using namespace core::state::modulation;
    ModulatorAdsrDraft draft{};
    draft.name = "ADSR 1";
    draft.reach = {
        .kind = ModulatorReachKind::MACRO,
        .track = kAddress.track,
        .page = kAddress.page,
        .macro = kAddress.macro,
    };
    return draft;
}

core::state::modulation::ModulationTriggerDraft defaultAdsrTrigger() {
    using namespace core::state::modulation;
    ModulationTriggerDraft trigger{};
    trigger.trigger = {
        ModulationTriggerKind::TRACK_NOTE,
        kAddress.track,
        PROJECT_MODULATION_TRIGGER_ANY_CHANNEL,
        PROJECT_MODULATION_TRIGGER_ANY_NOTE,
    };
    return trigger;
}

core::state::modulation::ModulationBindingDraft defaultBindingDraft() {
    using namespace core::state::modulation;
    ModulationBindingDraft draft{};
    draft.destination = projectControlDestination(kAddress);
    draft.amountQ15 = 8192;
    draft.application = ModulationApplication::AROUND_BASE;
    return draft;
}

core::state::modulation::ModulatorId addProjectLfo(
    macro::MacroPagesState& pages,
    const char* name
) {
    using namespace core::state::modulation;
    auto draft = defaultLfoDraft();
    draft.name = name;
    draft.reach = {.kind = ModulatorReachKind::PROJECT};
    const auto result = createLfoModulator(
        pages.control.authored.modulation,
        draft
    );
    assert(result.changed());
    pages.control.markAuthoredMutation();
    return result.sourceId;
}

core::state::modulation::ModulationBindingId addBinding(
    macro::MacroPagesState& pages,
    core::state::modulation::ModulatorId sourceId,
    const macro::MacroAutomationSlotAddress& address,
    int16_t amountQ15,
    bool enabled = true
) {
    using namespace core::state::modulation;
    auto draft = defaultBindingDraft();
    draft.sourceId = sourceId;
    draft.destination = projectControlDestination(address);
    draft.amountQ15 = amountQ15;
    draft.enabled = enabled;
    const auto result = addProjectModulationBinding(
        pages.control.authored.modulation,
        draft
    );
    assert(result.changed());
    pages.control.markAuthoredMutation();
    return result.bindingId;
}

void test_assignment_history_is_destination_scoped_and_order_stable() {
    using namespace core::state::modulation;
    constexpr macro::MacroAutomationSlotAddress kOtherAddress{
        .track = 1,
        .page = 2,
        .macro = 3,
    };
    macro::MacroPagesState pages;
    macro::MacroHistoryService history;
    const auto firstSource = addProjectLfo(pages, "First");
    const auto unrelatedSource = addProjectLfo(pages, "Unrelated");
    const auto secondSource = addProjectLfo(pages, "Second");
    const auto first = addBinding(pages, firstSource, kAddress, 4096);
    const auto unrelated = addBinding(
        pages,
        unrelatedSource,
        kOtherAddress,
        12288
    );
    const auto second = addBinding(pages, secondSource, kAddress, -8192);
    assert(setProjectModulationDestinationScale(
        pages.control.authored.modulation,
        projectControlDestination(kAddress),
        49152U
    ).changed());
    const auto before = pages.control.authored.modulation;

    assert(history.setModulationBindingDepthCoalesced(
        pages,
        kAddress,
        second,
        -0.75f
    ));
    history.endCoalescing();
    const auto afterDepth = pages.control.authored.modulation;
    assert(findProjectModulationBinding(afterDepth, first)->amountQ15 == 4096);
    assert(findProjectModulationBinding(afterDepth, unrelated)->amountQ15 == 12288);
    assert(findProjectModulationBinding(afterDepth, second)->amountQ15 < -24000);
    assert(history.undo(pages));
    assert(std::memcmp(
        &pages.control.authored.modulation,
        &before,
        sizeof(before)
    ) == 0);
    assert(history.redo(pages));
    assert(std::memcmp(
        &pages.control.authored.modulation,
        &afterDepth,
        sizeof(afterDepth)
    ) == 0);

    assert(history.setAllModulationBindingsEnabled(pages, kAddress, false));
    const auto afterBypass = pages.control.authored.modulation;
    assert((findProjectModulationBinding(afterBypass, first)->flags &
            PROJECT_MODULATION_BINDING_FLAG_ENABLED) == 0U);
    assert((findProjectModulationBinding(afterBypass, second)->flags &
            PROJECT_MODULATION_BINDING_FLAG_ENABLED) == 0U);
    assert((findProjectModulationBinding(afterBypass, unrelated)->flags &
            PROJECT_MODULATION_BINDING_FLAG_ENABLED) != 0U);
    assert(history.undo(pages));
    assert(std::memcmp(
        &pages.control.authored.modulation,
        &afterDepth,
        sizeof(afterDepth)
    ) == 0);
    assert(history.redo(pages));
    assert(std::memcmp(
        &pages.control.authored.modulation,
        &afterBypass,
        sizeof(afterBypass)
    ) == 0);
    std::cout << "[PASS] assignment history is destination-scoped and order-stable\n";
}

void test_assignment_remove_and_clear_keep_roots_and_unrelated_edges() {
    using namespace core::state::modulation;
    constexpr macro::MacroAutomationSlotAddress kOtherAddress{
        .track = 1,
        .page = 2,
        .macro = 3,
    };
    macro::MacroPagesState pages;
    macro::MacroHistoryService history;
    const auto firstSource = addProjectLfo(pages, "First");
    const auto unrelatedSource = addProjectLfo(pages, "Unrelated");
    const auto secondSource = addProjectLfo(pages, "Second");
    const auto first = addBinding(pages, firstSource, kAddress, 4096);
    const auto unrelated = addBinding(
        pages,
        unrelatedSource,
        kOtherAddress,
        12288
    );
    const auto second = addBinding(pages, secondSource, kAddress, -8192);
    assert(setProjectModulationDestinationScale(
        pages.control.authored.modulation,
        projectControlDestination(kAddress),
        49152U
    ).changed());
    const auto before = pages.control.authored.modulation;

    assert(history.removeModulationBinding(pages, kAddress, first));
    const auto afterRemove = pages.control.authored.modulation;
    assert(afterRemove.sourceCount == 3U);
    assert(afterRemove.outputBindingCount == 2U);
    assert(afterRemove.outputBindings[0].id == unrelated);
    assert(afterRemove.outputBindings[1].id == second);
    assert(history.undo(pages));
    assert(std::memcmp(
        &pages.control.authored.modulation,
        &before,
        sizeof(before)
    ) == 0);
    assert(history.redo(pages));
    assert(std::memcmp(
        &pages.control.authored.modulation,
        &afterRemove,
        sizeof(afterRemove)
    ) == 0);
    assert(history.undo(pages));

    assert(history.clearModulationBindings(pages, kAddress));
    const auto afterClear = pages.control.authored.modulation;
    assert(afterClear.sourceCount == 3U);
    assert(afterClear.outputBindingCount == 1U);
    assert(afterClear.outputBindings[0].id == unrelated);
    assert(history.undo(pages));
    assert(std::memcmp(
        &pages.control.authored.modulation,
        &before,
        sizeof(before)
    ) == 0);
    assert(history.redo(pages));
    assert(std::memcmp(
        &pages.control.authored.modulation,
        &afterClear,
        sizeof(afterClear)
    ) == 0);
    std::cout << "[PASS] assignment remove/clear retain roots and unrelated edges\n";
}

void test_sparse_macro_removal_purges_all_destination_state_atomically() {
    using namespace core::state::modulation;
    constexpr macro::MacroAutomationSlotAddress kUnrelatedAddress{
        .track = 0,
        .page = 0,
        .macro = 4,
    };
    macro::MacroPagesState pages;
    macro::MacroHistoryService history;
    seedCurves(pages);  // Absolute Automation + first modulation source.
    const auto secondSource = addProjectLfo(pages, "Second");
    const auto unrelatedSource = addProjectLfo(pages, "Unrelated");
    const auto secondBinding = addBinding(
        pages,
        secondSource,
        kAddress,
        -12288
    );
    const auto unrelatedBinding = addBinding(
        pages,
        unrelatedSource,
        kUnrelatedAddress,
        4096
    );
    assert(valid(secondBinding) && valid(unrelatedBinding));
    const auto sourceCountBefore =
        pages.control.authored.modulation.sourceCount;
    auto before = core::app::makeExtmemUnique<ProjectControlDomainState>();
    assert(before);
    *before = pages.control.authored;
    macro::MacroAutomationHistorySnapshot automationBefore{};
    assert(macro::captureMacroAutomationHistorySnapshot(
        pages,
        kAddress,
        automationBefore
    ));
    const auto pageBefore = pages.pageData(0U, 0U);

    assert(history.removeMacroSlot(pages, kAddress));
    assert(history.undoCount() == 1U);
    const auto& removedPage = pages.pageData(0U, 0U);
    assert(!removedPage.isMacroActive(kAddress.macro));
    assert(removedPage.cc[kAddress.macro] ==
           macro::defaultMacroCc(kAddress.page, kAddress.macro));
    assert(removedPage.values[kAddress.macro] == 0.5f);
    const auto removedSlot =
        test_support::project_control::readSlot(pages.control, kAddress);
    assert(!removedSlot.present);
    assert(pages.control.authored.modulation.sourceCount == sourceCountBefore);
    assert(findProjectModulationBinding(
        pages.control.authored.modulation,
        unrelatedBinding
    ) != nullptr);
    assert(findProjectModulationBinding(
        pages.control.authored.modulation,
        secondBinding
    ) == nullptr);
    assert(projectModulationDestinationScaleQ15(
        pages.control.authored.modulation,
        projectControlDestination(kAddress)
    ) == PROJECT_MODULATION_DESTINATION_SCALE_ONE_Q15);
    auto after = core::app::makeExtmemUnique<ProjectControlDomainState>();
    assert(after);
    *after = pages.control.authored;

    assert(history.undo(pages));
    assert(macro::liveMacroAutomationMatchesHistorySnapshot(
        pages,
        automationBefore
    ));
    assert(std::memcmp(
        &pages.control.authored.modulation,
        &before->modulation,
        sizeof(before->modulation)
    ) == 0);
    assert(std::memcmp(
        &pages.pageData(0U, 0U),
        &pageBefore,
        sizeof(pageBefore)
    ) == 0);
    assert(history.redo(pages));
    assert(std::memcmp(
        &pages.control.authored.modulation,
        &after->modulation,
        sizeof(after->modulation)
    ) == 0);
    assert(!test_support::project_control::readSlot(
        pages.control,
        kAddress
    ).present);
    assert(!pages.pageData(0U, 0U).isMacroActive(kAddress.macro));
    std::cout
        << "[PASS] sparse Macro removal purges every destination-owned value\n";
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
    const auto pageBefore = pages.pageData(0, 0);

    const auto result = history.beginLfoModulatorAudition(
        pages,
        kAddress,
        defaultLfoDraft(),
        defaultBindingDraft(),
        true
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
    assert(std::memcmp(
        &pages.pageData(0, 0),
        &pageBefore,
        sizeof(pageBefore)
    ) == 0);
    std::cout << "[PASS] LFO capacity failure is an exact no-op\n";
}

void test_macro_create_audition_cancel_restores_page_graph_and_ids() {
    using namespace core::state::modulation;
    macro::MacroPagesState pages;
    macro::MacroHistoryService history;
    const auto pageBefore = pages.pageData(0, 0);
    const auto graphBefore = pages.control.authored.modulation;
    const uint32_t revisionBefore = pages.control.authoredRevision;

    const auto begun = history.beginLfoModulatorAudition(
        pages,
        kAddress,
        defaultLfoDraft(),
        defaultBindingDraft(),
        true
    );
    assert(begun.changed());
    assert(pages.pageData(0, 0).isMacroActive(kAddress.macro));
    assert(pages.pageData(0, 0).cc[kAddress.macro] == 1U);
    assert(pages.pageData(0, 0).values[kAddress.macro] == 0.5f);
    assert(history.undoCount() == 0U);

    assert(history.cancelModulatorAudition(pages, kAddress));
    assert(std::memcmp(
        &pages.pageData(0, 0),
        &pageBefore,
        sizeof(pageBefore)
    ) == 0);
    assert(std::memcmp(
        &pages.control.authored.modulation,
        &graphBefore,
        sizeof(graphBefore)
    ) == 0);
    assert(pages.control.authoredRevision == revisionBefore);
    assert(history.undoCount() == 0U);
    std::cout << "[PASS] Macro-create audition Cancel restores page, graph, and IDs\n";
}

void test_macro_create_and_assignment_are_one_undo_redo_action() {
    macro::MacroPagesState pages;
    macro::MacroHistoryService history;
    const auto pageBefore = pages.pageData(0, 0);
    const auto begun = history.beginLfoModulatorAudition(
        pages,
        kAddress,
        defaultLfoDraft(),
        defaultBindingDraft(),
        true
    );
    assert(begun.changed());
    assert(history.commitModulatorAudition(pages, kAddress));
    assert(history.undoCount() == 1U);
    const auto pageAfter = pages.pageData(0, 0);
    const auto graphAfter = pages.control.authored.modulation;

    assert(history.undo(pages));
    assert(std::memcmp(
        &pages.pageData(0, 0),
        &pageBefore,
        sizeof(pageBefore)
    ) == 0);
    assert(pages.control.authored.modulation.sourceCount == 0U);
    assert(pages.control.authored.modulation.outputBindingCount == 0U);
    assert(pages.control.authored.modulation.nextSourceId == 1U);
    assert(pages.control.authored.modulation.nextBindingId == 1U);

    assert(history.redo(pages));
    assert(std::memcmp(
        &pages.pageData(0, 0),
        &pageAfter,
        sizeof(pageAfter)
    ) == 0);
    assert(std::memcmp(
        &pages.control.authored.modulation,
        &graphAfter,
        sizeof(graphAfter)
    ) == 0);
    std::cout << "[PASS] Macro creation plus assignment is one Undo/Redo action\n";
}

void test_missing_track_page_and_sparse_macro_commit_atomically() {
    using namespace core::state::modulation;
    macro::MacroPagesState pages;
    macro::MacroHistoryService history;
    constexpr macro::MacroAutomationSlotAddress address{1U, 0U, 5U};
    const auto plan = macro::MacroWorkflow::planDestinationActivation(
        pages,
        address
    );
    assert(plan.valid && plan.createTrack && plan.createPage &&
           plan.createMacro);
    auto binding = defaultBindingDraft();
    binding.destination = projectControlDestination(address);
    const auto trackBefore = pages.tracks[address.track];
    const auto graphBefore = pages.control.authored.modulation;

    auto begun = history.beginLfoModulatorAudition(
        pages,
        address,
        defaultLfoDraft(),
        binding,
        false,
        &plan
    );
    assert(begun.changed());
    assert(pages.currentTrackEnabledMask() == 0x0001U);
    assert(std::memcmp(
        &pages.tracks[address.track],
        &trackBefore,
        sizeof(trackBefore)
    ) == 0);
    assert(history.cancelModulatorAudition(pages, address));
    assert(pages.currentTrackEnabledMask() == 0x0001U);
    assert(std::memcmp(
        &pages.control.authored.modulation,
        &graphBefore,
        sizeof(graphBefore)
    ) == 0);

    begun = history.beginLfoModulatorAudition(
        pages,
        address,
        defaultLfoDraft(),
        binding,
        false,
        &plan
    );
    assert(begun.changed());
    assert(history.commitModulatorAudition(pages, address));
    assert(history.undoCount() == 1U);
    assert(pages.currentTrackEnabledMask() == 0x0003U);
    assert(pages.pageData(1U, 0U).activeMacroMask == 0x20U);
    assert(pages.pageData(1U, 0U).cc[5] == 5U);
    assert(pages.currentActiveTrack() == 0U);
    const auto trackAfter = pages.tracks[address.track];
    const auto graphAfter = pages.control.authored.modulation;

    assert(history.undo(pages));
    assert(pages.currentTrackEnabledMask() == 0x0001U);
    assert(std::memcmp(
        &pages.tracks[address.track],
        &trackBefore,
        sizeof(trackBefore)
    ) == 0);
    assert(std::memcmp(
        &pages.control.authored.modulation,
        &graphBefore,
        sizeof(graphBefore)
    ) == 0);
    assert(history.redo(pages));
    assert(pages.currentTrackEnabledMask() == 0x0003U);
    assert(std::memcmp(
        &pages.tracks[address.track],
        &trackAfter,
        sizeof(trackAfter)
    ) == 0);
    assert(std::memcmp(
        &pages.control.authored.modulation,
        &graphAfter,
        sizeof(graphAfter)
    ) == 0);
    std::cout << "[PASS] missing Track/Page/sparse Macro is one exact action\n";
}

void test_macro_create_duplicate_and_capacity_failures_are_exact_noops() {
    using namespace core::state::modulation;
    macro::MacroPagesState pages;
    macro::MacroHistoryService history;
    const auto source = createLfoModulator(
        pages.control.authored.modulation,
        defaultLfoDraft()
    );
    assert(source.changed());
    auto binding = defaultBindingDraft();
    binding.sourceId = source.sourceId;
    assert(addProjectModulationBinding(
        pages.control.authored.modulation,
        binding
    ).changed());
    const auto duplicatePageBefore = pages.pageData(0, 0);
    const auto duplicateGraphBefore = pages.control.authored.modulation;

    const auto duplicate = history.beginExistingModulatorAudition(
        pages,
        kAddress,
        source.sourceId,
        binding,
        nullptr,
        true
    );
    assert(duplicate.status == ProjectModulationStatus::DUPLICATE_BINDING);
    assert(std::memcmp(
        &pages.pageData(0, 0),
        &duplicatePageBefore,
        sizeof(duplicatePageBefore)
    ) == 0);
    assert(std::memcmp(
        &pages.control.authored.modulation,
        &duplicateGraphBefore,
        sizeof(duplicateGraphBefore)
    ) == 0);

    macro::MacroPagesState fullPages;
    macro::MacroHistoryService fullHistory;
    fullPages.control.authored.modulation.outputBindingCount =
        PROJECT_MODULATION_BINDING_CAPACITY;
    const auto fullPageBefore = fullPages.pageData(0, 0);
    const auto fullGraphBefore = fullPages.control.authored.modulation;
    const auto full = fullHistory.beginLfoModulatorAudition(
        fullPages,
        kAddress,
        defaultLfoDraft(),
        defaultBindingDraft(),
        true
    );
    assert(full.status == ProjectModulationStatus::BINDING_CAPACITY_EXCEEDED);
    assert(std::memcmp(
        &fullPages.pageData(0, 0),
        &fullPageBefore,
        sizeof(fullPageBefore)
    ) == 0);
    assert(std::memcmp(
        &fullPages.control.authored.modulation,
        &fullGraphBefore,
        sizeof(fullGraphBefore)
    ) == 0);
    std::cout << "[PASS] Macro-create duplicate/capacity failures are exact no-ops\n";
}

void test_macro_create_stale_add_slot_is_rejected_without_history() {
    using namespace core::state::modulation;
    macro::MacroPagesState pages;
    macro::MacroHistoryService history;
    pages.pageData(0, 0).setMacroActive(kAddress.macro, true);
    const auto pageBefore = pages.pageData(0, 0);
    const auto graphBefore = pages.control.authored.modulation;
    const auto result = history.beginLfoModulatorAudition(
        pages,
        kAddress,
        defaultLfoDraft(),
        defaultBindingDraft(),
        true
    );
    assert(result.status == ProjectModulationStatus::INVALID_ARGUMENT);
    assert(history.undoCount() == 0U);
    assert(!pages.control.audition.active);
    assert(std::memcmp(
        &pages.pageData(0, 0),
        &pageBefore,
        sizeof(pageBefore)
    ) == 0);
    assert(std::memcmp(
        &pages.control.authored.modulation,
        &graphBefore,
        sizeof(graphBefore)
    ) == 0);
    std::cout << "[PASS] stale Macro add-slot selection is an exact rejection\n";
}

void test_macro_create_widening_cancel_and_failed_commit_are_exact() {
    using namespace core::state::modulation;
    macro::MacroPagesState pages;
    macro::MacroHistoryService history;
    auto draft = defaultLfoDraft();
    draft.reach = {};
    const auto source = createLfoModulator(
        pages.control.authored.modulation,
        draft
    );
    assert(source.changed());
    const auto pageBefore = pages.pageData(0, 0);
    const auto graphBefore = pages.control.authored.modulation;
    const ModulatorReach widened{
        .kind = ModulatorReachKind::MACRO,
        .track = kAddress.track,
        .page = kAddress.page,
        .macro = kAddress.macro,
    };
    const auto begun = history.beginExistingModulatorAudition(
        pages,
        kAddress,
        source.sourceId,
        defaultBindingDraft(),
        &widened,
        true
    );
    assert(begun.changed());
    assert(pages.pageData(0, 0).isMacroActive(kAddress.macro));
    assert(isProjectModulatorGlobalReach(
        pages.control.authored.modulation.sources[0].reach
    ));

    constexpr macro::MacroAutomationSlotAddress wrongAddress{0, 0, 2};
    assert(!history.commitModulatorAudition(pages, wrongAddress));
    assert(history.modulatorAuditionPending(kAddress));
    assert(history.cancelModulatorAudition(pages, kAddress));
    assert(std::memcmp(
        &pages.pageData(0, 0),
        &pageBefore,
        sizeof(pageBefore)
    ) == 0);
    assert(std::memcmp(
        &pages.control.authored.modulation,
        &graphBefore,
        sizeof(graphBefore)
    ) == 0);
    assert(history.undoCount() == 0U);
    std::cout << "[PASS] widening Cancel after rejected commit is exact\n";
}

void test_existing_modulator_cancel_preserves_root_and_is_byte_stable() {
    using namespace core::state::modulation;
    macro::MacroPagesState pages;
    macro::MacroHistoryService history;
    const auto created = createLfoModulator(
        pages.control.authored.modulation,
        defaultLfoDraft()
    );
    assert(created.changed());
    const auto before = pages.control.authored.modulation;
    const uint32_t beforeRevision = pages.control.authoredRevision;

    const auto begun = history.beginExistingModulatorAudition(
        pages,
        kAddress,
        created.sourceId,
        defaultBindingDraft()
    );
    assert(begun.changed());
    assert(!pages.control.audition.sourceCreated);
    assert(pages.control.authored.modulation.sourceCount == 1U);
    pages.control.authored.modulation.outputBindings[0].amountQ15 = -8192;
    pages.control.markAuthoredMutation();

    assert(history.cancelModulatorAudition(pages, kAddress));
    assert(history.undoCount() == 0U);
    assert(pages.control.authoredRevision == beforeRevision);
    assert(std::memcmp(
        &pages.control.authored.modulation,
        &before,
        sizeof(before)
    ) == 0);
    std::cout << "[PASS] existing-source Cancel preserves root byte-for-byte\n";
}

void test_existing_modulator_apply_undo_redo_moves_only_binding() {
    using namespace core::state::modulation;
    macro::MacroPagesState pages;
    macro::MacroHistoryService history;
    const auto created = createLfoModulator(
        pages.control.authored.modulation,
        defaultLfoDraft()
    );
    assert(created.changed());
    const auto root = pages.control.authored.modulation.sources[0];

    const auto begun = history.beginExistingModulatorAudition(
        pages,
        kAddress,
        created.sourceId,
        defaultBindingDraft()
    );
    assert(begun.changed());
    pages.control.authored.modulation.outputBindings[0].amountQ15 = -12288;
    pages.control.markAuthoredMutation();
    assert(history.commitModulatorAudition(pages, kAddress));
    assert(history.undoCount() == 1U);
    const auto committedBinding =
        pages.control.authored.modulation.outputBindings[0];

    assert(history.undo(pages));
    assert(pages.control.authored.modulation.sourceCount == 1U);
    assert(pages.control.authored.modulation.outputBindingCount == 0U);
    assert(std::memcmp(
        &pages.control.authored.modulation.sources[0],
        &root,
        sizeof(root)
    ) == 0);

    assert(history.redo(pages));
    assert(pages.control.authored.modulation.sourceCount == 1U);
    assert(pages.control.authored.modulation.outputBindingCount == 1U);
    assert(std::memcmp(
        &pages.control.authored.modulation.sources[0],
        &root,
        sizeof(root)
    ) == 0);
    assert(std::memcmp(
        &pages.control.authored.modulation.outputBindings[0],
        &committedBinding,
        sizeof(committedBinding)
    ) == 0);
    std::cout << "[PASS] existing-source Apply Undo/Redo changes only the edge\n";
}

void test_project_source_edits_coalesce_and_restore_exact_source() {
    using namespace core::state::modulation;
    macro::MacroPagesState pages;
    macro::MacroHistoryService history;
    const auto created = createLfoModulator(
        pages.control.authored.modulation,
        defaultLfoDraft()
    );
    assert(created.changed());
    const auto original = pages.control.authored.modulation.sources[0];

    auto parameters = original.parameters.lfo;
    parameters.periodTicks = 192;
    assert(history.setProjectLfoParametersCoalesced(
        pages, created.sourceId, parameters
    ));
    parameters.periodTicks = 96;
    assert(history.setProjectLfoParametersCoalesced(
        pages, created.sourceId, parameters
    ));
    assert(history.undoCount() == 1U);
    history.endCoalescing();

    assert(history.setProjectModulatorEnabled(
        pages, created.sourceId, false
    ));
    assert(history.undoCount() == 2U);
    assert(history.undo(pages));
    const auto* source = findProjectModulator(
        pages.control.authored.modulation,
        created.sourceId
    );
    assert(source != nullptr);
    assert((source->flags & PROJECT_MODULATOR_FLAG_ENABLED) != 0U);
    assert(source->parameters.lfo.periodTicks == 96U);

    assert(history.undo(pages));
    source = findProjectModulator(
        pages.control.authored.modulation,
        created.sourceId
    );
    assert(source != nullptr);
    assert(std::memcmp(source, &original, sizeof(original)) == 0);
    assert(history.redo(pages));
    assert(history.redo(pages));
    source = findProjectModulator(
        pages.control.authored.modulation,
        created.sourceId
    );
    assert(source != nullptr);
    assert(source->parameters.lfo.periodTicks == 96U);
    assert((source->flags & PROJECT_MODULATOR_FLAG_ENABLED) == 0U);
    std::cout << "[PASS] Project source edits coalesce and Undo exactly\n";
}

void test_project_source_rename_is_one_exact_undo_action() {
    using namespace core::state::modulation;
    macro::MacroPagesState pages;
    macro::MacroHistoryService history;
    const auto sourceId = addProjectLfo(pages, "Original");
    auto binding = defaultBindingDraft();
    binding.sourceId = sourceId;
    assert(addProjectModulationBinding(
        pages.control.authored.modulation,
        binding
    ).changed());
    const auto graphBefore = pages.control.authored.modulation;

    assert(history.setProjectModulatorName(
        pages,
        sourceId,
        "Shared Motion"
    ));
    assert(history.undoCount() == 1U);
    assert(std::strcmp(
        pages.control.authored.modulation.sources[0].name.data(),
        "Shared Motion"
    ) == 0);
    assert(pages.control.authored.modulation.outputBindings[0].sourceId ==
           sourceId);
    const auto graphAfter = pages.control.authored.modulation;

    assert(history.undo(pages));
    assert(std::memcmp(
        &pages.control.authored.modulation,
        &graphBefore,
        sizeof(graphBefore)
    ) == 0);
    assert(history.redo(pages));
    assert(std::memcmp(
        &pages.control.authored.modulation,
        &graphAfter,
        sizeof(graphAfter)
    ) == 0);
    std::cout << "[PASS] Project source rename is one exact Undo action\n";
}

void test_unassigned_lfo_creation_is_one_undo_action() {
    using namespace core::state::modulation;
    macro::MacroPagesState pages;
    macro::MacroHistoryService history;
    auto draft = defaultLfoDraft();
    draft.reach = {};
    const auto created = history.createUnassignedLfo(pages, draft);
    assert(created.changed());
    assert(pages.control.authored.modulation.sourceCount == 1U);
    assert(pages.control.authored.modulation.outputBindingCount == 0U);
    assert(history.undoCount() == 1U);
    assert(history.undo(pages));
    assert(pages.control.authored.modulation.sourceCount == 0U);
    assert(pages.control.authored.modulation.outputBindingCount == 0U);
    assert(history.redo(pages));
    assert(pages.control.authored.modulation.sourceCount == 1U);
    assert(pages.control.authored.modulation.sources[0].id == created.sourceId);
    assert(pages.control.authored.modulation.outputBindingCount == 0U);
    std::cout << "[PASS] explicit Unassigned LFO creation is one Undo action\n";
}

void test_unassigned_adsr_creation_includes_trigger_in_one_undo_action() {
    using namespace core::state::modulation;
    macro::MacroPagesState pages;
    macro::MacroHistoryService history;
    auto source = defaultAdsrDraft();
    source.reach = {};
    const auto graphBefore = pages.control.authored.modulation;
    const auto created = history.createUnassignedAdsr(
        pages,
        source,
        defaultAdsrTrigger()
    );
    assert(created.changed());
    assert(history.undoCount() == 1U);
    const auto graphAfter = pages.control.authored.modulation;
    assert(graphAfter.sourceCount == 1U);
    assert(graphAfter.outputBindingCount == 0U);
    assert(graphAfter.triggerBindingCount == 1U);
    assert(graphAfter.triggerBindings[0].sourceId == created.sourceId);
    assert(graphAfter.triggerBindings[0].trigger.kind ==
           ModulationTriggerKind::TRACK_NOTE);

    assert(history.undo(pages));
    assert(std::memcmp(
        &pages.control.authored.modulation,
        &graphBefore,
        sizeof(graphBefore)
    ) == 0);
    assert(history.redo(pages));
    assert(std::memcmp(
        &pages.control.authored.modulation,
        &graphAfter,
        sizeof(graphAfter)
    ) == 0);
    std::cout << "[PASS] Unassigned ADSR and trigger share one Undo action\n";
}

void test_adsr_audition_cancel_and_apply_are_atomic() {
    using namespace core::state::modulation;
    macro::MacroPagesState pages;
    macro::MacroHistoryService history;
    auto binding = defaultBindingDraft();
    binding.application = ModulationApplication::NATURAL;
    const auto graphBefore = pages.control.authored.modulation;

    auto invalidTrigger = defaultAdsrTrigger();
    invalidTrigger.trigger.track = PROJECT_MODULATION_TRACK_COUNT;
    const auto rejected = history.beginAdsrModulatorAudition(
        pages,
        kAddress,
        defaultAdsrDraft(),
        invalidTrigger,
        binding
    );
    assert(rejected.status == ProjectModulationStatus::INVALID_ARGUMENT);
    assert(!pages.control.audition.active && history.undoCount() == 0U);
    assert(std::memcmp(
        &pages.control.authored.modulation,
        &graphBefore,
        sizeof(graphBefore)
    ) == 0);

    auto begun = history.beginAdsrModulatorAudition(
        pages,
        kAddress,
        defaultAdsrDraft(),
        defaultAdsrTrigger(),
        binding
    );
    assert(begun.changed());
    assert(pages.control.authored.modulation.sourceCount == 1U);
    assert(pages.control.authored.modulation.triggerBindingCount == 1U);
    assert(pages.control.authored.modulation.outputBindingCount == 1U);
    assert(history.cancelModulatorAudition(pages, kAddress));
    assert(history.undoCount() == 0U);
    assert(std::memcmp(
        &pages.control.authored.modulation,
        &graphBefore,
        sizeof(graphBefore)
    ) == 0);

    begun = history.beginAdsrModulatorAudition(
        pages,
        kAddress,
        defaultAdsrDraft(),
        defaultAdsrTrigger(),
        binding
    );
    assert(begun.changed());
    assert(history.commitModulatorAudition(pages, kAddress));
    assert(history.undoCount() == 1U);
    const auto graphAfter = pages.control.authored.modulation;
    assert(graphAfter.triggerBindings[0].sourceId == begun.sourceId);
    assert(graphAfter.outputBindings[0].sourceId == begun.sourceId);

    assert(history.undo(pages));
    assert(std::memcmp(
        &pages.control.authored.modulation,
        &graphBefore,
        sizeof(graphBefore)
    ) == 0);
    assert(history.redo(pages));
    assert(std::memcmp(
        &pages.control.authored.modulation,
        &graphAfter,
        sizeof(graphAfter)
    ) == 0);
    std::cout << "[PASS] ADSR audition owns source, trigger, and destination\n";
}

void test_adsr_parameters_and_trigger_route_coalesce_by_stable_identity() {
    using namespace core::state::modulation;
    macro::MacroPagesState pages;
    macro::MacroHistoryService history;
    const auto created = createAdsrModulator(
        pages.control.authored.modulation,
        defaultAdsrDraft()
    );
    assert(created.changed());
    auto triggerDraft = defaultAdsrTrigger();
    triggerDraft.sourceId = created.sourceId;
    assert(addProjectModulationTrigger(
        pages.control.authored.modulation,
        triggerDraft
    ).changed());
    const auto graphBefore = pages.control.authored.modulation;
    const auto triggerId = graphBefore.triggerBindings[0].id;

    auto parameters = graphBefore.sources[0].parameters.adsr;
    parameters.attack = 32U;
    assert(history.setProjectAdsrParametersCoalesced(
        pages,
        created.sourceId,
        parameters
    ));
    parameters.attack = 64U;
    assert(history.setProjectAdsrParametersCoalesced(
        pages,
        created.sourceId,
        parameters
    ));
    assert(history.undoCount() == 1U);
    history.endCoalescing();
    const auto graphAfterParameters = pages.control.authored.modulation;

    auto route = graphAfterParameters.triggerBindings[0].trigger;
    route.track = 1U;
    assert(history.setProjectModulationTriggerCoalesced(
        pages,
        created.sourceId,
        route,
        true
    ));
    route.track = 2U;
    assert(history.setProjectModulationTriggerCoalesced(
        pages,
        created.sourceId,
        route,
        false
    ));
    assert(history.undoCount() == 2U);
    assert(pages.control.authored.modulation.triggerBindings[0].id == triggerId);
    const auto graphAfter = pages.control.authored.modulation;

    assert(history.undo(pages));
    assert(std::memcmp(
        &pages.control.authored.modulation,
        &graphAfterParameters,
        sizeof(graphAfterParameters)
    ) == 0);
    assert(history.undo(pages));
    assert(std::memcmp(
        &pages.control.authored.modulation,
        &graphBefore,
        sizeof(graphBefore)
    ) == 0);
    assert(history.redo(pages));
    assert(history.redo(pages));
    assert(std::memcmp(
        &pages.control.authored.modulation,
        &graphAfter,
        sizeof(graphAfter)
    ) == 0);
    std::cout << "[PASS] ADSR and trigger gestures coalesce by stable ID\n";
}

void test_adsr_duplicate_copies_trigger_and_undo_is_exact() {
    using namespace core::state::modulation;
    macro::MacroPagesState pages;
    macro::MacroHistoryService history;
    const auto created = createAdsrModulator(
        pages.control.authored.modulation,
        defaultAdsrDraft()
    );
    assert(created.changed());
    auto trigger = defaultAdsrTrigger();
    trigger.sourceId = created.sourceId;
    assert(addProjectModulationTrigger(
        pages.control.authored.modulation,
        trigger
    ).changed());
    const auto graphBefore = pages.control.authored.modulation;

    const auto duplicated = history.duplicateProjectModulator(
        pages,
        created.sourceId,
        "ADSR 2"
    );
    assert(duplicated.changed());
    const auto graphAfter = pages.control.authored.modulation;
    assert(graphAfter.sourceCount == 2U);
    assert(graphAfter.triggerBindingCount == 2U);
    assert(graphAfter.triggerBindings[0].id != graphAfter.triggerBindings[1].id);
    assert(graphAfter.triggerBindings[1].sourceId == duplicated.sourceId);
    assert(graphAfter.triggerBindings[0].trigger ==
           graphAfter.triggerBindings[1].trigger);

    assert(history.undo(pages));
    assert(std::memcmp(
        &pages.control.authored.modulation,
        &graphBefore,
        sizeof(graphBefore)
    ) == 0);
    assert(history.redo(pages));
    assert(std::memcmp(
        &pages.control.authored.modulation,
        &graphAfter,
        sizeof(graphAfter)
    ) == 0);
    std::cout << "[PASS] ADSR duplicate carries its trigger route exactly\n";
}

void test_existing_global_assignment_cancel_and_undo_are_exact() {
    using namespace core::state::modulation;
    macro::MacroPagesState pages;
    macro::MacroHistoryService history;
    const auto created = createLfoModulator(
        pages.control.authored.modulation,
        defaultLfoDraft()
    );
    assert(created.changed());
    const auto original = pages.control.authored.modulation.sources[0];
    constexpr macro::MacroAutomationSlotAddress other{
        .track = 3,
        .page = 0,
        .macro = 2,
    };
    auto binding = defaultBindingDraft();
    binding.destination = projectControlDestination(other);
    const ModulatorReach widened{
        .trackMask = static_cast<uint16_t>((1U << 0U) | (1U << 3U)),
        .kind = ModulatorReachKind::TRACK_SET,
    };

    auto begun = history.beginExistingModulatorAudition(
        pages, other, created.sourceId, binding, &widened
    );
    assert(begun.changed());
    assert(history.cancelModulatorAudition(pages, other));
    assert(std::memcmp(
        &pages.control.authored.modulation.sources[0],
        &original,
        sizeof(original)
    ) == 0);
    assert(pages.control.authored.modulation.outputBindingCount == 0U);

    begun = history.beginExistingModulatorAudition(
        pages, other, created.sourceId, binding, &widened
    );
    assert(begun.changed());
    assert(history.commitModulatorAudition(pages, other));
    assert(isProjectModulatorGlobalReach(
        pages.control.authored.modulation.sources[0].reach
    ));
    assert(history.undo(pages));
    assert(std::memcmp(
        &pages.control.authored.modulation.sources[0],
        &original,
        sizeof(original)
    ) == 0);
    assert(pages.control.authored.modulation.outputBindingCount == 0U);
    assert(history.redo(pages));
    assert(isProjectModulatorGlobalReach(
        pages.control.authored.modulation.sources[0].reach
    ));
    assert(pages.control.authored.modulation.outputBindingCount == 1U);
    std::cout << "[PASS] Global assignment Cancel and Undo restore the root\n";
}

void test_project_modulator_split_is_one_exact_undo_action() {
    using namespace core::state::modulation;
    macro::MacroPagesState pages;
    macro::MacroHistoryService history;
    constexpr std::array<ProjectPackedCurvePoint, 3> points{{
        {0, -12000},
        {192, 8000},
        {384, -4000},
    }};
    RecordedShapeDraft sourceDraft{};
    sourceDraft.name = "Slow Tide";
    sourceDraft.reach = {.kind = ModulatorReachKind::PROJECT};
    sourceDraft.curve = {
        .sourceDurationTicks = 384,
        .durationTicks = 384,
        .valueDomain = ProjectCurveValueDomain::BIPOLAR,
    };
    sourceDraft.points = points.data();
    sourceDraft.pointCount = static_cast<uint16_t>(points.size());
    const auto created = createRecordedShapeModulator(
        pages.control.authored.modulation,
        pages.control.authored.curves,
        sourceDraft
    );
    assert(created.changed());

    constexpr macro::MacroAutomationSlotAddress movedAddress{
        .track = 1,
        .page = 0,
        .macro = 2,
    };
    const auto retainedBinding = addBinding(
        pages,
        created.sourceId,
        kAddress,
        8192
    );
    const auto movedBinding = addBinding(
        pages,
        created.sourceId,
        movedAddress,
        -4096
    );
    (void)retainedBinding;
    ModulationTriggerDraft trigger{};
    trigger.sourceId = created.sourceId;
    assert(addProjectModulationTrigger(
        pages.control.authored.modulation,
        trigger
    ).changed());

    const auto graphBefore = pages.control.authored.modulation;
    const auto arenaBefore = pages.control.authored.curves;
    const ModulatorSplitRequest request{
        .sourceId = created.sourceId,
        .cloneName = "Slow Tide T2",
        .retainedReach = {
            .kind = ModulatorReachKind::MACRO,
            .track = kAddress.track,
            .page = kAddress.page,
            .macro = kAddress.macro,
        },
        .cloneReach = {
            .kind = ModulatorReachKind::MACRO,
            .track = movedAddress.track,
            .page = movedAddress.page,
            .macro = movedAddress.macro,
        },
        .bindingIdsToMove = &movedBinding,
        .bindingCountToMove = 1,
    };
    const auto split = history.splitProjectModulator(pages, request);
    assert(split.changed());
    assert(history.undoCount() == 1U);
    const auto graphAfter = pages.control.authored.modulation;
    const auto arenaAfter = pages.control.authored.curves;
    assert(graphAfter.sourceCount == 2U);
    assert(graphAfter.triggerBindingCount == 2U);
    assert(graphAfter.outputBindings[1].sourceId == split.sourceId);
    assert(arenaAfter.recordCount == 1U);
    assert(arenaAfter.records[0].referenceCount == 2U);

    assert(history.undo(pages));
    assert(std::memcmp(
        &pages.control.authored.modulation,
        &graphBefore,
        sizeof(graphBefore)
    ) == 0);
    assert(std::memcmp(
        &pages.control.authored.curves,
        &arenaBefore,
        sizeof(arenaBefore)
    ) == 0);
    assert(history.redo(pages));
    assert(std::memcmp(
        &pages.control.authored.modulation,
        &graphAfter,
        sizeof(graphAfter)
    ) == 0);
    assert(std::memcmp(
        &pages.control.authored.curves,
        &arenaAfter,
        sizeof(arenaAfter)
    ) == 0);
    std::cout << "[PASS] Project source Split is one exact Undo/Redo action\n";
}

void test_root_delete_undo_restores_graph_and_recorded_curve_exactly() {
    using namespace core::state::modulation;
    macro::MacroPagesState pages;
    macro::MacroHistoryService history;
    constexpr std::array<ProjectPackedCurvePoint, 3> points{{
        {0, -12000},
        {192, 8000},
        {384, -4000},
    }};
    RecordedShapeDraft draft{};
    draft.name = "Motion 1";
    draft.reach = defaultLfoDraft().reach;
    draft.curve = {
        .sourceDurationTicks = 384,
        .durationTicks = 384,
        .valueDomain = ProjectCurveValueDomain::BIPOLAR,
    };
    draft.points = points.data();
    draft.pointCount = static_cast<uint16_t>(points.size());
    const auto created = createRecordedShapeModulator(
        pages.control.authored.modulation,
        pages.control.authored.curves,
        draft
    );
    assert(created.changed());
    auto binding = defaultBindingDraft();
    binding.sourceId = created.sourceId;
    assert(addProjectModulationBinding(
        pages.control.authored.modulation,
        binding
    ).changed());
    assert(setProjectModulationDestinationScale(
        pages.control.authored.modulation,
        binding.destination,
        49152U
    ).changed());
    const auto graphBefore = pages.control.authored.modulation;
    const auto arenaBefore = pages.control.authored.curves;

    const auto removed = history.deleteProjectModulator(
        pages,
        created.sourceId
    );
    assert(removed.changed());
    assert(pages.control.authored.modulation.sourceCount == 0U);
    assert(pages.control.authored.modulation.outputBindingCount == 0U);
    assert(pages.control.authored.curves.recordCount == 0U);
    assert(history.undo(pages));
    assert(std::memcmp(
        &pages.control.authored.modulation,
        &graphBefore,
        sizeof(graphBefore)
    ) == 0);
    assert(std::memcmp(
        &pages.control.authored.curves,
        &arenaBefore,
        sizeof(arenaBefore)
    ) == 0);
    assert(history.redo(pages));
    assert(pages.control.authored.modulation.sourceCount == 0U);
    assert(pages.control.authored.curves.recordCount == 0U);
    std::cout << "[PASS] root delete Undo restores graph and curve exactly\n";
}

void test_root_delete_undo_restores_shared_curve_reference() {
    using namespace core::state::modulation;
    macro::MacroPagesState pages;
    macro::MacroHistoryService history;
    constexpr std::array<ProjectPackedCurvePoint, 2> points{{
        {0, -1000},
        {384, 1000},
    }};
    RecordedShapeDraft draft{};
    draft.name = "Motion 1";
    draft.reach = defaultLfoDraft().reach;
    draft.curve = {
        .sourceDurationTicks = 384,
        .durationTicks = 384,
        .valueDomain = ProjectCurveValueDomain::BIPOLAR,
    };
    draft.points = points.data();
    draft.pointCount = static_cast<uint16_t>(points.size());
    const auto created = createRecordedShapeModulator(
        pages.control.authored.modulation,
        pages.control.authored.curves,
        draft
    );
    assert(created.changed());
    const auto clone = duplicateProjectModulator(
        pages.control.authored.modulation,
        pages.control.authored.curves,
        created.sourceId,
        "Motion 2"
    );
    assert(clone.changed());
    const auto graphBefore = pages.control.authored.modulation;
    const auto arenaBefore = pages.control.authored.curves;
    assert(history.deleteProjectModulator(pages, created.sourceId).changed());
    assert(pages.control.authored.curves.records[0].referenceCount == 1U);
    assert(history.undo(pages));
    assert(std::memcmp(
        &pages.control.authored.modulation,
        &graphBefore,
        sizeof(graphBefore)
    ) == 0);
    assert(std::memcmp(
        &pages.control.authored.curves,
        &arenaBefore,
        sizeof(arenaBefore)
    ) == 0);
    std::cout << "[PASS] shared curve reference is exact across delete Undo\n";
}

void test_recorded_source_duplicate_undo_restores_shared_reference() {
    using namespace core::state::modulation;
    macro::MacroPagesState pages;
    macro::MacroHistoryService history;
    constexpr std::array<ProjectPackedCurvePoint, 2> points{{
        {0, -5000},
        {384, 5000},
    }};
    RecordedShapeDraft draft{};
    draft.name = "Motion 1";
    draft.reach = defaultLfoDraft().reach;
    draft.curve = {
        .sourceDurationTicks = 384,
        .durationTicks = 384,
        .valueDomain = ProjectCurveValueDomain::BIPOLAR,
    };
    draft.points = points.data();
    draft.pointCount = static_cast<uint16_t>(points.size());
    const auto created = createRecordedShapeModulator(
        pages.control.authored.modulation,
        pages.control.authored.curves,
        draft
    );
    assert(created.changed());
    const auto graphBefore = pages.control.authored.modulation;
    const auto arenaBefore = pages.control.authored.curves;
    const auto duplicate = history.duplicateProjectModulator(
        pages,
        created.sourceId,
        "Motion 2"
    );
    assert(duplicate.changed());
    const auto graphAfter = pages.control.authored.modulation;
    const auto arenaAfter = pages.control.authored.curves;
    assert(arenaAfter.records[0].referenceCount == 2U);
    assert(history.undo(pages));
    assert(std::memcmp(
        &pages.control.authored.modulation,
        &graphBefore,
        sizeof(graphBefore)
    ) == 0);
    assert(std::memcmp(
        &pages.control.authored.curves,
        &arenaBefore,
        sizeof(arenaBefore)
    ) == 0);
    assert(history.redo(pages));
    assert(std::memcmp(
        &pages.control.authored.modulation,
        &graphAfter,
        sizeof(graphAfter)
    ) == 0);
    assert(std::memcmp(
        &pages.control.authored.curves,
        &arenaAfter,
        sizeof(arenaAfter)
    ) == 0);
    std::cout << "[PASS] recorded source duplicate shares and Undo restores\n";
}

void test_multi_macro_take_is_one_atomic_undo_redo_action() {
    using namespace core::state::modulation;
    macro::MacroPagesState pages;
    seedCurves(pages);
    constexpr macro::MacroAutomationSlotAddress second{
        .track = 0,
        .page = 0,
        .macro = 2,
    };
    macro::MacroAutomationLane oldSecond{};
    assert(macro::macroAutomationAppendPoint(oldSecond, 0.0f, 0.1f));
    assert(macro::macroAutomationAppendPoint(oldSecond, 1.0f, 0.3f));
    assert(test_support::project_control::assignAutomation(
        pages.control,
        second,
        oldSecond
    ));

    macro::MacroAutomationHistorySnapshot firstBefore{};
    macro::MacroAutomationHistorySnapshot secondBefore{};
    assert(macro::captureMacroAutomationHistorySnapshot(
        pages,
        kAddress,
        firstBefore
    ));
    assert(macro::captureMacroAutomationHistorySnapshot(
        pages,
        second,
        secondBefore
    ));
    const auto modulationBefore = pages.control.authored.modulation;

    macro::MacroHistoryService history;
    auto change = history.prepareAutomationTake(
        pages,
        0U,
        0U,
        static_cast<uint16_t>((1U << 1U) | (1U << 2U))
    );
    assert(change && change->automationTake);
    auto& payload = *change->automationTake;
    payload.touchedMask = payload.candidateMask;
    for (uint8_t macroIndex : {uint8_t{1U}, uint8_t{2U}}) {
        auto& snapshot = payload.after[macroIndex];
        snapshot.automation.active = true;
        snapshot.automation.playbackState = macro::MacroCurvePlaybackState::ACTIVE;
        snapshot.automation.pointOffset = 0U;
        snapshot.automation.pointCount = 2U;
        snapshot.automation.sourceDurationTicks = 768U;
        snapshot.automation.durationTicks = 768U;
        snapshot.automation.windowOffsetTicks = 100U;
        snapshot.automation.interpolation =
            macro::MacroAutomationInterpolation::LINEAR;
        snapshot.automation.modulationOrigin = macro::MacroModulationOrigin::NATIVE;
        snapshot.pointCount = 2U;
        snapshot.points[0] = {0U, static_cast<int16_t>(4000 * macroIndex)};
        snapshot.points[1] = {768U, static_cast<int16_t>(12000 + 3000 * macroIndex)};
    }

    auto staged = core::app::makeExtmemUnique<ProjectControlDomainState>();
    assert(staged);
    *staged = pages.control.authored;
    for (uint8_t macroIndex : {uint8_t{1U}, uint8_t{2U}}) {
        const auto& snapshot = payload.after[macroIndex];
        assert(replaceProjectControlAutomationInDomain(
            *staged,
            snapshot.address,
            snapshot.automation,
            snapshot.points.get(),
            snapshot.pointCount
        ));
    }
    assert(validProjectModulationDomain(
        staged->modulation,
        staged->curves,
        &staged->automation
    ));
    const uint32_t revisionBeforePublish = pages.control.authoredRevision;
    pages.control.authored = *staged;
    pages.control.markAuthoredMutation();
    assert(history.commitPreparedAutomationTake(pages, change));
    assert(history.undoCount() == 1U);
    assert(pages.control.authoredRevision == revisionBeforePublish + 1U);
    assert(std::memcmp(
        &pages.control.authored.modulation,
        &modulationBefore,
        sizeof(modulationBefore)
    ) == 0);

    assert(history.undo(pages));
    assert(macro::liveMacroAutomationMatchesHistorySnapshot(pages, firstBefore));
    assert(macro::liveMacroAutomationMatchesHistorySnapshot(pages, secondBefore));
    assert(std::memcmp(
        &pages.control.authored.modulation,
        &modulationBefore,
        sizeof(modulationBefore)
    ) == 0);
    assert(history.redo(pages));
    assert(history.undoCount() == 1U);
    std::cout << "[PASS] multi-Macro take is one atomic Undo/Redo action\n";
}

}  // namespace

int main() {
    test_snapshot_roundtrip_restores_exact_slot();
    test_clear_is_one_undo_redo_action();
    test_depth_turns_coalesce_without_extra_entries();
    test_global_depth_is_compact_coalesced_and_independent_from_bypass();
    test_assignment_undo_preserves_unrelated_macro_fields();
    test_history_admission_rejects_oversized_slot();
    test_history_evicts_oldest_entry_at_fixed_limit();
    test_new_mutation_after_undo_clears_redo_stack();
    test_lfo_audition_cancel_is_byte_stable_and_history_free();
    test_lfo_audition_apply_is_one_compact_undo_redo_action();
    test_lfo_audition_capacity_failure_has_no_partial_state();
    test_macro_create_audition_cancel_restores_page_graph_and_ids();
    test_macro_create_and_assignment_are_one_undo_redo_action();
    test_missing_track_page_and_sparse_macro_commit_atomically();
    test_macro_create_duplicate_and_capacity_failures_are_exact_noops();
    test_macro_create_stale_add_slot_is_rejected_without_history();
    test_macro_create_widening_cancel_and_failed_commit_are_exact();
    test_existing_modulator_cancel_preserves_root_and_is_byte_stable();
    test_existing_modulator_apply_undo_redo_moves_only_binding();
    test_project_source_edits_coalesce_and_restore_exact_source();
    test_project_source_rename_is_one_exact_undo_action();
    test_unassigned_lfo_creation_is_one_undo_action();
    test_unassigned_adsr_creation_includes_trigger_in_one_undo_action();
    test_adsr_audition_cancel_and_apply_are_atomic();
    test_adsr_parameters_and_trigger_route_coalesce_by_stable_identity();
    test_adsr_duplicate_copies_trigger_and_undo_is_exact();
    test_existing_global_assignment_cancel_and_undo_are_exact();
    test_project_modulator_split_is_one_exact_undo_action();
    test_root_delete_undo_restores_graph_and_recorded_curve_exactly();
    test_root_delete_undo_restores_shared_curve_reference();
    test_recorded_source_duplicate_undo_restores_shared_reference();
    test_multi_macro_take_is_one_atomic_undo_redo_action();
    test_assignment_history_is_destination_scoped_and_order_stable();
    test_assignment_remove_and_clear_keep_roots_and_unrelated_edges();
    test_sparse_macro_removal_purges_all_destination_state_atomically();
    std::cout << "All MacroHistory tests passed\n";
    return 0;
}
