#ifdef NDEBUG
#undef NDEBUG
#endif

#include <array>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <memory>

#include "persistence/SequencerPersistenceCodec.hpp"

namespace {

namespace codec = core::persistence::sequencer_codec;
namespace sequencer = core::state::sequencer;

using PatternBytes = std::array<uint8_t, codec::PATTERN_PAYLOAD_SIZE>;
using ProjectBytes =
    std::array<uint8_t, codec::PROJECT_SEQUENCER_PAYLOAD_SIZE>;

std::unique_ptr<PatternBytes> encodePattern(
    const sequencer::SequencerPatternState& source
) {
    auto bytes = std::make_unique<PatternBytes>();
    assert(bytes);
    assert(codec::fillPatternPayload(
        source,
        bytes->data(),
        static_cast<uint16_t>(bytes->size())
    ));
    return bytes;
}

void testPatternRoundTripIsExact() {
    sequencer::SequencerPatternState source{};
    assert(source.setContentLength(16U));
    source.setStepsPerBeat(6U);
    source.setPitchEditMode(sequencer::SequencerPitchEditMode::CHROMATIC);
    source.setPatternVariationRanges({
        .pitchSemitones = 12U,
        .velocity = 20U,
        .gatePercent = 30U,
        .nudge = 4U,
    });
    source.setPatternSwingOffsetPercent(-25);
    source.setPatternNudgePercent(17);
    source.setPatternScalePolicy(
        sequencer::SequencerPatternScalePolicy::OVERRIDE
    );
    source.setPatternScaleOverride({
        .root = 5U,
        .type = oc::note::sequencer::StepSequencerScaleType::HarmonicMinor,
        .mode = oc::note::sequencer::StepSequencerScaleConstraintMode::
            ConstrainNearest,
    });
    source.setEnabled(9U, true);
    source.note[9] = 73U;
    source.velocity[9] = 101U;
    source.gate[9] = 725U;
    source.nudge[9] = -12;
    source.probability[9] = 83U;

    const auto bytes = encodePattern(source);
    sequencer::SequencerPatternState decoded{};
    assert(codec::applyPatternPayload(
        bytes->data(),
        static_cast<uint16_t>(bytes->size()),
        decoded
    ));
    assert(decoded.length == 16U);
    assert(decoded.stepsPerBeat == 6U);
    assert(decoded.enabledMask.test(9U));
    assert(decoded.note[9] == 73U);
    assert(decoded.velocity[9] == 101U);
    assert(decoded.gate[9] == 725U);
    assert(decoded.nudge[9] == -12);
    assert(decoded.probability[9] == 83U);

    std::cout << "[PASS] canonical Pattern payload round-trip\n";
}

void testPatternEncoderRejectsInsteadOfRepairing() {
    sequencer::SequencerPatternState source{};
    auto bytes = std::make_unique<PatternBytes>();
    assert(bytes);

    source.setLength(0U);
    assert(!codec::fillPatternPayload(
        source,
        bytes->data(),
        static_cast<uint16_t>(bytes->size())
    ));
    source.setLength(sequencer::SequencerPatternState::DEFAULT_LENGTH);

    source.setStepsPerBeat(5U);
    assert(!codec::fillPatternPayload(
        source,
        bytes->data(),
        static_cast<uint16_t>(bytes->size())
    ));
    source.setStepsPerBeat(
        sequencer::SequencerPatternState::DEFAULT_STEPS_PER_BEAT
    );

    source.setEnabledMask(
        oc::note::sequencer::StepBitMask128::fromLower64(1ULL << 9U)
    );
    assert(!codec::fillPatternPayload(
        source,
        bytes->data(),
        static_cast<uint16_t>(bytes->size())
    ));
    source.setEnabledMask({});

    source.note[0] = 200U;
    assert(!codec::fillPatternPayload(
        source,
        bytes->data(),
        static_cast<uint16_t>(bytes->size())
    ));
    source.note[0] = 60U;

    source.variationRanges.pitchSemitones = 200U;
    assert(!codec::fillPatternPayload(
        source,
        bytes->data(),
        static_cast<uint16_t>(bytes->size())
    ));

    std::cout << "[PASS] Pattern encoder rejects non-canonical state\n";
}

void testPatternDecoderRejectsAtomically() {
    sequencer::SequencerPatternState source{};
    const auto canonical = encodePattern(source);
    auto malformed = std::make_unique<PatternBytes>(*canonical);
    assert(malformed);

    // Notes begin immediately after the 29-byte Pattern header.
    (*malformed)[codec::PATTERN_HEADER_PAYLOAD_SIZE] = 200U;
    sequencer::SequencerPatternState target{};
    assert(target.setContentLength(16U));
    target.note[0] = 31U;
    assert(!codec::applyPatternPayload(
        malformed->data(),
        static_cast<uint16_t>(malformed->size()),
        target
    ));
    assert(target.length == 16U);
    assert(target.note[0] == 31U);

    *malformed = *canonical;
    (*malformed)[0] = 0U;
    assert(!codec::applyPatternPayload(
        malformed->data(),
        static_cast<uint16_t>(malformed->size()),
        target
    ));
    assert(target.length == 16U);
    assert(target.note[0] == 31U);

    std::cout << "[PASS] malformed Pattern payload is rejected atomically\n";
}

void testProjectHeaderIsStrict() {
    sequencer::SequencerTrackBankSnapshot snapshot{};
    snapshot.activeTrack = 7U;
    snapshot.enabledMask = 0xFFFFU;
    snapshot.projectScaleSettings.root = 9U;
    for (uint8_t track = 0U; track < 16U; ++track) {
        auto& pattern = snapshot.tracks[track];
        pattern.length = 128U;
        pattern.stepsPerBeat = 6U;
        pattern.enabledMask.setBit(127U);
        pattern.note[127U] = static_cast<uint8_t>(70U + track);
        pattern.velocity[127U] = static_cast<uint8_t>(90U + track);
        pattern.gate[127U] = 725U;
        pattern.nudge[127U] = -23;
        pattern.probability[127U] = 83U;
        pattern.variationRanges = {12U, 20U, 30U, 4U};
        pattern.swingOffsetPercent = -25;
        pattern.patternNudgePercent = 17;
        pattern.pitchEditMode = sequencer::SequencerPitchEditMode::CHROMATIC;
        pattern.scalePolicy = sequencer::SequencerPatternScalePolicy::OVERRIDE;
        pattern.scaleOverride.root = static_cast<uint8_t>(track % 12U);
    }
    auto projectBytes = std::make_unique<ProjectBytes>();
    assert(projectBytes);
    assert(codec::fillProjectSequencerPayload(
        snapshot,
        95U,
        sequencer::StepProperty::PROBABILITY,
        projectBytes->data(),
        static_cast<uint16_t>(projectBytes->size())
    ));

    auto malformedProject = std::make_unique<ProjectBytes>(*projectBytes);
    assert(malformedProject);
    (*malformedProject)[3] = 1U;
    sequencer::SequencerTrackBankSnapshot projectBank{};
    projectBank.enabledMask = 0x0004U;
    projectBank.activeTrack = 2U;
    uint8_t focused = 7U;
    auto property = sequencer::StepProperty::GATE;
    assert(!codec::decodeProjectSequencerPayload(
        malformedProject->data(),
        static_cast<uint16_t>(malformedProject->size()),
        projectBank,
        focused,
        property
    ));
    assert(projectBank.enabledMask == 0x0004U);
    assert(projectBank.activeTrack == 2U);
    assert(focused == 7U && property == sequencer::StepProperty::GATE);

    // A late invalid field must not leave earlier Tracks partially decoded.
    *malformedProject = *projectBytes;
    malformedProject->back() = 1U;
    assert(!codec::decodeProjectSequencerPayload(
        malformedProject->data(), malformedProject->size(), projectBank, focused, property));
    assert(projectBank.enabledMask == 0x0004U && projectBank.activeTrack == 2U);
    assert(focused == 7U && property == sequencer::StepProperty::GATE);

    assert(codec::decodeProjectSequencerPayload(
        projectBytes->data(), projectBytes->size(), projectBank, focused, property));
    assert(focused == 95U && property == sequencer::StepProperty::PROBABILITY);
    assert(codec::fillProjectSequencerPayload(
        projectBank, focused, property, malformedProject->data(), malformedProject->size()));
    assert(*malformedProject == *projectBytes);

    std::cout << "[PASS] Project round-trip and strict atomic validation\n";
}

}  // namespace

int main() {
    testPatternRoundTripIsExact();
    testPatternEncoderRejectsInsteadOfRepairing();
    testPatternDecoderRejectsAtomically();
    testProjectHeaderIsStrict();
    std::cout << "All SequencerPersistenceCodec tests passed\n";
    return 0;
}
