#pragma once

#include <cmath>

#include <oc/util/Index.hpp>

namespace core::handler::nav {

/**
 * Mechanical encoder-turn helpers.
 *
 * This namespace intentionally contains no UI state or domain behavior; it only
 * converts turn deltas into signed/wrapped index movement.
 */
inline bool hasTurnDelta(float delta) {
    return delta != 0.0f;
}

inline int turnStep(float delta) {
    return (delta > 0.0f) ? 1 : -1;
}

/** Preserve coalesced RELATIVE detents while accepting legacy tiny UX deltas. */
inline int turnSteps(float delta) {
    if (!hasTurnDelta(delta)) return 0;
    const int steps = static_cast<int>(std::lround(delta));
    return steps != 0 ? steps : turnStep(delta);
}

inline int nextWrappedIndex(float delta, int current, int count) {
    if (!hasTurnDelta(delta) || count <= 0) {
        return current;
    }

    return oc::util::wrapIndex(current + turnStep(delta), count);
}

}  // namespace core::handler::nav
