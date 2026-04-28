#pragma once

#include <cstdint>

#include <oc/state/Signal.hpp>

#include "state/StructureSelectionState.hpp"

namespace core::state {

inline constexpr unsigned int kTrackNavigationMaxSubscribers = 8;

/**
 * Shared track navigation UI state.
 *
 * It mirrors add-slot preview and selection/hold state; the durable shared track
 * mask and active index are owned by CoreState and synchronized to domains.
 */
struct TrackNavigationState {
    oc::state::Signal<bool, kTrackNavigationMaxSubscribers> previewAddSlot{false};
    oc::state::Signal<uint8_t, kTrackNavigationMaxSubscribers> previewTrackIndex{0};
    StructureHoldState hold;
    StructureSelectionState selection;

    void syncPreviewTrack(uint8_t trackIndex) {
        previewTrackIndex.set(trackIndex);
    }

    void reset() {
        previewAddSlot.set(false);
        previewTrackIndex.set(0);
        hold.clear();
        selection.reset(StructureSelectionScope::TRACK);
    }
};

}  // namespace core::state
