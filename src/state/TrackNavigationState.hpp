#pragma once

#include <cstdint>

#include <oc/state/Signal.hpp>

#include "state/StructureNavigationState.hpp"

namespace core::state {

inline constexpr unsigned int kTrackNavigationMaxSubscribers = 8;

/**
 * Shared track navigation UI state.
 *
 * It mirrors add-slot preview, selection, and hold state; the durable shared track
 * mask and active index are owned by CoreState and synchronized to domains.
 */
struct TrackNavigationState {
    oc::state::Signal<bool, kTrackNavigationMaxSubscribers> previewAddSlot{false};
    oc::state::Signal<uint8_t, kTrackNavigationMaxSubscribers> previewTrackIndex{0};
    StructureHoldState hold;
    StructureSelectionState selection;

    ~TrackNavigationState();

    void syncPreviewTrack(uint8_t trackIndex);

    void reset();
};

}  // namespace core::state
