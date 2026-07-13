#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <cmath>
#include <iostream>

#include "../../src/state/macro/MacroAutomationState.hpp"

namespace {

namespace macro = core::state::macro;

constexpr uint8_t kFullPoolLaneCount =
    macro::MACRO_AUTOMATION_POINT_POOL_CAPACITY /
    macro::MACRO_AUTOMATION_RECORDING_MAX_POINTS;
static_assert(kFullPoolLaneCount <= macro::TRACK_COUNT);

bool near(float lhs, float rhs, float epsilon = 0.0001f) {
    return std::fabs(lhs - rhs) <= epsilon;
}

void fillDenseLane(macro::MacroAutomationLane& lane,
                   uint16_t pointCount = macro::MACRO_AUTOMATION_RECORDING_MAX_POINTS) {
    lane.active = true;
    lane.durationBeats = 300.0f;
    lane.pointCount = 0;
    for (uint16_t i = 0; i < pointCount; ++i) {
        const float beat = static_cast<float>(i) * 0.125f;
        const float value = (i & 1U) == 0U ? 0.25f : 0.75f;
        assert(macro::macroAutomationAppendPoint(lane, beat, value));
    }
}

void fillAutomationPool(macro::MacroAutomationBankState& bank) {
    macro::MacroAutomationLane lane;
    fillDenseLane(lane);

    for (uint8_t track = 0; track < kFullPoolLaneCount; ++track) {
        auto* slot = macro::macroAutomationGetOrCreateSlot(
            bank,
            macro::MacroAutomationSlotAddress{.track = track, .page = 0, .macro = 0}
        );
        assert(slot != nullptr);
        assert(macro::macroAutomationAssignAutomation(bank, *slot, lane));
    }
    assert(bank.pointPool.used == macro::MACRO_AUTOMATION_POINT_POOL_CAPACITY);
}

void test_macro_automation_bank_creates_finds_and_clears_slots() {
    macro::MacroAutomationBankState bank;
    const macro::MacroAutomationSlotAddress address{.track = 1, .page = 2, .macro = 3};

    assert(macro::macroAutomationFindSlot(bank, address) == nullptr);
    auto* slot = macro::macroAutomationGetOrCreateSlot(bank, address);
    assert(slot != nullptr);
    assert(bank.entryCount == 1);

    slot->modulationDepth = 0.5f;
    assert(macro::macroAutomationFindSlot(bank, address)->modulationDepth == 0.5f);
    assert(macro::macroAutomationGetOrCreateSlot(bank, address) == slot);
    assert(bank.entryCount == 1);

    assert(macro::macroAutomationClearSlot(bank, address));
    assert(bank.entryCount == 0);
    assert(macro::macroAutomationFindSlot(bank, address) == nullptr);

    std::cout << "[PASS] test_macro_automation_bank_creates_finds_and_clears_slots\n";
}

void test_macro_automation_bank_rejects_invalid_or_full_slots() {
    macro::MacroAutomationBankState bank;
    assert(macro::macroAutomationGetOrCreateSlot(
               bank,
               macro::MacroAutomationSlotAddress{
                   .track = macro::TRACK_COUNT,
                   .page = 0,
                   .macro = 0,
               }) == nullptr);

    for (uint8_t i = 0; i < macro::MACRO_AUTOMATION_SLOT_CAPACITY; ++i) {
        auto* slot = macro::macroAutomationGetOrCreateSlot(
            bank,
            macro::MacroAutomationSlotAddress{
                .track = static_cast<uint8_t>(i / macro::PAGE_COUNT),
                .page = static_cast<uint8_t>(i % macro::PAGE_COUNT),
                .macro = 0,
            }
        );
        assert(slot != nullptr);
    }
    assert(bank.entryCount == macro::MACRO_AUTOMATION_SLOT_CAPACITY);
    assert(macro::macroAutomationGetOrCreateSlot(
               bank,
               macro::MacroAutomationSlotAddress{.track = 15, .page = 15, .macro = 7}) ==
           nullptr);

    std::cout << "[PASS] test_macro_automation_bank_rejects_invalid_or_full_slots\n";
}

void test_macro_automation_point_pool_rejects_full_pool_without_mutating_destination() {
    macro::MacroAutomationBankState bank;
    fillAutomationPool(bank);

    auto* slot = macro::macroAutomationGetOrCreateSlot(
        bank,
        macro::MacroAutomationSlotAddress{.track = 0, .page = 1, .macro = 0}
    );
    assert(slot != nullptr);
    assert(!slot->automation.active);

    macro::MacroAutomationLane lane;
    assert(macro::macroAutomationAppendPoint(lane, 0.0f, 0.5f));
    assert(!macro::macroAutomationAssignAutomation(bank, *slot, lane));
    assert(!slot->automation.active);
    assert(bank.pointPool.used == macro::MACRO_AUTOMATION_POINT_POOL_CAPACITY);

    std::cout
        << "[PASS] "
        << "test_macro_automation_point_pool_rejects_full_pool_without_mutating_destination\n";
}

void test_macro_automation_replacement_reclaims_existing_curve_capacity() {
    macro::MacroAutomationBankState bank;
    fillAutomationPool(bank);

    auto* slot = macro::macroAutomationFindMutableSlot(
        bank,
        macro::MacroAutomationSlotAddress{.track = 3, .page = 0, .macro = 0}
    );
    assert(slot != nullptr);
    assert(slot->automation.active);

    macro::MacroAutomationLane replacement;
    fillDenseLane(replacement);
    assert(macro::macroAutomationAssignAutomation(bank, *slot, replacement));
    assert(slot->automation.active);
    assert(slot->automation.pointCount == macro::MACRO_AUTOMATION_RECORDING_MAX_POINTS);
    assert(bank.pointPool.used == macro::MACRO_AUTOMATION_POINT_POOL_CAPACITY);

    std::cout
        << "[PASS] test_macro_automation_replacement_reclaims_existing_curve_capacity\n";
}

void test_macro_automation_compaction_preserves_multicurve_references() {
    macro::MacroAutomationBankState bank;
    auto* first = macro::macroAutomationGetOrCreateSlot(
        bank,
        macro::MacroAutomationSlotAddress{.track = 0, .page = 0, .macro = 0}
    );
    auto* second = macro::macroAutomationGetOrCreateSlot(
        bank,
        macro::MacroAutomationSlotAddress{.track = 0, .page = 0, .macro = 1}
    );
    assert(first != nullptr);
    assert(second != nullptr);

    macro::MacroAutomationLane firstAutomation;
    firstAutomation.durationBeats = 4.0f;
    assert(macro::macroAutomationAppendPoint(firstAutomation, 0.0f, 0.1f));
    assert(macro::macroAutomationAppendPoint(firstAutomation, 2.0f, 0.9f));
    assert(macro::macroAutomationAssignAutomation(bank, *first, firstAutomation));

    macro::MacroModulationShape firstModulation;
    firstModulation.durationBeats = 4.0f;
    assert(macro::macroModulationAppendPoint(firstModulation, 0.0f, -0.4f));
    assert(macro::macroModulationAppendPoint(firstModulation, 2.0f, 0.4f));
    assert(macro::macroAutomationAssignModulation(bank, *first, firstModulation));
    first->modulationDepth = 0.5f;

    macro::MacroAutomationLane secondAutomation;
    secondAutomation.durationBeats = 4.0f;
    assert(macro::macroAutomationAppendPoint(secondAutomation, 0.0f, 0.2f));
    assert(macro::macroAutomationAppendPoint(secondAutomation, 1.0f, 0.8f));
    assert(macro::macroAutomationAppendPoint(secondAutomation, 4.0f, 0.2f));
    assert(macro::macroAutomationAssignAutomation(bank, *second, secondAutomation));

    assert(bank.pointPool.used == 7);
    macro::macroAutomationClearAutomation(bank, *first);

    assert(!first->automation.active);
    assert(first->modulation.active);
    assert(second->automation.active);
    assert(bank.pointPool.used == 5);
    assert(first->modulation.pointOffset == 0);
    assert(second->automation.pointOffset == 2);

    assert(near(
        macro::macroModulationEvaluate(first->modulation, bank.pointPool, 2.0f),
        0.4f
    ));
    assert(near(
        macro::macroAutomationEvaluate(second->automation, bank.pointPool, 1.0f, 0.0f),
        0.8f
    ));

    std::cout
        << "[PASS] test_macro_automation_compaction_preserves_multicurve_references\n";
}

void test_macro_automation_copy_failure_preserves_existing_destination() {
    macro::MacroAutomationBankState sourceBank;
    auto* source = macro::macroAutomationGetOrCreateSlot(
        sourceBank,
        macro::MacroAutomationSlotAddress{.track = 0, .page = 0, .macro = 0}
    );
    assert(source != nullptr);
    macro::MacroAutomationLane sourceLane;
    fillDenseLane(sourceLane);
    assert(macro::macroAutomationAssignAutomation(sourceBank, *source, sourceLane));

    macro::MacroAutomationBankState destBank;
    fillAutomationPool(destBank);
    auto* dest = macro::macroAutomationGetOrCreateSlot(
        destBank,
        macro::MacroAutomationSlotAddress{.track = 0, .page = 1, .macro = 0}
    );
    assert(dest != nullptr);
    dest->modulationDepth = 0.42f;

    assert(!macro::macroAutomationCopySlotState(
        destBank,
        *dest,
        sourceBank.pointPool,
        *source
    ));
    assert(!dest->automation.active);
    assert(near(dest->modulationDepth, 0.42f));

    std::cout << "[PASS] test_macro_automation_copy_failure_preserves_existing_destination\n";
}

void test_macro_automation_dense_curve_evaluates_interpolation_and_wrapped_window() {
    macro::MacroAutomationBankState bank;
    auto* slot = macro::macroAutomationGetOrCreateSlot(
        bank,
        macro::MacroAutomationSlotAddress{.track = 0, .page = 0, .macro = 0}
    );
    assert(slot != nullptr);

    macro::MacroAutomationLane lane;
    fillDenseLane(lane);
    assert(macro::macroAutomationAssignAutomation(bank, *slot, lane));
    assert(slot->automation.pointCount == macro::MACRO_AUTOMATION_RECORDING_MAX_POINTS);

    assert(near(
        macro::macroAutomationEvaluate(slot->automation, bank.pointPool, 0.0f, 0.0f),
        0.25f
    ));
    assert(near(
        macro::macroAutomationEvaluate(slot->automation, bank.pointPool, 0.0625f, 0.0f),
        0.5f
    ));
    assert(near(
        macro::macroAutomationEvaluate(slot->automation, bank.pointPool, 0.125f, 0.0f),
        0.75f
    ));

    slot->automation.windowOffsetTicks =
        static_cast<uint16_t>(slot->automation.sourceDurationTicks - 12U);
    assert(near(
        macro::macroAutomationEvaluate(slot->automation, bank.pointPool, 0.125f, 0.0f),
        0.5f
    ));

    std::cout
        << "[PASS] "
        << "test_macro_automation_dense_curve_evaluates_interpolation_and_wrapped_window\n";
}

void test_conversion_preview_is_non_mutating_and_commit_is_atomic() {
    macro::MacroAutomationBankState bank;
    const macro::MacroAutomationSlotAddress address{.track = 1, .page = 2, .macro = 3};
    auto* slot = macro::macroAutomationGetOrCreateSlot(bank, address);
    assert(slot != nullptr);

    macro::MacroAutomationLane lane;
    lane.durationBeats = 2.0f;
    assert(macro::macroAutomationAppendPoint(lane, 0.0f, 0.25f));
    assert(macro::macroAutomationAppendPoint(lane, 2.0f, 0.75f));
    assert(macro::macroAutomationAssignAutomation(bank, *slot, lane));

    float staticBase = 0.33f;
    const uint16_t usedBefore = bank.pointPool.used;
    const auto automationBefore = slot->automation;
    const auto firstPointBefore = bank.pointPool.points[slot->automation.pointOffset];
    const auto plan = macro::macroAutomationPreflightConversion(
        bank,
        address,
        macro::MacroAutomationConversionPolicy::MEAN,
        staticBase
    );
    assert(plan.status == macro::MacroAutomationConversionStatus::READY);
    assert(plan.actionable());
    assert(!plan.overwritesModulation);
    assert(plan.pointCount == 2);
    assert(near(plan.reference, 0.5f));
    assert(near(staticBase, 0.33f));
    assert(bank.pointPool.used == usedBefore);
    assert(slot->automation.pointOffset == automationBefore.pointOffset);
    assert(slot->automation.playbackState == macro::MacroCurvePlaybackState::ACTIVE);
    assert(!slot->modulation.active);
    assert(bank.pointPool.points[slot->automation.pointOffset].tick == firstPointBefore.tick);
    assert(bank.pointPool.points[slot->automation.pointOffset].value == firstPointBefore.value);

    assert(macro::macroAutomationApplyConversion(bank, staticBase, plan, false));
    slot = macro::macroAutomationFindMutableSlot(bank, address);
    assert(slot != nullptr);
    assert(near(staticBase, 0.5f));
    assert(slot->automation.active);
    assert(slot->automation.playbackState == macro::MacroCurvePlaybackState::OFF);
    assert(slot->modulation.active);
    assert(slot->modulation.playbackState == macro::MacroCurvePlaybackState::ACTIVE);
    assert(slot->modulation.modulationOrigin == macro::MacroModulationOrigin::CONVERTED_MEAN);
    assert(near(slot->modulationDepth, 1.0f));
    assert(near(macro::macroModulationEvaluate(slot->modulation, bank.pointPool, 0.0f), -0.25f));
    macro::MacroCurvePoint convertedTail{};
    assert(macro::macroAutomationReadPoint(
        slot->modulation,
        bank.pointPool,
        1,
        true,
        convertedTail
    ));
    assert(near(convertedTail.value, 0.25f));

    std::cout << "[PASS] test_conversion_preview_is_non_mutating_and_commit_is_atomic\n";
}

void test_conversion_requires_overwrite_confirmation_and_rejects_stale_plan() {
    macro::MacroAutomationBankState bank;
    const macro::MacroAutomationSlotAddress address{.track = 0, .page = 0, .macro = 0};
    auto* slot = macro::macroAutomationGetOrCreateSlot(bank, address);
    assert(slot != nullptr);

    macro::MacroAutomationLane automation;
    assert(macro::macroAutomationAppendPoint(automation, 0.0f, 0.2f));
    assert(macro::macroAutomationAppendPoint(automation, 1.0f, 0.8f));
    assert(macro::macroAutomationAssignAutomation(bank, *slot, automation));
    macro::MacroModulationShape modulation;
    assert(macro::macroModulationAppendPoint(modulation, 0.0f, -0.1f));
    assert(macro::macroModulationAppendPoint(modulation, 1.0f, 0.1f));
    assert(macro::macroAutomationAssignModulation(bank, *slot, modulation));
    slot->modulationDepth = 0.4f;

    float staticBase = 0.6f;
    const auto overwritePlan = macro::macroAutomationPreflightConversion(
        bank,
        address,
        macro::MacroAutomationConversionPolicy::FIRST,
        staticBase
    );
    assert(overwritePlan.status == macro::MacroAutomationConversionStatus::OVERWRITE_REQUIRED);
    const uint16_t usedBefore = bank.pointPool.used;
    const uint32_t targetFingerprint = overwritePlan.targetFingerprint;
    assert(!macro::macroAutomationApplyConversion(bank, staticBase, overwritePlan, false));
    assert(bank.pointPool.used == usedBefore);
    assert(near(staticBase, 0.6f));
    assert(macro::macroAutomationPreflightConversion(
               bank,
               address,
               macro::MacroAutomationConversionPolicy::FIRST,
               staticBase
           ).targetFingerprint == targetFingerprint);

    staticBase = 0.7f;
    assert(!macro::macroAutomationApplyConversion(bank, staticBase, overwritePlan, true));
    assert(bank.pointPool.used == usedBefore);
    assert(near(staticBase, 0.7f));

    staticBase = 0.6f;
    const auto freshPlan = macro::macroAutomationPreflightConversion(
        bank,
        address,
        macro::MacroAutomationConversionPolicy::MIN,
        staticBase
    );
    slot->automation.playbackState = macro::MacroCurvePlaybackState::OFF;
    assert(!macro::macroAutomationApplyConversion(bank, staticBase, freshPlan, true));
    assert(bank.pointPool.used == usedBefore);
    assert(near(staticBase, 0.6f));

    std::cout << "[PASS] test_conversion_requires_overwrite_confirmation_and_rejects_stale_plan\n";
}

void test_conversion_pool_exhaustion_has_no_partial_mutation() {
    macro::MacroAutomationBankState bank;
    fillAutomationPool(bank);
    const macro::MacroAutomationSlotAddress address{.track = 0, .page = 0, .macro = 0};
    auto* slot = macro::macroAutomationFindMutableSlot(bank, address);
    assert(slot != nullptr);
    const auto sourceBefore = slot->automation;
    float staticBase = 0.42f;

    const auto plan = macro::macroAutomationPreflightConversion(
        bank,
        address,
        macro::MacroAutomationConversionPolicy::MEAN,
        staticBase
    );
    assert(plan.status == macro::MacroAutomationConversionStatus::POINT_POOL_EXHAUSTED);
    assert(!plan.actionable());
    assert(!macro::macroAutomationApplyConversion(bank, staticBase, plan, true));
    assert(bank.pointPool.used == macro::MACRO_AUTOMATION_POINT_POOL_CAPACITY);
    assert(slot->automation.pointOffset == sourceBefore.pointOffset);
    assert(slot->automation.pointCount == sourceBefore.pointCount);
    assert(slot->automation.playbackState == macro::MacroCurvePlaybackState::ACTIVE);
    assert(!slot->modulation.active);
    assert(near(staticBase, 0.42f));

    std::cout << "[PASS] test_conversion_pool_exhaustion_has_no_partial_mutation\n";
}

}  // namespace

int main() {
    std::cout << "==============================================\n";
    std::cout << "MacroAutomationState tests\n";
    std::cout << "==============================================\n\n";

    test_macro_automation_bank_creates_finds_and_clears_slots();
    test_macro_automation_bank_rejects_invalid_or_full_slots();
    test_macro_automation_point_pool_rejects_full_pool_without_mutating_destination();
    test_macro_automation_replacement_reclaims_existing_curve_capacity();
    test_macro_automation_compaction_preserves_multicurve_references();
    test_macro_automation_copy_failure_preserves_existing_destination();
    test_macro_automation_dense_curve_evaluates_interpolation_and_wrapped_window();
    test_conversion_preview_is_non_mutating_and_commit_is_atomic();
    test_conversion_requires_overwrite_confirmation_and_rejects_stale_plan();
    test_conversion_pool_exhaustion_has_no_partial_mutation();

    std::cout << "\n==============================================\n";
    std::cout << "All tests passed\n";
    std::cout << "==============================================\n";
    return 0;
}
