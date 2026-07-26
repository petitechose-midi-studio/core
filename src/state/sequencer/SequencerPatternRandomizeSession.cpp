#include "state/sequencer/SequencerPatternRandomizeSession.hpp"

#include <algorithm>

#include <config/PlatformCompat.hpp>
#include <oc/util/Index.hpp>

#include "state/sequencer/SequencerSnapshotOps.hpp"

namespace core::state::sequencer {

FLASHMEM void SequencerPatternRandomizeSession::begin(
    const SequencerPatternSnapshot& source,
    uint8_t step
) {
    beginWithSnapshot_(source, step);
}

FLASHMEM void SequencerPatternRandomizeSession::beginWithSnapshot_(
    const SequencerPatternSnapshot& source,
    uint8_t step
) {
    active = true;
    focusedField = SequencerPatternRandomizeField::PROPERTY;
    focusedStep = step;
    draft = {};
    draft.seed = nextSeed;
    nextSeed = rerollPatternRandomizeSeed(draft.seed);
    base = source;
    rebuildPreview();
}

FLASHMEM void SequencerPatternRandomizeSession::begin(
    const SequencerPatternState& source,
    uint8_t step
) {
    SequencerPatternSnapshot captured{};
    captureSnapshot(source, captured);
    beginWithSnapshot_(captured, step);
}

FLASHMEM void SequencerPatternRandomizeSession::cancel() {
    active = false;
    focusedField = SequencerPatternRandomizeField::PROPERTY;
    focusedStep = 0;
    draft = {};
    summary = {};
}

FLASHMEM bool SequencerPatternRandomizeSession::moveField(int direction) {
    if (!active || direction == 0) return false;
    const auto next = static_cast<SequencerPatternRandomizeField>(
        oc::util::wrapIndex(
            static_cast<int>(focusedField) + (direction > 0 ? 1 : -1),
            static_cast<int>(SequencerPatternRandomizeField::COUNT)
        )
    );
    if (next == focusedField) return false;
    focusedField = next;
    return true;
}

FLASHMEM bool SequencerPatternRandomizeSession::setFocusedValue(int32_t value) {
    if (!active) return false;
    const auto range = patternRandomizeValueRange(*this);
    const int32_t next = std::clamp(value, range.minimum, range.maximum);
    bool changed = false;
    switch (focusedField) {
        case SequencerPatternRandomizeField::PROPERTY: {
            const auto property = sanitizePatternRandomizeProperty(
                static_cast<uint8_t>(next)
            );
            if (property == draft.property) return false;
            draft.property = property;
            draft.range = defaultPatternRandomizeRange(property);
            changed = true;
            break;
        }
        case SequencerPatternRandomizeField::AMOUNT:
            if (draft.amount == static_cast<uint8_t>(next)) return false;
            draft.amount = static_cast<uint8_t>(next);
            changed = true;
            break;
        case SequencerPatternRandomizeField::RANGE:
            if (draft.range == static_cast<uint16_t>(next)) return false;
            draft.range = static_cast<uint16_t>(next);
            changed = true;
            break;
        case SequencerPatternRandomizeField::SCOPE: {
            const bool activeOnly = next == 0;
            if (draft.activeOnly == activeOnly) return false;
            draft.activeOnly = activeOnly;
            changed = true;
            break;
        }
        case SequencerPatternRandomizeField::COUNT:
        default:
            return false;
    }
    if (changed) rebuildPreview();
    return changed;
}

FLASHMEM bool SequencerPatternRandomizeSession::reroll() {
    if (!active) return false;
    draft.seed = rerollPatternRandomizeSeed(draft.seed);
    nextSeed = rerollPatternRandomizeSeed(draft.seed);
    rebuildPreview();
    return true;
}

FLASHMEM void SequencerPatternRandomizeSession::rebuildPreview() {
    draft = sanitizePatternRandomizeDraft(draft);
    summary = materializePatternRandomizeSnapshot(base, draft, preview);
}

FLASHMEM SequencerPatternRandomizeValueRange patternRandomizeValueRange(
    const SequencerPatternRandomizeSession& session
) {
    switch (session.focusedField) {
        case SequencerPatternRandomizeField::PROPERTY:
            return {
                0,
                static_cast<int32_t>(SequencerPatternRandomizeProperty::PROBABILITY),
            };
        case SequencerPatternRandomizeField::AMOUNT:
            return {0, SEQUENCER_PATTERN_RANDOMIZE_AMOUNT_MAX};
        case SequencerPatternRandomizeField::RANGE:
            return {0, maxPatternRandomizeRange(session.draft.property)};
        case SequencerPatternRandomizeField::SCOPE:
            return {0, 1};
        case SequencerPatternRandomizeField::COUNT:
        default:
            return {1, 0};
    }
}

FLASHMEM int32_t patternRandomizeFocusedValue(
    const SequencerPatternRandomizeSession& session
) {
    switch (session.focusedField) {
        case SequencerPatternRandomizeField::PROPERTY:
            return static_cast<int32_t>(session.draft.property);
        case SequencerPatternRandomizeField::AMOUNT:
            return session.draft.amount;
        case SequencerPatternRandomizeField::RANGE:
            return session.draft.range;
        case SequencerPatternRandomizeField::SCOPE:
            return session.draft.activeOnly ? 0 : 1;
        case SequencerPatternRandomizeField::COUNT:
        default:
            return 0;
    }
}

}  // namespace core::state::sequencer
