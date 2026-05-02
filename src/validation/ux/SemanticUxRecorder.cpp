#include "validation/ux/SemanticUxRecorder.hpp"

#include <cstdio>
#include <cstring>

#include <config/PlatformCompat.hpp>

#include "config/InputIDs.hpp"
#include "state/CoreState.hpp"
#include "validation/ux/SemanticUxContext.hpp"
#include "validation/ux/SemanticUxNames.hpp"

namespace core::validation::ux {
namespace {

FLASHMEM int32_t milliFromFloat(float value) {
    const float scaled = value * 1000.0f;
    return static_cast<int32_t>(scaled >= 0.0f ? scaled + 0.5f : scaled - 0.5f);
}

FLASHMEM EncoderValueKind valueKindForEncoder(oc::type::EncoderID id) {
    return id == static_cast<oc::type::EncoderID>(Config::EncoderID::NAV)
               ? EncoderValueKind::Delta
               : EncoderValueKind::Absolute;
}

FLASHMEM const char* valueKindName(EncoderValueKind kind) {
    switch (kind) {
        case EncoderValueKind::Delta:
            return "delta";
        case EncoderValueKind::Absolute:
            return "absolute";
    }
    return "absolute";
}

FLASHMEM void appendField(char* out, std::size_t size, const char* key, const char* value) {
    if (!value || value[0] == '\0') return;
    const std::size_t used = std::strlen(out);
    if (used >= size) return;
    std::snprintf(out + used, size - used, ",\"%s\":\"%s\"", key, value);
}

FLASHMEM void appendIntField(char* out, std::size_t size, const char* key, int value) {
    const std::size_t used = std::strlen(out);
    if (used >= size) return;
    std::snprintf(out + used, size - used, ",\"%s\":%d", key, value);
}

FLASHMEM void appendBoolField(char* out, std::size_t size, const char* key, bool value) {
    const std::size_t used = std::strlen(out);
    if (used >= size) return;
    std::snprintf(out + used, size - used, ",\"%s\":%u", key, value ? 1U : 0U);
}

FLASHMEM void formatContextFields(char* out,
                                  std::size_t size,
                                  const SemanticUxContext& pre,
                                  const SemanticUxContext& post) {
    if (!out || size == 0) return;
    out[0] = '\0';

    const bool sameSurface =
        (!pre.mode || !post.mode || std::strcmp(pre.mode, post.mode) == 0) &&
        (!pre.target || !post.target || std::strcmp(pre.target, post.target) == 0);

    appendField(out, size, "mode", pre.mode ? pre.mode : post.mode);
    appendField(out, size, "effect", pre.effect ? pre.effect : post.effect);
    appendField(out, size, "outcome", pre.outcome ? pre.outcome : post.outcome);
    appendField(out, size, "reason", pre.reason ? pre.reason : post.reason);
    appendField(out, size, "target", pre.target ? pre.target : post.target);
    if (sameSurface && post.targetIndex >= 0) {
        appendIntField(out, size, "target_index", static_cast<int>(post.targetIndex));
    } else if (pre.targetIndex >= 0) {
        appendIntField(out, size, "target_index", static_cast<int>(pre.targetIndex));
    } else if (post.targetIndex >= 0) {
        appendIntField(out, size, "target_index", static_cast<int>(post.targetIndex));
    }
    if (sameSurface && post.targetStep >= 0) {
        appendIntField(out, size, "target_step", static_cast<int>(post.targetStep));
    } else if (pre.targetStep >= 0) {
        appendIntField(out, size, "target_step", static_cast<int>(pre.targetStep));
    } else if (post.targetStep >= 0) {
        appendIntField(out, size, "target_step", static_cast<int>(post.targetStep));
    }
    if (pre.targetMask >= 0) {
        appendIntField(out, size, "pre_target_mask", static_cast<int>(pre.targetMask));
    }
    if (post.targetMask >= 0) {
        appendIntField(out, size, "target_mask", static_cast<int>(post.targetMask));
    } else if (pre.targetMask >= 0) {
        appendIntField(out, size, "target_mask", static_cast<int>(pre.targetMask));
    }
    if (pre.property) appendField(out, size, "pre_property", pre.property);
    appendField(out, size, "property", sameSurface && post.property ? post.property : pre.property);
    appendField(
        out,
        size,
        "value_label",
        sameSurface && post.valueLabel[0] ? post.valueLabel : pre.valueLabel
    );
    if (post.hasStepOn) {
        appendBoolField(out, size, "step_on", post.stepOn);
    } else if (pre.hasStepOn) {
        appendBoolField(out, size, "step_on", pre.stepOn);
    }
}

}  // namespace

FLASHMEM SemanticUxSnapshot makeSemanticUxSnapshot(const core::state::CoreState& state) {
    return SemanticUxSnapshot{
        .view = state.activeView.get(),
        .overlay = state.overlays.current(),
        .playing = state.statusBar.playing.get(),
        .playheadStep = state.sequencer.playheadStep.get(),
        .sequencerPage = state.sequencer.page.get(),
        .sharedTrack = state.sharedTrackActive.get(),
        .sharedTrackMask = state.sharedTrackEnabledMask.get(),
    };
}

FLASHMEM SemanticUxRecorder::SemanticUxRecorder(SemanticUxRecorderOptions options) {
    configure(options);
}

FLASHMEM void SemanticUxRecorder::configure(SemanticUxRecorderOptions options) {
    sink_ = options.sink;
    enabled_ = options.enabled;
}

FLASHMEM void SemanticUxRecorder::setEnabled(bool enabled) {
    enabled_ = enabled;
}

FLASHMEM bool SemanticUxRecorder::enabled() const {
    return enabled_;
}

FLASHMEM void SemanticUxRecorder::onBindingTrace(const oc::core::input::InputBindingTraceEvent& event) {
    onBindingTrace(event, SemanticUxSnapshot{});
}

FLASHMEM void SemanticUxRecorder::onBindingTrace(
    const oc::core::input::InputBindingTraceEvent& event,
    const SemanticUxSnapshot& preSnapshot
) {
    if (!enabled_ || sink_ == nullptr) {
        return;
    }

    if (event.stage != oc::core::input::InputBindingTraceStage::Dispatch || !event.dispatched) {
        return;
    }

    PendingRecord record{};
    record.traceEvent = event;
    record.sequence = next_sequence_++;
    record.bindingId = event.bindingId;
    record.scopeId = event.scopeId;
    record.authorityScope = event.authorityScope;
    record.preSnapshot = preSnapshot;

    if (event.domain == oc::core::input::InputBindingTraceDomain::Encoder) {
        record.kind = RecordKind::Encoder;
        record.encoderId = event.encoderId;
        record.encoderType = event.encoderType;
        record.encoderValueKind = valueKindForEncoder(event.encoderId);
        record.encoderValueMilli = milliFromFloat(event.encoderValue);
    } else {
        record.kind = RecordKind::Button;
        record.buttonId = event.buttonId;
        record.buttonType = event.buttonType;
    }

    if (auto* provider = currentSemanticUxContextProvider()) {
        provider->captureSemanticUxContext(event, record.preContext);
    }

    enqueue_(record);
}

FLASHMEM void SemanticUxRecorder::flush(uint32_t nowMs, const core::state::CoreState& state) {
    flush(nowMs, makeSemanticUxSnapshot(state));
}

FLASHMEM void SemanticUxRecorder::flush(uint32_t nowMs, const SemanticUxSnapshot& snapshot) {
    if (!enabled_ || sink_ == nullptr) {
        return;
    }

    PendingRecord record{};
    while (pop_(record)) {
        writeRecord_(nowMs, record, snapshot);
    }

    if (reported_dropped_count_ != dropped_count_) {
        writeDropReport_(nowMs);
        reported_dropped_count_ = dropped_count_;
    }
}

FLASHMEM std::size_t SemanticUxRecorder::pendingCount() const {
    return count_;
}

FLASHMEM uint32_t SemanticUxRecorder::droppedCount() const {
    return dropped_count_;
}

FLASHMEM bool SemanticUxRecorder::enqueue_(const PendingRecord& record) {
    if (count_ == CAPACITY) {
        ++dropped_count_;
        return false;
    }

    queue_[tail_] = record;
    tail_ = (tail_ + 1U) % CAPACITY;
    ++count_;
    return true;
}

FLASHMEM bool SemanticUxRecorder::pop_(PendingRecord& record) {
    if (count_ == 0) {
        return false;
    }

    record = queue_[head_];
    head_ = (head_ + 1U) % CAPACITY;
    --count_;
    return true;
}

FLASHMEM void SemanticUxRecorder::writeRecord_(uint32_t nowMs,
                                               const PendingRecord& record,
                                               const SemanticUxSnapshot& postSnapshot) {
    SemanticUxContext postContext{};
    if (auto* provider = currentSemanticUxContextProvider()) {
        provider->captureSemanticUxContext(record.traceEvent, postContext);
    }

    char contextFields[384];
    formatContextFields(contextFields, sizeof(contextFields), record.preContext, postContext);

    char line[1408];
    if (record.kind == RecordKind::Encoder) {
        std::snprintf(
            line,
            sizeof(line),
            "UXR {\"seq\":%lu,\"ms\":%lu,\"kind\":\"encoder\",\"gesture\":\"%s\","
            "\"encoder\":\"%s\",\"encoder_id\":%u,\"value_kind\":\"%s\",\"%s\":%ld,"
            "\"binding\":%u,\"scope\":%u,\"authority_scope\":%u,"
            "\"pre_view\":\"%s\",\"pre_overlay\":\"%s\",\"pre_playing\":%u,"
            "\"pre_playhead\":%d,\"pre_page\":%u,\"pre_shared_track\":%u,"
            "\"pre_shared_mask\":%u,\"view\":\"%s\",\"overlay\":\"%s\",\"playing\":%u,"
            "\"playhead\":%d,\"page\":%u,\"shared_track\":%u,\"shared_mask\":%u%s}",
            static_cast<unsigned long>(record.sequence),
            static_cast<unsigned long>(nowMs),
            encoderGestureName(record.encoderType),
            encoderName(record.encoderId),
            static_cast<unsigned int>(record.encoderId),
            valueKindName(record.encoderValueKind),
            record.encoderValueKind == EncoderValueKind::Delta ? "delta_milli" : "value_milli",
            static_cast<long>(record.encoderValueMilli),
            static_cast<unsigned int>(record.bindingId),
            static_cast<unsigned int>(record.scopeId),
            static_cast<unsigned int>(record.authorityScope),
            viewName(record.preSnapshot.view),
            overlayName(record.preSnapshot.overlay),
            record.preSnapshot.playing ? 1U : 0U,
            static_cast<int>(record.preSnapshot.playheadStep),
            static_cast<unsigned int>(record.preSnapshot.sequencerPage),
            static_cast<unsigned int>(record.preSnapshot.sharedTrack),
            static_cast<unsigned int>(record.preSnapshot.sharedTrackMask),
            viewName(postSnapshot.view),
            overlayName(postSnapshot.overlay),
            postSnapshot.playing ? 1U : 0U,
            static_cast<int>(postSnapshot.playheadStep),
            static_cast<unsigned int>(postSnapshot.sequencerPage),
            static_cast<unsigned int>(postSnapshot.sharedTrack),
            static_cast<unsigned int>(postSnapshot.sharedTrackMask),
            contextFields
        );
    } else {
        std::snprintf(
            line,
            sizeof(line),
            "UXR {\"seq\":%lu,\"ms\":%lu,\"kind\":\"button\",\"gesture\":\"%s\","
            "\"button\":\"%s\",\"button_id\":%u,\"binding\":%u,\"scope\":%u,"
            "\"authority_scope\":%u,\"pre_view\":\"%s\",\"pre_overlay\":\"%s\","
            "\"pre_playing\":%u,\"pre_playhead\":%d,\"pre_page\":%u,"
            "\"pre_shared_track\":%u,\"pre_shared_mask\":%u,\"view\":\"%s\","
            "\"overlay\":\"%s\",\"playing\":%u,\"playhead\":%d,\"page\":%u,"
            "\"shared_track\":%u,\"shared_mask\":%u%s}",
            static_cast<unsigned long>(record.sequence),
            static_cast<unsigned long>(nowMs),
            buttonGestureName(record.buttonType),
            buttonName(record.buttonId),
            static_cast<unsigned int>(record.buttonId),
            static_cast<unsigned int>(record.bindingId),
            static_cast<unsigned int>(record.scopeId),
            static_cast<unsigned int>(record.authorityScope),
            viewName(record.preSnapshot.view),
            overlayName(record.preSnapshot.overlay),
            record.preSnapshot.playing ? 1U : 0U,
            static_cast<int>(record.preSnapshot.playheadStep),
            static_cast<unsigned int>(record.preSnapshot.sequencerPage),
            static_cast<unsigned int>(record.preSnapshot.sharedTrack),
            static_cast<unsigned int>(record.preSnapshot.sharedTrackMask),
            viewName(postSnapshot.view),
            overlayName(postSnapshot.overlay),
            postSnapshot.playing ? 1U : 0U,
            static_cast<int>(postSnapshot.playheadStep),
            static_cast<unsigned int>(postSnapshot.sequencerPage),
            static_cast<unsigned int>(postSnapshot.sharedTrack),
            static_cast<unsigned int>(postSnapshot.sharedTrackMask),
            contextFields
        );
    }

    line[sizeof(line) - 1U] = '\0';
    sink_->writeLine(line);
}

FLASHMEM void SemanticUxRecorder::writeDropReport_(uint32_t nowMs) {
    char line[96];
    std::snprintf(
        line,
        sizeof(line),
        "UXR {\"ms\":%lu,\"kind\":\"drop\",\"dropped\":%lu}",
        static_cast<unsigned long>(nowMs),
        static_cast<unsigned long>(dropped_count_)
    );
    line[sizeof(line) - 1U] = '\0';
    sink_->writeLine(line);
}

}  // namespace core::validation::ux
