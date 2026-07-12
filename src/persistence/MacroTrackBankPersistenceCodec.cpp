#include "persistence/MacroTrackBankPersistenceCodec.hpp"

#include <config/PlatformCompat.hpp>

#include "app/ExtmemAllocator.hpp"
#include "persistence/PersistenceBinaryCodec.hpp"

namespace core::persistence::macro_track_codec {

namespace {

namespace macro = core::state::macro;
namespace binary = core::persistence::binary_codec;

FLASHMEM uint8_t sanitizeMidi7(uint8_t value) {
    return (value > 127U) ? 127U : value;
}

FLASHMEM uint8_t sanitizeMidiChannel(uint8_t value) {
    return static_cast<uint8_t>(value % 16U);
}

FLASHMEM uint8_t sanitizeTrack(uint8_t value) {
    return macro::MacroPagesState::clampTrackIndex(value);
}

FLASHMEM uint8_t sanitizePage(uint8_t value) {
    return (value >= macro::PAGE_COUNT) ? 0U : value;
}

FLASHMEM uint16_t sanitizePageMask(uint16_t value) {
    constexpr uint16_t allPages =
        static_cast<uint16_t>((1UL << macro::PAGE_COUNT) - 1UL);
    const uint16_t masked = static_cast<uint16_t>(value & allPages);
    return masked == 0 ? 0x0001U : masked;
}

FLASHMEM uint16_t sanitizeTrackMask(uint16_t value) {
    constexpr uint16_t allTracks =
        static_cast<uint16_t>((1UL << macro::TRACK_COUNT) - 1UL);
    const uint16_t masked = static_cast<uint16_t>(value & allTracks);
    return masked == 0 ? macro::MacroPagesState::DEFAULT_TRACK_ENABLED_MASK : masked;
}

FLASHMEM uint8_t sanitizeMacroMask(uint8_t value) {
    constexpr uint8_t allMacros = static_cast<uint8_t>((1U << macro::MACRO_COUNT) - 1U);
    return static_cast<uint8_t>(value & allMacros);
}

FLASHMEM bool writePage(binary::Writer& writer, const macro::MacroPageData& page) {
    if (!writer.writeBytes(page.name, macro::PAGE_NAME_SIZE)) return false;
    for (uint8_t i = 0; i < macro::MACRO_COUNT; ++i) {
        if (!writer.writeU8(sanitizeMidi7(page.cc[i]))) return false;
    }
    for (uint8_t i = 0; i < macro::MACRO_COUNT; ++i) {
        if (!writer.writeFloat32(page.values[i])) return false;
    }
    return writer.writeU8(sanitizeMacroMask(page.activeMacroMask)) &&
           writer.writeZeroes(MACRO_PAGE_RESERVED_SIZE);
}

FLASHMEM bool readPage(binary::Reader& reader, macro::MacroPageData& page) {
    if (!reader.readBytes(page.name, macro::PAGE_NAME_SIZE)) return false;
    page.name[macro::PAGE_NAME_SIZE - 1U] = '\0';
    for (uint8_t i = 0; i < macro::MACRO_COUNT; ++i) {
        uint8_t cc = 0;
        if (!reader.readU8(cc)) return false;
        page.cc[i] = sanitizeMidi7(cc);
    }
    for (uint8_t i = 0; i < macro::MACRO_COUNT; ++i) {
        if (!reader.readFloat32(page.values[i])) return false;
    }
    uint8_t activeMacroMask = 0;
    if (!reader.readU8(activeMacroMask)) return false;
    page.activeMacroMask = sanitizeMacroMask(activeMacroMask);
    return reader.skip(MACRO_PAGE_RESERVED_SIZE);
}

FLASHMEM bool writeTrack(binary::Writer& writer, const macro::MacroTrackData& track) {
    if (!writer.writeU8(sanitizeMidiChannel(track.channel)) ||
        !writer.writeU8(sanitizePage(track.activePage)) ||
        !writer.writeU16(sanitizePageMask(track.enabledPageMask))) {
        return false;
    }
    for (uint8_t i = 0; i < macro::PAGE_COUNT; ++i) {
        if (!writePage(writer, track.pages[i])) return false;
    }
    return true;
}

FLASHMEM bool readTrack(binary::Reader& reader, macro::MacroTrackData& track) {
    uint8_t channel = 0;
    uint8_t activePage = 0;
    uint16_t enabledPageMask = 0;
    if (!reader.readU8(channel) ||
        !reader.readU8(activePage) ||
        !reader.readU16(enabledPageMask)) {
        return false;
    }

    track.channel = sanitizeMidiChannel(channel);
    track.activePage = sanitizePage(activePage);
    track.enabledPageMask = sanitizePageMask(enabledPageMask);
    for (uint8_t i = 0; i < macro::PAGE_COUNT; ++i) {
        if (!readPage(reader, track.pages[i])) return false;
    }
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
    if (capacity != MACRO_TRACK_BANK_PAYLOAD_SIZE) return false;

    binary::Writer writer(out, capacity);
    if (!writer.writeU8(sanitizeTrack(activeTrack)) ||
        !writer.writeU8(0) ||
        !writer.writeU16(sanitizeTrackMask(enabledTrackMask))) {
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
    if (size != MACRO_TRACK_BANK_PAYLOAD_SIZE) return false;

    binary::Reader reader(data, size);
    uint8_t active = 0;
    uint8_t reserved = 0;
    uint16_t enabled = 0;
    if (!reader.readU8(active) || !reader.readU8(reserved) || !reader.readU16(enabled)) {
        return false;
    }
    (void)reserved;

    for (uint8_t i = 0; i < macro::TRACK_COUNT; ++i) {
        if (!readTrack(reader, tracks[i])) return false;
    }
    if (!reader.ok() || reader.offset() != MACRO_TRACK_BANK_PAYLOAD_SIZE) return false;

    enabledTrackMask = sanitizeTrackMask(enabled);
    activeTrack = sanitizeTrack(active);
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
    using TrackBank = std::array<macro::MacroTrackData, macro::TRACK_COUNT>;
    auto tracks = core::app::makeExtmemUnique<TrackBank>();
    if (!tracks) return false;

    uint16_t enabledTrackMask = macro::MacroPagesState::DEFAULT_TRACK_ENABLED_MASK;
    uint8_t activeTrack = 0;
    if (!decodeTrackBankPayloadInto(
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
