#ifdef NDEBUG
#undef NDEBUG
#endif

#include <array>
#include <cassert>
#include <cstring>
#include <iostream>

#include "persistence/PatternPresetFactoryLibrary.hpp"

namespace {

namespace factory = core::persistence;
namespace codec = core::persistence::sequencer_pattern_preset_codec;
namespace seq = core::state::sequencer;

void testCatalogIsSmallSortedAndTyped() {
    for (const auto kind : {
             seq::SequencerTrackKind::INSTRUMENT,
             seq::SequencerTrackKind::DRUM,
         }) {
        assert(factory::PatternPresetFactoryLibrary::count(kind) == 4U);
        const char* previous = "";
        for (uint8_t index = 0U; index < 4U; ++index) {
            factory::PatternPresetFactoryDescriptor descriptor{};
            assert(factory::PatternPresetFactoryLibrary::descriptorAt(
                kind,
                index,
                descriptor
            ));
            assert(descriptor.trackKind == kind);
            assert(descriptor.id != nullptr);
            assert(descriptor.semanticName != nullptr);
            assert(std::strcmp(previous, descriptor.id) < 0);
            assert(factory::PatternPresetFactoryLibrary::contains(
                descriptor.id
            ));
            previous = descriptor.id;
        }
    }
    std::cout << "[PASS] Factory catalog is bounded, sorted, and typed\n";
}

void testInstrumentAndDrumEncodeThroughCanonicalCodec() {
    std::array<uint8_t, codec::MAX_ENCODED_SIZE> bytes{};
    seq::SequencerState scratch{};
    seq::SequencerPatternPresetMetadata metadata{};
    auto encoded = factory::PatternPresetFactoryLibrary::encode(
        "factory-instrument-rising",
        scratch.pattern,
        nullptr,
        metadata,
        bytes.data(),
        static_cast<uint16_t>(bytes.size())
    );
    assert(encoded.ok());
    assert(metadata.trackKind == seq::SequencerTrackKind::INSTRUMENT);

    seq::SequencerState decoded{};
    seq::SequencerPatternPresetMetadata decodedMetadata{};
    assert(codec::decode(
        bytes.data(),
        encoded.bytesWritten,
        decodedMetadata,
        decoded.pattern,
        nullptr
    ));
    assert(decoded.pattern.length.get() == 16U);
    assert(decoded.pattern.enabledMask.get().test(0U));
    assert(decoded.pattern.note[0U] == 60U);
    assert(decoded.pattern.note[7U] == 72U);

    seq::DrumTrackState drumScratch{};
    encoded = factory::PatternPresetFactoryLibrary::encode(
        "factory-drum-polymeter",
        scratch.pattern,
        &drumScratch,
        metadata,
        bytes.data(),
        static_cast<uint16_t>(bytes.size())
    );
    assert(encoded.ok());
    assert(metadata.trackKind == seq::SequencerTrackKind::DRUM);

    seq::DrumTrackState decodedDrum{};
    assert(codec::decode(
        bytes.data(),
        encoded.bytesWritten,
        decodedMetadata,
        decoded.pattern,
        &decodedDrum
    ));
    assert(decodedDrum.pattern.effectiveLength(0U) == 7U);
    assert(decodedDrum.pattern.effectiveLength(1U) == 11U);
    assert(decodedDrum.pattern.effectiveLength(2U) == 5U);
    assert(decodedDrum.pattern.stepEnabled(0U, 0U));
    assert(decodedDrum.pattern.stepEnabled(1U, 3U));

    std::cout << "[PASS] Factory assets use the canonical Pattern codec\n";
}

}  // namespace

int main() {
    testCatalogIsSmallSortedAndTyped();
    testInstrumentAndDrumEncodeThroughCanonicalCodec();
    std::cout << "[PASS] PatternPresetFactoryLibrary tests\n";
    return 0;
}
