#include "state/sequencer/SequencerPatternRandomizeOps.hpp"

#include <algorithm>
#include <config/PlatformCompat.hpp>

#include "state/sequencer/SequencerPitchEditAuthority.hpp"

namespace core::state::sequencer {

namespace {

constexpr uint32_t kSelectionSalt = 0xA511E9B3U;
constexpr uint32_t kValueSalt = 0x63D83595U;
constexpr uint32_t kStepSalt = 0x9E3779B9U;
constexpr uint32_t kPropertySalt = 0x85EBCA6BU;
constexpr uint32_t kRerollSalt = 0xD1B54A35U;

FLASHMEM uint32_t mix32(uint32_t value) {
    value ^= value >> 16U;
    value *= 0x7FEB352DU;
    value ^= value >> 15U;
    value *= 0x846CA68BU;
    value ^= value >> 16U;
    return value;
}

FLASHMEM uint32_t streamForStep(
    const SequencerPatternRandomizeDraft& draft,
    uint8_t step,
    uint32_t streamSalt
) {
    return mix32(
        draft.seed ^
        streamSalt ^
        (static_cast<uint32_t>(step) * kStepSalt) ^
        (static_cast<uint32_t>(draft.property) * kPropertySalt)
    );
}

FLASHMEM uint8_t sanitizeContentLength(uint8_t length) {
    if (length == 0U || length > SequencerPatternState::MAX_STEPS) {
        return SequencerPatternState::DEFAULT_LENGTH;
    }
    return length;
}

FLASHMEM int32_t sourceValue(
    const SequencerPatternSnapshot& base,
    SequencerPatternRandomizeProperty property,
    uint8_t step
) {
    switch (property) {
        case SequencerPatternRandomizeProperty::NOTE:
            return base.note[step];
        case SequencerPatternRandomizeProperty::VELOCITY:
            return base.velocity[step];
        case SequencerPatternRandomizeProperty::GATE:
            return base.gate[step];
        case SequencerPatternRandomizeProperty::NUDGE:
            return base.nudge[step];
        case SequencerPatternRandomizeProperty::PROBABILITY:
            return base.probability[step];
    }
    return 0;
}

FLASHMEM int32_t clampTargetValue(
    SequencerPatternRandomizeProperty property,
    int32_t value
) {
    switch (property) {
        case SequencerPatternRandomizeProperty::NOTE:
        case SequencerPatternRandomizeProperty::VELOCITY:
            return std::clamp<int32_t>(value, 0, 127);
        case SequencerPatternRandomizeProperty::GATE:
            return std::clamp<int32_t>(
                value,
                0,
                SequencerPatternState::MAX_GATE_PERCENT
            );
        case SequencerPatternRandomizeProperty::NUDGE:
            return std::clamp<int32_t>(value, -50, 50);
        case SequencerPatternRandomizeProperty::PROBABILITY:
            return std::clamp<int32_t>(value, 0, 100);
    }
    return value;
}

FLASHMEM int32_t noteTargetValue(
    const SequencerPatternSnapshot& base,
    int32_t source,
    int32_t delta
) {
    if (delta == 0) return source;

    auto scale = base.effectiveScaleSettings;
    scale.clamp();
    if (!content_view_internal::usesScaleDegreePitchEdit(
            StepProperty::NOTE,
            base.pitchEditMode,
            scale
        )) {
        return clampTargetValue(
            SequencerPatternRandomizeProperty::NOTE,
            source + delta
        );
    }

    const int sourceDegree = content_view_internal::scaleDegreeIndexForNote(
        static_cast<uint8_t>(std::clamp<int32_t>(source, 0, 127)),
        scale
    );
    return content_view_internal::scaleNoteForDegreeIndex(
        sourceDegree + static_cast<int>(delta),
        scale
    );
}

FLASHMEM int32_t randomizedTargetValue(
    const SequencerPatternSnapshot& base,
    SequencerPatternRandomizeProperty property,
    int32_t source,
    int32_t delta
) {
    if (delta == 0) return source;
    if (property == SequencerPatternRandomizeProperty::NOTE) {
        return noteTargetValue(base, source, delta);
    }
    return clampTargetValue(property, source + delta);
}

FLASHMEM int32_t signedDelta(uint32_t valueStream, uint16_t range) {
    if (range == 0U) return 0;
    const uint32_t span = static_cast<uint32_t>(range) * 2U + 1U;
    return static_cast<int32_t>(valueStream % span) - static_cast<int32_t>(range);
}

FLASHMEM bool selectedByAmount(uint32_t selectionStream, uint8_t amount) {
    if (amount == 0U) return false;
    if (amount >= SEQUENCER_PATTERN_RANDOMIZE_AMOUNT_MAX) return true;
    // Multiplication maps the full 32-bit stream to [0, 99] without floats.
    const uint8_t bucket = static_cast<uint8_t>(
        (static_cast<uint64_t>(selectionStream) * 100ULL) >> 32U
    );
    return bucket < amount;
}

FLASHMEM void writeTargetValue(
    SequencerPatternSnapshot& target,
    SequencerPatternRandomizeProperty property,
    uint8_t step,
    int32_t value
) {
    switch (property) {
        case SequencerPatternRandomizeProperty::NOTE:
            target.note[step] = static_cast<uint8_t>(value);
            return;
        case SequencerPatternRandomizeProperty::VELOCITY:
            target.velocity[step] = static_cast<uint8_t>(value);
            return;
        case SequencerPatternRandomizeProperty::GATE:
            target.gate[step] = static_cast<uint16_t>(value);
            return;
        case SequencerPatternRandomizeProperty::NUDGE:
            target.nudge[step] = static_cast<int8_t>(value);
            return;
        case SequencerPatternRandomizeProperty::PROBABILITY:
            target.probability[step] = static_cast<uint8_t>(value);
            return;
    }
}

FLASHMEM void includeProjection(
    SequencerPatternRandomizeSummary& summary,
    const SequencerPatternRandomizeStepProjection& projection
) {
    if (projection.eligible) ++summary.eligibleCount;
    if (projection.selected) ++summary.selectedCount;
    if (projection.changed) ++summary.changedCount;
    if (projection.clamped) ++summary.clampedCount;
}

FLASHMEM SequencerPatternRandomizeStepProjection projectSanitizedDraftStep(
    const SequencerPatternSnapshot& base,
    const SequencerPatternRandomizeDraft& draft,
    uint8_t step
) {
    SequencerPatternRandomizeStepProjection out{};
    if (step >= sanitizeContentLength(base.length)) return out;

    out.eligible = !draft.activeOnly || base.enabledMask.test(step);
    out.sourceValue = sourceValue(base, draft.property, step);
    out.targetValue = out.sourceValue;
    if (!out.eligible) return out;

    out.selected = selectedByAmount(
        streamForStep(draft, step, kSelectionSalt),
        draft.amount
    );
    if (!out.selected || draft.range == 0U) return out;

    out.delta = signedDelta(
        streamForStep(draft, step, kValueSalt),
        draft.range
    );
    out.targetValue = randomizedTargetValue(
        base,
        draft.property,
        out.sourceValue,
        out.delta
    );
    out.changed = out.targetValue != out.sourceValue;

    if (out.delta != 0) {
        const int32_t nativeUnclamped = out.sourceValue + out.delta;
        if (draft.property == SequencerPatternRandomizeProperty::NOTE &&
            content_view_internal::usesScaleDegreePitchEdit(
                StepProperty::NOTE,
                base.pitchEditMode,
                base.effectiveScaleSettings
            )) {
            const auto scale = sanitizedScaleSettings(base.effectiveScaleSettings);
            const int degree = content_view_internal::scaleDegreeIndexForNote(
                static_cast<uint8_t>(std::clamp<int32_t>(out.sourceValue, 0, 127)),
                scale
            );
            const int targetDegree = degree + static_cast<int>(out.delta);
            out.clamped = targetDegree < 0 ||
                targetDegree >= content_view_internal::countScaleNotes(scale);
        } else {
            out.clamped = clampTargetValue(draft.property, nativeUnclamped) !=
                nativeUnclamped;
        }
    }
    return out;
}

}  // namespace

FLASHMEM bool isPatternRandomizeProperty(uint8_t value) {
    switch (static_cast<SequencerPatternRandomizeProperty>(value)) {
        case SequencerPatternRandomizeProperty::NOTE:
        case SequencerPatternRandomizeProperty::VELOCITY:
        case SequencerPatternRandomizeProperty::GATE:
        case SequencerPatternRandomizeProperty::NUDGE:
        case SequencerPatternRandomizeProperty::PROBABILITY:
            return true;
    }
    return false;
}

FLASHMEM SequencerPatternRandomizeProperty sanitizePatternRandomizeProperty(
    uint8_t value
) {
    return isPatternRandomizeProperty(value)
        ? static_cast<SequencerPatternRandomizeProperty>(value)
        : SequencerPatternRandomizeProperty::NOTE;
}

FLASHMEM uint16_t maxPatternRandomizeRange(
    SequencerPatternRandomizeProperty property
) {
    switch (sanitizePatternRandomizeProperty(static_cast<uint8_t>(property))) {
        case SequencerPatternRandomizeProperty::NOTE:
        case SequencerPatternRandomizeProperty::VELOCITY:
            return 127;
        case SequencerPatternRandomizeProperty::GATE:
            return SequencerPatternState::MAX_GATE_PERCENT;
        case SequencerPatternRandomizeProperty::NUDGE:
        case SequencerPatternRandomizeProperty::PROBABILITY:
            return 100;
    }
    return 0;
}

FLASHMEM uint16_t defaultPatternRandomizeRange(
    SequencerPatternRandomizeProperty property
) {
    switch (sanitizePatternRandomizeProperty(static_cast<uint8_t>(property))) {
        case SequencerPatternRandomizeProperty::NOTE:
            return 2;
        case SequencerPatternRandomizeProperty::VELOCITY:
            return 16;
        case SequencerPatternRandomizeProperty::GATE:
            return 25;
        case SequencerPatternRandomizeProperty::NUDGE:
            return 6;
        case SequencerPatternRandomizeProperty::PROBABILITY:
            return 20;
    }
    return 0;
}

FLASHMEM SequencerPatternRandomizeDraft sanitizePatternRandomizeDraft(
    SequencerPatternRandomizeDraft draft
) {
    draft.property = sanitizePatternRandomizeProperty(
        static_cast<uint8_t>(draft.property)
    );
    draft.amount = std::min<uint8_t>(
        draft.amount,
        SEQUENCER_PATTERN_RANDOMIZE_AMOUNT_MAX
    );
    draft.range = std::min<uint16_t>(
        draft.range,
        maxPatternRandomizeRange(draft.property)
    );
    return draft;
}

FLASHMEM SequencerPatternRandomizeStepProjection projectPatternRandomizeStep(
    const SequencerPatternSnapshot& base,
    const SequencerPatternRandomizeDraft& rawDraft,
    uint8_t step
) {
    const auto draft = sanitizePatternRandomizeDraft(rawDraft);
    return projectSanitizedDraftStep(base, draft, step);
}

FLASHMEM SequencerPatternRandomizeSummary summarizePatternRandomize(
    const SequencerPatternSnapshot& base,
    const SequencerPatternRandomizeDraft& draft
) {
    const auto sanitizedDraft = sanitizePatternRandomizeDraft(draft);
    SequencerPatternRandomizeSummary summary{};
    summary.contentLength = sanitizeContentLength(base.length);
    for (uint16_t step = 0; step < summary.contentLength; ++step) {
        includeProjection(
            summary,
            projectSanitizedDraftStep(
                base, sanitizedDraft, static_cast<uint8_t>(step)
            )
        );
    }
    return summary;
}

FLASHMEM SequencerPatternRandomizeSummary materializePatternRandomizeSnapshot(
    const SequencerPatternSnapshot& base,
    const SequencerPatternRandomizeDraft& rawDraft,
    SequencerPatternSnapshot& target
) {
    const auto draft = sanitizePatternRandomizeDraft(rawDraft);
    target = base;

    SequencerPatternRandomizeSummary summary{};
    summary.contentLength = sanitizeContentLength(base.length);
    for (uint16_t step = 0; step < summary.contentLength; ++step) {
        const auto projection = projectSanitizedDraftStep(
            base, draft, static_cast<uint8_t>(step)
        );
        includeProjection(summary, projection);
        if (projection.changed) {
            writeTargetValue(
                target,
                draft.property,
                static_cast<uint8_t>(step),
                projection.targetValue
            );
        }
    }
    return summary;
}

FLASHMEM uint32_t rerollPatternRandomizeSeed(uint32_t seed) {
    return mix32(seed + kRerollSalt);
}

}  // namespace core::state::sequencer
