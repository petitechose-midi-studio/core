#include "persistence/MacroTrackBankPersistenceCodec.hpp"

#include <cmath>

#include <config/PlatformCompat.hpp>

#include "app/ExtmemAllocator.hpp"
#include "persistence/PersistenceBinaryCodec.hpp"

namespace core::persistence::macro_track_codec {

namespace {

namespace macro = core::state::macro;
namespace binary = core::persistence::binary_codec;
using TrackBank = std::array<macro::MacroTrackData, macro::TRACK_COUNT>;

constexpr uint16_t kAllTracksMask =
    static_cast<uint16_t>((1UL << macro::TRACK_COUNT) - 1UL);
constexpr uint16_t kAllPagesMask =
    static_cast<uint16_t>((1UL << macro::PAGE_COUNT) - 1UL);
constexpr uint8_t kAllMacrosMask =
    static_cast<uint8_t>((1U << macro::MACRO_COUNT) - 1U);

FLASHMEM bool fixedTextCanonical(
    const char* text,
    uint8_t capacity
) {
    if (text == nullptr || capacity == 0U) return false;

    bool terminated = false;
    for (uint8_t index = 0U; index < capacity; ++index) {
        if (!terminated) {
            terminated = text[index] == '\0';
        } else if (text[index] != '\0') {
            return false;
        }
    }
    return terminated;
}

FLASHMEM bool maskCanonical(
    uint16_t mask,
    uint16_t availableMask
) {
    return mask != 0U &&
           (mask & static_cast<uint16_t>(~availableMask)) == 0U;
}

FLASHMEM bool activeIndexCanonical(
    uint8_t active,
    uint8_t count,
    uint16_t enabledMask
) {
    return active < count &&
           (enabledMask & static_cast<uint16_t>(1U << active)) != 0U;
}

FLASHMEM bool pageCanonical(const macro::MacroPageData& page) {
    if (!fixedTextCanonical(page.name, macro::PAGE_NAME_SIZE) ||
        (page.activeMacroMask & static_cast<uint8_t>(~kAllMacrosMask)) != 0U) {
        return false;
    }
    for (uint8_t index = 0U; index < macro::MACRO_COUNT; ++index) {
        if (page.cc[index] > 127U ||
            !std::isfinite(page.values[index]) ||
            page.values[index] < 0.0F ||
            page.values[index] > 1.0F) {
            return false;
        }
    }
    return true;
}

FLASHMEM bool trackCanonical(const macro::MacroTrackData& track) {
    if (!maskCanonical(track.enabledPageMask, kAllPagesMask) ||
        !activeIndexCanonical(
            track.activePage,
            macro::PAGE_COUNT,
            track.enabledPageMask
        )) {
        return false;
    }
    for (const auto& page : track.pages) {
        if (!pageCanonical(page)) return false;
    }
    return true;
}

FLASHMEM bool reservedZero(binary::Reader& reader, uint16_t size) {
    for (uint16_t index = 0U; index < size; ++index) {
        uint8_t value = 0U;
        if (!reader.readU8(value) || value != 0U) return false;
    }
    return true;
}

FLASHMEM bool writePage(binary::Writer& writer, const macro::MacroPageData& page) {
    if (!pageCanonical(page)) return false;
    if (!writer.writeBytes(page.name, macro::PAGE_NAME_SIZE)) return false;
    for (uint8_t i = 0; i < macro::MACRO_COUNT; ++i) {
        if (!writer.writeU8(page.cc[i])) return false;
    }
    for (uint8_t i = 0; i < macro::MACRO_COUNT; ++i) {
        if (!writer.writeFloat32(page.values[i])) return false;
    }
    return writer.writeU8(page.activeMacroMask) &&
           writer.writeZeroes(MACRO_PAGE_RESERVED_SIZE);
}

FLASHMEM bool readPage(binary::Reader& reader, macro::MacroPageData& page) {
    if (!reader.readBytes(page.name, macro::PAGE_NAME_SIZE)) return false;
    for (uint8_t i = 0; i < macro::MACRO_COUNT; ++i) {
        if (!reader.readU8(page.cc[i])) return false;
    }
    for (uint8_t i = 0; i < macro::MACRO_COUNT; ++i) {
        if (!reader.readFloat32(page.values[i])) return false;
    }
    if (!reader.readU8(page.activeMacroMask) ||
        !reservedZero(reader, MACRO_PAGE_RESERVED_SIZE)) {
        return false;
    }
    return pageCanonical(page);
}

FLASHMEM bool writeTrack(binary::Writer& writer, const macro::MacroTrackData& track) {
    if (!trackCanonical(track) ||
        !writer.writeU8(track.activePage) ||
        !writer.writeU16(track.enabledPageMask)) {
        return false;
    }
    for (uint8_t i = 0; i < macro::PAGE_COUNT; ++i) {
        if (!writePage(writer, track.pages[i])) return false;
    }
    return true;
}

FLASHMEM bool readTrack(binary::Reader& reader, macro::MacroTrackData& track) {
    uint8_t activePage = 0;
    uint16_t enabledPageMask = 0;
    if (!reader.readU8(activePage) ||
        !reader.readU16(enabledPageMask)) {
        return false;
    }

    track.activePage = activePage;
    track.enabledPageMask = enabledPageMask;
    for (uint8_t i = 0; i < macro::PAGE_COUNT; ++i) {
        if (!readPage(reader, track.pages[i])) return false;
    }
    return trackCanonical(track);
}

FLASHMEM bool decodeTrackBankPayloadToScratch(
    const uint8_t* data,
    uint32_t size,
    TrackBank& tracks,
    uint16_t& enabledTrackMask,
    uint8_t& activeTrack
) {
    if (size != MACRO_TRACK_BANK_PAYLOAD_SIZE) return false;

    binary::Reader reader(data, size);
    uint8_t active = 0;
    uint8_t reserved = 0;
    uint16_t enabled = 0;
    if (!reader.readU8(active) ||
        !reader.readU8(reserved) ||
        !reader.readU16(enabled) ||
        reserved != 0U ||
        !maskCanonical(enabled, kAllTracksMask) ||
        !activeIndexCanonical(active, macro::TRACK_COUNT, enabled)) {
        return false;
    }

    for (uint8_t i = 0; i < macro::TRACK_COUNT; ++i) {
        if (!readTrack(reader, tracks[i])) return false;
    }
    if (!reader.ok() || reader.offset() != MACRO_TRACK_BANK_PAYLOAD_SIZE) {
        return false;
    }

    enabledTrackMask = enabled;
    activeTrack = active;
    return true;
}

}  // namespace

FLASHMEM bool encodeTrackBankPayload(
    const std::array<macro::MacroTrackData, macro::TRACK_COUNT>& tracks,
    uint16_t enabledTrackMask,
    uint8_t activeTrack,
    uint8_t* out,
    uint32_t capacity
) {
    if (capacity != MACRO_TRACK_BANK_PAYLOAD_SIZE ||
        !maskCanonical(enabledTrackMask, kAllTracksMask) ||
        !activeIndexCanonical(activeTrack, macro::TRACK_COUNT, enabledTrackMask)) {
        return false;
    }
    for (const auto& track : tracks) {
        if (!trackCanonical(track)) return false;
    }

    binary::Writer writer(out, capacity);
    if (!writer.writeU8(activeTrack) ||
        !writer.writeU8(0) ||
        !writer.writeU16(enabledTrackMask)) {
        return false;
    }
    for (uint8_t i = 0; i < macro::TRACK_COUNT; ++i) {
        if (!writeTrack(writer, tracks[i])) return false;
    }
    return writer.ok() && writer.offset() == MACRO_TRACK_BANK_PAYLOAD_SIZE;
}

FLASHMEM bool decodeTrackBankPayloadInto(
    const uint8_t* data,
    uint32_t size,
    std::array<macro::MacroTrackData, macro::TRACK_COUNT>& tracks,
    uint16_t& enabledTrackMask,
    uint8_t& activeTrack
) {
    auto pending = core::app::makeExtmemUnique<TrackBank>();
    if (!pending) return false;

    uint16_t pendingEnabledTrackMask = 0U;
    uint8_t pendingActiveTrack = 0U;
    if (!decodeTrackBankPayloadToScratch(
            data,
            size,
            *pending,
            pendingEnabledTrackMask,
            pendingActiveTrack
        )) {
        return false;
    }

    tracks = *pending;
    enabledTrackMask = pendingEnabledTrackMask;
    activeTrack = pendingActiveTrack;
    return true;
}

FLASHMEM bool encodePagesPayload(const macro::MacroPagesState& pages,
                                 uint8_t* out,
                                 uint32_t capacity) {
    uint16_t enabledTrackMask = macro::MacroPagesState::DEFAULT_TRACK_ENABLED_MASK;
    uint8_t activeTrack = 0;
    pages.captureSharedTrackState(enabledTrackMask, activeTrack);
    return encodeTrackBankPayload(pages.tracks, enabledTrackMask, activeTrack, out, capacity);
}

FLASHMEM bool applyPagesPayload(const uint8_t* data,
                                uint32_t size,
                                macro::MacroPagesState& pages) {
    auto tracks = core::app::makeExtmemUnique<TrackBank>();
    if (!tracks) return false;

    uint16_t enabledTrackMask = 0U;
    uint8_t activeTrack = 0U;
    if (!decodeTrackBankPayloadToScratch(
            data,
            size,
            *tracks,
            enabledTrackMask,
            activeTrack
        )) {
        return false;
    }
    pages.restoreTracksWithSharedState(*tracks, enabledTrackMask, activeTrack);
    return true;
}

}  // namespace core::persistence::macro_track_codec
