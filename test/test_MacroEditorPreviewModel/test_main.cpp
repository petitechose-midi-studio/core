#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <iostream>

#include "state/macro/MacroAutomationAddress.hpp"
#include "state/macro/MacroAutomationDomain.hpp"
#include "state/modulation/ProjectModulationDomainOps.hpp"
#include "state/modulation/ProjectModulationRuntimePlan.hpp"
#include "state/modulation/ProjectRecordedShapeCaptureState.hpp"
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

core::state::modulation::ProjectModulationResult addRecordedShape(
    core::state::modulation::ProjectControlState& control,
    const core::state::modulation::ModulationDestination& destination,
    int16_t startQ15,
    int16_t endQ15,
    int16_t amountQ15,
    uint16_t durationTicks,
    const char* name,
    bool enabled = true
) {
    namespace mod = core::state::modulation;
    const std::array<mod::ProjectPackedCurvePoint, 2> points{{
        {0U, startQ15},
        {durationTicks, endQ15},
    }};
    const mod::RecordedShapeDraft source{
        .name = name,
        .curve = {
            .sourceDurationTicks = durationTicks,
            .durationTicks = durationTicks,
            .windowOffsetTicks = 0U,
            .interpolation = mod::ProjectCurveInterpolation::LINEAR,
            .valueDomain = mod::ProjectCurveValueDomain::BIPOLAR,
            .origin = mod::ProjectCurveOrigin::NATIVE,
        },
        .points = points.data(),
        .pointCount = static_cast<uint16_t>(points.size()),
    };
    const auto created = mod::createRecordedShapeModulator(
        control.authored.modulation,
        control.authored.curves,
        source
    );
    assert(created.changed());
    mod::ModulationBindingDraft binding{};
    binding.sourceId = created.sourceId;
    binding.destination = destination;
    binding.amountQ15 = amountQ15;
    binding.enabled = enabled;
    const auto bound = mod::addProjectModulationBinding(
        control.authored.modulation,
        binding
    );
    assert(bound.changed());
    return bound;
}

core::state::modulation::ProjectModulationResult addConstantRecordedShape(
    core::state::modulation::ProjectControlState& control,
    const core::state::modulation::ModulationDestination& destination,
    int16_t valueQ15,
    int16_t amountQ15,
    const char* name
) {
    return addRecordedShape(
        control,
        destination,
        valueQ15,
        valueQ15,
        amountQ15,
        MACRO_AUTOMATION_TICKS_PER_BEAT,
        name
    );
}

core::state::modulation::ProjectModulationResult addLfo(
    core::state::modulation::ProjectControlState& control,
    const core::state::modulation::ModulationDestination& destination,
    core::state::modulation::ModulatorTimingMode timing,
    uint32_t periodTicks,
    uint32_t freePeriodMs,
    bool enabled = true
) {
    namespace mod = core::state::modulation;
    mod::ModulatorLfoDraft source{};
    source.name = "Timeline LFO";
    source.parameters.timing = timing;
    source.parameters.periodTicks = periodTicks;
    source.parameters.freePeriodMs = freePeriodMs;
    source.parameters.shape = mod::ModulatorLfoShape::SINE;
    const auto created = mod::createLfoModulator(
        control.authored.modulation,
        source
    );
    assert(created.changed());
    mod::ModulationBindingDraft binding{};
    binding.sourceId = created.sourceId;
    binding.destination = destination;
    binding.amountQ15 = 32767;
    binding.enabled = enabled;
    const auto bound = mod::addProjectModulationBinding(
        control.authored.modulation,
        binding
    );
    assert(bound.changed());
    return bound;
}

void assignAutomation(
    core::state::modulation::ProjectControlState& control,
    const MacroAutomationSlotAddress& address,
    float start,
    float end,
    float durationBeats = 1.0f
) {
    MacroAutomationLane lane{};
    lane.durationBeats = durationBeats;
    assert(macroAutomationAppendPoint(lane, 0.0f, start));
    assert(macroAutomationAppendPoint(lane, durationBeats, end));
    assert(core::state::modulation::assignProjectControlAutomation(
        control,
        address,
        lane
    ));
}

void assignConstantAutomation(
    core::state::modulation::ProjectControlState& control,
    const MacroAutomationSlotAddress& address,
    float value
) {
    assignAutomation(control, address, value, value);
}

void configureCapture(
    core::state::modulation::ProjectRecordedShapeCaptureState& capture,
    core::state::modulation::ProjectRecordedShapeCaptureMode mode,
    const core::state::modulation::ModulationDestination& destination,
    core::state::modulation::ModulatorId sourceId,
    int16_t amountQ15,
    int16_t valueQ15,
    uint16_t durationTicks
) {
    namespace mod = core::state::modulation;
    capture.take = core::app::makeExtmemUnique<mod::ProjectRecordedShapeTake>();
    assert(capture.take != nullptr);
    assert(capture.take->begin(durationTicks, 0U));
    capture.take->values.fill(valueQ15);
    capture.take->currentValue = valueQ15;
    capture.take->touched = true;
    capture.take->changed = true;
    capture.mode = mode;
    capture.status = mod::ProjectRecordedShapeCaptureStatus::RECORDING;
    capture.destination = destination;
    capture.sourceId = sourceId;
    capture.amountQ15 = amountQ15;
    capture.durationTicks = durationTicks;
    capture.enabled = true;
}

void test_manual_disengages_only_automation() {
    namespace mod = core::state::modulation;
    mod::ProjectControlState control{};
    const MacroAutomationSlotAddress address{.track = 0, .page = 0, .macro = 0};
    assignAutomation(control, address, 0.1f, 0.9f);
    (void)addRecordedShape(
        control,
        mod::projectControlDestination(address),
        -6553,
        6553,
        32767,
        MACRO_AUTOMATION_TICKS_PER_BEAT,
        "Manual override"
    );
    core::ui::MacroEditorPreviewModel model{};
    core::ui::buildMacroEditorPreviewModel(
        0.5f,
        control,
        address,
        true,
        model
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
    namespace mod = core::state::modulation;
    mod::ProjectControlState control{};
    const MacroAutomationSlotAddress address{.track = 0, .page = 0, .macro = 0};
    assignConstantAutomation(control, address, 0.5f);
    assert(mod::setProjectControlAutomationEnabled(control, address, false));
    (void)addRecordedShape(
        control,
        mod::projectControlDestination(address),
        -32767,
        32767,
        32767,
        MACRO_AUTOMATION_TICKS_PER_BEAT,
        "Clipping"
    );
    core::ui::MacroEditorPreviewModel model{};
    core::ui::buildMacroEditorPreviewModel(
        0.5f,
        control,
        address,
        false,
        model
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
    namespace mod = core::state::modulation;
    mod::ProjectControlState control{};
    const MacroAutomationSlotAddress address{.track = 0, .page = 0, .macro = 0};
    assignAutomation(control, address, 0.25f, 0.75f);
    (void)addRecordedShape(
        control,
        mod::projectControlDestination(address),
        -16384,
        16384,
        32767,
        MACRO_AUTOMATION_TICKS_PER_BEAT,
        "Stored off",
        false
    );
    core::ui::MacroEditorPreviewModel model{};
    core::ui::buildMacroEditorPreviewModel(
        0.25f,
        control,
        address,
        false,
        model
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
    namespace mod = core::state::modulation;
    mod::ProjectControlState control{};
    const MacroAutomationSlotAddress address{.track = 0, .page = 0, .macro = 0};
    assignAutomation(control, address, 0.2f, 0.8f);
    (void)addRecordedShape(
        control,
        mod::projectControlDestination(address),
        -8192,
        8192,
        32767,
        static_cast<uint16_t>(2U * MACRO_AUTOMATION_TICKS_PER_BEAT),
        "Independent timeline"
    );
    core::ui::MacroEditorPreviewModel model{};
    core::ui::buildMacroEditorPreviewModel(
        0.5f,
        control,
        address,
        false,
        model
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

void test_short_automation_repeats_on_longest_active_timeline() {
    namespace mod = core::state::modulation;
    mod::ProjectControlState control{};
    const MacroAutomationSlotAddress address{.track = 0, .page = 0, .macro = 0};
    assignAutomation(control, address, 0.1f, 0.9f);
    (void)addRecordedShape(
        control,
        mod::projectControlDestination(address),
        0,
        0,
        32767,
        static_cast<uint16_t>(2U * MACRO_AUTOMATION_TICKS_PER_BEAT),
        "Long reference"
    );
    core::ui::MacroEditorPreviewModel model{};
    core::ui::buildMacroEditorPreviewModel(
        0.5f,
        control,
        address,
        false,
        model
    );

    const auto firstCycleMiddle = sampleAt(
        model,
        core::ui::MacroEditorPreviewFocus::AUTOMATION,
        16384U
    );
    const auto secondCycleMiddle = sampleAt(
        model,
        core::ui::MacroEditorPreviewFocus::AUTOMATION,
        49152U
    );
    assert(std::abs(
        static_cast<int>(firstCycleMiddle.automationQ16) -
        static_cast<int>(secondCycleMiddle.automationQ16)
    ) <= 300);
    std::cout << "[PASS] Short Automation repeats on the shared timeline\n";
}

void test_inactive_longer_source_does_not_expand_shared_timeline() {
    namespace mod = core::state::modulation;
    mod::ProjectControlState control{};
    const MacroAutomationSlotAddress address{.track = 0, .page = 0, .macro = 0};
    const auto destination = mod::projectControlDestination(address);
    (void)addRecordedShape(
        control,
        destination,
        -8192,
        8192,
        32767,
        MACRO_AUTOMATION_TICKS_PER_BEAT,
        "Active reference"
    );
    (void)addRecordedShape(
        control,
        destination,
        -8192,
        8192,
        32767,
        static_cast<uint16_t>(4U * MACRO_AUTOMATION_TICKS_PER_BEAT),
        "Stored inactive",
        false
    );
    core::ui::MacroEditorPreviewModel model{};
    core::ui::buildMacroEditorPreviewModel(
        0.5f,
        control,
        address,
        false,
        model
    );

    assert(model.timelineHasActiveSource);
    assert(model.timelineDurationTicks == MACRO_AUTOMATION_TICKS_PER_BEAT);
    assert(model.modulationDurationTicks ==
           4U * MACRO_AUTOMATION_TICKS_PER_BEAT);
    std::cout << "[PASS] Inactive sources remain visible without changing zoom\n";
}

void test_free_lfo_uses_editor_open_tempo_for_musical_reference() {
    namespace mod = core::state::modulation;
    mod::ProjectControlState control{};
    const MacroAutomationSlotAddress address{.track = 0, .page = 0, .macro = 0};
    (void)addLfo(
        control,
        mod::projectControlDestination(address),
        mod::ModulatorTimingMode::FREE,
        MACRO_AUTOMATION_TICKS_PER_BEAT,
        2000U
    );
    core::ui::MacroEditorPreviewModel model{};
    core::ui::buildMacroEditorPreviewModel(
        0.5f,
        control,
        address,
        false,
        model,
        120.0f
    );

    assert(model.timelineTempoBpm == 120.0f);
    assert(model.timelineDurationTicks ==
           4U * MACRO_AUTOMATION_TICKS_PER_BEAT);
    assert(model.modulationDurationTicks == model.timelineDurationTicks);
    std::cout << "[PASS] Free LFO period freezes to editor-open tempo\n";
}

void test_all_modulation_and_destination_sum_every_active_source() {
    namespace mod = core::state::modulation;
    mod::ProjectControlState control{};
    const MacroAutomationSlotAddress address{.track = 0, .page = 0, .macro = 0};
    const auto destination = mod::projectControlDestination(address);
    assignConstantAutomation(control, address, 0.5f);
    const auto first = addConstantRecordedShape(
        control,
        destination,
        8192,
        32767,
        "Positive"
    );
    (void)addConstantRecordedShape(
        control,
        destination,
        -4096,
        32767,
        "Negative"
    );
    core::ui::MacroEditorPreviewModel model{};
    core::ui::buildMacroEditorPreviewModel(
        0.5f,
        control,
        address,
        false,
        first.bindingId,
        model
    );

    const auto all = sampleAt(
        model,
        core::ui::MacroEditorPreviewFocus::ALL_MODULATION,
        0U
    );
    const auto focused = sampleAt(
        model,
        core::ui::MacroEditorPreviewFocus::FOCUSED_MODULATOR,
        0U
    );
    const auto output = sampleAt(
        model,
        core::ui::MacroEditorPreviewFocus::DESTINATION,
        0U
    );
    assert(std::abs(static_cast<int>(all.modulationQ15) - 4096) <= 2);
    assert(std::abs(static_cast<int>(focused.modulationQ15) - 8192) <= 2);
    assert(std::abs(
        static_cast<int>(output.outQ16) -
        static_cast<int>(std::lround(
            (0.5f + 4096.0f / 32767.0f) * 65535.0f
        ))
    ) <= 3);
    std::cout << "[PASS] Modulation focus and Destination use the full sum\n";
}

void test_live_modulation_capture_cannot_shorten_shared_reference() {
    namespace mod = core::state::modulation;
    mod::ProjectControlState control{};
    const MacroAutomationSlotAddress address{.track = 0, .page = 0, .macro = 0};
    assignAutomation(control, address, 0.5f, 0.5f, 4.0f);

    mod::ProjectRecordedShapeCaptureState capture{};
    configureCapture(
        capture,
        mod::ProjectRecordedShapeCaptureMode::CREATE_ASSIGNED,
        mod::projectControlDestination(address),
        {},
        32767,
        0,
        MACRO_AUTOMATION_TICKS_PER_BEAT
    );
    assert(capture.take->sampleCount > 1U);
    const uint16_t last = static_cast<uint16_t>(capture.take->sampleCount - 1U);
    for (uint16_t sample = 0U; sample <= last; ++sample) {
        capture.take->values[sample] = static_cast<int16_t>(
            (static_cast<int32_t>(sample) * 16384) / last
        );
    }

    core::ui::MacroEditorPreviewModel model{};
    core::ui::buildMacroEditorPreviewModel(
        0.5f,
        control,
        address,
        false,
        model
    );
    core::ui::attachProjectRecordedShapeCapturePreview(capture, model);

    assert(model.timelineDurationTicks ==
           4U * MACRO_AUTOMATION_TICKS_PER_BEAT);
    const auto firstCycleMiddle = sampleAt(
        model,
        core::ui::MacroEditorPreviewFocus::ALL_MODULATION,
        8192U
    );
    const auto secondCycleMiddle = sampleAt(
        model,
        core::ui::MacroEditorPreviewFocus::ALL_MODULATION,
        24576U
    );
    assert(std::abs(
        static_cast<int>(firstCycleMiddle.modulationQ15) -
        static_cast<int>(secondCycleMiddle.modulationQ15)
    ) <= 100);
    std::cout << "[PASS] Live capture loops without shortening shared time\n";
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

void test_stale_preview_model_reads_live_destination_global_depth() {
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

    core::ui::MacroEditorPreviewModel model{};
    core::ui::buildMacroEditorPreviewModel(
        0.5f,
        control,
        address,
        false,
        model
    );
    const auto peakFor = [](const core::ui::MacroEditorPreviewModel& preview) {
        int peak = 0;
        for (uint16_t index = 0; index < 64U; ++index) {
            const uint16_t position = static_cast<uint16_t>(
                (static_cast<uint32_t>(index) * 65535U) / 63U
            );
            peak = std::max(
                peak,
                std::abs(static_cast<int>(sampleAt(
                    preview,
                    core::ui::MacroEditorPreviewFocus::ALL_MODULATION,
                    position
                ).modulationQ15))
            );
        }
        return peak;
    };
    const int unityPeak = peakFor(model);
    assert(mod::setProjectModulationDestinationScale(
        control.authored.modulation,
        target,
        16384U
    ).changed());
    control.markAuthoredDestinationScaleMutation(target);
    assert(model.authoredRevision != control.authoredRevision);

    const int halfPeak = peakFor(model);
    assert(unityPeak > 0 && halfPeak > 0);
    assert(std::abs(unityPeak - halfPeak * 2) <= 2);
    std::cout
        << "[PASS] stale preview model reads live destination Global Depth\n";
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

void test_provisional_recorded_shape_sums_before_global_scale() {
    namespace mod = core::state::modulation;
    mod::ProjectControlState control{};
    const MacroAutomationSlotAddress address{.track = 0, .page = 0, .macro = 0};
    const auto destination = mod::projectControlDestination(address);
    assignConstantAutomation(control, address, 0.1f);
    (void)addConstantRecordedShape(
        control,
        destination,
        24575,
        32767,
        "Durable"
    );
    assert(mod::setProjectModulationDestinationScale(
        control.authored.modulation,
        destination,
        16384U
    ).changed());

    mod::ProjectRecordedShapeCaptureState capture{};
    configureCapture(
        capture,
        mod::ProjectRecordedShapeCaptureMode::CREATE_ASSIGNED,
        destination,
        {},
        32767,
        24575,
        static_cast<uint16_t>(2U * MACRO_AUTOMATION_TICKS_PER_BEAT)
    );
    core::ui::MacroEditorPreviewModel model{};
    core::ui::buildMacroEditorPreviewModel(
        0.0f,
        control,
        address,
        false,
        model
    );
    core::ui::attachProjectRecordedShapeCapturePreview(capture, model);
    assert(model.recordedShapeCapture == &capture);
    assert(model.timelineDurationTicks ==
           2U * MACRO_AUTOMATION_TICKS_PER_BEAT);
    assert(model.modulationStored && model.modulationPlayback);

    const float source = 24575.0f / 32767.0f;
    const float expectedModulation = (source + source) * (16384.0f / 32768.0f);
    const auto destinationSample = sampleAt(
        model,
        core::ui::MacroEditorPreviewFocus::DESTINATION,
        0U
    );
    assert(std::abs(
        static_cast<int>(destinationSample.modulationQ15) -
        static_cast<int>(std::lround(expectedModulation * 32767.0f))
    ) <= 2);
    assert(std::abs(
        static_cast<int>(destinationSample.outQ16) -
        static_cast<int>(std::lround(
            (0.1f + expectedModulation) * 65535.0f
        ))
    ) <= 3);

    // Focused Modulation presents the gesture itself, while Destination above
    // remains the truthful Automation + durable sources + gesture output.
    const auto focusedSample = sampleAt(
        model,
        core::ui::MacroEditorPreviewFocus::FOCUSED_MODULATOR,
        0U
    );
    const float expectedFocused = source * (16384.0f / 32768.0f);
    assert(std::abs(
        static_cast<int>(focusedSample.modulationQ15) -
        static_cast<int>(std::lround(expectedFocused * 32767.0f))
    ) <= 2);

    assignConstantAutomation(control, address, 0.8f);
    core::ui::MacroEditorPreviewModel clipped{};
    core::ui::buildMacroEditorPreviewModel(
        0.0f,
        control,
        address,
        false,
        clipped
    );
    core::ui::attachProjectRecordedShapeCapturePreview(capture, clipped);
    const auto clippedSample = sampleAt(
        clipped,
        core::ui::MacroEditorPreviewFocus::DESTINATION,
        0U
    );
    assert(clippedSample.clippedHigh);
    assert(clippedSample.outQ16 == 65535U);
    assert(std::abs(
        static_cast<int>(clippedSample.modulationQ15) -
        static_cast<int>(destinationSample.modulationQ15)
    ) <= 1);
    static_assert(sizeof(core::ui::MacroEditorPreviewModel) <= 96U);
    std::cout << "[PASS] provisional shape sums before one global scale\n";
}

void test_recorded_shape_overdub_substitutes_only_its_source() {
    namespace mod = core::state::modulation;
    mod::ProjectControlState control{};
    const MacroAutomationSlotAddress address{.track = 0, .page = 0, .macro = 0};
    const auto destination = mod::projectControlDestination(address);
    assignConstantAutomation(control, address, 0.6f);
    const auto replaced = addConstantRecordedShape(
        control,
        destination,
        8192,
        32767,
        "Replace me"
    );
    (void)addConstantRecordedShape(
        control,
        destination,
        3277,
        32767,
        "Keep me"
    );
    assert(mod::setProjectModulationDestinationScale(
        control.authored.modulation,
        destination,
        16384U
    ).changed());

    mod::ProjectRecordedShapeCaptureState capture{};
    configureCapture(
        capture,
        mod::ProjectRecordedShapeCaptureMode::REPLACE_EXISTING,
        destination,
        replaced.sourceId,
        0,
        -16384,
        MACRO_AUTOMATION_TICKS_PER_BEAT
    );
    core::ui::MacroEditorPreviewModel model{};
    core::ui::buildMacroEditorPreviewModel(
        0.0f,
        control,
        address,
        false,
        replaced.bindingId,
        model
    );
    core::ui::attachProjectRecordedShapeCapturePreview(capture, model);
    assert(model.recordedShapeCapture == &capture);

    const float replacement = -16384.0f / 32767.0f;
    const float retained = 3277.0f / 32767.0f;
    const float scale = 16384.0f / 32768.0f;
    const float expectedAll = (replacement + retained) * scale;
    const auto destinationSample = sampleAt(
        model,
        core::ui::MacroEditorPreviewFocus::DESTINATION,
        0U
    );
    assert(std::abs(
        static_cast<int>(destinationSample.modulationQ15) -
        static_cast<int>(std::lround(expectedAll * 32767.0f))
    ) <= 2);
    assert(std::abs(
        static_cast<int>(destinationSample.outQ16) -
        static_cast<int>(std::lround((0.6f + expectedAll) * 65535.0f))
    ) <= 3);
    const auto focusedSample = sampleAt(
        model,
        core::ui::MacroEditorPreviewFocus::FOCUSED_MODULATOR,
        0U
    );
    assert(std::abs(
        static_cast<int>(focusedSample.modulationQ15) -
        static_cast<int>(std::lround(replacement * scale * 32767.0f))
    ) <= 2);

    mod::ProjectRecordedShapeCaptureState unrelated{};
    configureCapture(
        unrelated,
        mod::ProjectRecordedShapeCaptureMode::CREATE_ASSIGNED,
        mod::projectControlDestination({.track = 0, .page = 0, .macro = 1}),
        {},
        32767,
        32767,
        MACRO_AUTOMATION_TICKS_PER_BEAT
    );
    core::ui::MacroEditorPreviewModel untouched{};
    core::ui::buildMacroEditorPreviewModel(
        0.0f,
        control,
        address,
        false,
        untouched
    );
    core::ui::attachProjectRecordedShapeCapturePreview(unrelated, untouched);
    assert(untouched.recordedShapeCapture == nullptr);
    std::cout << "[PASS] overdub substitutes only the captured source\n";
}

void test_active_take_is_the_editor_automation_curve_and_write_head() {
    namespace mod = core::state::modulation;
    mod::ProjectControlState control{};
    const MacroAutomationSlotAddress address{.track = 0, .page = 0, .macro = 0};
    core::ui::MacroEditorPreviewModel model{};
    core::ui::buildMacroEditorPreviewModel(
        0.5f,
        control,
        address,
        false,
        model
    );

    MacroAutomationTakeState take{};
    std::array<uint8_t, MacroAutomationTakeState::VALUE_COLUMN_COUNT> bases{};
    bases.fill(64U);
    const uint16_t duration = macroAutomationTakeFixedDurationTicks(
        MacroAutomationTakeTiming::NOTE_1_4
    );
    take.arm(MacroAutomationTakeTiming::NOTE_1_4, 0x01U, bases);
    assert(take.begin(1000U, 0U, duration / 4U, 1U, 0U));
    assert(take.touch(0U, 32U, 0U));
    assert(take.touch(0U, 96U, duration / 2U));
    core::ui::attachMacroAutomationTakePreview(take, 0U, model);

    assert(model.activeTake == &take);
    assert(model.automationStored);
    assert(model.automationDrivingBase);
    assert(model.automationDurationTicks == duration);
    const auto quarter = sampleAt(
        model,
        core::ui::MacroEditorPreviewFocus::AUTOMATION,
        16384U
    );
    const auto threeQuarters = sampleAt(
        model,
        core::ui::MacroEditorPreviewFocus::AUTOMATION,
        49151U
    );
    assert(std::abs(static_cast<int>(quarter.automationQ16) -
                    static_cast<int>(32U * 65535U / 127U)) <= 300);
    assert(std::abs(static_cast<int>(threeQuarters.automationQ16) -
                    static_cast<int>(96U * 65535U / 127U)) <= 300);
    uint16_t writePosition = 0U;
    assert(take.fixedWritePositionQ16(0U, writePosition));
    assert(std::abs(static_cast<int>(writePosition) - 49151) <= 1);
    std::cout << "[PASS] Active take drives editor curve and write head\n";
}

void test_compiled_preview_sparse_path_matches_authored_fallback() {
    namespace mod = core::state::modulation;
    mod::ProjectControlState control{};
    const MacroAutomationSlotAddress address{
        .track = 0,
        .page = 0,
        .macro = 0,
    };
    const auto destination = mod::projectControlDestination(address);
    assignAutomation(control, address, 0.15f, 0.75f, 2.0f);
    const auto focused = addLfo(
        control,
        destination,
        mod::ModulatorTimingMode::SYNC,
        MACRO_AUTOMATION_TICKS_PER_BEAT,
        1000U
    );
    (void)addConstantRecordedShape(
        control,
        destination,
        8192,
        16384,
        "Sparse peer"
    );
    assert(mod::setProjectModulationDestinationScale(
        control.authored.modulation,
        destination,
        24576U
    ).changed());

    core::ui::MacroEditorPreviewModel authored{};
    core::ui::buildMacroEditorPreviewModel(
        0.25f,
        control,
        address,
        false,
        focused.bindingId,
        authored
    );
    assert(authored.runtimeDestinationIndex == UINT16_MAX);

    mod::ProjectModulationCompileContext context{};
    context.enabledTrackMask = 0x0001U;
    context.activePage[0] = 0U;
    context.activeMacroMask[0] = 0x01U;
    const auto compiled = mod::compileProjectControlRuntimePlan(
        control.authored,
        context,
        control.plan
    );
    assert(compiled.compiled());
    control.compiledRevision = control.authoredRevision;
    control.runtimeContextHash =
        mod::projectModulationCompileContextHash(context);

    core::ui::MacroEditorPreviewModel sparse{};
    core::ui::buildMacroEditorPreviewModel(
        0.25f,
        control,
        address,
        false,
        focused.bindingId,
        sparse
    );
    assert(sparse.runtimeDestinationIndex < control.plan.destinationCount);
    assert(sparse.focusedRuntimeBindingIndex < control.plan.bindingCount);
    assert(sparse.planCompiledRevision == control.authoredRevision);

    for (const auto focus : {
             core::ui::MacroEditorPreviewFocus::DESTINATION,
             core::ui::MacroEditorPreviewFocus::AUTOMATION,
             core::ui::MacroEditorPreviewFocus::FOCUSED_MODULATOR,
             core::ui::MacroEditorPreviewFocus::ALL_MODULATION,
         }) {
        for (const uint16_t position : {
                 uint16_t{0U},
                 uint16_t{8192U},
                 uint16_t{32768U},
                 uint16_t{65535U},
             }) {
            const auto expected = sampleAt(authored, focus, position);
            const auto actual = sampleAt(sparse, focus, position);
            assert(actual.automationQ16 == expected.automationQ16);
            assert(actual.baseQ16 == expected.baseQ16);
            assert(actual.modulationQ15 == expected.modulationQ15);
            assert(actual.outQ16 == expected.outQ16);
            assert(actual.clippedLow == expected.clippedLow);
            assert(actual.clippedHigh == expected.clippedHigh);
            assert(
                actual.discontinuityBefore ==
                expected.discontinuityBefore
            );
        }
    }
    std::cout
        << "[PASS] compiled sparse preview matches authored fallback\n";
}

void test_preview_cache_falls_back_after_curve_directory_compaction() {
    namespace mod = core::state::modulation;
    mod::ProjectControlState control{};
    const MacroAutomationSlotAddress first{
        .track = 0U,
        .page = 0U,
        .macro = 0U,
    };
    const MacroAutomationSlotAddress target{
        .track = 0U,
        .page = 0U,
        .macro = 1U,
    };
    assignAutomation(control, first, 0.1f, 0.3f, 1.0f);
    assignAutomation(control, target, 0.2f, 0.8f, 1.0f);

    core::ui::MacroEditorPreviewModel model{};
    core::ui::buildMacroEditorPreviewModel(
        0.4f,
        control,
        target,
        false,
        model
    );
    assert(model.automationCurveRecordIndex == 1U);
    const auto expected = sampleAt(
        model,
        core::ui::MacroEditorPreviewFocus::AUTOMATION,
        65535U
    );

    assert(mod::removeProjectAutomationCurve(
        control.authored.automation,
        control.authored.curves,
        mod::projectControlDestination(first)
    ).changed());
    control.markAuthoredMutation();
    assert(model.authoredRevision != control.authoredRevision);
    assert(control.authored.curves.recordCount == 1U);

    const auto afterCompaction = sampleAt(
        model,
        core::ui::MacroEditorPreviewFocus::AUTOMATION,
        65535U
    );
    assert(afterCompaction.automationQ16 == expected.automationQ16);
    assert(afterCompaction.baseQ16 == expected.baseQ16);
    std::cout
        << "[PASS] stale preview cache resolves stable Curve ID after compaction\n";
}


}  // namespace

int main() {
    test_manual_disengages_only_automation();
    test_out_clamps_and_reports_both_clip_directions();
    test_modulation_off_preserves_stored_preview_but_not_output_motion();
    test_domains_keep_their_own_truthful_timelines();
    test_short_automation_repeats_on_longest_active_timeline();
    test_inactive_longer_source_does_not_expand_shared_timeline();
    test_free_lfo_uses_editor_open_tempo_for_musical_reference();
    test_all_modulation_and_destination_sum_every_active_source();
    test_live_modulation_capture_cannot_shorten_shared_reference();
    test_project_preview_applies_destination_global_depth();
    test_stale_preview_model_reads_live_destination_global_depth();
    test_project_square_reports_explicit_discontinuity();
    test_provisional_recorded_shape_sums_before_global_scale();
    test_recorded_shape_overdub_substitutes_only_its_source();
    test_active_take_is_the_editor_automation_curve_and_write_head();
    test_compiled_preview_sparse_path_matches_authored_fallback();
    test_preview_cache_falls_back_after_curve_directory_compaction();
    std::cout << "All MacroEditorPreviewModel tests passed.\n";
    return 0;
}
