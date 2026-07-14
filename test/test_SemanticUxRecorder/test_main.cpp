#include <cassert>
#include <cstdio>
#include <iostream>
#include <limits>
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
        out.sourceMask = 5;
        out.createMask = calls == 1 ? 4 : 0;
        out.overwriteMask = calls == 1 ? 0 : 4;
        out.routePolicy = "preserve_destination";
        out.projection = calls == 1 ? "preview" : "live";
        out.source = "sequencer_cc_lane";
        out.winner = calls == 1 ? "macro_computed" : "sequencer_cc_lane";
        out.winnerSource = calls == 1 ? "preflight" : "runtime_telemetry";
        out.activationOrigin = "track_paste";
        out.hasActivationGeneration = true;
        out.activationGeneration = calls == 1 ? 41 : 42;
        out.mappingIndex = calls == 1 ? 0 : 1;
        out.mappingCount = 2;
        out.sourceTrack = calls == 1 ? 2 : 3;
        out.targetTrack = calls == 1 ? 4 : 5;
        out.targetKind = calls == 1 ? "free" : "overwrite";
        out.inheritedLaneCount = calls == 1 ? 1 : 2;
        out.pinnedLaneCount = calls == 1 ? 3 : 4;
        out.operationOrigin = "track_paste";
        out.hasOperationGeneration = true;
        out.operationGeneration = calls == 1 ? 51 : 52;
        out.operationStatus = calls == 1 ? "pressed" : "applied";
        out.hasTargetRoute = true;
        out.targetRoute = 10;
        out.targetRouteValid = true;
        out.property = calls == 1 ? "Gate" : "Velocity";
        std::snprintf(out.valueLabel, sizeof(out.valueLabel), "%s", calls == 1 ? "50%" : "85");
        out.hasConflict = true;
        out.conflict = calls == 1;
        out.hasAuthoredValue = true;
        out.authoredValue = calls == 1 ? 64 : 96;
        out.hasResolvedValue = true;
        out.resolvedValue = calls == 1 ? 80 : 96;
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

class TextContextProvider : public core::validation::ux::SemanticUxContextProvider {
public:
    TextContextProvider(const char* mode, const char* effect)
        : mode_(mode), effect_(effect) {}

    void captureSemanticUxContext(
        const oc::core::input::InputBindingTraceEvent&,
        core::validation::ux::SemanticUxContext& out
    ) const override {
        out.mode = mode_;
        out.effect = effect_;
    }

private:
    const char* mode_ = nullptr;
    const char* effect_ = nullptr;
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

void test_non_finite_and_oversized_encoder_values_are_serialized_safely() {
    CapturingSink sink;
    core::validation::ux::SemanticUxRecorder recorder{{.sink = &sink, .enabled = true}};

    auto nonFinite = dispatchedMacroEncoder();
    nonFinite.encoderValue = std::numeric_limits<float>::infinity();
    recorder.onBindingTrace(nonFinite);

    auto oversized = dispatchedMacroEncoder();
    oversized.encoderValue = std::numeric_limits<float>::max();
    recorder.onBindingTrace(oversized);
    recorder.flush(2000, sequencerSnapshot());

    assert(sink.lines.size() == 2);
    assert(contains(sink.lines[0], "\"value_milli\":0"));
    assert(contains(sink.lines[1], "\"value_milli\":2147483647"));
    std::cout
        << "[PASS] "
        << "test_non_finite_and_oversized_encoder_values_are_serialized_safely\n";
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
    assert(contains(sink.lines[0], "\"source_mask\":5"));
    assert(contains(sink.lines[0], "\"create_mask\":0"));
    assert(contains(sink.lines[0], "\"overwrite_mask\":4"));
    assert(contains(sink.lines[0], "\"route_policy\":\"preserve_destination\""));
    assert(contains(sink.lines[0], "\"projection\":\"live\""));
    assert(contains(sink.lines[0], "\"source\":\"sequencer_cc_lane\""));
    assert(contains(sink.lines[0], "\"winner\":\"sequencer_cc_lane\""));
    assert(contains(sink.lines[0], "\"winner_source\":\"runtime_telemetry\""));
    assert(contains(sink.lines[0], "\"activation_origin\":\"track_paste\""));
    assert(contains(sink.lines[0], "\"activation_generation\":42"));
    assert(contains(sink.lines[0], "\"mapping_index\":1"));
    assert(contains(sink.lines[0], "\"mapping_count\":2"));
    assert(contains(sink.lines[0], "\"source_track\":3"));
    assert(contains(sink.lines[0], "\"target_track\":5"));
    assert(contains(sink.lines[0], "\"target_kind\":\"overwrite\""));
    assert(contains(sink.lines[0], "\"inherited_lane_count\":2"));
    assert(contains(sink.lines[0], "\"pinned_lane_count\":4"));
    assert(contains(sink.lines[0], "\"operation_origin\":\"track_paste\""));
    assert(contains(sink.lines[0], "\"operation_generation\":52"));
    assert(contains(sink.lines[0], "\"operation_status\":\"applied\""));
    assert(contains(sink.lines[0], "\"target_route\":10"));
    assert(contains(sink.lines[0], "\"target_route_valid\":1"));
    assert(contains(sink.lines[0], "\"pre_property\":\"Gate\""));
    assert(contains(sink.lines[0], "\"property\":\"Velocity\""));
    assert(contains(sink.lines[0], "\"value_label\":\"85\""));
    assert(contains(sink.lines[0], "\"conflict\":0"));
    assert(contains(sink.lines[0], "\"authored_value\":96"));
    assert(contains(sink.lines[0], "\"resolved_value\":96"));
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

void test_associates_capture_with_live_surface_context() {
    CapturingSink sink;
    FakeContextProvider provider;
    core::validation::ux::setCurrentSemanticUxContextProvider(&provider);
    core::validation::ux::SemanticUxRecorder recorder{{.sink = &sink, .enabled = true}};

    recorder.onBindingTrace(dispatchedMacroEncoder());
    recorder.flush(2000, sequencerSnapshot());
    recorder.capture(2100, "cc_lane_live", sequencerSnapshot());
    core::validation::ux::clearCurrentSemanticUxContextProvider(&provider);

    assert(sink.lines.size() == 2);
    const auto& capture = sink.lines[1];
    assert(contains(capture, "\"kind\":\"capture\""));
    assert(contains(capture, "\"label\":\"cc_lane_live\""));
    assert(contains(capture, "\"surface_context\":true"));
    assert(contains(capture, "\"source_seq\":1"));
    assert(contains(capture, "\"view\":\"sequencer\""));
    assert(contains(capture, "\"playing\":true"));
    assert(contains(capture, "\"mode\":\"sequencer.step_grid\""));
    assert(contains(capture, "\"projection\":\"live\""));
    assert(contains(capture, "\"activation_origin\":\"track_paste\""));
    assert(contains(capture, "\"activation_generation\":42"));
    assert(contains(capture, "\"authored_value\":96"));
    assert(contains(capture, "\"resolved_value\":96"));
    assert(provider.calls == 3);
    std::cout << "[PASS] test_associates_capture_with_live_surface_context\n";
}

void test_capture_context_reset_prevents_cross_scenario_claims() {
    CapturingSink sink;
    FakeContextProvider provider;
    core::validation::ux::setCurrentSemanticUxContextProvider(&provider);
    core::validation::ux::SemanticUxRecorder recorder{{.sink = &sink, .enabled = true}};

    recorder.onBindingTrace(dispatchedButton());
    recorder.flush(100, sequencerSnapshot());
    recorder.resetCaptureContext();
    recorder.capture(120, "synthetic scenario", sequencerSnapshot());
    core::validation::ux::clearCurrentSemanticUxContextProvider(&provider);

    assert(sink.lines.size() == 2);
    const auto& capture = sink.lines[1];
    assert(contains(capture, "\"label\":\"synthetic_scenario\""));
    assert(contains(capture, "\"surface_context\":false"));
    assert(contains(capture, "\"source_seq\":0"));
    assert(!contains(capture, "\"mode\""));
    assert(provider.calls == 2);
    std::cout << "[PASS] test_capture_context_reset_prevents_cross_scenario_claims\n";
}

void test_explicit_state_projection_capture_has_no_binding_source() {
    CapturingSink sink;
    FakeContextProvider provider;
    core::validation::ux::setCurrentSemanticUxContextProvider(&provider);
    core::validation::ux::SemanticUxRecorder recorder{{.sink = &sink, .enabled = true}};

    recorder.resetCaptureContext(true);
    recorder.capture(120, "synthetic projection", sequencerSnapshot());
    core::validation::ux::clearCurrentSemanticUxContextProvider(&provider);

    assert(sink.lines.size() == 1);
    const auto& capture = sink.lines[0];
    assert(contains(capture, "\"label\":\"synthetic_projection\""));
    assert(contains(capture, "\"surface_context\":true"));
    assert(contains(capture, "\"source_seq\":0"));
    assert(contains(capture, "\"mode\":\"sequencer.step_grid\""));
    assert(provider.calls == 1);
    std::cout
        << "[PASS] test_explicit_state_projection_capture_has_no_binding_source\n";
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

void test_escapes_semantic_text_as_valid_json() {
    CapturingSink sink;
    constexpr char SPECIAL_TEXT[] =
        "quote\" slash\\ back\b form\f line\n return\r tab\t ctrl" "\x01";
    TextContextProvider provider(SPECIAL_TEXT, "safe_tail");
    core::validation::ux::setCurrentSemanticUxContextProvider(&provider);
    core::validation::ux::SemanticUxRecorder recorder{
        {.sink = &sink, .enabled = true}
    };

    recorder.onBindingTrace(dispatchedButton());
    recorder.flush(3100, sequencerSnapshot());
    core::validation::ux::clearCurrentSemanticUxContextProvider(&provider);

    assert(sink.lines.size() == 1);
    assert(contains(
        sink.lines[0],
        "\"mode\":\"quote\\\" slash\\\\ back\\b form\\f line\\n "
        "return\\r tab\\t ctrl\\u0001\""
    ));
    assert(contains(sink.lines[0], "\"effect\":\"safe_tail\""));
    assert(sink.lines[0].find('\n') == std::string::npos);
    assert(sink.lines[0].find('\r') == std::string::npos);
    assert(sink.lines[0].find('\x01') == std::string::npos);
    std::cout << "[PASS] test_escapes_semantic_text_as_valid_json\n";
}

void test_oversized_semantic_field_is_skipped_atomically() {
    CapturingSink sink;
    const std::string oversized(1800, 'x');
    TextContextProvider provider(oversized.c_str(), "safe_tail");
    core::validation::ux::setCurrentSemanticUxContextProvider(&provider);
    core::validation::ux::SemanticUxRecorder recorder{
        {.sink = &sink, .enabled = true}
    };

    recorder.onBindingTrace(dispatchedButton());
    recorder.flush(3200, sequencerSnapshot());
    core::validation::ux::clearCurrentSemanticUxContextProvider(&provider);

    assert(sink.lines.size() == 1);
    assert(!contains(sink.lines[0], "\"mode\":"));
    assert(contains(sink.lines[0], "\"effect\":\"safe_tail\""));
    assert(!sink.lines[0].empty() && sink.lines[0].back() == '}');
    std::cout << "[PASS] test_oversized_semantic_field_is_skipped_atomically\n";
}

}  // namespace

int main() {
    test_ignores_when_disabled();
    test_ignores_non_dispatch_rows();
    test_writes_button_semantics_with_snapshot();
    test_writes_relative_encoder_delta_without_float_formatting();
    test_writes_normalized_encoder_value_without_delta_semantics();
    test_non_finite_and_oversized_encoder_values_are_serialized_safely();
    test_writes_native_context_provider_fields();
    test_associates_capture_with_live_surface_context();
    test_capture_context_reset_prevents_cross_scenario_claims();
    test_explicit_state_projection_capture_has_no_binding_source();
    test_reports_dropped_records();
    test_escapes_semantic_text_as_valid_json();
    test_oversized_semantic_field_is_skipped_atomically();

    std::cout << "All SemanticUxRecorder tests passed\n";
    return 0;
}
