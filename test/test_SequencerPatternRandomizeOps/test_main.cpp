#ifdef NDEBUG
#undef NDEBUG
#endif

#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>

#include <oc/note/sequencer/StepSequencerScale.hpp>

#include "state/sequencer/SequencerPatternRandomizeOps.hpp"
#include "state/sequencer/SequencerPatternRandomizeSession.hpp"

namespace seq = core::state::sequencer;

namespace {

seq::SequencerPatternSnapshot populatedSnapshot(uint8_t length = 32) {
    seq::SequencerPatternSnapshot snapshot{};
    snapshot.length = length;
    snapshot.playStart = 2;
    snapshot.loopStart = 5;
    snapshot.loopEnd = length;
    snapshot.stepsPerBeat = 6;
    snapshot.stepDataRevision = 17;
    snapshot.patternVariationRevision = 18;
    snapshot.patternScaleRevision = 19;
    snapshot.patternTimingRevision = 20;
    snapshot.graphRevision = 21;
    snapshot.swingOffsetPercent = -11;
    snapshot.patternNudgePercent = 7;
    snapshot.effectiveSwingPercent = 23;
    snapshot.variationRanges = {
        .pitchSemitones = 4,
        .velocity = 5,
        .gatePercent = 6,
        .nudge = 7,
    };
    snapshot.scalePolicy = seq::SequencerPatternScalePolicy::OVERRIDE;
    snapshot.scaleOverride = {
        .root = 3,
        .type = oc::note::sequencer::StepSequencerScaleType::Dorian,
        .mode = oc::note::sequencer::StepSequencerScaleConstraintMode::ConstrainUp,
    };
    snapshot.pitchEditMode = seq::SequencerPitchEditMode::CHROMATIC;
    snapshot.effectiveScaleSettings = snapshot.scaleOverride;

    for (uint16_t step = 0; step < seq::SequencerPatternState::MAX_STEPS; ++step) {
        const auto stepIndex = static_cast<uint8_t>(step);
        snapshot.enabledMask.setBit(stepIndex, (step % 3U) != 1U);
        snapshot.note[step] = static_cast<uint8_t>(24U + (step % 80U));
        snapshot.velocity[step] = static_cast<uint8_t>(20U + (step % 100U));
        snapshot.gate[step] = static_cast<uint16_t>(50U + step * 3U);
        snapshot.nudge[step] = static_cast<int8_t>(-50 + (step % 101U));
        snapshot.probability[step] = static_cast<uint8_t>(step % 101U);
    }
    return snapshot;
}

bool sameScale(
    const oc::note::sequencer::StepSequencerScaleSettings& lhs,
    const oc::note::sequencer::StepSequencerScaleSettings& rhs
) {
    return lhs.root == rhs.root && lhs.type == rhs.type && lhs.mode == rhs.mode;
}

bool sameSnapshot(
    const seq::SequencerPatternSnapshot& lhs,
    const seq::SequencerPatternSnapshot& rhs
) {
    return lhs.length == rhs.length &&
           lhs.playStart == rhs.playStart &&
           lhs.loopStart == rhs.loopStart &&
           lhs.loopEnd == rhs.loopEnd &&
           lhs.stepsPerBeat == rhs.stepsPerBeat &&
           lhs.enabledMask == rhs.enabledMask &&
           lhs.stepDataRevision == rhs.stepDataRevision &&
           lhs.patternVariationRevision == rhs.patternVariationRevision &&
           lhs.patternScaleRevision == rhs.patternScaleRevision &&
           lhs.patternTimingRevision == rhs.patternTimingRevision &&
           lhs.graphRevision == rhs.graphRevision &&
           lhs.swingOffsetPercent == rhs.swingOffsetPercent &&
           lhs.patternNudgePercent == rhs.patternNudgePercent &&
           lhs.effectiveSwingPercent == rhs.effectiveSwingPercent &&
           std::memcmp(&lhs.variationRanges, &rhs.variationRanges, sizeof(lhs.variationRanges)) == 0 &&
           lhs.scalePolicy == rhs.scalePolicy &&
           sameScale(lhs.scaleOverride, rhs.scaleOverride) &&
           lhs.pitchEditMode == rhs.pitchEditMode &&
           sameScale(lhs.effectiveScaleSettings, rhs.effectiveScaleSettings) &&
           lhs.note == rhs.note &&
           lhs.velocity == rhs.velocity &&
           lhs.gate == rhs.gate &&
           lhs.nudge == rhs.nudge &&
           lhs.probability == rhs.probability;
}

bool sameSummary(
    const seq::SequencerPatternRandomizeSummary& lhs,
    const seq::SequencerPatternRandomizeSummary& rhs
) {
    return lhs.contentLength == rhs.contentLength &&
           lhs.eligibleCount == rhs.eligibleCount &&
           lhs.selectedCount == rhs.selectedCount &&
           lhs.changedCount == rhs.changedCount &&
           lhs.clampedCount == rhs.clampedCount;
}

seq::SequencerPatternRandomizeDraft draftFor(
    seq::SequencerPatternRandomizeProperty property,
    uint16_t range,
    uint32_t seed = 1
) {
    return {
        .property = property,
        .amount = 100,
        .range = range,
        .activeOnly = false,
        .seed = seed,
    };
}

uint32_t seedForDelta(
    const seq::SequencerPatternSnapshot& base,
    seq::SequencerPatternRandomizeProperty property,
    uint16_t range,
    bool positive
) {
    for (uint32_t seed = 0; seed < 100000U; ++seed) {
        const auto projection = seq::projectPatternRandomizeStep(
            base,
            draftFor(property, range, seed),
            0
        );
        if ((positive && projection.delta > 0) ||
            (!positive && projection.delta < 0)) {
            return seed;
        }
    }
    assert(false && "deterministic stream must expose both delta signs");
    return 0;
}

void test_draft_validation_defaults_and_reroll() {
    using Property = seq::SequencerPatternRandomizeProperty;
    assert(seq::isPatternRandomizeProperty(0));
    assert(seq::isPatternRandomizeProperty(4));
    assert(!seq::isPatternRandomizeProperty(5));
    assert(!seq::isPatternRandomizeProperty(255));

    seq::SequencerPatternRandomizeDraft invalid{
        .property = static_cast<Property>(255),
        .amount = 255,
        .range = 65535,
        .activeOnly = false,
        .seed = 0,
    };
    const auto sanitized = seq::sanitizePatternRandomizeDraft(invalid);
    assert(sanitized.property == Property::NOTE);
    assert(sanitized.amount == 100);
    assert(sanitized.range == 127);
    assert(!sanitized.activeOnly);
    assert(sanitized.seed == 0);

    assert(seq::defaultPatternRandomizeRange(Property::NOTE) == 2);
    assert(seq::defaultPatternRandomizeRange(Property::VELOCITY) == 16);
    assert(seq::defaultPatternRandomizeRange(Property::GATE) == 25);
    assert(seq::defaultPatternRandomizeRange(Property::NUDGE) == 6);
    assert(seq::defaultPatternRandomizeRange(Property::PROBABILITY) == 20);
    assert(seq::maxPatternRandomizeRange(Property::GATE) == 1600);

    const uint32_t first = seq::rerollPatternRandomizeSeed(42);
    assert(first == seq::rerollPatternRandomizeSeed(42));
    assert(first != 42);
    assert(seq::rerollPatternRandomizeSeed(first) != first);
}

void test_determinism_preview_equals_materialized_apply() {
    const auto base = populatedSnapshot();
    auto draft = draftFor(
        seq::SequencerPatternRandomizeProperty::VELOCITY,
        24,
        0x12345678U
    );
    draft.amount = 63;

    seq::SequencerPatternSnapshot first{};
    seq::SequencerPatternSnapshot second{};
    const auto materializedSummary =
        seq::materializePatternRandomizeSnapshot(base, draft, first);
    const auto repeatedSummary =
        seq::materializePatternRandomizeSnapshot(base, draft, second);
    const auto previewSummary = seq::summarizePatternRandomize(base, draft);

    assert(sameSnapshot(first, second));
    assert(sameSummary(materializedSummary, repeatedSummary));
    assert(sameSummary(materializedSummary, previewSummary));

    for (uint8_t step = 0; step < base.length; ++step) {
        const auto projection = seq::projectPatternRandomizeStep(base, draft, step);
        assert(first.velocity[step] == static_cast<uint8_t>(projection.targetValue));
    }

    draft.seed = seq::rerollPatternRandomizeSeed(draft.seed);
    seq::SequencerPatternSnapshot rerolled{};
    (void)seq::materializePatternRandomizeSnapshot(base, draft, rerolled);
    assert(rerolled.velocity != first.velocity);
}

void test_amount_selection_is_monotone_and_endpoints_are_exact() {
    auto base = populatedSnapshot(128);
    auto draft = draftFor(
        seq::SequencerPatternRandomizeProperty::PROBABILITY,
        100,
        0xCAFEBABEU
    );

    std::array<bool, seq::SequencerPatternState::MAX_STEPS> previous{};
    for (uint8_t amount = 0; amount <= 100; ++amount) {
        draft.amount = amount;
        uint8_t selected = 0;
        for (uint8_t step = 0; step < base.length; ++step) {
            const auto projection = seq::projectPatternRandomizeStep(base, draft, step);
            if (previous[step]) assert(projection.selected);
            previous[step] = projection.selected;
            if (projection.selected) ++selected;
        }
        if (amount == 0) assert(selected == 0);
        if (amount == 100) assert(selected == 128);
    }

    seq::SequencerPatternSnapshot untouched{};
    draft.amount = 0;
    auto summary = seq::materializePatternRandomizeSnapshot(base, draft, untouched);
    assert(summary.selectedCount == 0);
    assert(summary.changedCount == 0);
    assert(sameSnapshot(base, untouched));

    draft.amount = 100;
    draft.range = 0;
    summary = seq::materializePatternRandomizeSnapshot(base, draft, untouched);
    assert(summary.selectedCount == 128);
    assert(summary.changedCount == 0);
    assert(sameSnapshot(base, untouched));
}

void test_active_only_and_content_length_bound_scope() {
    auto base = populatedSnapshot(10);
    base.enabledMask = {};
    base.enabledMask.setBit(1);
    base.enabledMask.setBit(3);
    base.enabledMask.setBit(9);
    base.enabledMask.setBit(10);
    base.enabledMask.setBit(127);

    auto draft = draftFor(
        seq::SequencerPatternRandomizeProperty::GATE,
        100,
        99
    );
    draft.activeOnly = true;
    auto summary = seq::summarizePatternRandomize(base, draft);
    assert(summary.contentLength == 10);
    assert(summary.eligibleCount == 3);
    assert(summary.selectedCount == 3);
    assert(!seq::projectPatternRandomizeStep(base, draft, 0).eligible);
    assert(seq::projectPatternRandomizeStep(base, draft, 9).eligible);
    assert(!seq::projectPatternRandomizeStep(base, draft, 10).eligible);

    draft.activeOnly = false;
    summary = seq::summarizePatternRandomize(base, draft);
    assert(summary.eligibleCount == 10);
    assert(summary.selectedCount == 10);

    base.length = 0;
    summary = seq::summarizePatternRandomize(base, draft);
    assert(summary.contentLength == seq::SequencerPatternState::DEFAULT_LENGTH);
    assert(summary.eligibleCount == seq::SequencerPatternState::DEFAULT_LENGTH);
}

void test_native_ranges_clamp_all_five_properties() {
    using Property = seq::SequencerPatternRandomizeProperty;
    struct BoundaryCase {
        Property property;
        int32_t minimum;
        int32_t maximum;
        uint16_t range;
    };
    constexpr std::array cases{
        BoundaryCase{Property::NOTE, 0, 127, 127},
        BoundaryCase{Property::VELOCITY, 0, 127, 127},
        BoundaryCase{Property::GATE, 0, 1600, 1600},
        BoundaryCase{Property::NUDGE, -50, 50, 100},
        BoundaryCase{Property::PROBABILITY, 0, 100, 100},
    };

    for (const auto& test : cases) {
        auto base = populatedSnapshot(1);
        base.effectiveScaleSettings = {};
        base.pitchEditMode = seq::SequencerPitchEditMode::CHROMATIC;

        switch (test.property) {
            case Property::NOTE:
                base.note[0] = static_cast<uint8_t>(test.maximum);
                break;
            case Property::VELOCITY:
                base.velocity[0] = static_cast<uint8_t>(test.maximum);
                break;
            case Property::GATE:
                base.gate[0] = static_cast<uint16_t>(test.maximum);
                break;
            case Property::NUDGE:
                base.nudge[0] = static_cast<int8_t>(test.maximum);
                break;
            case Property::PROBABILITY:
                base.probability[0] = static_cast<uint8_t>(test.maximum);
                break;
        }
        auto projection = seq::projectPatternRandomizeStep(
            base,
            draftFor(
                test.property,
                test.range,
                seedForDelta(base, test.property, test.range, true)
            ),
            0
        );
        assert(projection.targetValue == test.maximum);
        assert(projection.clamped);

        switch (test.property) {
            case Property::NOTE:
                base.note[0] = static_cast<uint8_t>(test.minimum);
                break;
            case Property::VELOCITY:
                base.velocity[0] = static_cast<uint8_t>(test.minimum);
                break;
            case Property::GATE:
                base.gate[0] = static_cast<uint16_t>(test.minimum);
                break;
            case Property::NUDGE:
                base.nudge[0] = static_cast<int8_t>(test.minimum);
                break;
            case Property::PROBABILITY:
                base.probability[0] = static_cast<uint8_t>(test.minimum);
                break;
        }
        projection = seq::projectPatternRandomizeStep(
            base,
            draftFor(
                test.property,
                test.range,
                seedForDelta(base, test.property, test.range, false)
            ),
            0
        );
        assert(projection.targetValue == test.minimum);
        assert(projection.clamped);
    }
}

void test_note_range_uses_semitones_or_effective_scale_degrees() {
    using Property = seq::SequencerPatternRandomizeProperty;
    auto chromatic = populatedSnapshot(1);
    chromatic.note[0] = 60;
    chromatic.pitchEditMode = seq::SequencerPitchEditMode::CHROMATIC;
    chromatic.effectiveScaleSettings = {};
    const uint32_t positiveSeed = seedForDelta(chromatic, Property::NOTE, 1, true);
    auto projection = seq::projectPatternRandomizeStep(
        chromatic,
        draftFor(Property::NOTE, 1, positiveSeed),
        0
    );
    assert(projection.delta == 1);
    assert(projection.targetValue == 61);

    auto scaleDegrees = chromatic;
    scaleDegrees.pitchEditMode = seq::SequencerPitchEditMode::SCALE_DEGREES;
    scaleDegrees.effectiveScaleSettings = {
        .root = 0,
        .type = oc::note::sequencer::StepSequencerScaleType::Major,
        .mode = oc::note::sequencer::StepSequencerScaleConstraintMode::Free,
    };
    projection = seq::projectPatternRandomizeStep(
        scaleDegrees,
        draftFor(Property::NOTE, 1, positiveSeed),
        0
    );
    assert(projection.delta == 1);
    assert(projection.targetValue == 62);

    auto constrained = chromatic;
    constrained.effectiveScaleSettings = scaleDegrees.effectiveScaleSettings;
    constrained.effectiveScaleSettings.mode =
        oc::note::sequencer::StepSequencerScaleConstraintMode::ConstrainNearest;
    projection = seq::projectPatternRandomizeStep(
        constrained,
        draftFor(Property::NOTE, 1, positiveSeed),
        0
    );
    assert(projection.targetValue == 62);
}

void test_only_target_root_property_changes_semantically() {
    const auto base = populatedSnapshot(8);
    auto draft = draftFor(
        seq::SequencerPatternRandomizeProperty::VELOCITY,
        127,
        0x10203040U
    );
    draft.amount = 100;

    seq::SequencerPatternSnapshot target{};
    const auto summary = seq::materializePatternRandomizeSnapshot(base, draft, target);
    assert(summary.changedCount > 0);

    assert(target.length == base.length);
    assert(target.playStart == base.playStart);
    assert(target.loopStart == base.loopStart);
    assert(target.loopEnd == base.loopEnd);
    assert(target.stepsPerBeat == base.stepsPerBeat);
    assert(target.enabledMask == base.enabledMask);
    assert(target.stepDataRevision == base.stepDataRevision);
    assert(target.patternVariationRevision == base.patternVariationRevision);
    assert(target.patternScaleRevision == base.patternScaleRevision);
    assert(target.patternTimingRevision == base.patternTimingRevision);
    assert(target.graphRevision == base.graphRevision);
    assert(target.swingOffsetPercent == base.swingOffsetPercent);
    assert(target.patternNudgePercent == base.patternNudgePercent);
    assert(target.effectiveSwingPercent == base.effectiveSwingPercent);
    assert(std::memcmp(
        &target.variationRanges,
        &base.variationRanges,
        sizeof(base.variationRanges)
    ) == 0);
    assert(target.scalePolicy == base.scalePolicy);
    assert(sameScale(target.scaleOverride, base.scaleOverride));
    assert(target.pitchEditMode == base.pitchEditMode);
    assert(sameScale(target.effectiveScaleSettings, base.effectiveScaleSettings));
    assert(target.note == base.note);
    assert(target.gate == base.gate);
    assert(target.nudge == base.nudge);
    assert(target.probability == base.probability);
    for (uint16_t step = base.length; step < seq::SequencerPatternState::MAX_STEPS; ++step) {
        assert(target.velocity[step] == base.velocity[step]);
    }
}

void test_cold_session_rebuilds_from_one_base_and_resets_property_range() {
    auto base = populatedSnapshot(16);
    seq::SequencerPatternRandomizeSession session{};
    session.begin(base, 7);
    assert(session.active);
    assert(session.focusedStep == 7);
    assert(session.summary.changedCount > 0U);

    assert(session.setFocusedValue(
        static_cast<int32_t>(seq::SequencerPatternRandomizeProperty::VELOCITY)
    ));
    assert(session.draft.property ==
           seq::SequencerPatternRandomizeProperty::VELOCITY);
    assert(session.draft.range == seq::defaultPatternRandomizeRange(
        seq::SequencerPatternRandomizeProperty::VELOCITY
    ));
    assert(session.moveField(1));
    assert(session.focusedField == seq::SequencerPatternRandomizeField::AMOUNT);
    assert(session.setFocusedValue(0));
    assert(session.summary.changedCount == 0U);
    assert(session.preview.velocity == base.velocity);
    session.cancel();
    assert(!session.active);
}

void test_successive_sessions_evolve_without_losing_determinism() {
    const auto base = populatedSnapshot(16);
    seq::SequencerPatternRandomizeSession session{};

    session.begin(base, 0);
    const uint32_t firstSeed = session.draft.seed;
    const auto firstPreview = session.preview;
    session.cancel();

    session.begin(base, 0);
    const uint32_t secondSeed = session.draft.seed;
    assert(secondSeed == seq::rerollPatternRandomizeSeed(firstSeed));
    assert(secondSeed != firstSeed);
    assert(session.preview.note != firstPreview.note);

    const uint32_t rerolledSeed = seq::rerollPatternRandomizeSeed(secondSeed);
    assert(session.reroll());
    assert(session.draft.seed == rerolledSeed);
    session.cancel();
    session.begin(base, 0);
    assert(session.draft.seed == seq::rerollPatternRandomizeSeed(rerolledSeed));
}

void test_signed_distribution_is_statistically_neutral() {
    auto base = populatedSnapshot(16);
    base.pitchEditMode = seq::SequencerPitchEditMode::CHROMATIC;
    base.note.fill(64);

    constexpr uint16_t range = 12;
    constexpr uint32_t seedCount = 4096;
    int64_t deltaSum = 0;
    uint32_t positiveCount = 0;
    uint32_t negativeCount = 0;
    uint32_t zeroCount = 0;

    for (uint32_t seed = 0; seed < seedCount; ++seed) {
        const auto draft = draftFor(
            seq::SequencerPatternRandomizeProperty::NOTE,
            range,
            seed
        );
        for (uint8_t step = 0; step < base.length; ++step) {
            const int32_t delta =
                seq::projectPatternRandomizeStep(base, draft, step).delta;
            deltaSum += delta;
            positiveCount += delta > 0 ? 1U : 0U;
            negativeCount += delta < 0 ? 1U : 0U;
            zeroCount += delta == 0 ? 1U : 0U;
        }
    }

    const uint32_t signedCount = positiveCount + negativeCount;
    const uint32_t signDifference = positiveCount > negativeCount
        ? positiveCount - negativeCount
        : negativeCount - positiveCount;
    const int64_t sampleCount =
        static_cast<int64_t>(seedCount) * base.length;

    // The generator stays neutral over a large population without forcing
    // artificial positive/negative pairing inside a small Pattern.
    assert(signDifference * 100U < signedCount * 2U);
    assert(deltaSum * 100 < sampleCount * 15);
    assert(deltaSum * 100 > -sampleCount * 15);
    assert(zeroCount > 0U);
}

void test_full_128_step_contract_materializes_and_summarizes() {
    const auto base = populatedSnapshot(seq::SequencerPatternState::MAX_STEPS);
    const auto draft = draftFor(
        seq::SequencerPatternRandomizeProperty::VELOCITY,
        32,
        0x1280ABCDU
    );
    seq::SequencerPatternSnapshot target{};

    // Keep the call outside assert: this is also a termination regression for
    // the exact uint8_t domain boundary (128 steps).
    const auto summary = seq::materializePatternRandomizeSnapshot(
        base, draft, target
    );
    assert(summary.contentLength == seq::SequencerPatternState::MAX_STEPS);
    assert(summary.eligibleCount == seq::SequencerPatternState::MAX_STEPS);
    assert(summary.selectedCount == seq::SequencerPatternState::MAX_STEPS);
    assert(target.length == seq::SequencerPatternState::MAX_STEPS);
}

}  // namespace

int main() {
    test_draft_validation_defaults_and_reroll();
    test_determinism_preview_equals_materialized_apply();
    test_amount_selection_is_monotone_and_endpoints_are_exact();
    test_active_only_and_content_length_bound_scope();
    test_native_ranges_clamp_all_five_properties();
    test_note_range_uses_semitones_or_effective_scale_degrees();
    test_only_target_root_property_changes_semantically();
    test_cold_session_rebuilds_from_one_base_and_resets_property_range();
    test_successive_sessions_evolve_without_losing_determinism();
    test_signed_distribution_is_statistically_neutral();
    test_full_128_step_contract_materializes_and_summarizes();
    std::cout << "[PASS] deterministic Pattern Randomize domain\n";
    return 0;
}
