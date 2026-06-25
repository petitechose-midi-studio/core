#include <cassert>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

#include "../../src/config/InputIDs.hpp"
#include "../../src/validation/ux/SemanticUxContext.hpp"
#include "../../src/validation/ux/SemanticUxRecorder.hpp"

namespace {

class CapturingSink : public core::validation::ux::SemanticUxLineSink {
public:
    void writeLine(const char* line) override {
        lines.emplace_back(line);
    }

    std::vector<std::string> lines;
};

class FakeContextProvider : public core::validation::ux::SemanticUxContextProvider {
public:
    void captureSemanticUxContext(
        const oc::core::input::InputBindingTraceEvent&,
        core::validation::ux::SemanticUxContext& out
    ) const override {
        ++calls;
        out.mode = "sequencer.step_grid";
        out.effect = "edit_step_property";
        out.outcome = calls == 1 ? "noop" : nullptr;
        out.reason = calls == 1 ? "test_reason" : nullptr;
        out.target = "step";
        out.targetIndex = calls == 1 ? 2 : 1;
        out.targetStep = calls == 1 ? 2 : 1;
        out.targetMask = calls == 1 ? 3 : 7;
        out.property = calls == 1 ? "Gate" : "Velocity";
        std::snprintf(out.valueLabel, sizeof(out.valueLabel), "%s", calls == 1 ? "50%" : "85");
        out.hasStepOn = true;
        out.stepOn = calls > 1;
        out.hasResolvedStep = true;
        out.resolvedNote = calls == 1 ? 60 : 67;
        out.resolvedVelocity = calls == 1 ? 72 : 85;
        out.resolvedGate = calls == 1 ? 50 : 120;
        out.resolvedNudge = calls == 1 ? -2 : 3;
        out.resolvedProbability = calls == 1 ? 90 : 100;
        out.resolvedVariationVisible = calls > 1;
    }

    mutable int calls = 0;
};

oc::core::input::InputBindingTraceEvent dispatchedButton() {
    oc::core::input::InputBindingTraceEvent event{};
    event.stage = oc::core::input::InputBindingTraceStage::Dispatch;
    event.domain = oc::core::input::InputBindingTraceDomain::Button;
    event.dispatched = true;
    event.buttonId = static_cast<oc::type::ButtonID>(Config::ButtonID::MACRO_1);
    event.buttonType = oc::core::input::ButtonBindingType::LONG_PRESS;
    event.bindingId = 42;
    event.scopeId = 7;
    event.authorityScope = 7;
    return event;
}

oc::core::input::InputBindingTraceEvent dispatchedEncoder() {
    oc::core::input::InputBindingTraceEvent event{};
    event.stage = oc::core::input::InputBindingTraceStage::Dispatch;
    event.domain = oc::core::input::InputBindingTraceDomain::Encoder;
    event.dispatched = true;
    event.encoderId = static_cast<oc::type::EncoderID>(Config::EncoderID::NAV);
    event.encoderType = oc::core::input::EncoderBindingType::TURN;
    event.encoderValue = -1.0f;
    event.bindingId = 84;
    event.scopeId = 9;
    event.authorityScope = 9;
    return event;
}

oc::core::input::InputBindingTraceEvent dispatchedMacroEncoder() {
    auto event = dispatchedEncoder();
    event.encoderId = static_cast<oc::type::EncoderID>(Config::EncoderID::MACRO_1);
    event.encoderValue = 0.378f;
    return event;
}

core::validation::ux::SemanticUxSnapshot sequencerSnapshot() {
    return core::validation::ux::SemanticUxSnapshot{
        .view = core::ui::ViewType::SEQUENCER,
        .overlay = core::ui::OverlayType::SEQ_STEP_EDIT,
        .playing = true,
        .playheadStep = 5,
        .sequencerPage = 2,
        .sharedTrack = 3,
        .sharedTrackMask = 0x000f,
    };
}

bool contains(const std::string& value, const char* fragment) {
    return value.find(fragment) != std::string::npos;
}

void test_ignores_when_disabled() {
    CapturingSink sink;
    core::validation::ux::SemanticUxRecorder recorder{{.sink = &sink, .enabled = false}};

    recorder.onBindingTrace(dispatchedButton());
    recorder.flush(100, sequencerSnapshot());

    assert(sink.lines.empty());
    std::cout << "[PASS] test_ignores_when_disabled\n";
}

void test_ignores_non_dispatch_rows() {
    CapturingSink sink;
    core::validation::ux::SemanticUxRecorder recorder{{.sink = &sink, .enabled = true}};

    auto event = dispatchedButton();
    event.stage = oc::core::input::InputBindingTraceStage::Candidate;

    recorder.onBindingTrace(event);
    recorder.flush(100, sequencerSnapshot());

    assert(sink.lines.empty());
    std::cout << "[PASS] test_ignores_non_dispatch_rows\n";
}

void test_writes_button_semantics_with_snapshot() {
    CapturingSink sink;
    core::validation::ux::SemanticUxRecorder recorder{{.sink = &sink, .enabled = true}};

    recorder.onBindingTrace(
        dispatchedButton(),
        core::validation::ux::SemanticUxSnapshot{
            .view = core::ui::ViewType::MACRO,
            .overlay = core::ui::OverlayType::NONE,
            .playing = false,
            .playheadStep = -1,
            .sequencerPage = 0,
            .sharedTrack = 0,
            .sharedTrackMask = 1,
        }
    );
    assert(recorder.pendingCount() == 1);

    recorder.flush(1234, sequencerSnapshot());

    assert(sink.lines.size() == 1);
    assert(contains(sink.lines[0], "\"kind\":\"button\""));
    assert(contains(sink.lines[0], "\"gesture\":\"long_press\""));
    assert(contains(sink.lines[0], "\"button\":\"MACRO_1\""));
    assert(contains(sink.lines[0], "\"pre_view\":\"macro\""));
    assert(contains(sink.lines[0], "\"pre_overlay\":\"none\""));
    assert(contains(sink.lines[0], "\"view\":\"sequencer\""));
    assert(contains(sink.lines[0], "\"overlay\":\"seq_step_edit\""));
    assert(contains(sink.lines[0], "\"playing\":1"));
    assert(contains(sink.lines[0], "\"playhead\":5"));
    assert(recorder.pendingCount() == 0);
    std::cout << "[PASS] test_writes_button_semantics_with_snapshot\n";
}

void test_writes_relative_encoder_delta_without_float_formatting() {
    CapturingSink sink;
    core::validation::ux::SemanticUxRecorder recorder{{.sink = &sink, .enabled = true}};

    recorder.onBindingTrace(dispatchedEncoder());
    recorder.flush(2000, sequencerSnapshot());

    assert(sink.lines.size() == 1);
    assert(contains(sink.lines[0], "\"kind\":\"encoder\""));
    assert(contains(sink.lines[0], "\"encoder\":\"NAV\""));
    assert(contains(sink.lines[0], "\"value_kind\":\"delta\""));
    assert(contains(sink.lines[0], "\"delta_milli\":-1000"));
    std::cout << "[PASS] test_writes_relative_encoder_delta_without_float_formatting\n";
}

void test_writes_normalized_encoder_value_without_delta_semantics() {
    CapturingSink sink;
    core::validation::ux::SemanticUxRecorder recorder{{.sink = &sink, .enabled = true}};

    recorder.onBindingTrace(dispatchedMacroEncoder());
    recorder.flush(2000, sequencerSnapshot());

    assert(sink.lines.size() == 1);
    assert(contains(sink.lines[0], "\"kind\":\"encoder\""));
    assert(contains(sink.lines[0], "\"encoder\":\"MACRO_1\""));
    assert(contains(sink.lines[0], "\"value_kind\":\"absolute\""));
    assert(contains(sink.lines[0], "\"value_milli\":378"));
    assert(!contains(sink.lines[0], "\"delta_milli\""));
    std::cout << "[PASS] test_writes_normalized_encoder_value_without_delta_semantics\n";
}

void test_writes_native_context_provider_fields() {
    CapturingSink sink;
    FakeContextProvider provider;
    core::validation::ux::setCurrentSemanticUxContextProvider(&provider);
    core::validation::ux::SemanticUxRecorder recorder{{.sink = &sink, .enabled = true}};

    recorder.onBindingTrace(dispatchedMacroEncoder());
    recorder.flush(2000, sequencerSnapshot());
    core::validation::ux::clearCurrentSemanticUxContextProvider(&provider);

    assert(sink.lines.size() == 1);
    assert(contains(sink.lines[0], "\"mode\":\"sequencer.step_grid\""));
    assert(contains(sink.lines[0], "\"effect\":\"edit_step_property\""));
    assert(contains(sink.lines[0], "\"outcome\":\"noop\""));
    assert(contains(sink.lines[0], "\"reason\":\"test_reason\""));
    assert(contains(sink.lines[0], "\"target\":\"step\""));
    assert(contains(sink.lines[0], "\"target_index\":1"));
    assert(contains(sink.lines[0], "\"target_step\":1"));
    assert(contains(sink.lines[0], "\"pre_target_mask\":3"));
    assert(contains(sink.lines[0], "\"target_mask\":7"));
    assert(contains(sink.lines[0], "\"pre_property\":\"Gate\""));
    assert(contains(sink.lines[0], "\"property\":\"Velocity\""));
    assert(contains(sink.lines[0], "\"value_label\":\"85\""));
    assert(contains(sink.lines[0], "\"step_on\":1"));
    assert(contains(sink.lines[0], "\"resolved_note\":67"));
    assert(contains(sink.lines[0], "\"resolved_velocity\":85"));
    assert(contains(sink.lines[0], "\"resolved_gate\":120"));
    assert(contains(sink.lines[0], "\"resolved_nudge\":3"));
    assert(contains(sink.lines[0], "\"resolved_probability\":100"));
    assert(contains(sink.lines[0], "\"resolved_variation\":1"));
    assert(provider.calls == 2);
    std::cout << "[PASS] test_writes_native_context_provider_fields\n";
}

void test_reports_dropped_records() {
    CapturingSink sink;
    core::validation::ux::SemanticUxRecorder recorder{{.sink = &sink, .enabled = true}};

    for (std::size_t i = 0; i < core::validation::ux::SemanticUxRecorder::CAPACITY + 2U; ++i) {
        recorder.onBindingTrace(dispatchedButton());
    }

    assert(recorder.droppedCount() == 2);
    recorder.flush(3000, sequencerSnapshot());

    assert(sink.lines.size() == core::validation::ux::SemanticUxRecorder::CAPACITY + 1U);
    assert(contains(sink.lines.back(), "\"kind\":\"drop\""));
    assert(contains(sink.lines.back(), "\"dropped\":2"));
    std::cout << "[PASS] test_reports_dropped_records\n";
}

}  // namespace

int main() {
    test_ignores_when_disabled();
    test_ignores_non_dispatch_rows();
    test_writes_button_semantics_with_snapshot();
    test_writes_relative_encoder_delta_without_float_formatting();
    test_writes_normalized_encoder_value_without_delta_semantics();
    test_writes_native_context_provider_fields();
    test_reports_dropped_records();

    std::cout << "All SemanticUxRecorder tests passed\n";
    return 0;
}
