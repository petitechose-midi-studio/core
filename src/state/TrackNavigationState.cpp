#include "state/TrackNavigationState.hpp"

#include <config/PlatformCompat.hpp>

namespace core::state {

FLASHMEM TrackNavigationState::~TrackNavigationState() = default;

FLASHMEM void TrackNavigationState::syncPreviewTrack(uint8_t trackIndex) {
    previewTrackIndex.set(trackIndex);
}

FLASHMEM void TrackNavigationState::reset() {
    previewAddSlot.set(false);
    previewTrackIndex.set(0);
    hold.clear();
    selection.reset(StructureSelectionScope::TRACK);
}

}  // namespace core::state
