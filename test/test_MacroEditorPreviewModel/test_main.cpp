#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <iostream>

#include "state/macro/MacroAutomationState.hpp"
#include "state/modulation/ProjectModulationDomainOps.hpp"
#include "ui/macro/MacroEditorLiveTrace.hpp"
#include "ui/macro/MacroEditorPreviewModel.hpp"

// The firmware UI is not part of the native core library. Keep this pure,
// allocation-free projection under the fast CMake test gate directly.
#include "../../src/ui/macro/MacroEditorPreviewModel.cpp"

namespace {

using namespace core::state::macro;

core::ui::MacroEditorPreviewSample sampleAt(
    const core::ui::MacroEditorPreviewModel& model,
    core::ui::MacroEditorPreviewFocus focus,
    uint16_t positionQ16
) {
    core::ui::MacroEditorPreviewSample sample{};
    assert(core::ui::sampleMacroEditorPreview(
        model,
        focus,
        positionQ16,
        0U,
        false,
        sample
    ));
    return sample;
}

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
    const auto first = sampleAt(
        model,
        core::ui::MacroEditorPreviewFocus::DESTINATION,
        0U
    );
    const auto last = sampleAt(
        model,
        core::ui::MacroEditorPreviewFocus::DESTINATION,
        65535U
    );
    assert(std::abs(static_cast<int>(first.baseQ16) - 32768) <= 1);
    assert(std::abs(static_cast<int>(last.baseQ16) - 32768) <= 1);
    assert(first.automationQ16 < last.automationQ16);
    assert(first.outQ16 < last.outQ16);
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
    const auto first = sampleAt(
        model,
        core::ui::MacroEditorPreviewFocus::DESTINATION,
        0U
    );
    const auto last = sampleAt(
        model,
        core::ui::MacroEditorPreviewFocus::DESTINATION,
        65535U
    );
    assert(first.clippedLow);
    assert(last.clippedHigh);
    assert(first.outQ16 == 0U);
    assert(last.outQ16 == 65535U);
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
    const auto first = sampleAt(
        model,
        core::ui::MacroEditorPreviewFocus::DESTINATION,
        0U
    );
    const auto last = sampleAt(
        model,
        core::ui::MacroEditorPreviewFocus::DESTINATION,
        65535U
    );
    assert(first.modulationQ15 < last.modulationQ15);
    assert(first.outQ16 == first.baseQ16);
    assert(last.outQ16 == last.baseQ16);
    std::cout << "[PASS] test_modulation_off_preserves_stored_preview_but_not_output_motion\n";
}

void test_domains_keep_their_own_truthful_timelines() {
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
    assert(model.automationDurationTicks == MACRO_AUTOMATION_TICKS_PER_BEAT);
    assert(model.modulationDurationTicks ==
           2U * MACRO_AUTOMATION_TICKS_PER_BEAT);
    assert(model.timelineDurationTicks == 2U * MACRO_AUTOMATION_TICKS_PER_BEAT);
    static_assert(sizeof(core::ui::MacroEditorPreviewModel) <= 96U);
    const auto automationStart = sampleAt(
        model,
        core::ui::MacroEditorPreviewFocus::AUTOMATION,
        0U
    );
    const auto automationEnd = sampleAt(
        model,
        core::ui::MacroEditorPreviewFocus::AUTOMATION,
        65535U
    );
    const auto modulationStart = sampleAt(
        model,
        core::ui::MacroEditorPreviewFocus::ALL_MODULATION,
        0U
    );
    const auto modulationEnd = sampleAt(
        model,
        core::ui::MacroEditorPreviewFocus::ALL_MODULATION,
        65535U
    );
    assert(automationStart.automationQ16 < automationEnd.automationQ16);
    assert(modulationStart.modulationQ15 < modulationEnd.modulationQ15);

    std::cout
        << "[PASS] Automation loop and Modulation cycle keep distinct timelines\n";
}

void test_project_preview_applies_destination_global_depth() {
    namespace mod = core::state::modulation;
    mod::ProjectControlState control{};
    const MacroAutomationSlotAddress address{.track = 0, .page = 0, .macro = 0};
    const auto target = mod::projectControlDestination(address);
    mod::ModulatorLfoDraft source{};
    source.name = "Square";
    source.parameters.shape = mod::ModulatorLfoShape::SQUARE;
    const auto created = mod::createLfoModulator(control.authored.modulation, source);
    assert(created.changed());
    mod::ModulationBindingDraft binding{};
    binding.sourceId = created.sourceId;
    binding.destination = target;
    binding.amountQ15 = 16384;
    assert(mod::addProjectModulationBinding(
        control.authored.modulation,
        binding
    ).changed());

    core::ui::MacroEditorPreviewModel unity{};
    core::ui::buildMacroEditorPreviewModel(
        0.5f,
        control,
        address,
        false,
        unity
    );
    const auto peakFor = [](const core::ui::MacroEditorPreviewModel& model) {
        int peak = 0;
        for (uint16_t index = 0; index < 64U; ++index) {
            const uint16_t position = static_cast<uint16_t>(
                (static_cast<uint32_t>(index) * 65535U) / 63U
            );
            peak = std::max(
                peak,
                std::abs(static_cast<int>(sampleAt(
                    model,
                    core::ui::MacroEditorPreviewFocus::ALL_MODULATION,
                    position
                ).modulationQ15))
            );
        }
        return peak;
    };
    const int unityPeak = peakFor(unity);
    assert(mod::setProjectModulationDestinationScale(
        control.authored.modulation,
        target,
        16384U
    ).changed());
    core::ui::MacroEditorPreviewModel half{};
    core::ui::buildMacroEditorPreviewModel(
        0.5f,
        control,
        address,
        false,
        half
    );
    const int halfPeak = peakFor(half);
    assert(unityPeak > 0 && halfPeak > 0);
    assert(std::abs(unityPeak - halfPeak * 2) <= 2);
    std::cout << "[PASS] Project preview reflects destination Global Depth\n";
}

void test_project_square_reports_explicit_discontinuity() {
    namespace mod = core::state::modulation;
    mod::ProjectControlState control{};
    const MacroAutomationSlotAddress address{.track = 0, .page = 0, .macro = 0};
    mod::ModulatorLfoDraft source{};
    source.name = "Square";
    source.parameters.shape = mod::ModulatorLfoShape::SQUARE;
    const auto created = mod::createLfoModulator(control.authored.modulation, source);
    assert(created.changed());
    mod::ModulationBindingDraft binding{};
    binding.sourceId = created.sourceId;
    binding.destination = mod::projectControlDestination(address);
    binding.amountQ15 = 16384;
    const auto bound = mod::addProjectModulationBinding(
        control.authored.modulation,
        binding
    );
    assert(bound.changed());
    core::ui::MacroEditorPreviewModel model{};
    core::ui::buildMacroEditorPreviewModel(
        0.5f,
        control,
        address,
        false,
        bound.bindingId,
        {},
        model
    );
    core::ui::MacroEditorPreviewSample sample{};
    assert(core::ui::sampleMacroEditorPreview(
        model,
        core::ui::MacroEditorPreviewFocus::FOCUSED_MODULATOR,
        32768U,
        32000U,
        true,
        sample
    ));
    assert(sample.discontinuityBefore);
    std::cout << "[PASS] Project square exposes its authored edge\n";
}

void test_live_trace_is_bounded_contextual_and_time_true() {
    core::ui::MacroEditorLiveTrace trace{};
    trace.append(0x000001U, 1000U, {
        .base = 0.0f,
        .modulation = -1.0f,
        .out = 0.0f,
        .valid = true,
    });
    trace.append(0x000001U, 2000U, {
        .base = 1.0f,
        .modulation = 1.0f,
        .out = 1.0f,
        .valid = true,
    });
    core::ui::MacroEditorLiveTrace::Cursor cursor{};
    core::ui::MacroEditorPreviewSample sample{};
    assert(trace.sample(49152U, 2000U, cursor, sample));
    assert(std::abs(static_cast<int>(sample.baseQ16) - 32768) <= 2);
    assert(std::abs(static_cast<int>(sample.modulationQ15)) <= 2);
    assert(std::abs(static_cast<int>(sample.outQ16) - 32768) <= 260);

    trace.append(0x000002U, 2100U, {
        .base = 0.25f,
        .out = 0.25f,
        .valid = true,
    });
    assert(trace.count() == 1U);
    for (uint16_t index = 0U;
         index < core::ui::MACRO_EDITOR_LIVE_TRACE_CAPACITY + 1U;
         ++index) {
        trace.append(0x000002U, 2200U + index, {
            .base = 0.5f,
            .out = 0.5f,
            .valid = true,
        });
    }
    assert(trace.count() == core::ui::MACRO_EDITOR_LIVE_TRACE_CAPACITY);
    static_assert(sizeof(core::ui::MacroEditorLiveTrace) <= 4096U);
    std::cout << "[PASS] rolling Destination trace is bounded and contextual\n";
}

}  // namespace

int main() {
    test_manual_disengages_only_automation();
    test_out_clamps_and_reports_both_clip_directions();
    test_modulation_off_preserves_stored_preview_but_not_output_motion();
    test_domains_keep_their_own_truthful_timelines();
    test_project_preview_applies_destination_global_depth();
    test_project_square_reports_explicit_discontinuity();
    test_live_trace_is_bounded_contextual_and_time_true();
    std::cout << "All MacroEditorPreviewModel tests passed.\n";
    return 0;
}
