#pragma once

#include <algorithm>

namespace core::state::normalized {

// Quantize non-NaN normalized values to inclusive integer ranges. The upper
// bound must remain representable after float rounding and conversion to int.
inline float clampNormalized(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

inline int normalizedToInclusiveInt(float normalized, int maxInclusive) {
    if (maxInclusive <= 0) return 0;
    const float value = clampNormalized(normalized);
    return std::clamp(static_cast<int>(value * static_cast<float>(maxInclusive) + 0.5f),
                      0, maxInclusive);
}

inline int normalizedToIndex(float normalized, int itemCount) {
    return itemCount <= 1 ? 0 : normalizedToInclusiveInt(normalized, itemCount - 1);
}

inline float indexToNormalized(int index, int itemCount) {
    if (itemCount <= 1) return 0.0f;
    return static_cast<float>(std::clamp(index, 0, itemCount - 1)) /
           static_cast<float>(itemCount - 1);
}

}  // namespace core::state::normalized
