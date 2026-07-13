#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace core::state::contextual {

struct ContextPath {
    static constexpr std::size_t CAPACITY = 64;
    static constexpr uint8_t MAX_SEGMENTS = 4;

    std::array<char, CAPACITY> text{};
    uint8_t length = 0;
    uint8_t segmentCount = 0;
    bool truncated = false;
};

void clearContextPath(ContextPath& path);

/** Appends one complete segment or leaves the existing path untouched. */
bool appendContextPathSegment(ContextPath& path, const char* segment);

/** Appends "label displayIndex" without formatting or heap allocation. */
bool appendIndexedContextPathSegment(
    ContextPath& path,
    const char* label,
    uint16_t displayIndex
);

const char* contextPathText(const ContextPath& path);
bool contextPathEmpty(const ContextPath& path);

}  // namespace core::state::contextual
