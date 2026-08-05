#pragma once

#include <cstddef>

#include <oc/state/StaticSignalWatcher.hpp>

#include "state/StructureNavigationState.hpp"

namespace core::ui {

inline constexpr size_t STRUCTURE_SELECTION_INVALIDATION_SIGNAL_COUNT = 9U;

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
    const core::state::StructureSelectionState& selection
) {
    return watcher.watchAll(
        selection.active,
        selection.placing,
        selection.scope,
        selection.cursorIndex,
        selection.selectedMask,
        selection.destinationMask,
        selection.overwriteMask,
        selection.pasteBlocked,
        selection.clipboardRevision
    );
}

}  // namespace core::ui
