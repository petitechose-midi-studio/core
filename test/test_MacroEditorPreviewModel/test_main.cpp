#include <cassert>
#include <cstdlib>
#include <iostream>

#include "state/macro/MacroAutomationState.hpp"
#include "ui/macro/MacroEditorPreviewModel.hpp"

// The firmware UI is not part of the native core library. Keep this pure,
// allocation-free projection under the fast CMake test gate directly.
#include "../../src/ui/macro/MacroEditorPreviewModel.cpp"

namespace {

using namespace core::state::macro;

MacroAutomationSlotState& makeSlot(
    MacroAutomationBankState& bank,
    float automationStart,
    float automationEnd,
    float modulationStart,
    float modulationEnd,
    float depth
) {
    auto* slot = macroAutomationGetOrCreateSlot(
        bank,
        {.track = 0, .page = 0, .macro = 0}
    );
    assert(slot != nullptr);
    MacroAutomationLane automation;
    assert(macroAutomationAppendPoint(automation, 0.0f, automationStart));
    assert(macroAutomationAppendPoint(automation, 1.0f, automationEnd));
    assert(macroAutomationAssignAutomation(bank, *slot, automation));
    MacroModulationShape modulation;
    assert(macroModulationAppendPoint(modulation, 0.0f, modulationStart));
    assert(macroModulationAppendPoint(modulation, 1.0f, modulationEnd));
    assert(macroAutomationAssignModulation(bank, *slot, modulation));
    slot->modulationDepth = depth;
    return *slot;
}

void test_manual_disengages_only_automation() {
    MacroAutomationBankState bank;
    auto& slot = makeSlot(bank, 0.1f, 0.9f, -0.2f, 0.2f, 1.0f);
    const auto model = core::ui::buildMacroEditorPreviewModel(
        0.5f,
        &slot,
        bank.pointPool,
        true
    );
    assert(model.automationStored);
    assert(model.modulationStored);
    assert(!model.automationPlayback);
    assert(model.modulationPlayback);
    assert(!model.automationDrivingBase);
    for (const auto base : model.base) assert(base == 128U);
    assert(model.automation.front() < model.automation.back());
    assert(model.out.front() < model.out.back());
    std::cout << "[PASS] test_manual_disengages_only_automation\n";
}

void test_out_clamps_and_reports_both_clip_directions() {
    MacroAutomationBankState bank;
    auto& slot = makeSlot(bank, 0.5f, 0.5f, -1.0f, 1.0f, 1.0f);
    slot.automation.playbackState = MacroCurvePlaybackState::OFF;
    const auto model = core::ui::buildMacroEditorPreviewModel(
        0.5f,
        &slot,
        bank.pointPool,
        false
    );
    assert(model.clippedLow);
    assert(model.clippedHigh);
    assert(model.out.front() == 0U);
    assert(model.out.back() == 255U);
    std::cout << "[PASS] test_out_clamps_and_reports_both_clip_directions\n";
}

void test_modulation_off_preserves_stored_preview_but_not_output_motion() {
    MacroAutomationBankState bank;
    auto& slot = makeSlot(bank, 0.25f, 0.75f, -0.5f, 0.5f, 1.0f);
    slot.modulation.playbackState = MacroCurvePlaybackState::OFF;
    const auto model = core::ui::buildMacroEditorPreviewModel(
        0.25f,
        &slot,
        bank.pointPool,
        false
    );
    assert(model.modulationStored);
    assert(!model.modulationPlayback);
    assert(model.modulation.front() < model.modulation.back());
    for (size_t i = 0; i < model.out.size(); ++i) {
        assert(model.out[i] == model.base[i]);
    }
    std::cout << "[PASS] test_modulation_off_preserves_stored_preview_but_not_output_motion\n";
}

void test_sources_share_one_timeline_and_keep_independent_loop_rates() {
    MacroAutomationBankState bank;
    auto* slot = macroAutomationGetOrCreateSlot(
        bank,
        {.track = 0, .page = 0, .macro = 0}
    );
    assert(slot != nullptr);

    MacroAutomationLane automation;
    automation.durationBeats = 1.0f;
    assert(macroAutomationAppendPoint(automation, 0.0f, 0.2f));
    assert(macroAutomationAppendPoint(automation, 1.0f, 0.8f));
    assert(macroAutomationAssignAutomation(bank, *slot, automation));

    MacroModulationShape modulation;
    modulation.durationBeats = 2.0f;
    assert(macroModulationAppendPoint(modulation, 0.0f, -0.25f));
    assert(macroModulationAppendPoint(modulation, 2.0f, 0.25f));
    assert(macroAutomationAssignModulation(bank, *slot, modulation));
    slot->modulationDepth = 1.0f;

    const auto model = core::ui::buildMacroEditorPreviewModel(
        0.5f,
        slot,
        bank.pointPool,
        false
    );
    assert(model.timelineDurationTicks == 2U * MACRO_AUTOMATION_TICKS_PER_BEAT);
    static_assert(core::ui::MACRO_EDITOR_PREVIEW_SAMPLE_COUNT == 64U);
    const int start = model.automation.front();
    const int secondCycleStart = model.automation[32];
    assert(std::abs(start - secondCycleStart) < 12);
    assert(model.automation[15] > model.automation.front());

    std::cout
        << "[PASS] preview shares timeline and preserves independent loop rates\n";
}

}  // namespace

int main() {
    test_manual_disengages_only_automation();
    test_out_clamps_and_reports_both_clip_directions();
    test_modulation_off_preserves_stored_preview_but_not_output_motion();
    test_sources_share_one_timeline_and_keep_independent_loop_rates();
    std::cout << "All MacroEditorPreviewModel tests passed.\n";
    return 0;
}
