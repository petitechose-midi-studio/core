#include "state/contextual/ContextPath.hpp"

namespace core::state::contextual {

namespace {

constexpr char SEPARATOR[] = " / ";
constexpr std::size_t SEPARATOR_LENGTH = sizeof(SEPARATOR) - 1U;

bool boundedLength(
    const char* text,
    std::size_t limit,
    std::size_t& length
) {
    if (text == nullptr) {
        return false;
    }

    length = 0;
    while (length < limit && text[length] != '\0') {
        ++length;
    }
    return length < limit;
}

}  // namespace

void clearContextPath(ContextPath& path) {
    path = ContextPath{};
}

bool appendContextPathSegment(ContextPath& path, const char* segment) {
    if (segment == nullptr) {
        return false;
    }

    std::size_t segmentLength = 0;
    if (!boundedLength(segment, ContextPath::CAPACITY, segmentLength)) {
        path.truncated = true;
        return false;
    }
    if (segmentLength == 0) {
        return false;
    }

    if (path.segmentCount >= ContextPath::MAX_SEGMENTS) {
        path.truncated = true;
        return false;
    }

    const std::size_t separatorLength =
        path.segmentCount == 0 ? 0U : SEPARATOR_LENGTH;
    const std::size_t required =
        static_cast<std::size_t>(path.length) + separatorLength +
        segmentLength;
    if (required >= ContextPath::CAPACITY) {
        path.truncated = true;
        return false;
    }

    std::size_t writeAt = path.length;
    for (std::size_t i = 0; i < separatorLength; ++i) {
        path.text[writeAt++] = SEPARATOR[i];
    }
    for (std::size_t i = 0; i < segmentLength; ++i) {
        path.text[writeAt++] = segment[i];
    }
    path.text[writeAt] = '\0';
    path.length = static_cast<uint8_t>(writeAt);
    ++path.segmentCount;
    return true;
}

bool appendIndexedContextPathSegment(
    ContextPath& path,
    const char* label,
    uint16_t displayIndex
) {
    if (label == nullptr) {
        return false;
    }

    std::size_t labelLength = 0;
    if (!boundedLength(label, ContextPath::CAPACITY, labelLength)) {
        path.truncated = true;
        return false;
    }
    if (labelLength == 0) {
        return false;
    }

    char digits[5]{};
    std::size_t digitCount = 0;
    do {
        digits[digitCount++] = static_cast<char>('0' + (displayIndex % 10U));
        displayIndex = static_cast<uint16_t>(displayIndex / 10U);
    } while (displayIndex != 0 && digitCount < sizeof(digits));

    if (labelLength + 1U + digitCount >= ContextPath::CAPACITY) {
        path.truncated = true;
        return false;
    }

    std::array<char, ContextPath::CAPACITY> segment{};
    std::size_t writeAt = 0;
    for (std::size_t i = 0; i < labelLength; ++i) {
        segment[writeAt++] = label[i];
    }
    segment[writeAt++] = ' ';
    while (digitCount > 0) {
        segment[writeAt++] = digits[--digitCount];
    }
    segment[writeAt] = '\0';
    return appendContextPathSegment(path, segment.data());
}

const char* contextPathText(const ContextPath& path) {
    return path.text.data();
}

bool contextPathEmpty(const ContextPath& path) {
    return path.length == 0;
}

}  // namespace core::state::contextual
