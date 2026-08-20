#include "state/sequencer/SequencerPresetMetadata.hpp"

#include <cctype>
#include <cstdint>

#include <config/PlatformCompat.hpp>

#include "state/project/ProjectSlug.hpp"

namespace core::state::sequencer {

namespace {

FLASHMEM bool boundedTextLength(
    const char* text,
    size_t capacity,
    size_t& length
) {
    length = 0U;
    if (text == nullptr || capacity == 0U) return false;
    while (length < capacity && text[length] != '\0') ++length;
    return length < capacity;
}

FLASHMEM bool validUtf8Text(const char* text, size_t length) {
    size_t offset = 0U;
    while (offset < length) {
        const uint8_t first = static_cast<uint8_t>(text[offset]);
        uint32_t codePoint = 0U;
        size_t width = 0U;
        if (first < 0x80U) {
            codePoint = first;
            width = 1U;
        } else if (first >= 0xC2U && first <= 0xDFU) {
            codePoint = static_cast<uint32_t>(first & 0x1FU);
            width = 2U;
        } else if (first >= 0xE0U && first <= 0xEFU) {
            codePoint = static_cast<uint32_t>(first & 0x0FU);
            width = 3U;
        } else if (first >= 0xF0U && first <= 0xF4U) {
            codePoint = static_cast<uint32_t>(first & 0x07U);
            width = 4U;
        } else {
            return false;
        }
        if (width > length - offset) return false;
        for (size_t i = 1U; i < width; ++i) {
            const uint8_t continuation = static_cast<uint8_t>(text[offset + i]);
            if ((continuation & 0xC0U) != 0x80U) return false;
            codePoint = (codePoint << 6U) | (continuation & 0x3FU);
        }
        const bool overlong =
            (width == 2U && codePoint < 0x80U) ||
            (width == 3U && codePoint < 0x800U) ||
            (width == 4U && codePoint < 0x10000U);
        if (overlong || codePoint > 0x10FFFFU ||
            (codePoint >= 0xD800U && codePoint <= 0xDFFFU) ||
            codePoint < 0x20U ||
            (codePoint >= 0x7FU && codePoint <= 0x9FU)) {
            return false;
        }
        offset += width;
    }
    return true;
}

}  // namespace

FLASHMEM bool validSequencerPresetTechnicalId(const char* technicalId) {
    size_t length = 0U;
    return boundedTextLength(
               technicalId,
               SEQUENCER_PRESET_TECHNICAL_ID_SIZE,
               length
           ) &&
        length > 0U &&
        core::state::project::validProjectSlug(technicalId);
}

FLASHMEM bool validSequencerPresetSemanticName(const char* semanticName) {
    size_t length = 0U;
    if (!boundedTextLength(
            semanticName,
            SEQUENCER_PRESET_SEMANTIC_NAME_SIZE,
            length
        ) ||
        length == 0U ||
        semanticName[0] == ' ' ||
        semanticName[length - 1U] == ' ') {
        return false;
    }
    return validUtf8Text(semanticName, length);
}

FLASHMEM uint32_t sequencerPresetIdHash(const char* presetId) {
    constexpr uint32_t FNV_OFFSET = 2166136261U;
    constexpr uint32_t FNV_PRIME = 16777619U;
    uint32_t hash = FNV_OFFSET;
    if (presetId == nullptr) return hash;
    for (const auto* cursor = reinterpret_cast<const uint8_t*>(presetId);
         *cursor != 0U;
         ++cursor) {
        hash = (hash ^ *cursor) * FNV_PRIME;
    }
    return hash;
}

FLASHMEM void sequencerPresetSemanticName(
    const char* presetId,
    char* out,
    size_t outSize
) {
    if (out == nullptr || outSize == 0U) return;
    out[0] = '\0';
    if (presetId == nullptr) return;

    size_t written = 0U;
    bool capitalize = true;
    for (size_t i = 0U;
         presetId[i] != '\0' && written + 1U < outSize;
         ++i) {
        const auto ch = static_cast<unsigned char>(presetId[i]);
        if (ch == '-' || ch == '_') {
            if (written > 0U && out[written - 1U] != ' ') {
                out[written++] = ' ';
            }
            capitalize = true;
            continue;
        }
        out[written++] = static_cast<char>(
            capitalize ? std::toupper(ch) : ch
        );
        capitalize = false;
    }
    while (written > 0U && out[written - 1U] == ' ') --written;
    out[written] = '\0';
}

}  // namespace core::state::sequencer
