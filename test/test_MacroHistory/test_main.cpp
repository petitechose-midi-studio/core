#ifdef NDEBUG
#undef NDEBUG
#endif

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
#include <utility>
#include <vector>

#include "../../src/state/macro/MacroHistory.hpp"
#include "../support/ProjectControlTestUtils.hpp"

namespace {

namespace macro = core::state::macro;

constexpr macro::MacroAutomationSlotAddress kAddress{
    .track = 0,
    .page = 0,
    .macro = 1,
};

void compileActiveControlPlan(
    core::state::modulation::ProjectControlState& control
) {
    using namespace core::state::modulation;
    ProjectModulationCompileContext context{};
    context.enabledTrackMask = 0x0001U;
    context.activePage[0] = kAddress.page;
    context.activeMacroMask[0] =
        static_cast<uint8_t>(1U << kAddress.macro);
    assert(compileProjectControlRuntimePlan(
        control.authored,
        context,
        control.plan
    ).compiled());
    control.compiledRevision = control.authoredRevision;
    control.runtimeContextHash =
        projectModulationCompileContextHash(context);
}

const core::state::modulation::ProjectModulationRuntimeBinding*
runtimeBinding(
    const core::state::modulation::ProjectControlState& control,
    core::state::modulation::ModulationBindingId bindingId
) {
    const auto begin = control.plan.bindings.begin();
    const auto end = begin + control.plan.bindingCount;
    const auto found = std::find_if(
        begin,
        end,
        [bindingId](
            const core::state::modulation::ProjectModulationRuntimeBinding&
                binding
        ) {
            return binding.id == bindingId;
        }
    );
    return found == end ? nullptr : &*found;
}

const core::state::modulation::ProjectModulationRuntimeDestination*
runtimeDestination(
    const core::state::modulation::ProjectControlState& control,
    const core::state::modulation::ModulationDestination& destination
) {
    const auto begin = control.plan.destinations.begin();
    const auto end = begin + control.plan.destinationCount;
    const auto found = std::find_if(
        begin,
        end,
        [&destination](
            const core::state::modulation::ProjectModulationRuntimeDestination&
                runtime
        ) {
            return runtime.destination == destination;
        }
    );
    return found == end ? nullptr : &*found;
}

void seedCurves(macro::MacroPagesState& pages) {
    macro::MacroAutomationLane automation{};
    assert(macro::macroAutomationAppendPoint(automation, 0.0f, 0.2f));
    assert(macro::macroAutomationAppendPoint(automation, 1.0f, 0.8f));
    assert(test_support::project_control::assignAutomation(
        pages.control,
        kAddress,
        automation
    ));

    test_support::project_control::ModulationShape modulation{};
    assert(test_support::project_control::appendModulationPoint(
        modulation, 0.0f, -0.2f
    ));
    assert(test_support::project_control::appendModulationPoint(
        modulation, 1.0f, 0.3f
    ));
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
    assert(slot.primaryModulation.enabled);
    assert(slot.automation.enabled);
    assert(std::fabs(slot.primaryModulation.amount - 0.65f) < 0.0001f);
    assert(history.redo(pages));
    slot = test_support::project_control::readSlot(pages.control, kAddress);
    assert(slot.modulationCount == 0U);
    assert(slot.automation.enabled);
    std::cout << "[PASS] clear is one exact Undo/Redo action\n";
}

void test_depth_turns_coalesce_without_extra_entries() {
    using namespace core::state::modulation;
    macro::MacroPagesState pages;
    seedCurves(pages);
    const auto bindingId =
        pages.control.authored.modulation.outputBindings[0].id;
    compileActiveControlPlan(pages.control);
    assert(runtimeBinding(pages.control, bindingId) != nullptr);
    macro::MacroHistoryService history;
    assert(history.setModulationDepthCoalesced(pages, kAddress, 0.5f));
    assert(pages.control.compiledRevision ==
           pages.control.authoredRevision);
    assert(runtimeBinding(pages.control, bindingId)->amountQ15 == 16384);
    assert(history.setModulationDepthCoalesced(pages, kAddress, 0.25f));
    assert(pages.control.compiledRevision ==
           pages.control.authoredRevision);
    assert(runtimeBinding(pages.control, bindingId)->amountQ15 == 8192);
    assert(history.setModulationDepthCoalesced(pages, kAddress, 0.0f));
    assert(pages.control.compiledRevision ==
           pages.control.authoredRevision);
    assert(runtimeBinding(pages.control, bindingId)->amountQ15 == 0);
    assert(history.undoCount() == 1);
    assert(history.undo(pages));
    assert(pages.control.compiledRevision !=
           pages.control.authoredRevision);
    auto slot = test_support::project_control::readSlot(pages.control, kAddress);
    assert(std::fabs(slot.primaryModulation.amount - 0.65f) < 0.0001f);
    assert(history.redo(pages));
    slot = test_support::project_control::readSlot(pages.control, kAddress);
    assert(slot.primaryModulation.amount == 0.0f);
    std::cout << "[PASS] Depth turns coalesce to one action\n";
}

void test_global_depth_is_compact_coalesced_and_independent_from_bypass() {
    using namespace core::state::modulation;
    macro::MacroPagesState pages;
    seedCurves(pages);
    macro::MacroHistoryService history;
    const auto destination = projectControlDestination(kAddress);
    const auto bindingId = pages.control.authored.modulation.outputBindings[0].id;
    compileActiveControlPlan(pages.control);
    assert(runtimeDestination(pages.control, destination) != nullptr);

    assert(history.setModulationDestinationScaleCoalesced(
        pages,
        kAddress,
        32768U
    ));
    assert(pages.control.compiledRevision ==
           pages.control.authoredRevision);
    assert(runtimeDestination(
        pages.control,
        destination
    )->destinationScaleQ15 == 32768U);
    assert(history.setModulationDestinationScaleCoalesced(
        pages,
        kAddress,
        16384U
    ));
    assert(pages.control.compiledRevision ==
           pages.control.authoredRevision);
    assert(runtimeDestination(
        pages.control,
        destination
    )->destinationScaleQ15 == 16384U);
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
    assert(pages.control.compiledRevision !=
           pages.control.authoredRevision);
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

void test_automation_timing_edits_are_compact_coalesced_and_exact() {
    using namespace core::state::modulation;
    macro::MacroPagesState pages;
    seedCurves(pages);
    macro::MacroHistoryService history;
    ProjectControlMacroDestinationView before{};
    assert(readProjectControlMacroDestination(pages.control, kAddress, before));
    assert(before.automation.stored());
    const auto* beforeRecord = findProjectCurve(
        pages.control.authored.curves,
        before.automation.id
    );
    assert(beforeRecord != nullptr && beforeRecord->pointCount == 2U);
    const std::array<ProjectPackedCurvePoint, 2> expectedPoints{{
        pages.control.authored.curves.points[beforeRecord->pointOffset],
        pages.control.authored.curves.points[beforeRecord->pointOffset + 1U],
    }};
    const uint16_t originalDuration = before.automation.spec.durationTicks;
    const uint16_t originalWindow = before.automation.spec.windowOffsetTicks;

    assert(history.setAutomationDurationBeatsCoalesced(
        pages,
        kAddress,
        2.0f
    ));
    assert(history.setAutomationDurationBeatsCoalesced(
        pages,
        kAddress,
        4.0f
    ));
    assert(history.undoCount() == 1U);
    ProjectControlMacroDestinationView edited{};
    assert(readProjectControlMacroDestination(pages.control, kAddress, edited));
    assert(edited.automation.spec.durationTicks ==
           4U * macro::MACRO_AUTOMATION_TICKS_PER_BEAT);

    history.endCoalescing();
    assert(history.setAutomationWindowOffsetBeatsCoalesced(
        pages,
        kAddress,
        0.25f
    ));
    assert(history.setAutomationWindowOffsetBeatsCoalesced(
        pages,
        kAddress,
        0.5f
    ));
    assert(history.undoCount() == 2U);
    assert(readProjectControlMacroDestination(pages.control, kAddress, edited));
    assert(edited.automation.spec.windowOffsetTicks ==
           macro::MACRO_AUTOMATION_TICKS_PER_BEAT / 2U);

    assert(history.undo(pages));
    assert(readProjectControlMacroDestination(pages.control, kAddress, edited));
    assert(edited.automation.spec.windowOffsetTicks == originalWindow);
    assert(edited.automation.spec.durationTicks ==
           4U * macro::MACRO_AUTOMATION_TICKS_PER_BEAT);
    assert(history.undo(pages));
    assert(readProjectControlMacroDestination(pages.control, kAddress, edited));
    assert(edited.automation.spec.durationTicks == originalDuration);
    assert(history.redo(pages));
    assert(history.redo(pages));

    assert(readProjectControlMacroDestination(pages.control, kAddress, edited));
    const auto* editedRecord = findProjectCurve(
        pages.control.authored.curves,
        edited.automation.id
    );
    assert(editedRecord != nullptr && editedRecord->pointCount == 2U);
    assert(pages.control.authored.curves.points[editedRecord->pointOffset].tick ==
           expectedPoints[0].tick);
    assert(pages.control.authored.curves.points[editedRecord->pointOffset].value ==
           expectedPoints[0].value);
    assert(pages.control.authored.curves.points[editedRecord->pointOffset + 1U].tick ==
           expectedPoints[1].tick);
    assert(pages.control.authored.curves.points[editedRecord->pointOffset + 1U].value ==
           expectedPoints[1].value);
    assert(std::fabs(edited.primaryModulation.amount - 0.65f) < 0.0001f);
    std::cout << "[PASS] Automation timing uses compact exact coalesced history\n";
}

void test_automation_timing_copy_on_write_preserves_shared_owner() {
    using namespace core::state::modulation;
    macro::MacroPagesState pages;
    seedCurves(pages);
    constexpr macro::MacroAutomationSlotAddress copyAddress{
        .track = 0U,
        .page = 0U,
        .macro = 2U,
    };
    assert(duplicateProjectAutomationCurve(
        pages.control.authored.automation,
        pages.control.authored.curves,
        projectControlDestination(kAddress),
        projectControlDestination(copyAddress)
    ).changed());
    pages.control.markAuthoredMutation();
    ProjectControlMacroDestinationView sourceBefore{};
    ProjectControlMacroDestinationView copyBefore{};
    assert(readProjectControlMacroDestination(
        pages.control,
        kAddress,
        sourceBefore
    ));
    assert(readProjectControlMacroDestination(
        pages.control,
        copyAddress,
        copyBefore
    ));
    assert(sourceBefore.automation.id == copyBefore.automation.id);

    macro::MacroHistoryService history;
    assert(history.setAutomationDurationBeatsCoalesced(
        pages,
        kAddress,
        8.0f
    ));
    ProjectControlMacroDestinationView sourceAfter{};
    ProjectControlMacroDestinationView copyAfter{};
    assert(readProjectControlMacroDestination(
        pages.control,
        kAddress,
        sourceAfter
    ));
    assert(readProjectControlMacroDestination(
        pages.control,
        copyAddress,
        copyAfter
    ));
    assert(sourceAfter.automation.id != copyAfter.automation.id);
    assert(copyAfter.automation.spec.durationTicks ==
           copyBefore.automation.spec.durationTicks);
    assert(history.undo(pages));
    assert(readProjectControlMacroDestination(
        pages.control,
        kAddress,
        sourceAfter
    ));
    assert(sourceAfter.automation.spec.durationTicks ==
           sourceBefore.automation.spec.durationTicks);
    assert(readProjectControlMacroDestination(
        pages.control,
        copyAddress,
        copyAfter
    ));
    assert(copyAfter.automation.spec.durationTicks ==
           copyBefore.automation.spec.durationTicks);
    std::cout << "[PASS] Automation timing copy-on-write preserves shared owner\n";
}

void test_automation_timing_undo_fails_closed_on_point_corruption() {
    using namespace core::state::modulation;
    macro::MacroPagesState pages;
    seedCurves(pages);
    macro::MacroHistoryService history;
    assert(history.setAutomationDurationBeatsCoalesced(
        pages,
        kAddress,
        2.0f
    ));
    ProjectControlMacroDestinationView view{};
    assert(readProjectControlMacroDestination(pages.control, kAddress, view));
    const auto* record = findProjectCurve(
        pages.control.authored.curves,
        view.automation.id
    );
    assert(record != nullptr);
    ++pages.control.authored.curves.points[record->pointOffset].value;
    pages.control.markAuthoredMutation();
    assert(!history.undo(pages));
    assert(history.undoCount() == 1U);
    assert(readProjectControlMacroDestination(pages.control, kAddress, view));
    assert(view.automation.spec.durationTicks ==
           2U * macro::MACRO_AUTOMATION_TICKS_PER_BEAT);
    std::cout << "[PASS] Automation timing Undo rejects point corruption\n";
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
    assert(std::fabs(slot.primaryModulation.amount - 0.65f) < 0.0001f);
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
    assert(test_support::project_control::readSlot(pages.control, kAddress).present());
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
    assert(std::fabs(slot.primaryModulation.amount - 0.1f) < 0.0001f);
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
    assert(std::fabs(slot.primaryModulation.amount - 0.3f) < 0.0001f);
    std::cout << "[PASS] new mutation after Undo clears Redo\n";
}

core::state::modulation::ModulatorLfoDraft defaultLfoDraft() {
    using namespace core::state::modulation;
    ModulatorLfoDraft draft{};
    draft.name = "LFO 1";
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
    return draft;
}

core::state::modulation::ModulationTriggerDraft defaultAdsrTrigger() {
    using namespace core::state::modulation;
    ModulationTriggerDraft trigger{};
    trigger.trigger = {
        ModulationTriggerKind::TRACK_NOTE,
        kAddress.track,
        0U,
        127U,
    };
    trigger.velocityMin = 0U;
    trigger.velocityMax = 127U;
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

void appendAutomationFiller(
    macro::MacroPagesState& pages,
    uint16_t pointCount
) {
    using namespace core::state::modulation;
    assert(pointCount > 1U);
    auto& domain = pages.control.authored;
    auto& arena = domain.curves;
    assert(arena.recordCount < PROJECT_CURVE_LIVE_CAPACITY);
    assert(pointCount <= PROJECT_CURVE_POINT_CAPACITY - arena.pointCount);
    assert(domain.automation.entryCount < PROJECT_AUTOMATION_ENTRY_CAPACITY);
    assert(arena.nextCurveId != 0U);
    const ProjectCurveId id{arena.nextCurveId++};
    const uint16_t offset = arena.pointCount;
    for (uint16_t index = 0U; index < pointCount; ++index) {
        arena.points[static_cast<uint16_t>(offset + index)] = {index, 1200};
    }
    arena.records[arena.recordCount++] = {
        .id = id,
        .pointOffset = offset,
        .pointCount = pointCount,
        .sourceDurationTicks = static_cast<uint16_t>(pointCount - 1U),
        .durationTicks = static_cast<uint16_t>(pointCount - 1U),
        .referenceCount = 1U,
        .valueDomain = ProjectCurveValueDomain::ABSOLUTE_UNIPOLAR,
    };
    arena.pointCount = static_cast<uint16_t>(arena.pointCount + pointCount);
    domain.automation.entries[domain.automation.entryCount++] = {
        .destination = projectControlDestination(kAddress),
        .curveId = id,
        .flags = PROJECT_AUTOMATION_CURVE_FLAG_ENABLED,
    };
    assert(validProjectModulationDomain(
        domain.modulation,
        domain.curves,
        &domain.automation
    ));
}

core::state::modulation::ModulatorId addProjectLfo(
    macro::MacroPagesState& pages,
    const char* name
) {
    using namespace core::state::modulation;
    auto draft = defaultLfoDraft();
    draft.name = name;
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
    assert(!removedSlot.present());
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
    ).present());
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
    assert(pages.control.audition.active());
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
    assert(!pages.control.audition.active());
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
    assert(!pages.control.audition.active());
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
    assert(!pages.control.audition.active());
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
    assert(!pages.control.audition.active());
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

void test_macro_create_cancel_and_failed_commit_are_exact() {
    using namespace core::state::modulation;
    macro::MacroPagesState pages;
    macro::MacroHistoryService history;
    auto draft = defaultLfoDraft();
    const auto source = createLfoModulator(
        pages.control.authored.modulation,
        draft
    );
    assert(source.changed());
    const auto pageBefore = pages.pageData(0, 0);
    const auto graphBefore = pages.control.authored.modulation;
    const auto begun = history.beginExistingModulatorAudition(
        pages,
        kAddress,
        source.sourceId,
        defaultBindingDraft(),
        true
    );
    assert(begun.changed());
    assert(pages.pageData(0, 0).isMacroActive(kAddress.macro));

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
    std::cout << "[PASS] Macro-create Cancel after rejected commit is exact\n";
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
    assert(!pages.control.audition.sourceCreated());
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
    assert(!pages.control.audition.active() && history.undoCount() == 0U);
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
    route.noteMin = 24U;
    route.noteMax = 108U;
    assert(history.setProjectModulationTriggerCoalesced(
        pages,
        created.sourceId,
        route,
        true,
        8U,
        120U
    ));
    route.track = 2U;
    route.noteMin = 36U;
    route.noteMax = 96U;
    assert(history.setProjectModulationTriggerCoalesced(
        pages,
        created.sourceId,
        route,
        false,
        16U,
        112U
    ));
    assert(history.undoCount() == 2U);
    assert(pages.control.authored.modulation.triggerBindings[0].id == triggerId);
    assert(pages.control.authored.modulation.triggerBindings[0].trigger == route);
    assert(pages.control.authored.modulation.triggerBindings[0].velocityMin ==
           16U);
    assert(pages.control.authored.modulation.triggerBindings[0].velocityMax ==
           112U);
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
    assert(graphAfter.triggerBindings[0].velocityMin ==
           graphAfter.triggerBindings[1].velocityMin);
    assert(graphAfter.triggerBindings[0].velocityMax ==
           graphAfter.triggerBindings[1].velocityMax);

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

void test_existing_assignment_cancel_and_undo_are_exact() {
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

    auto begun = history.beginExistingModulatorAudition(
        pages, other, created.sourceId, binding
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
        pages, other, created.sourceId, binding
    );
    assert(begun.changed());
    assert(history.commitModulatorAudition(pages, other));
    assert(history.undo(pages));
    assert(std::memcmp(
        &pages.control.authored.modulation.sources[0],
        &original,
        sizeof(original)
    ) == 0);
    assert(pages.control.authored.modulation.outputBindingCount == 0U);
    assert(history.redo(pages));
    assert(pages.control.authored.modulation.outputBindingCount == 1U);
    std::cout << "[PASS] Existing assignment Cancel and Undo restore the root\n";
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

void test_unassigned_recorded_shape_creation_is_one_exact_undo_action() {
    using namespace core::state::modulation;
    macro::MacroPagesState pages;
    macro::MacroHistoryService history;
    constexpr std::array<ProjectPackedCurvePoint, 4> points{{
        {0U, -12000},
        {128U, 4000},
        {256U, 10000},
        {384U, -3000},
    }};
    RecordedShapeDraft draft{};
    draft.name = "Recorded 1";
    draft.curve = {
        .sourceDurationTicks = 384U,
        .durationTicks = 384U,
        .valueDomain = ProjectCurveValueDomain::BIPOLAR,
    };
    draft.points = points.data();
    draft.pointCount = static_cast<uint16_t>(points.size());
    const auto graphBefore = pages.control.authored.modulation;
    const auto arenaBefore = pages.control.authored.curves;

    const auto created = history.createUnassignedRecordedShape(pages, draft);
    assert(created.changed());
    assert(history.undoCount() == 1U);
    const auto graphAfter = pages.control.authored.modulation;
    const auto arenaAfter = pages.control.authored.curves;
    assert(graphAfter.sourceCount == 1U);
    assert(arenaAfter.recordCount == 1U);
    assert(arenaAfter.records[0].id == created.curveId);

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
    std::cout << "[PASS] unassigned Recorded Shape creation is exact\n";
}

void test_assigned_recorded_shape_creation_commits_topology_atomically() {
    using namespace core::state::modulation;
    macro::MacroPagesState pages;
    macro::MacroHistoryService history;
    constexpr macro::MacroAutomationSlotAddress address{1U, 0U, 5U};
    constexpr std::array<ProjectPackedCurvePoint, 3> points{{
        {0U, -8000},
        {192U, 6000},
        {384U, -2000},
    }};
    RecordedShapeDraft draft{};
    draft.name = "Gesture 1";
    draft.curve = {
        .sourceDurationTicks = 384U,
        .durationTicks = 384U,
        .valueDomain = ProjectCurveValueDomain::BIPOLAR,
    };
    draft.points = points.data();
    draft.pointCount = static_cast<uint16_t>(points.size());
    auto binding = defaultBindingDraft();
    binding.destination = projectControlDestination(address);
    const auto plan = macro::MacroWorkflow::planDestinationActivation(
        pages,
        address
    );
    assert(plan.valid && plan.changesTopology());
    const auto graphBefore = pages.control.authored.modulation;
    const auto arenaBefore = pages.control.authored.curves;
    const auto trackBefore = pages.tracks[address.track];
    const uint16_t enabledBefore = pages.currentTrackEnabledMask();

    const auto created = history.createAssignedRecordedShape(
        pages,
        address,
        draft,
        binding,
        false,
        &plan
    );
    assert(created.changed());
    assert(valid(created.bindingId));
    assert(pages.pageData(address.track, address.page).isMacroActive(
        address.macro
    ));
    const auto graphAfter = pages.control.authored.modulation;
    const auto arenaAfter = pages.control.authored.curves;
    const auto trackAfter = pages.tracks[address.track];
    const uint16_t enabledAfter = pages.currentTrackEnabledMask();

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
    assert(std::memcmp(
        &pages.tracks[address.track],
        &trackBefore,
        sizeof(trackBefore)
    ) == 0);
    assert(pages.currentTrackEnabledMask() == enabledBefore);
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
    assert(std::memcmp(
        &pages.tracks[address.track],
        &trackAfter,
        sizeof(trackAfter)
    ) == 0);
    assert(pages.currentTrackEnabledMask() == enabledAfter);
    std::cout << "[PASS] assigned Recorded Shape topology is atomic\n";
}

void test_unique_recorded_shape_replace_restores_compacted_arena_exactly() {
    using namespace core::state::modulation;
    macro::MacroPagesState pages;
    macro::MacroHistoryService history;
    constexpr std::array<ProjectPackedCurvePoint, 3> firstPoints{{
        {0U, -10000}, {192U, 5000}, {384U, -2000},
    }};
    constexpr std::array<ProjectPackedCurvePoint, 2> secondPoints{{
        {0U, -4000}, {384U, 4000},
    }};
    RecordedShapeDraft first{};
    first.name = "First";
    first.curve = {
        .sourceDurationTicks = 384U,
        .durationTicks = 384U,
        .valueDomain = ProjectCurveValueDomain::BIPOLAR,
    };
    first.points = firstPoints.data();
    first.pointCount = static_cast<uint16_t>(firstPoints.size());
    const auto firstSource = createRecordedShapeModulator(
        pages.control.authored.modulation,
        pages.control.authored.curves,
        first
    );
    assert(firstSource.changed());
    auto second = first;
    second.name = "Second";
    second.points = secondPoints.data();
    second.pointCount = static_cast<uint16_t>(secondPoints.size());
    const auto secondSource = createRecordedShapeModulator(
        pages.control.authored.modulation,
        pages.control.authored.curves,
        second
    );
    assert(secondSource.changed());
    const auto graphBefore = pages.control.authored.modulation;
    const auto arenaBefore = pages.control.authored.curves;
    constexpr std::array<ProjectPackedCurvePoint, 5> replacement{{
        {0U, -15000},
        {96U, -5000},
        {192U, 8000},
        {288U, 12000},
        {384U, -1000},
    }};
    auto spec = first.curve;
    spec.windowOffsetTicks = 24U;
    const auto replaced = history.replaceProjectRecordedShapeCurve(
        pages,
        firstSource.sourceId,
        spec,
        replacement.data(),
        static_cast<uint16_t>(replacement.size())
    );
    assert(replaced.changed());
    assert(replaced.curveId == firstSource.curveId);
    const auto graphAfter = pages.control.authored.modulation;
    const auto arenaAfter = pages.control.authored.curves;
    assert(arenaAfter.records[1].id == secondSource.curveId);
    assert(arenaAfter.records[1].pointOffset == replacement.size());

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
    std::cout << "[PASS] unique Recorded Shape replace is byte-exact\n";
}

void test_unique_recorded_shape_shrink_restores_nonzero_tails_exactly() {
    using namespace core::state::modulation;
    macro::MacroPagesState pages;
    macro::MacroHistoryService history;
    constexpr std::array<ProjectPackedCurvePoint, 5> points{{
        {0U, -12000},
        {96U, -6000},
        {192U, 0},
        {288U, 6000},
        {384U, 12000},
    }};
    RecordedShapeDraft draft{};
    draft.name = "Shrink";
    draft.curve = {
        .sourceDurationTicks = 384U,
        .durationTicks = 384U,
        .valueDomain = ProjectCurveValueDomain::BIPOLAR,
    };
    draft.points = points.data();
    draft.pointCount = static_cast<uint16_t>(points.size());
    const auto source = createRecordedShapeModulator(
        pages.control.authored.modulation,
        pages.control.authored.curves,
        draft
    );
    assert(source.changed());
    constexpr std::array<ProjectPackedCurvePoint, 2> trailing{{
        {0U, -2000}, {384U, 2000},
    }};
    auto trailingDraft = draft;
    trailingDraft.name = "Trailing";
    trailingDraft.points = trailing.data();
    trailingDraft.pointCount = static_cast<uint16_t>(trailing.size());
    assert(createRecordedShapeModulator(
        pages.control.authored.modulation,
        pages.control.authored.curves,
        trailingDraft
    ).changed());
    auto& arena = pages.control.authored.curves;
    for (uint16_t index = 0U; index < 4U; ++index) {
        arena.points[static_cast<uint16_t>(arena.pointCount + index)] = {
            static_cast<uint16_t>(500U + index),
            static_cast<int16_t>(14000 + index),
        };
    }
    const auto graphBefore = pages.control.authored.modulation;
    const auto arenaBefore = arena;
    constexpr std::array<ProjectPackedCurvePoint, 2> replacement{{
        {0U, 9000}, {384U, -9000},
    }};
    const auto replaced = history.replaceProjectRecordedShapeCurve(
        pages,
        source.sourceId,
        draft.curve,
        replacement.data(),
        static_cast<uint16_t>(replacement.size())
    );
    assert(replaced.changed());
    const auto graphAfter = pages.control.authored.modulation;
    const auto arenaAfter = arena;
    assert(arenaAfter.pointCount + 3U == arenaBefore.pointCount);
    bool nonzeroTail = false;
    for (uint16_t index = arenaAfter.pointCount;
         index < arenaBefore.pointCount;
         ++index) {
        nonzeroTail |= arenaAfter.points[index].tick != 0U ||
            arenaAfter.points[index].value != 0;
    }
    assert(nonzeroTail);

    assert(history.undo(pages));
    assert(std::memcmp(
        &pages.control.authored.modulation,
        &graphBefore,
        sizeof(graphBefore)
    ) == 0);
    assert(std::memcmp(&arena, &arenaBefore, sizeof(arenaBefore)) == 0);
    assert(history.redo(pages));
    assert(std::memcmp(
        &pages.control.authored.modulation,
        &graphAfter,
        sizeof(graphAfter)
    ) == 0);
    assert(std::memcmp(&arena, &arenaAfter, sizeof(arenaAfter)) == 0);
    std::cout << "[PASS] unique shrink restores non-zero inactive tails\n";
}

void test_shared_recorded_shape_replace_is_exact_copy_on_write() {
    using namespace core::state::modulation;
    macro::MacroPagesState pages;
    macro::MacroHistoryService history;
    constexpr std::array<ProjectPackedCurvePoint, 3> points{{
        {0U, -9000}, {192U, 7000}, {384U, -3000},
    }};
    RecordedShapeDraft draft{};
    draft.name = "Shared";
    draft.curve = {
        .sourceDurationTicks = 384U,
        .durationTicks = 384U,
        .valueDomain = ProjectCurveValueDomain::BIPOLAR,
    };
    draft.points = points.data();
    draft.pointCount = static_cast<uint16_t>(points.size());
    const auto source = createRecordedShapeModulator(
        pages.control.authored.modulation,
        pages.control.authored.curves,
        draft
    );
    assert(source.changed());
    const auto clone = duplicateProjectModulator(
        pages.control.authored.modulation,
        pages.control.authored.curves,
        source.sourceId,
        "Shared 2"
    );
    assert(clone.changed());
    constexpr std::array<ProjectPackedCurvePoint, 2> foreignPoints{{
        {0U, -2000}, {384U, 2000},
    }};
    auto foreignDraft = draft;
    foreignDraft.name = "Foreign";
    foreignDraft.points = foreignPoints.data();
    foreignDraft.pointCount = static_cast<uint16_t>(foreignPoints.size());
    const auto foreign = createRecordedShapeModulator(
        pages.control.authored.modulation,
        pages.control.authored.curves,
        foreignDraft
    );
    assert(foreign.changed());
    auto& liveArena = pages.control.authored.curves;
    liveArena.records[liveArena.recordCount] = {
        .id = ProjectCurveId{999U},
        .pointOffset = 313U,
        .pointCount = 7U,
        .sourceDurationTicks = 999U,
        .durationTicks = 888U,
        .referenceCount = 77U,
    };
    for (uint16_t index = 0U; index < 4U; ++index) {
        liveArena.points[static_cast<uint16_t>(liveArena.pointCount + index)] = {
            static_cast<uint16_t>(700U + index),
            static_cast<int16_t>(13000 + index),
        };
    }
    const auto graphBefore = pages.control.authored.modulation;
    const auto arenaBefore = liveArena;
    constexpr std::array<ProjectPackedCurvePoint, 4> replacement{{
        {0U, 1000}, {128U, 12000}, {256U, -12000}, {384U, 2000},
    }};
    const auto replaced = history.replaceProjectRecordedShapeCurve(
        pages,
        source.sourceId,
        draft.curve,
        replacement.data(),
        static_cast<uint16_t>(replacement.size())
    );
    assert(replaced.changed());
    assert(replaced.curveId != source.curveId);
    const auto graphAfter = pages.control.authored.modulation;
    const auto arenaAfter = liveArena;
    assert(arenaAfter.recordCount == 3U);
    assert(arenaAfter.records[0].referenceCount == 1U);
    assert(arenaAfter.records[1].id == foreign.curveId);
    assert(arenaAfter.records[2].referenceCount == 1U);
    assert(findProjectModulator(
        graphAfter,
        clone.sourceId
    )->parameters.recordedCurveId == source.curveId);

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
    std::cout << "[PASS] shared Recorded Shape replace is exact COW\n";
}

void test_recorded_shape_history_limits_fail_before_mutation() {
    using namespace core::state::modulation;
    macro::MacroPagesState pages;
    macro::MacroHistoryService history;
    std::vector<ProjectPackedCurvePoint> oversized(
        static_cast<size_t>(macro::RECORDED_SHAPE_HISTORY_POINT_CAPACITY) + 1U
    );
    for (uint16_t index = 0U; index < oversized.size(); ++index) {
        oversized[index] = {index, 0};
    }
    RecordedShapeDraft draft{};
    draft.name = "Too Large";
    draft.curve = {
        .sourceDurationTicks = static_cast<uint16_t>(oversized.size()),
        .durationTicks = static_cast<uint16_t>(oversized.size()),
        .valueDomain = ProjectCurveValueDomain::BIPOLAR,
    };
    draft.points = oversized.data();
    draft.pointCount = static_cast<uint16_t>(oversized.size());
    const auto graphBefore = pages.control.authored.modulation;
    const auto arenaBefore = pages.control.authored.curves;
    const uint32_t revisionBefore = pages.control.authoredRevision;
    const auto failed = history.createUnassignedRecordedShape(pages, draft);
    assert(failed.status == ProjectModulationStatus::HISTORY_CAPACITY_EXCEEDED);
    assert(history.undoCount() == 0U);
    assert(pages.control.authoredRevision == revisionBefore);
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

    std::vector<ProjectPackedCurvePoint> deletePoints(
        static_cast<size_t>(macro::MACRO_HISTORY_POINT_CAPACITY) + 1U
    );
    for (uint16_t index = 0U; index < deletePoints.size(); ++index) {
        deletePoints[index] = {index, 0};
    }
    draft.name = "Delete Limit";
    draft.curve.sourceDurationTicks =
        static_cast<uint16_t>(deletePoints.size());
    draft.curve.durationTicks = static_cast<uint16_t>(deletePoints.size());
    draft.points = deletePoints.data();
    draft.pointCount = static_cast<uint16_t>(deletePoints.size());
    const auto created = createRecordedShapeModulator(
        pages.control.authored.modulation,
        pages.control.authored.curves,
        draft
    );
    assert(created.changed());
    const auto deleteGraphBefore = pages.control.authored.modulation;
    const auto deleteArenaBefore = pages.control.authored.curves;
    const auto rejected = history.deleteProjectModulator(
        pages,
        created.sourceId
    );
    assert(rejected.status == ProjectModulationStatus::HISTORY_CAPACITY_EXCEEDED);
    assert(history.undoCount() == 0U);
    assert(std::memcmp(
        &pages.control.authored.modulation,
        &deleteGraphBefore,
        sizeof(deleteGraphBefore)
    ) == 0);
    assert(std::memcmp(
        &pages.control.authored.curves,
        &deleteArenaBefore,
        sizeof(deleteArenaBefore)
    ) == 0);
    std::cout << "[PASS] Recorded Shape history limits are atomic\n";
}

void test_recorded_shape_undo_fails_closed_on_live_corruption() {
    using namespace core::state::modulation;
    macro::MacroPagesState pages;
    macro::MacroHistoryService history;
    constexpr std::array<ProjectPackedCurvePoint, 2> points{{
        {0U, -4000}, {384U, 4000},
    }};
    RecordedShapeDraft draft{};
    draft.name = "Guarded";
    draft.curve = {
        .sourceDurationTicks = 384U,
        .durationTicks = 384U,
        .valueDomain = ProjectCurveValueDomain::BIPOLAR,
    };
    draft.points = points.data();
    draft.pointCount = static_cast<uint16_t>(points.size());
    const auto source = history.createUnassignedRecordedShape(pages, draft);
    assert(source.changed());
    constexpr std::array<ProjectPackedCurvePoint, 3> replacement{{
        {0U, -10000}, {192U, 10000}, {384U, 0},
    }};
    assert(history.replaceProjectRecordedShapeCurve(
        pages,
        source.sourceId,
        draft.curve,
        replacement.data(),
        static_cast<uint16_t>(replacement.size())
    ).changed());
    auto& arena = pages.control.authored.curves;
    const auto beforeAttempt = arena;
    arena.points[arena.records[0].pointOffset].value ^= 1;
    const auto corrupted = arena;
    assert(!history.undo(pages));
    assert(std::memcmp(&arena, &corrupted, sizeof(arena)) == 0);
    assert(std::memcmp(&arena, &beforeAttempt, sizeof(arena)) != 0);
    assert(history.undoCount() == 2U);
    std::cout << "[PASS] Recorded Shape Undo fails closed on corruption\n";
}

void test_recorded_shape_redo_is_invalidated_for_all_three_apis() {
    using namespace core::state::modulation;
    constexpr std::array<ProjectPackedCurvePoint, 2> points{{
        {0U, -4000}, {384U, 4000},
    }};
    RecordedShapeDraft draft{};
    draft.name = "Redo Guard";
    draft.curve = {
        .sourceDurationTicks = 384U,
        .durationTicks = 384U,
        .valueDomain = ProjectCurveValueDomain::BIPOLAR,
    };
    draft.points = points.data();
    draft.pointCount = static_cast<uint16_t>(points.size());

    macro::MacroPagesState unassignedPages;
    macro::MacroHistoryService unassignedHistory;
    assert(unassignedHistory.createUnassignedRecordedShape(
        unassignedPages,
        draft
    ).changed());
    assert(unassignedHistory.undo(unassignedPages));
    unassignedPages.control.authored.curves.points[0] = {777U, 1234};
    const auto unassignedGraph = unassignedPages.control.authored.modulation;
    const auto unassignedArena = unassignedPages.control.authored.curves;
    assert(!unassignedHistory.redo(unassignedPages));
    assert(std::memcmp(
        &unassignedPages.control.authored.modulation,
        &unassignedGraph,
        sizeof(unassignedGraph)
    ) == 0);
    assert(std::memcmp(
        &unassignedPages.control.authored.curves,
        &unassignedArena,
        sizeof(unassignedArena)
    ) == 0);

    macro::MacroPagesState assignedPages;
    macro::MacroHistoryService assignedHistory;
    auto binding = defaultBindingDraft();
    assert(assignedHistory.createAssignedRecordedShape(
        assignedPages,
        kAddress,
        draft,
        binding,
        true
    ).changed());
    assert(assignedHistory.undo(assignedPages));
    assignedPages.pageData(kAddress.track, kAddress.page).cc[kAddress.macro] ^=
        1U;
    const auto assignedGraph = assignedPages.control.authored.modulation;
    const auto assignedArena = assignedPages.control.authored.curves;
    const auto assignedPage = assignedPages.pageData(
        kAddress.track,
        kAddress.page
    );
    assert(!assignedHistory.redo(assignedPages));
    assert(std::memcmp(
        &assignedPages.control.authored.modulation,
        &assignedGraph,
        sizeof(assignedGraph)
    ) == 0);
    assert(std::memcmp(
        &assignedPages.control.authored.curves,
        &assignedArena,
        sizeof(assignedArena)
    ) == 0);
    assert(std::memcmp(
        &assignedPages.pageData(kAddress.track, kAddress.page),
        &assignedPage,
        sizeof(assignedPage)
    ) == 0);

    macro::MacroPagesState replacePages;
    macro::MacroHistoryService replaceHistory;
    const auto source = createRecordedShapeModulator(
        replacePages.control.authored.modulation,
        replacePages.control.authored.curves,
        draft
    );
    assert(source.changed());
    binding.sourceId = source.sourceId;
    assert(addProjectModulationBinding(
        replacePages.control.authored.modulation,
        binding
    ).changed());
    constexpr std::array<ProjectPackedCurvePoint, 3> replacement{{
        {0U, -12000}, {192U, 12000}, {384U, 0},
    }};
    assert(replaceHistory.replaceProjectRecordedShapeCurve(
        replacePages,
        source.sourceId,
        draft.curve,
        replacement.data(),
        static_cast<uint16_t>(replacement.size())
    ).changed());
    assert(replaceHistory.undo(replacePages));
    replacePages.control.authored.modulation.outputBindings[0].amountQ15 ^=
        1;
    const auto replaceGraph = replacePages.control.authored.modulation;
    const auto replaceArena = replacePages.control.authored.curves;
    assert(!replaceHistory.redo(replacePages));
    assert(std::memcmp(
        &replacePages.control.authored.modulation,
        &replaceGraph,
        sizeof(replaceGraph)
    ) == 0);
    assert(std::memcmp(
        &replacePages.control.authored.curves,
        &replaceArena,
        sizeof(replaceArena)
    ) == 0);
    std::cout << "[PASS] all Recorded Shape Redo paths fail closed\n";
}

void test_assigned_recorded_shape_rolls_back_post_curve_failure_exactly() {
    using namespace core::state::modulation;
    macro::MacroPagesState pages;
    macro::MacroHistoryService history;
    constexpr std::array<ProjectPackedCurvePoint, 2> points{{
        {0U, -3000}, {384U, 3000},
    }};
    RecordedShapeDraft draft{};
    draft.name = "Rollback";
    draft.curve = {
        .sourceDurationTicks = 384U,
        .durationTicks = 384U,
        .valueDomain = ProjectCurveValueDomain::BIPOLAR,
    };
    draft.points = points.data();
    draft.pointCount = static_cast<uint16_t>(points.size());
    pages.control.authored.modulation.nextBindingId = 0U;
    const auto graphBefore = pages.control.authored.modulation;
    const auto arenaBefore = pages.control.authored.curves;
    const auto pageBefore = pages.pageData(kAddress.track, kAddress.page);
    const uint32_t revisionBefore = pages.control.authoredRevision;
    const auto failed = history.createAssignedRecordedShape(
        pages,
        kAddress,
        draft,
        defaultBindingDraft(),
        true
    );
    assert(failed.status == ProjectModulationStatus::ID_EXHAUSTED);
    assert(history.undoCount() == 0U);
    assert(pages.control.authoredRevision == revisionBefore);
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
    assert(std::memcmp(
        &pages.pageData(kAddress.track, kAddress.page),
        &pageBefore,
        sizeof(pageBefore)
    ) == 0);
    std::cout << "[PASS] post-curve binding failure rolls back exactly\n";
}

void test_recorded_shape_creation_undo_rejects_source_permutation() {
    using namespace core::state::modulation;
    macro::MacroPagesState pages;
    macro::MacroHistoryService history;
    assert(createLfoModulator(
        pages.control.authored.modulation,
        defaultLfoDraft()
    ).changed());
    constexpr std::array<ProjectPackedCurvePoint, 2> points{{
        {0U, -3000}, {384U, 3000},
    }};
    RecordedShapeDraft draft{};
    draft.name = "Permuted";
    draft.curve = {
        .sourceDurationTicks = 384U,
        .durationTicks = 384U,
        .valueDomain = ProjectCurveValueDomain::BIPOLAR,
    };
    draft.points = points.data();
    draft.pointCount = static_cast<uint16_t>(points.size());
    assert(history.createUnassignedRecordedShape(pages, draft).changed());
    auto& domain = pages.control.authored;
    assert(domain.modulation.sourceCount == 2U);
    std::swap(domain.modulation.sources[0], domain.modulation.sources[1]);
    assert(validProjectModulationDomain(
        domain.modulation,
        domain.curves,
        &domain.automation
    ));
    const auto graphCorrupted = domain.modulation;
    const auto arenaCorrupted = domain.curves;
    assert(!history.undo(pages));
    assert(history.undoCount() == 1U);
    assert(std::memcmp(
        &domain.modulation,
        &graphCorrupted,
        sizeof(graphCorrupted)
    ) == 0);
    assert(std::memcmp(
        &domain.curves,
        &arenaCorrupted,
        sizeof(arenaCorrupted)
    ) == 0);
    std::cout << "[PASS] creation Undo rejects a permuted appended source\n";
}

void test_recorded_shape_creation_undo_rejects_new_source_references() {
    using namespace core::state::modulation;
    constexpr std::array<ProjectPackedCurvePoint, 2> points{{
        {0U, -2500}, {384U, 2500},
    }};
    RecordedShapeDraft draft{};
    draft.name = "Referenced";
    draft.curve = {
        .sourceDurationTicks = 384U,
        .durationTicks = 384U,
        .valueDomain = ProjectCurveValueDomain::BIPOLAR,
    };
    draft.points = points.data();
    draft.pointCount = static_cast<uint16_t>(points.size());

    {
        macro::MacroPagesState pages;
        macro::MacroHistoryService history;
        const auto existing = createLfoModulator(
            pages.control.authored.modulation,
            defaultLfoDraft()
        );
        assert(existing.changed());
        auto binding = defaultBindingDraft();
        binding.sourceId = existing.sourceId;
        assert(addProjectModulationBinding(
            pages.control.authored.modulation,
            binding
        ).changed());
        const auto created = history.createUnassignedRecordedShape(pages, draft);
        assert(created.changed());
        auto& domain = pages.control.authored;
        domain.modulation.outputBindings[0].sourceId = created.sourceId;
        assert(validProjectModulationDomain(
            domain.modulation,
            domain.curves,
            &domain.automation
        ));
        const auto graphCorrupted = domain.modulation;
        const auto arenaCorrupted = domain.curves;
        assert(!history.undo(pages));
        assert(history.undoCount() == 1U);
        assert(std::memcmp(
            &domain.modulation,
            &graphCorrupted,
            sizeof(graphCorrupted)
        ) == 0);
        assert(std::memcmp(
            &domain.curves,
            &arenaCorrupted,
            sizeof(arenaCorrupted)
        ) == 0);
    }

    {
        macro::MacroPagesState pages;
        macro::MacroHistoryService history;
        const auto existing = createLfoModulator(
            pages.control.authored.modulation,
            defaultLfoDraft()
        );
        assert(existing.changed());
        auto trigger = defaultAdsrTrigger();
        trigger.sourceId = existing.sourceId;
        assert(addProjectModulationTrigger(
            pages.control.authored.modulation,
            trigger
        ).changed());
        const auto created = history.createUnassignedRecordedShape(pages, draft);
        assert(created.changed());
        auto& domain = pages.control.authored;
        domain.modulation.triggerBindings[0].sourceId = created.sourceId;
        assert(validProjectModulationDomain(
            domain.modulation,
            domain.curves,
            &domain.automation
        ));
        const auto graphCorrupted = domain.modulation;
        const auto arenaCorrupted = domain.curves;
        assert(!history.undo(pages));
        assert(history.undoCount() == 1U);
        assert(std::memcmp(
            &domain.modulation,
            &graphCorrupted,
            sizeof(graphCorrupted)
        ) == 0);
        assert(std::memcmp(
            &domain.curves,
            &arenaCorrupted,
            sizeof(arenaCorrupted)
        ) == 0);
    }
    std::cout << "[PASS] creation Undo rejects foreign binding/trigger refs\n";
}

void test_recorded_shape_apis_reject_invalid_domains_atomically() {
    using namespace core::state::modulation;
    constexpr std::array<ProjectPackedCurvePoint, 2> points{{
        {0U, -4000}, {384U, 4000},
    }};
    RecordedShapeDraft draft{};
    draft.name = "Invariant";
    draft.curve = {
        .sourceDurationTicks = 384U,
        .durationTicks = 384U,
        .valueDomain = ProjectCurveValueDomain::BIPOLAR,
    };
    draft.points = points.data();
    draft.pointCount = static_cast<uint16_t>(points.size());

    {
        macro::MacroPagesState pages;
        macro::MacroHistoryService history;
        assert(createRecordedShapeModulator(
            pages.control.authored.modulation,
            pages.control.authored.curves,
            draft
        ).changed());
        ++pages.control.authored.curves.records[0].referenceCount;
        const auto before = pages.control.authored;
        const uint32_t revision = pages.control.authoredRevision;
        const auto failed = history.createUnassignedRecordedShape(pages, draft);
        assert(failed.status == ProjectModulationStatus::INVARIANT_VIOLATION);
        assert(history.undoCount() == 0U);
        assert(pages.control.authoredRevision == revision);
        assert(std::memcmp(
            &pages.control.authored,
            &before,
            sizeof(before)
        ) == 0);
    }

    {
        macro::MacroPagesState pages;
        macro::MacroHistoryService history;
        auto secondDraft = defaultLfoDraft();
        secondDraft.name = "LFO 2";
        assert(createLfoModulator(
            pages.control.authored.modulation,
            defaultLfoDraft()
        ).changed());
        assert(createLfoModulator(
            pages.control.authored.modulation,
            secondDraft
        ).changed());
        pages.control.authored.modulation.sources[1].id =
            pages.control.authored.modulation.sources[0].id;
        const auto before = pages.control.authored;
        const auto pageBefore = pages.pageData(kAddress.track, kAddress.page);
        const auto failed = history.createAssignedRecordedShape(
            pages,
            kAddress,
            draft,
            defaultBindingDraft(),
            true
        );
        assert(failed.status == ProjectModulationStatus::INVARIANT_VIOLATION);
        assert(history.undoCount() == 0U);
        assert(std::memcmp(
            &pages.control.authored,
            &before,
            sizeof(before)
        ) == 0);
        assert(std::memcmp(
            &pages.pageData(kAddress.track, kAddress.page),
            &pageBefore,
            sizeof(pageBefore)
        ) == 0);
    }

    {
        macro::MacroPagesState pages;
        macro::MacroHistoryService history;
        assert(createLfoModulator(
            pages.control.authored.modulation,
            defaultLfoDraft()
        ).changed());
        pages.control.authored.modulation.nextSourceId =
            pages.control.authored.modulation.sources[0].id.value;
        const auto before = pages.control.authored;
        const auto failed = history.createUnassignedRecordedShape(pages, draft);
        assert(failed.status == ProjectModulationStatus::INVARIANT_VIOLATION);
        assert(history.undoCount() == 0U);
        assert(std::memcmp(
            &pages.control.authored,
            &before,
            sizeof(before)
        ) == 0);
    }

    {
        macro::MacroPagesState pages;
        macro::MacroHistoryService history;
        const auto source = createRecordedShapeModulator(
            pages.control.authored.modulation,
            pages.control.authored.curves,
            draft
        );
        assert(source.changed());
        pages.control.authored.curves.nextCurveId = source.curveId.value;
        const auto before = pages.control.authored;
        constexpr std::array<ProjectPackedCurvePoint, 3> replacement{{
            {0U, -7000}, {192U, 7000}, {384U, 0},
        }};
        const auto failed = history.replaceProjectRecordedShapeCurve(
            pages,
            source.sourceId,
            draft.curve,
            replacement.data(),
            static_cast<uint16_t>(replacement.size())
        );
        assert(failed.status == ProjectModulationStatus::INVARIANT_VIOLATION);
        assert(history.undoCount() == 0U);
        assert(std::memcmp(
            &pages.control.authored,
            &before,
            sizeof(before)
        ) == 0);
    }

    {
        macro::MacroPagesState pages;
        macro::MacroHistoryService history;
        const auto source = createRecordedShapeModulator(
            pages.control.authored.modulation,
            pages.control.authored.curves,
            draft
        );
        assert(source.changed());
        pages.control.authored.curves.points[1].tick = 385U;
        const auto before = pages.control.authored;
        constexpr std::array<ProjectPackedCurvePoint, 3> replacement{{
            {0U, -8000}, {192U, 8000}, {384U, 0},
        }};
        const auto failed = history.replaceProjectRecordedShapeCurve(
            pages,
            source.sourceId,
            draft.curve,
            replacement.data(),
            static_cast<uint16_t>(replacement.size())
        );
        assert(failed.status == ProjectModulationStatus::INVARIANT_VIOLATION);
        assert(history.undoCount() == 0U);
        assert(std::memcmp(
            &pages.control.authored,
            &before,
            sizeof(before)
        ) == 0);
    }
    std::cout << "[PASS] Recorded Shape APIs reject invalid domains atomically\n";
}

void test_recorded_shape_id_and_no_change_failures_are_atomic() {
    using namespace core::state::modulation;
    constexpr std::array<ProjectPackedCurvePoint, 2> points{{
        {0U, -5000}, {384U, 5000},
    }};
    RecordedShapeDraft draft{};
    draft.name = "Atomic";
    draft.curve = {
        .sourceDurationTicks = 384U,
        .durationTicks = 384U,
        .valueDomain = ProjectCurveValueDomain::BIPOLAR,
    };
    draft.points = points.data();
    draft.pointCount = static_cast<uint16_t>(points.size());

    {
        macro::MacroPagesState pages;
        macro::MacroHistoryService history;
        pages.control.authored.curves.nextCurveId = 0U;
        const auto before = pages.control.authored;
        const auto failed = history.createUnassignedRecordedShape(pages, draft);
        assert(failed.status == ProjectModulationStatus::ID_EXHAUSTED);
        assert(history.undoCount() == 0U);
        assert(std::memcmp(
            &pages.control.authored,
            &before,
            sizeof(before)
        ) == 0);
    }

    {
        macro::MacroPagesState pages;
        macro::MacroHistoryService history;
        const auto source = createRecordedShapeModulator(
            pages.control.authored.modulation,
            pages.control.authored.curves,
            draft
        );
        assert(source.changed());
        assert(duplicateProjectModulator(
            pages.control.authored.modulation,
            pages.control.authored.curves,
            source.sourceId,
            "Atomic 2"
        ).changed());
        pages.control.authored.curves.nextCurveId = 0U;
        assert(validProjectModulationDomain(
            pages.control.authored.modulation,
            pages.control.authored.curves,
            &pages.control.authored.automation
        ));
        const auto before = pages.control.authored;
        constexpr std::array<ProjectPackedCurvePoint, 3> replacement{{
            {0U, -9000}, {192U, 9000}, {384U, 0},
        }};
        const auto failed = history.replaceProjectRecordedShapeCurve(
            pages,
            source.sourceId,
            draft.curve,
            replacement.data(),
            static_cast<uint16_t>(replacement.size())
        );
        assert(failed.status == ProjectModulationStatus::ID_EXHAUSTED);
        assert(history.undoCount() == 0U);
        assert(std::memcmp(
            &pages.control.authored,
            &before,
            sizeof(before)
        ) == 0);
    }

    {
        macro::MacroPagesState pages;
        macro::MacroHistoryService history;
        const auto source = createRecordedShapeModulator(
            pages.control.authored.modulation,
            pages.control.authored.curves,
            draft
        );
        assert(source.changed());
        const auto before = pages.control.authored;
        const uint32_t revision = pages.control.authoredRevision;
        const auto unchanged = history.replaceProjectRecordedShapeCurve(
            pages,
            source.sourceId,
            draft.curve,
            points.data(),
            static_cast<uint16_t>(points.size())
        );
        assert(unchanged.status == ProjectModulationStatus::NO_CHANGE);
        assert(history.undoCount() == 0U);
        assert(pages.control.authoredRevision == revision);
        assert(std::memcmp(
            &pages.control.authored,
            &before,
            sizeof(before)
        ) == 0);
    }
    std::cout << "[PASS] Recorded Shape ID/NO_CHANGE failures are atomic\n";
}

void test_recorded_shape_point_capacity_failures_are_atomic() {
    using namespace core::state::modulation;
    constexpr std::array<ProjectPackedCurvePoint, 2> points{{
        {0U, -6000}, {384U, 6000},
    }};
    RecordedShapeDraft draft{};
    draft.name = "Capacity";
    draft.curve = {
        .sourceDurationTicks = 384U,
        .durationTicks = 384U,
        .valueDomain = ProjectCurveValueDomain::BIPOLAR,
    };
    draft.points = points.data();
    draft.pointCount = static_cast<uint16_t>(points.size());
    constexpr uint16_t fillerPointCount = static_cast<uint16_t>(
        PROJECT_CURVE_POINT_CAPACITY - points.size() - 1U
    );

    {
        macro::MacroPagesState pages;
        macro::MacroHistoryService history;
        const auto source = createRecordedShapeModulator(
            pages.control.authored.modulation,
            pages.control.authored.curves,
            draft
        );
        assert(source.changed());
        appendAutomationFiller(pages, fillerPointCount);
        const auto before = pages.control.authored;
        constexpr std::array<ProjectPackedCurvePoint, 4> replacement{{
            {0U, -9000}, {128U, 0}, {256U, 9000}, {384U, 0},
        }};
        const auto failed = history.replaceProjectRecordedShapeCurve(
            pages,
            source.sourceId,
            draft.curve,
            replacement.data(),
            static_cast<uint16_t>(replacement.size())
        );
        assert(failed.status ==
            ProjectModulationStatus::CURVE_POINT_CAPACITY_EXCEEDED);
        assert(history.undoCount() == 0U);
        assert(std::memcmp(
            &pages.control.authored,
            &before,
            sizeof(before)
        ) == 0);
    }

    {
        macro::MacroPagesState pages;
        macro::MacroHistoryService history;
        const auto source = createRecordedShapeModulator(
            pages.control.authored.modulation,
            pages.control.authored.curves,
            draft
        );
        assert(source.changed());
        assert(duplicateProjectModulator(
            pages.control.authored.modulation,
            pages.control.authored.curves,
            source.sourceId,
            "Capacity 2"
        ).changed());
        appendAutomationFiller(pages, fillerPointCount);
        const auto before = pages.control.authored;
        constexpr std::array<ProjectPackedCurvePoint, 2> replacement{{
            {0U, 7000}, {384U, -7000},
        }};
        const auto failed = history.replaceProjectRecordedShapeCurve(
            pages,
            source.sourceId,
            draft.curve,
            replacement.data(),
            static_cast<uint16_t>(replacement.size())
        );
        assert(failed.status ==
            ProjectModulationStatus::CURVE_POINT_CAPACITY_EXCEEDED);
        assert(history.undoCount() == 0U);
        assert(std::memcmp(
            &pages.control.authored,
            &before,
            sizeof(before)
        ) == 0);
    }
    std::cout << "[PASS] unique/COW point capacity failures are atomic\n";
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
        snapshot.automation.enabled = true;
        snapshot.automation.pointOffset = 0U;
        snapshot.automation.pointCount = 2U;
        snapshot.automation.spec = {
            .sourceDurationTicks = 768U,
            .durationTicks = 768U,
            .windowOffsetTicks = 100U,
            .interpolation = ProjectCurveInterpolation::LINEAR,
            .valueDomain = ProjectCurveValueDomain::ABSOLUTE_UNIPOLAR,
            .origin = ProjectCurveOrigin::NATIVE,
        };
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
    test_automation_timing_edits_are_compact_coalesced_and_exact();
    test_automation_timing_copy_on_write_preserves_shared_owner();
    test_automation_timing_undo_fails_closed_on_point_corruption();
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
    test_macro_create_cancel_and_failed_commit_are_exact();
    test_existing_modulator_cancel_preserves_root_and_is_byte_stable();
    test_existing_modulator_apply_undo_redo_moves_only_binding();
    test_project_source_edits_coalesce_and_restore_exact_source();
    test_project_source_rename_is_one_exact_undo_action();
    test_unassigned_lfo_creation_is_one_undo_action();
    test_unassigned_adsr_creation_includes_trigger_in_one_undo_action();
    test_adsr_audition_cancel_and_apply_are_atomic();
    test_adsr_parameters_and_trigger_route_coalesce_by_stable_identity();
    test_adsr_duplicate_copies_trigger_and_undo_is_exact();
    test_existing_assignment_cancel_and_undo_are_exact();
    test_project_modulator_split_is_one_exact_undo_action();
    test_root_delete_undo_restores_graph_and_recorded_curve_exactly();
    test_root_delete_undo_restores_shared_curve_reference();
    test_recorded_source_duplicate_undo_restores_shared_reference();
    test_unassigned_recorded_shape_creation_is_one_exact_undo_action();
    test_assigned_recorded_shape_creation_commits_topology_atomically();
    test_unique_recorded_shape_replace_restores_compacted_arena_exactly();
    test_unique_recorded_shape_shrink_restores_nonzero_tails_exactly();
    test_shared_recorded_shape_replace_is_exact_copy_on_write();
    test_recorded_shape_history_limits_fail_before_mutation();
    test_recorded_shape_undo_fails_closed_on_live_corruption();
    test_recorded_shape_redo_is_invalidated_for_all_three_apis();
    test_assigned_recorded_shape_rolls_back_post_curve_failure_exactly();
    test_recorded_shape_creation_undo_rejects_source_permutation();
    test_recorded_shape_creation_undo_rejects_new_source_references();
    test_recorded_shape_apis_reject_invalid_domains_atomically();
    test_recorded_shape_id_and_no_change_failures_are_atomic();
    test_recorded_shape_point_capacity_failures_are_atomic();
    test_multi_macro_take_is_one_atomic_undo_redo_action();
    test_assignment_history_is_destination_scoped_and_order_stable();
    test_assignment_remove_and_clear_keep_roots_and_unrelated_edges();
    test_sparse_macro_removal_purges_all_destination_state_atomically();
    std::cout << "All MacroHistory tests passed\n";
    return 0;
}
