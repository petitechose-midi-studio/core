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
using SetBytes = std::array<uint8_t, codec::SET_PAYLOAD_SIZE>;

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
    source.stepsPerBeat.set(6U);
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
    assert(decoded.length.get() == 16U);
    assert(decoded.stepsPerBeat.get() == 6U);
    assert(decoded.enabledMask.get().test(9U));
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

    source.length.set(0U);
    assert(!codec::fillPatternPayload(
        source,
        bytes->data(),
        static_cast<uint16_t>(bytes->size())
    ));
    source.length.set(sequencer::SequencerPatternState::DEFAULT_LENGTH);

    source.stepsPerBeat.set(5U);
    assert(!codec::fillPatternPayload(
        source,
        bytes->data(),
        static_cast<uint16_t>(bytes->size())
    ));
    source.stepsPerBeat.set(
        sequencer::SequencerPatternState::DEFAULT_STEPS_PER_BEAT
    );

    source.enabledMask.set(
        oc::note::sequencer::StepBitMask128::fromLower64(1ULL << 9U)
    );
    assert(!codec::fillPatternPayload(
        source,
        bytes->data(),
        static_cast<uint16_t>(bytes->size())
    ));
    source.enabledMask.set({});

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
    assert(target.length.get() == 16U);
    assert(target.note[0] == 31U);

    *malformed = *canonical;
    (*malformed)[0] = 0U;
    assert(!codec::applyPatternPayload(
        malformed->data(),
        static_cast<uint16_t>(malformed->size()),
        target
    ));
    assert(target.length.get() == 16U);
    assert(target.note[0] == 31U);

    std::cout << "[PASS] malformed Pattern payload is rejected atomically\n";
}

void testProjectAndSetHeadersAreStrict() {
    sequencer::SequencerTrackBankSnapshot snapshot{};
    auto projectBytes = std::make_unique<ProjectBytes>();
    assert(projectBytes);
    assert(codec::fillProjectSequencerPayload(
        snapshot,
        0U,
        sequencer::StepProperty::NOTE,
        projectBytes->data(),
        static_cast<uint16_t>(projectBytes->size())
    ));

    auto malformedProject = std::make_unique<ProjectBytes>(*projectBytes);
    assert(malformedProject);
    (*malformedProject)[3] = 1U;
    sequencer::SequencerTrackBankState projectBank{};
    projectBank.syncSharedTrackState(0x0004U, 2U);
    sequencer::SequencerState projectActive{};
    assert(!codec::applyProjectSequencerPayload(
        malformedProject->data(),
        static_cast<uint16_t>(malformedProject->size()),
        projectBank,
        projectActive
    ));
    assert(projectBank.currentEnabledMask() == 0x0004U);
    assert(projectBank.activeTrackIndex() == 2U);

    sequencer::SequencerTrackBankState setBank{};
    sequencer::SequencerState setActive{};
    auto setBytes = std::make_unique<SetBytes>();
    assert(setBytes);
    assert(codec::fillSetPayload(
        setBank,
        setActive,
        setBytes->data(),
        static_cast<uint16_t>(setBytes->size())
    ));
    (*setBytes)[0] =
        static_cast<uint8_t>(sequencer::SequencerTrackBankState::TRACK_COUNT - 1U);
    setBank.syncSharedTrackState(0x0008U, 3U);
    assert(!codec::applySetPayload(
        setBytes->data(),
        static_cast<uint16_t>(setBytes->size()),
        setBank,
        setActive
    ));
    assert(setBank.currentEnabledMask() == 0x0008U);
    assert(setBank.activeTrackIndex() == 3U);

    std::cout << "[PASS] Project/Set headers accept only the current shape\n";
}

}  // namespace

int main() {
    testPatternRoundTripIsExact();
    testPatternEncoderRejectsInsteadOfRepairing();
    testPatternDecoderRejectsAtomically();
    testProjectAndSetHeadersAreStrict();
    std::cout << "All SequencerPersistenceCodec tests passed\n";
    return 0;
}
