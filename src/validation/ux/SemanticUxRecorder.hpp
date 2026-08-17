#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <oc/core/input/InputBindingTrace.hpp>

#include "app/OverlayTypes.hpp"
#include "app/ViewTypes.hpp"
#include "validation/ux/SemanticUxContext.hpp"

namespace core::state {
struct CoreState;
}

namespace core::validation::ux {

enum class EncoderValueKind : uint8_t {
    Delta,
    Absolute,
};

enum class EncoderContractOwner : uint8_t {
    SequencerRoot,
    DrumLaneEditor,
};

enum class EncoderContractMode : uint8_t {
    Raw,
    Normalized,
};

struct EncoderContractTraceEvent {
    EncoderContractOwner owner = EncoderContractOwner::SequencerRoot;
    EncoderContractMode mode = EncoderContractMode::Normalized;
    oc::type::EncoderID encoderId = 0;
    int32_t minimumMilli = 0;
    int32_t maximumMilli = 1000;
    int32_t positionMilli = 0;
    int32_t normalizedTurnsMilli = 0;
    uint8_t discreteSteps = 0;
    uint8_t discreteTicksPerStep = 0;
};

struct SemanticUxSnapshot {
    core::ui::ViewType view = core::ui::ViewType::MACRO;
    core::ui::OverlayType overlay = core::ui::OverlayType::NONE;
    bool playing = false;
    int16_t playheadStep = -1;
    uint8_t sequencerPage = 0;
    uint8_t sharedTrack = 0;
    uint16_t sharedTrackMask = 0;
};

SemanticUxSnapshot makeSemanticUxSnapshot(const core::state::CoreState& state);

class SemanticUxLineSink {
public:
    virtual ~SemanticUxLineSink() = default;
    virtual void writeLine(const char* line) = 0;
};

struct SemanticUxRecorderOptions {
    SemanticUxLineSink* sink = nullptr;
    bool enabled = false;
    bool emitSemanticEncoderDispatch = true;
};

class SemanticUxRecorder {
public:
    static constexpr std::size_t CAPACITY = 16;

    explicit SemanticUxRecorder(SemanticUxRecorderOptions options = {});

    void configure(SemanticUxRecorderOptions options);
    void setEnabled(bool enabled);
    bool enabled() const;

    void onBindingTrace(const oc::core::input::InputBindingTraceEvent& event);
    void onBindingTrace(const oc::core::input::InputBindingTraceEvent& event,
                        const SemanticUxSnapshot& preSnapshot);
    void onEncoderContractTrace(const EncoderContractTraceEvent& event);
    void flush(uint32_t nowMs, const core::state::CoreState& state);
    void flush(uint32_t nowMs, const SemanticUxSnapshot& snapshot);

    // Records the semantic state rendered by a named SDL capture. The context
    // is re-read from the active surface with the last meaningful dispatched
    // gesture, so capture evidence cannot drift away from the live UI model.
    void capture(uint32_t nowMs,
                 const char* label,
                 const core::state::CoreState& state);
    void capture(uint32_t nowMs,
                 const char* label,
                 const SemanticUxSnapshot& snapshot);

    // Capture scenarios may replace the complete state without dispatching an
    // input gesture. Do not let a gesture from the previous scenario claim
    // semantic ownership of the new capture. An explicit projection opt-in
    // lets the next capture describe that synthetic surface with source_seq=0.
    void resetCaptureContext(bool allowCurrentSurfaceProjection = false);

    std::size_t pendingCount() const;
    uint32_t droppedCount() const;

private:
    enum class RecordKind : uint8_t {
        InputButton,
        InputEncoder,
        Button,
        Encoder,
        EncoderContract,
    };

    struct PendingRecord {
        uint32_t sequence = 0;
        RecordKind kind = RecordKind::Button;
        oc::type::ButtonID buttonId = 0;
        oc::type::EncoderID encoderId = 0;
        oc::core::input::ButtonBindingType buttonType =
            oc::core::input::ButtonBindingType::PRESS;
        oc::core::input::EncoderBindingType encoderType =
            oc::core::input::EncoderBindingType::TURN;
        oc::type::BindingID bindingId = 0;
        oc::type::ScopeID scopeId = 0;
        oc::type::ScopeID authorityScope = 0;
        EncoderValueKind encoderValueKind = EncoderValueKind::Absolute;
        int32_t encoderValueMilli = 0;
        EncoderContractTraceEvent encoderContract{};
        SemanticUxSnapshot preSnapshot{};
        SemanticUxContext preContext{};
        oc::core::input::InputBindingTraceEvent traceEvent{};
    };

    bool enqueue_(const PendingRecord& record);
    bool pop_(PendingRecord& record);
    void writeRecord_(uint32_t nowMs,
                      const PendingRecord& record,
                      const SemanticUxSnapshot& snapshot);
    void writeCapture_(uint32_t nowMs,
                       const char* label,
                       const SemanticUxSnapshot& snapshot);
    void writeDropReport_(uint32_t nowMs);

    std::array<PendingRecord, CAPACITY> queue_{};
    std::size_t head_ = 0;
    std::size_t tail_ = 0;
    std::size_t count_ = 0;
    uint32_t next_sequence_ = 1;
    uint32_t dropped_count_ = 0;
    uint32_t reported_dropped_count_ = 0;
    oc::core::input::InputBindingTraceEvent last_semantic_event_{};
    core::state::interaction::ControllerIntent last_semantic_intent_ =
        core::state::interaction::ControllerIntent::NONE;
    const char* last_semantic_effect_ = nullptr;
    const char* last_semantic_outcome_ = nullptr;
    uint32_t last_semantic_sequence_ = 0;
    bool has_last_semantic_event_ = false;
    bool allow_state_projection_capture_ = false;
    SemanticUxLineSink* sink_ = nullptr;
    bool enabled_ = false;
    bool emit_semantic_encoder_dispatch_ = true;
};

// Diagnostic producers stay allocation-free and unaware of the recorder
// lifetime. The firmware installs its PSRAM-backed recorder explicitly; the
// normal product profile leaves this provider unset and pays no runtime cost.
void setCurrentEncoderContractTraceRecorder(SemanticUxRecorder* recorder);
void clearCurrentEncoderContractTraceRecorder(SemanticUxRecorder* recorder);
void recordEncoderContractTrace(
    EncoderContractOwner owner,
    EncoderContractMode mode,
    oc::type::EncoderID encoderId,
    float minimum,
    float maximum,
    uint8_t discreteSteps,
    uint8_t discreteTicksPerStep,
    float normalizedTurns,
    float position
);

}  // namespace core::validation::ux
