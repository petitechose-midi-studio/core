#ifdef NDEBUG
#undef NDEBUG
#endif

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>

#include "persistence/MacroTrackBankPersistenceCodec.hpp"

namespace {

namespace codec = core::persistence::macro_track_codec;
namespace macro = core::state::macro;

using TrackBank = std::array<macro::MacroTrackData, macro::TRACK_COUNT>;
using Payload = std::array<uint8_t, codec::MACRO_TRACK_BANK_PAYLOAD_SIZE>;

constexpr uint32_t kBankReservedOffset = 1U;
constexpr uint32_t kFirstTrackOffset = 4U;
constexpr uint32_t kFirstTrackActivePageOffset = kFirstTrackOffset;
constexpr uint32_t kFirstTrackPageMaskOffset = kFirstTrackOffset + 1U;
constexpr uint32_t kFirstPageOffset = kFirstTrackOffset + 3U;
constexpr uint32_t kFirstPageCcOffset =
    kFirstPageOffset + macro::PAGE_NAME_SIZE;
constexpr uint32_t kFirstPageValueOffset =
    kFirstPageCcOffset + macro::MACRO_COUNT;
constexpr uint32_t kFirstPageReservedOffset =
    kFirstPageValueOffset +
    static_cast<uint32_t>(sizeof(float) * macro::MACRO_COUNT) +
    1U;

std::unique_ptr<TrackBank> makeTrackBank() {
    auto tracks = std::make_unique<TrackBank>();
    assert(tracks);
    (*tracks)[3].activePage = 2U;
    (*tracks)[3].enabledPageMask = 0x0005U;
    (*tracks)[3].pages[2].cc[4] = 74U;
    (*tracks)[3].pages[2].values[4] = 0.625F;
    (*tracks)[3].pages[2].activeMacroMask = 0x15U;
    (*tracks)[3].pages[0].activeMacroMask = 0U;
    return tracks;
}

std::unique_ptr<Payload> encodeCanonical(const TrackBank& tracks) {
    auto payload = std::make_unique<Payload>();
    assert(payload);
    assert(codec::encodeTrackBankPayload(
        tracks,
        0x0009U,
        3U,
        payload->data(),
        static_cast<uint32_t>(payload->size())
    ));
    return payload;
}

void testCanonicalRoundTrip() {
    const auto source = makeTrackBank();
    const auto payload = encodeCanonical(*source);
    auto decoded = std::make_unique<TrackBank>();
    assert(decoded);
    uint16_t enabledTrackMask = 0U;
    uint8_t activeTrack = 0U;
    assert(codec::decodeTrackBankPayloadInto(
        payload->data(),
        static_cast<uint32_t>(payload->size()),
        *decoded,
        enabledTrackMask,
        activeTrack
    ));
    assert(enabledTrackMask == 0x0009U);
    assert(activeTrack == 3U);
    assert((*decoded)[3].activePage == 2U);
    assert((*decoded)[3].enabledPageMask == 0x0005U);
    assert((*decoded)[3].pages[2].cc[4] == 74U);
    assert((*decoded)[3].pages[2].values[4] == 0.625F);
    assert((*decoded)[3].pages[2].activeMacroMask == 0x15U);
    assert((*decoded)[3].pages[0].activeMacroMask == 0U);

    std::cout << "[PASS] canonical Macro Track bank round-trip\n";
}

void testEncoderRejectsNonCanonicalState() {
    auto tracks = makeTrackBank();
    auto payload = std::make_unique<Payload>();
    assert(payload);

    assert(!codec::encodeTrackBankPayload(
        *tracks,
        0U,
        0U,
        payload->data(),
        static_cast<uint32_t>(payload->size())
    ));
    assert(!codec::encodeTrackBankPayload(
        *tracks,
        0x0001U,
        3U,
        payload->data(),
        static_cast<uint32_t>(payload->size())
    ));

    (*tracks)[0].activePage = 1U;
    assert(!codec::encodeTrackBankPayload(
        *tracks,
        0x0009U,
        3U,
        payload->data(),
        static_cast<uint32_t>(payload->size())
    ));
    (*tracks)[0].activePage = 0U;

    (*tracks)[0].pages[0].cc[0] = 128U;
    assert(!codec::encodeTrackBankPayload(
        *tracks,
        0x0009U,
        3U,
        payload->data(),
        static_cast<uint32_t>(payload->size())
    ));
    (*tracks)[0].pages[0].cc[0] = 0U;

    (*tracks)[0].pages[0].values[0] = NAN;
    assert(!codec::encodeTrackBankPayload(
        *tracks,
        0x0009U,
        3U,
        payload->data(),
        static_cast<uint32_t>(payload->size())
    ));
    (*tracks)[0].pages[0].values[0] = 0.5F;

    std::fill(
        std::begin((*tracks)[0].pages[0].name),
        std::end((*tracks)[0].pages[0].name),
        'x'
    );
    assert(!codec::encodeTrackBankPayload(
        *tracks,
        0x0009U,
        3U,
        payload->data(),
        static_cast<uint32_t>(payload->size())
    ));

    std::cout << "[PASS] encoder rejects non-canonical Macro state\n";
}

bool decodeRejected(const Payload& payload) {
    auto decoded = std::make_unique<TrackBank>();
    assert(decoded);
    (*decoded)[0].activePage = 7U;
    (*decoded)[0].pages[0].cc[0] = 99U;
    (*decoded)[0].pages[0].values[0] = 0.75F;
    uint16_t enabledTrackMask = 0x1234U;
    uint8_t activeTrack = 9U;
    return !codec::decodeTrackBankPayloadInto(
               payload.data(),
               static_cast<uint32_t>(payload.size()),
               *decoded,
               enabledTrackMask,
               activeTrack
           ) &&
           enabledTrackMask == 0x1234U &&
           activeTrack == 9U &&
           (*decoded)[0].activePage == 7U &&
           (*decoded)[0].pages[0].cc[0] == 99U &&
           (*decoded)[0].pages[0].values[0] == 0.75F;
}

void testDecoderRejectsMalformedPayloads() {
    const auto tracks = makeTrackBank();
    const auto canonical = encodeCanonical(*tracks);
    auto malformed = std::make_unique<Payload>();
    assert(malformed);

    *malformed = *canonical;
    (*malformed)[kBankReservedOffset] = 1U;
    assert(decodeRejected(*malformed));

    *malformed = *canonical;
    (*malformed)[kFirstTrackActivePageOffset] = 1U;
    (*malformed)[kFirstTrackPageMaskOffset] = 1U;
    (*malformed)[kFirstTrackPageMaskOffset + 1U] = 0U;
    assert(decodeRejected(*malformed));

    *malformed = *canonical;
    (*malformed)[kFirstPageCcOffset] = 128U;
    assert(decodeRejected(*malformed));

    *malformed = *canonical;
    std::fill_n(
        malformed->begin() + kFirstPageOffset,
        macro::PAGE_NAME_SIZE,
        static_cast<uint8_t>('x')
    );
    assert(decodeRejected(*malformed));

    *malformed = *canonical;
    const float invalidValue = 1.25F;
    std::memcpy(
        malformed->data() + kFirstPageValueOffset,
        &invalidValue,
        sizeof(invalidValue)
    );
    assert(decodeRejected(*malformed));

    *malformed = *canonical;
    (*malformed)[kFirstPageReservedOffset] = 1U;
    assert(decodeRejected(*malformed));

    std::cout << "[PASS] decoder rejects malformed Macro payloads\n";
}

}  // namespace

int main() {
    testCanonicalRoundTrip();
    testEncoderRejectsNonCanonicalState();
    testDecoderRejectsMalformedPayloads();
    std::cout << "All MacroTrackBankPersistenceCodec tests passed\n";
    return 0;
}
