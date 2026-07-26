#pragma once

#include <cstddef>

#include <oc/state/StaticSignalWatcher.hpp>

#include "state/StructureNavigationState.hpp"

namespace core::ui {

inline constexpr size_t STRUCTURE_SELECTION_INVALIDATION_SIGNAL_COUNT = 4U;

/**
 * Registers every signal that can change the visual projection of a
 * page/track selection.
 *
 * Keeping this list shared prevents retained views from reading a complete
 * selection model while subscribing to only part of it.
 */
template <size_t MaxSignals>
OC_ALWAYS_INLINE bool watchStructureSelectionInvalidation(
    oc::state::StaticWatchGroup<MaxSignals>& watcher,
    core::state::StructureSelectionState& selection
) {
    return watcher.watchAll(
        selection.active,
        selection.scope,
        selection.cursorIndex,
        selection.selectedMask
    );
}

}  // namespace core::ui
