#pragma once

#include <oc/util/Index.hpp>

namespace core::handler::nav {

inline bool hasTurnDelta(float delta) {
    return delta != 0.0f;
}

inline int turnStep(float delta) {
    return (delta > 0.0f) ? 1 : -1;
}

inline int nextWrappedIndex(float delta, int current, int count) {
    if (!hasTurnDelta(delta) || count <= 0) {
        return current;
    }

    return oc::util::wrapIndex(current + turnStep(delta), count);
}

}  // namespace core::handler::nav
