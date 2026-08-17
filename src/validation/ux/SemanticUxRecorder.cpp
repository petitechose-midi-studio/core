#include "validation/ux/SemanticUxRecorder.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

#include "config/InputIDs.hpp"
#include "config/PlatformCompat.hpp"
#include "state/CoreState.hpp"
#include "validation/ux/SemanticUxContext.hpp"
#include "validation/ux/SemanticUxNames.hpp"

namespace core::validation::ux {
namespace {

SemanticUxRecorder* currentEncoderContractTraceRecorder = nullptr;

FLASHMEM int32_t milliFromFloat(float value) {
    if (!std::isfinite(value)) return 0;

    const double scaled = static_cast<double>(value) * 1000.0;
    const double rounded = scaled >= 0.0
        ? std::floor(scaled + 0.5)
        : std::ceil(scaled - 0.5);
    constexpr double minimum =
        static_cast<double>(std::numeric_limits<int32_t>::min());
    constexpr double maximum =
        static_cast<double>(std::numeric_limits<int32_t>::max());
    if (rounded <= minimum) return std::numeric_limits<int32_t>::min();
    if (rounded >= maximum) return std::numeric_limits<int32_t>::max();
    return static_cast<int32_t>(rounded);
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

FLASHMEM const char* encoderContractOwnerName(EncoderContractOwner owner) {
    switch (owner) {
        case EncoderContractOwner::SequencerRoot:
            return "sequencer_root";
        case EncoderContractOwner::DrumLaneEditor:
            return "drum_lane_editor";
    }
    return "unknown";
}

FLASHMEM const char* encoderContractModeName(EncoderContractMode mode) {
    switch (mode) {
        case EncoderContractMode::Raw:
            return "raw";
        case EncoderContractMode::Normalized:
            return "normalized";
    }
    return "unknown";
}

FLASHMEM std::size_t escapedJsonLength(const char* value) {
    if (!value) return 0;

    std::size_t length = 0;
    for (const unsigned char* cursor =
             reinterpret_cast<const unsigned char*>(value);
         *cursor != '\0';
         ++cursor) {
        switch (*cursor) {
            case '"':
            case '\\':
            case '\b':
            case '\f':
            case '\n':
            case '\r':
            case '\t':
                length += 2U;
                break;
            default:
                length += *cursor < 0x20U ? 6U : 1U;
                break;
        }
    }
    return length;
}

FLASHMEM void appendEscapedJson(char* out, std::size_t& used, const char* value) {
    static constexpr char HEX_DIGITS[] = "0123456789abcdef";
    if (!value) return;

    for (const unsigned char* cursor =
             reinterpret_cast<const unsigned char*>(value);
         *cursor != '\0';
         ++cursor) {
        switch (*cursor) {
            case '"':
                out[used++] = '\\';
                out[used++] = '"';
                break;
            case '\\':
                out[used++] = '\\';
                out[used++] = '\\';
                break;
            case '\b':
                out[used++] = '\\';
                out[used++] = 'b';
                break;
            case '\f':
                out[used++] = '\\';
                out[used++] = 'f';
                break;
            case '\n':
                out[used++] = '\\';
                out[used++] = 'n';
                break;
            case '\r':
                out[used++] = '\\';
                out[used++] = 'r';
                break;
            case '\t':
                out[used++] = '\\';
                out[used++] = 't';
                break;
            default:
                if (*cursor < 0x20U) {
                    out[used++] = '\\';
                    out[used++] = 'u';
                    out[used++] = '0';
                    out[used++] = '0';
                    out[used++] = HEX_DIGITS[(*cursor >> 4U) & 0x0FU];
                    out[used++] = HEX_DIGITS[*cursor & 0x0FU];
                } else {
                    out[used++] = static_cast<char>(*cursor);
                }
                break;
        }
    }
}

FLASHMEM void appendField(char* out, std::size_t size, const char* key, const char* value) {
    if (!out || size == 0U || !key || key[0] == '\0' || !value ||
        value[0] == '\0') {
        return;
    }
    const std::size_t used = std::strlen(out);
    if (used >= size) return;

    const std::size_t keyLength = escapedJsonLength(key);
    const std::size_t valueLength = escapedJsonLength(value);
    constexpr std::size_t JSON_FIELD_PUNCTUATION = 6U;  // ,"":""
    const std::size_t available = size - used;
    if (keyLength >= available) return;
    std::size_t remaining = available - keyLength;
    if (valueLength >= remaining) return;
    remaining -= valueLength;
    if (JSON_FIELD_PUNCTUATION >= remaining) return;

    std::size_t cursor = used;
    out[cursor++] = ',';
    out[cursor++] = '"';
    appendEscapedJson(out, cursor, key);
    out[cursor++] = '"';
    out[cursor++] = ':';
    out[cursor++] = '"';
    appendEscapedJson(out, cursor, value);
    out[cursor++] = '"';
    out[cursor] = '\0';
}

FLASHMEM void appendRawField(
    char* out,
    std::size_t size,
    const char* key,
    const char* rawValue
) {
    if (!out || size == 0U || !key || key[0] == '\0' || !rawValue ||
        rawValue[0] == '\0') {
        return;
    }
    const std::size_t used = std::strlen(out);
    if (used >= size) return;

    const std::size_t keyLength = escapedJsonLength(key);
    const std::size_t valueLength = std::strlen(rawValue);
    constexpr std::size_t JSON_FIELD_PUNCTUATION = 4U;  // ,"":
    const std::size_t available = size - used;
    if (keyLength >= available) return;
    std::size_t remaining = available - keyLength;
    if (valueLength >= remaining) return;
    remaining -= valueLength;
    if (JSON_FIELD_PUNCTUATION >= remaining) return;

    std::size_t cursor = used;
    out[cursor++] = ',';
    out[cursor++] = '"';
    appendEscapedJson(out, cursor, key);
    out[cursor++] = '"';
    out[cursor++] = ':';
    std::memcpy(out + cursor, rawValue, valueLength);
    cursor += valueLength;
    out[cursor] = '\0';
}

FLASHMEM void appendIntField(char* out, std::size_t size, const char* key, int value) {
    char encoded[16]{};
    const int written = std::snprintf(encoded, sizeof(encoded), "%d", value);
    if (written <= 0 || static_cast<std::size_t>(written) >= sizeof(encoded)) return;
    appendRawField(out, size, key, encoded);
}

FLASHMEM void appendUint32Field(char* out,
                       std::size_t size,
                       const char* key,
                       uint32_t value) {
    char encoded[16]{};
    const int written = std::snprintf(
        encoded,
        sizeof(encoded),
        "%lu",
        static_cast<unsigned long>(value)
    );
    if (written <= 0 || static_cast<std::size_t>(written) >= sizeof(encoded)) return;
    appendRawField(out, size, key, encoded);
}

FLASHMEM void appendBoolField(char* out, std::size_t size, const char* key, bool value) {
    appendRawField(out, size, key, value ? "1" : "0");
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

    const auto intent =
        pre.intent != core::state::interaction::ControllerIntent::NONE
            ? pre.intent
            : post.intent;
    appendField(out, size, "intent", controllerIntentName(intent));
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
    const SemanticUxContext& targetRange = post.targetCount >= 0 ? post : pre;
    if (targetRange.targetCount >= 0) {
        appendIntField(out, size, "target_count", static_cast<int>(targetRange.targetCount));
    }
    if (targetRange.targetPage >= 0) {
        appendIntField(out, size, "target_page", static_cast<int>(targetRange.targetPage));
    }
    if (pre.targetMask >= 0) {
        appendIntField(out, size, "pre_target_mask", static_cast<int>(pre.targetMask));
    }
    if (post.targetMask >= 0) {
        appendIntField(out, size, "target_mask", static_cast<int>(post.targetMask));
    } else if (pre.targetMask >= 0) {
        appendIntField(out, size, "target_mask", static_cast<int>(pre.targetMask));
    }
    const SemanticUxContext& transfer = post.sourceMask >= 0 ? post : pre;
    if (transfer.sourceMask >= 0) {
        appendIntField(out, size, "source_mask", static_cast<int>(transfer.sourceMask));
    }
    if (transfer.createMask >= 0) {
        appendIntField(out, size, "create_mask", static_cast<int>(transfer.createMask));
    }
    if (transfer.overwriteMask >= 0) {
        appendIntField(out, size, "overwrite_mask", static_cast<int>(transfer.overwriteMask));
    }
    appendField(
        out,
        size,
        "route_policy",
        post.routePolicy ? post.routePolicy : pre.routePolicy
    );
    appendField(out, size, "projection", post.projection ? post.projection : pre.projection);
    appendField(out, size, "source", post.source ? post.source : pre.source);
    appendField(out, size, "winner", post.winner ? post.winner : pre.winner);
    appendField(
        out,
        size,
        "winner_source",
        post.winnerSource ? post.winnerSource : pre.winnerSource
    );
    const SemanticUxContext& activation =
        post.hasActivationGeneration ? post : pre;
    if (activation.hasActivationGeneration && activation.activationOrigin &&
        activation.activationGeneration != 0) {
        appendField(
            out,
            size,
            "activation_origin",
            activation.activationOrigin
        );
        appendUint32Field(
            out,
            size,
            "activation_generation",
            activation.activationGeneration
        );
    }
    const SemanticUxContext& mapping = post.mappingCount >= 0 ? post : pre;
    if (mapping.mappingIndex >= 0) {
        appendIntField(out, size, "mapping_index", static_cast<int>(mapping.mappingIndex));
    }
    if (mapping.mappingCount >= 0) {
        appendIntField(out, size, "mapping_count", static_cast<int>(mapping.mappingCount));
    }
    if (mapping.sourceTrack >= 0) {
        appendIntField(out, size, "source_track", static_cast<int>(mapping.sourceTrack));
    }
    if (mapping.targetTrack >= 0) {
        appendIntField(out, size, "target_track", static_cast<int>(mapping.targetTrack));
    }
    appendField(out, size, "target_kind", mapping.targetKind);
    if (mapping.inheritedLaneCount >= 0) {
        appendIntField(
            out,
            size,
            "inherited_lane_count",
            static_cast<int>(mapping.inheritedLaneCount)
        );
    }
    if (mapping.pinnedLaneCount >= 0) {
        appendIntField(
            out,
            size,
            "pinned_lane_count",
            static_cast<int>(mapping.pinnedLaneCount)
        );
    }
    const SemanticUxContext& operation =
        post.operationOrigin || post.hasOperationGeneration || post.operationStatus ? post : pre;
    appendField(out, size, "operation_origin", operation.operationOrigin);
    if (operation.hasOperationGeneration && operation.operationGeneration != 0) {
        appendUint32Field(
            out,
            size,
            "operation_generation",
            operation.operationGeneration
        );
    }
    appendField(out, size, "operation_status", operation.operationStatus);
    const SemanticUxContext& route = post.hasTargetRoute ? post : pre;
    if (route.hasTargetRoute) {
        appendIntField(out, size, "target_route", static_cast<int>(route.targetRoute));
        appendBoolField(out, size, "target_route_valid", route.targetRouteValid);
    }
    if (pre.property) appendField(out, size, "pre_property", pre.property);
    appendField(out, size, "property", sameSurface && post.property ? post.property : pre.property);
    appendField(
        out,
        size,
        "value_label",
        sameSurface && post.valueLabel[0] ? post.valueLabel : pre.valueLabel
    );
    const SemanticUxContext& conflict = post.hasConflict ? post : pre;
    if (conflict.hasConflict) appendBoolField(out, size, "conflict", conflict.conflict);
    const SemanticUxContext& authored = post.hasAuthoredValue ? post : pre;
    if (authored.hasAuthoredValue) {
        appendIntField(out, size, "authored_value", authored.authoredValue);
    }
    const SemanticUxContext& resolvedValue = post.hasResolvedValue ? post : pre;
    if (resolvedValue.hasResolvedValue) {
        appendIntField(out, size, "resolved_value", resolvedValue.resolvedValue);
    }
    const SemanticUxContext& controller = post.controller >= 0 ? post : pre;
    if (controller.controller >= 0) {
        appendIntField(out, size, "controller", static_cast<int>(controller.controller));
    }
    if (controller.defaultController >= 0) {
        appendIntField(
            out,
            size,
            "default_controller",
            static_cast<int>(controller.defaultController)
        );
    }
    if (post.hasStepOn) {
        appendBoolField(out, size, "step_on", post.stepOn);
    } else if (pre.hasStepOn) {
        appendBoolField(out, size, "step_on", pre.stepOn);
    }
    const SemanticUxContext& resolved = post.hasResolvedStep ? post : pre;
    if (resolved.hasResolvedStep) {
        appendIntField(out, size, "resolved_note", resolved.resolvedNote);
        appendIntField(out, size, "resolved_velocity", resolved.resolvedVelocity);
        appendIntField(out, size, "resolved_gate", resolved.resolvedGate);
        appendIntField(out, size, "resolved_nudge", resolved.resolvedNudge);
        appendIntField(out, size, "resolved_probability", resolved.resolvedProbability);
        appendBoolField(out, size, "resolved_variation", resolved.resolvedVariationVisible);
    }

    appendField(
        out,
        size,
        "draft_kind",
        post.draftKind ? post.draftKind : pre.draftKind
    );
    const SemanticUxContext& draftState = post.hasDraftActive ? post : pre;
    if (draftState.hasDraftActive) {
        appendBoolField(out, size, "draft_active", draftState.draftActive);
    }
    const SemanticUxContext& draftDirty =
        post.hasDraftActive && post.draftActive && post.hasDraftDirty ? post : pre;
    if (draftDirty.hasDraftDirty) {
        appendBoolField(out, size, "dirty", draftDirty.draftDirty);
    }
    appendField(
        out,
        size,
        "exit_choice",
        post.exitChoice ? post.exitChoice : pre.exitChoice
    );
    appendField(
        out,
        size,
        "draft_failure",
        post.draftFailure ? post.draftFailure : pre.draftFailure
    );
    const char* action = pre.action ? pre.action : post.action;
    appendField(out, size, "action", action);

    // Publication is a transition fact, not another retained bit of UI state.
    // Derive it from the pre/post draft authority so a failed Apply remains
    // active and can never be reported as published.
    const SemanticUxContext& publication = post.hasPublished ? post : pre;
    if (publication.hasPublished) {
        appendBoolField(out, size, "published", publication.published);
    } else if (pre.hasDraftActive && pre.draftActive && post.hasDraftActive &&
               !post.draftActive) {
        const bool published = action != nullptr &&
            (std::strcmp(action, "apply") == 0 ||
             std::strcmp(action, "save") == 0);
        appendBoolField(out, size, "published", published);
    }
}

FLASHMEM bool hasSemanticContext(const SemanticUxContext& context) {
    return context.intent != core::state::interaction::ControllerIntent::NONE ||
           context.mode || context.effect || context.outcome || context.reason ||
           context.target || context.routePolicy || context.projection ||
           context.source || context.winner || context.winnerSource ||
           context.property ||
           context.valueLabel[0] != '\0' || context.targetIndex >= 0 ||
           context.targetStep >= 0 || context.targetCount >= 0 ||
           context.targetPage >= 0 || context.targetMask >= 0 ||
           context.sourceMask >= 0 || context.createMask >= 0 ||
           context.overwriteMask >= 0 || context.hasTargetRoute ||
           context.hasActivationGeneration || context.mappingIndex >= 0 ||
           context.mappingCount >= 0 || context.sourceTrack >= 0 ||
           context.targetTrack >= 0 || context.targetKind ||
           context.inheritedLaneCount >= 0 || context.pinnedLaneCount >= 0 ||
           context.operationOrigin || context.hasOperationGeneration ||
           context.operationStatus ||
           context.hasConflict || context.hasAuthoredValue ||
           context.hasResolvedValue || context.controller >= 0 ||
           context.defaultController >= 0 || context.hasStepOn ||
           context.hasResolvedStep || context.draftKind ||
           context.hasDraftActive || context.hasDraftDirty ||
           context.hasPublished ||
           context.exitChoice || context.draftFailure || context.action;
}

FLASHMEM bool isIgnoredSemanticContext(const SemanticUxContext& context) {
    return (context.outcome && std::strcmp(context.outcome, "ignored") == 0) ||
           (context.effect && std::strcmp(context.effect, "release_ignored") == 0);
}

FLASHMEM void copyCaptureLabel(char (&out)[65], const char* label) {
    std::size_t written = 0;
    if (label) {
        for (const char* cursor = label; *cursor != '\0' && written + 1U < sizeof(out);
             ++cursor) {
            const unsigned char value = static_cast<unsigned char>(*cursor);
            const bool safe = (value >= 'a' && value <= 'z') ||
                              (value >= 'A' && value <= 'Z') ||
                              (value >= '0' && value <= '9') || value == '-' ||
                              value == '_';
            out[written++] = safe ? static_cast<char>(value) : '_';
        }
    }
    if (written == 0) {
        constexpr char fallback[] = "capture";
        std::memcpy(out, fallback, sizeof(fallback));
        return;
    }
    out[written] = '\0';
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
    emit_semantic_encoder_dispatch_ = options.emitSemanticEncoderDispatch;
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

FLASHMEM void SemanticUxRecorder::onBindingTrace(const oc::core::input::InputBindingTraceEvent& event,
                                        const SemanticUxSnapshot& preSnapshot) {
    if (!enabled_ || sink_ == nullptr) {
        return;
    }

    const bool input =
        event.stage == oc::core::input::InputBindingTraceStage::Event;
    const bool dispatch =
        event.stage == oc::core::input::InputBindingTraceStage::Dispatch &&
        event.dispatched;
    if (!input && !dispatch) {
        return;
    }
    if (dispatch &&
        event.domain == oc::core::input::InputBindingTraceDomain::Encoder &&
        !emit_semantic_encoder_dispatch_) {
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
        record.kind = input ? RecordKind::InputEncoder : RecordKind::Encoder;
        record.encoderId = event.encoderId;
        record.encoderType = event.encoderType;
        record.encoderValueKind = valueKindForEncoder(event.encoderId);
        record.encoderValueMilli = milliFromFloat(event.encoderValue);
    } else {
        record.kind = input ? RecordKind::InputButton : RecordKind::Button;
        record.buttonId = event.buttonId;
        record.buttonType = event.buttonType;
    }

    if (dispatch) {
        if (auto* provider = currentSemanticUxContextProvider()) {
            provider->captureSemanticUxContext(event, record.preContext);
        }
    }

    enqueue_(record);
}

FLASHMEM void SemanticUxRecorder::onEncoderContractTrace(
    const EncoderContractTraceEvent& event
) {
    if (!enabled_ || sink_ == nullptr) return;

    PendingRecord record{};
    record.kind = RecordKind::EncoderContract;
    record.sequence = next_sequence_++;
    record.encoderId = event.encoderId;
    record.encoderContract = event;
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

FLASHMEM void SemanticUxRecorder::capture(uint32_t nowMs,
                                 const char* label,
                                 const core::state::CoreState& state) {
    capture(nowMs, label, makeSemanticUxSnapshot(state));
}

FLASHMEM void SemanticUxRecorder::capture(uint32_t nowMs,
                                 const char* label,
                                 const SemanticUxSnapshot& snapshot) {
    if (!enabled_ || sink_ == nullptr) return;

    // Keep sequence order deterministic even if a caller forgot to flush the
    // input record before taking the screenshot.
    flush(nowMs, snapshot);
    writeCapture_(nowMs, label, snapshot);
}

FLASHMEM void SemanticUxRecorder::resetCaptureContext(bool allowCurrentSurfaceProjection) {
    has_last_semantic_event_ = false;
    last_semantic_intent_ = core::state::interaction::ControllerIntent::NONE;
    last_semantic_effect_ = nullptr;
    last_semantic_outcome_ = nullptr;
    last_semantic_sequence_ = 0;
    allow_state_projection_capture_ = allowCurrentSurfaceProjection;
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
    if (record.kind == RecordKind::EncoderContract) {
        const auto& contract = record.encoderContract;
        char line[640];
        std::snprintf(
            line,
            sizeof(line),
            "UXR {\"seq\":%lu,\"ms\":%lu,\"kind\":\"encoder_contract\","
            "\"owner\":\"%s\",\"encoder\":\"%s\",\"encoder_id\":%u,"
            "\"mode\":\"%s\",\"minimum_milli\":%ld,\"maximum_milli\":%ld,"
            "\"position_milli\":%ld,\"discrete_steps\":%u,"
            "\"ticks_per_step\":%u,\"normalized_turns_milli\":%ld,"
            "\"view\":\"%s\",\"overlay\":\"%s\"}",
            static_cast<unsigned long>(record.sequence),
            static_cast<unsigned long>(nowMs),
            encoderContractOwnerName(contract.owner),
            encoderName(contract.encoderId),
            static_cast<unsigned int>(contract.encoderId),
            encoderContractModeName(contract.mode),
            static_cast<long>(contract.minimumMilli),
            static_cast<long>(contract.maximumMilli),
            static_cast<long>(contract.positionMilli),
            static_cast<unsigned int>(contract.discreteSteps),
            static_cast<unsigned int>(contract.discreteTicksPerStep),
            static_cast<long>(contract.normalizedTurnsMilli),
            viewName(postSnapshot.view),
            overlayName(postSnapshot.overlay)
        );
        line[sizeof(line) - 1U] = '\0';
        sink_->writeLine(line);
        return;
    }

    if (record.kind == RecordKind::InputEncoder) {
        char line[256];
        std::snprintf(
            line,
            sizeof(line),
            "UXR {\"seq\":%lu,\"ms\":%lu,\"kind\":\"input\","
            "\"gesture\":\"turn\",\"encoder\":\"%s\",\"encoder_id\":%u,"
            "\"value_kind\":\"%s\",\"%s\":%ld}",
            static_cast<unsigned long>(record.sequence),
            static_cast<unsigned long>(nowMs),
            encoderName(record.encoderId),
            static_cast<unsigned int>(record.encoderId),
            valueKindName(record.encoderValueKind),
            record.encoderValueKind == EncoderValueKind::Delta
                ? "delta_milli"
                : "value_milli",
            static_cast<long>(record.encoderValueMilli)
        );
        line[sizeof(line) - 1U] = '\0';
        sink_->writeLine(line);
        return;
    }

    if (record.kind == RecordKind::InputButton) {
        char line[224];
        std::snprintf(
            line,
            sizeof(line),
            "UXR {\"seq\":%lu,\"ms\":%lu,\"kind\":\"input\","
            "\"gesture\":\"%s\",\"button\":\"%s\",\"button_id\":%u}",
            static_cast<unsigned long>(record.sequence),
            static_cast<unsigned long>(nowMs),
            buttonGestureName(record.buttonType),
            buttonName(record.buttonId),
            static_cast<unsigned int>(record.buttonId)
        );
        line[sizeof(line) - 1U] = '\0';
        sink_->writeLine(line);
        return;
    }

    SemanticUxContext postContext{};
    if (auto* provider = currentSemanticUxContextProvider()) {
        provider->captureSemanticUxContext(record.traceEvent, postContext);
    }

    const SemanticUxContext& meaningfulContext =
        hasSemanticContext(postContext) ? postContext : record.preContext;
    const bool ignoredContext =
        isIgnoredSemanticContext(record.preContext) ||
        isIgnoredSemanticContext(postContext);
    if (hasSemanticContext(meaningfulContext) &&
        !ignoredContext) {
        last_semantic_event_ = record.traceEvent;
        last_semantic_intent_ =
            record.preContext.intent !=
                    core::state::interaction::ControllerIntent::NONE
                ? record.preContext.intent
                : postContext.intent;
        last_semantic_effect_ = record.preContext.effect
            ? record.preContext.effect
            : postContext.effect;
        last_semantic_outcome_ = record.preContext.outcome
            ? record.preContext.outcome
            : postContext.outcome;
        last_semantic_sequence_ = record.sequence;
        has_last_semantic_event_ = true;
    }

    char contextFields[1536];
    formatContextFields(contextFields, sizeof(contextFields), record.preContext, postContext);

    char line[2560];
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

FLASHMEM void SemanticUxRecorder::writeCapture_(uint32_t nowMs,
                                       const char* label,
                                       const SemanticUxSnapshot& snapshot) {
    SemanticUxContext context{};
    if (has_last_semantic_event_) {
        if (auto* provider = currentSemanticUxContextProvider()) {
            provider->captureSemanticUxContext(last_semantic_event_, context);
        }
        if (last_semantic_intent_ !=
            core::state::interaction::ControllerIntent::NONE) {
            context.intent = last_semantic_intent_;
        }
        if (last_semantic_effect_) context.effect = last_semantic_effect_;
        if (last_semantic_outcome_ && !context.outcome) {
            context.outcome = last_semantic_outcome_;
        }
    } else if (allow_state_projection_capture_) {
        oc::core::input::InputBindingTraceEvent projectionEvent{};
        projectionEvent.buttonId =
            std::numeric_limits<oc::type::ButtonID>::max();
        projectionEvent.encoderId =
            std::numeric_limits<oc::type::EncoderID>::max();
        if (auto* provider = currentSemanticUxContextProvider()) {
            provider->captureSemanticUxContext(projectionEvent, context);
        }
    }
    allow_state_projection_capture_ = false;
    const bool surfaceContext = hasSemanticContext(context);

    char contextFields[1536];
    formatContextFields(contextFields, sizeof(contextFields), context, context);

    char safeLabel[65];
    copyCaptureLabel(safeLabel, label);

    char line[2560];
    std::snprintf(
        line,
        sizeof(line),
        "UXR {\"seq\":%lu,\"ms\":%lu,\"kind\":\"capture\","
        "\"label\":\"%s\",\"surface_context\":%s,\"source_seq\":%lu,"
        "\"view\":\"%s\",\"overlay\":\"%s\",\"playing\":%s,"
        "\"playhead\":%d,\"page\":%u,\"shared_track\":%u,"
        "\"shared_mask\":%u%s}",
        static_cast<unsigned long>(next_sequence_++),
        static_cast<unsigned long>(nowMs),
        safeLabel,
        surfaceContext ? "true" : "false",
        static_cast<unsigned long>(surfaceContext ? last_semantic_sequence_ : 0U),
        viewName(snapshot.view),
        overlayName(snapshot.overlay),
        snapshot.playing ? "true" : "false",
        static_cast<int>(snapshot.playheadStep),
        static_cast<unsigned int>(snapshot.sequencerPage),
        static_cast<unsigned int>(snapshot.sharedTrack),
        static_cast<unsigned int>(snapshot.sharedTrackMask),
        contextFields
    );
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

FLASHMEM void setCurrentEncoderContractTraceRecorder(
    SemanticUxRecorder* recorder
) {
    currentEncoderContractTraceRecorder = recorder;
}

FLASHMEM void clearCurrentEncoderContractTraceRecorder(
    SemanticUxRecorder* recorder
) {
    if (currentEncoderContractTraceRecorder == recorder) {
        currentEncoderContractTraceRecorder = nullptr;
    }
}

FLASHMEM void recordEncoderContractTrace(
    EncoderContractOwner owner,
    EncoderContractMode mode,
    oc::type::EncoderID encoderId,
    float minimum,
    float maximum,
    uint8_t discreteSteps,
    uint8_t discreteTicksPerStep,
    float normalizedTurns,
    float position
) {
    if (currentEncoderContractTraceRecorder == nullptr) return;
    currentEncoderContractTraceRecorder->onEncoderContractTrace({
        .owner = owner,
        .mode = mode,
        .encoderId = encoderId,
        .minimumMilli = milliFromFloat(minimum),
        .maximumMilli = milliFromFloat(maximum),
        .positionMilli = milliFromFloat(position),
        .normalizedTurnsMilli = milliFromFloat(normalizedTurns),
        .discreteSteps = discreteSteps,
        .discreteTicksPerStep = discreteTicksPerStep,
    });
}

}  // namespace core::validation::ux
