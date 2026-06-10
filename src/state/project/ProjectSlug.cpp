#include "state/project/ProjectSlug.hpp"

#include <array>
#include <cstdio>
#include <cstring>

#include <config/PlatformCompat.hpp>
#include <oc/interface/IFileSystem.hpp>

namespace core::state::project {

namespace {

static_assert(
    PROJECT_SLUG_MAX_LENGTH + PROJECT_FILE_EXTENSION_LENGTH <
        oc::interface::FILESYSTEM_MAX_NAME_LENGTH,
    "Project file names must fit directory entry names"
);
static_assert(
    PROJECT_SLUG_MAX_LENGTH + PROJECT_BACKUP_FILE_SUFFIX_LENGTH <
        oc::interface::FILESYSTEM_MAX_NAME_LENGTH,
    "Project backup file names must fit directory entry names"
);
static_assert(
    PROJECT_SLUG_MAX_LENGTH + PROJECT_TEMP_FILE_SUFFIX_LENGTH <
        oc::interface::FILESYSTEM_MAX_NAME_LENGTH,
    "Project tmp file names must fit directory entry names"
);

FLASHMEM char asciiLower(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

FLASHMEM bool sameTextIgnoreCase(const char* left, size_t leftLength, const char* right) {
    if (left == nullptr || right == nullptr) return false;
    if (leftLength != std::strlen(right)) return false;
    for (size_t i = 0; i < leftLength; ++i) {
        if (asciiLower(left[i]) != right[i]) return false;
    }
    return true;
}

FLASHMEM bool isReservedWindowsDeviceName(const char* slug, size_t length) {
    if (slug == nullptr || length == 0) return false;

    size_t baseLength = 0;
    while (baseLength < length && slug[baseLength] != '.') {
        ++baseLength;
    }

    if (sameTextIgnoreCase(slug, baseLength, "con") ||
        sameTextIgnoreCase(slug, baseLength, "prn") ||
        sameTextIgnoreCase(slug, baseLength, "aux") ||
        sameTextIgnoreCase(slug, baseLength, "nul")) {
        return true;
    }

    if (baseLength == 4 &&
        ((asciiLower(slug[0]) == 'c' && asciiLower(slug[1]) == 'o' && asciiLower(slug[2]) == 'm') ||
         (asciiLower(slug[0]) == 'l' && asciiLower(slug[1]) == 'p' && asciiLower(slug[2]) == 't')) &&
        slug[3] >= '1' &&
        slug[3] <= '9') {
        return true;
    }

    return false;
}

template <size_t Size>
FLASHMEM bool copySlug(std::array<char, Size>& target, const char* slug, size_t length) {
    if (length >= Size) return false;
    target.fill('\0');
    std::memcpy(target.data(), slug, length);
    target[length] = '\0';
    return true;
}

}  // namespace

FLASHMEM bool isProjectSlugChar(char c) {
    return (c >= 'a' && c <= 'z') ||
           (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') ||
           c == '-' ||
           c == '.' ||
           c == ' ';
}

FLASHMEM bool validProjectSlug(const char* slug) {
    if (slug == nullptr || slug[0] == '\0') return false;

    uint8_t length = 0;
    char previous = '\0';
    while (slug[length] != '\0') {
        if (length >= PROJECT_SLUG_MAX_LENGTH) return false;
        const char c = slug[length];
        if (!isProjectSlugChar(c)) return false;
        if (length == 0 && (c == '.' || c == ' ')) return false;
        if (c == '.' && previous == '.') return false;
        previous = c;
        ++length;
    }

    if (length == 0) return false;
    if (slug[length - 1U] == '.' || slug[length - 1U] == ' ') return false;
    if (isReservedWindowsDeviceName(slug, length)) return false;
    return true;
}

FLASHMEM bool assignProjectSlug(ProjectMetadata& metadata, const char* slug) {
    if (!validProjectSlug(slug)) return false;
    const size_t length = std::strlen(slug);
    if (!copySlug(metadata.id, slug, length)) return false;
    if (!copySlug(metadata.name, slug, length)) return false;
    return true;
}

FLASHMEM bool formatGeneratedProjectSlug(uint16_t index, char* out, size_t outSize) {
    if (out == nullptr || outSize == 0 || index == 0 || index > 999) return false;
    const int written = std::snprintf(out, outSize, "p%03u", static_cast<unsigned>(index));
    return written > 0 && static_cast<size_t>(written) < outSize;
}

}  // namespace core::state::project
