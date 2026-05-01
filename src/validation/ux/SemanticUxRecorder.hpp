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
    void flush(uint32_t nowMs, const core::state::CoreState& state);
    void flush(uint32_t nowMs, const SemanticUxSnapshot& snapshot);

    std::size_t pendingCount() const;
    uint32_t droppedCount() const;

private:
    enum class RecordKind : uint8_t {
        Button,
        Encoder,
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
        SemanticUxSnapshot preSnapshot{};
        SemanticUxContext preContext{};
        oc::core::input::InputBindingTraceEvent traceEvent{};
    };

    bool enqueue_(const PendingRecord& record);
    bool pop_(PendingRecord& record);
    void writeRecord_(uint32_t nowMs,
                      const PendingRecord& record,
                      const SemanticUxSnapshot& snapshot);
    void writeDropReport_(uint32_t nowMs);

    std::array<PendingRecord, CAPACITY> queue_{};
    std::size_t head_ = 0;
    std::size_t tail_ = 0;
    std::size_t count_ = 0;
    uint32_t next_sequence_ = 1;
    uint32_t dropped_count_ = 0;
    uint32_t reported_dropped_count_ = 0;
    SemanticUxLineSink* sink_ = nullptr;
    bool enabled_ = false;
};

}  // namespace core::validation::ux
