#include <cstddef>
#include <cstdint>

#include "persistence/SequencerPersistenceEnvelope.hpp"
#include "state/sequencer/SequencerPatternState.hpp"

namespace {

using namespace core::persistence::sequencer_codec;

struct CanonicalPatternEnvelope {
    PatternEnvelopeBuffer bytes{};
    uint32_t size = 0U;

    CanonicalPatternEnvelope() {
        core::state::sequencer::SequencerPatternState pattern{};
        pattern.reset();
        const auto encoded = fillPatternEnvelope(
            pattern, bytes.bytes.data(), bytes.bytes.size());
        if (encoded.ok) size = encoded.size;
    }
};

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size > MAX_PATTERN_ENVELOPE_PAYLOAD_SIZE) return 0;

    core::state::sequencer::SequencerPatternState pattern{};
    pattern.reset();
    (void)applyPatternEnvelope(data, static_cast<uint32_t>(size), pattern);

    static const CanonicalPatternEnvelope seed{};
    if (seed.size != 0U) {
        PatternEnvelopeBuffer candidate = seed.bytes;
        for (size_t i = 0U; i < size; ++i) {
            candidate.bytes[i % seed.size] ^= data[i];
        }
        pattern.reset();
        (void)applyPatternEnvelope(candidate.bytes.data(), seed.size, pattern);
    }
    return 0;
}
