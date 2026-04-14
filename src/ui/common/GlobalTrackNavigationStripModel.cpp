#include "ui/common/GlobalTrackNavigationStripModel.hpp"

#include <algorithm>

namespace core::ui {

namespace structure_slots = core::state::shared;

namespace {

uint8_t clampTrackIndex(uint8_t index) {
    return static_cast<uint8_t>(std::min<uint16_t>(index, TrackNavigationStripProps::TRACK_COUNT - 1U));
}

TrackNavigationStripProps buildTrackNavigationStripProps(const core::state::CoreState& state) {
    TrackNavigationStripProps props;
    const bool selectingTrack =
        state.trackNavigation.selection.active.get() &&
        state.trackNavigation.selection.scope.get() == core::state::StructureSelectionScope::TRACK;
    const bool previewAddSlot =
        !state.trackNavigation.selection.active.get() && state.trackNavigation.previewAddSlot.get();
    const bool focusingTrack = !state.trackNavigation.selection.active.get() &&
        state.structureNavigationFocus.get() == core::state::StructureNavigationFocus::TRACK;
    const uint16_t enabledMask = state.sharedTrackEnabledMask.get();
    const uint8_t activeTrack = state.sharedTrackActive.get();
    const uint8_t previewAddIndex =
        previewAddSlot && focusingTrack
            ? clampTrackIndex(state.trackNavigation.previewTrackIndex.get())
            : TrackNavigationStripProps::TRACK_COUNT;

    props.activeTrack = activeTrack;
    props.previewTrack =
        selectingTrack
            ? state.trackNavigation.selection.cursorIndex.get()
            : (focusingTrack ? clampTrackIndex(state.trackNavigation.previewTrackIndex.get())
                             : activeTrack);
    props.addTrackIndex = previewAddIndex;
    props.enabledMask = enabledMask;
    props.selectedMask = selectingTrack ? state.trackNavigation.selection.selectedMask.get() : 0;
    props.focusingTrack = focusingTrack;
    props.selectingTrack = selectingTrack;
    for (uint8_t i = 0; i < TrackNavigationStripProps::TRACK_COUNT; ++i) {
        props.activity[i] = state.statusBar.trackNoteActivity[i].get();
    }
    return props;
}

}  // namespace

TrackNavigationStripProps buildGlobalTrackNavigationStripProps(const core::state::CoreState& state) {
    return buildTrackNavigationStripProps(state);
}

bool globalTrackNavigationStripPropsEqual(
    const TrackNavigationStripProps& lhs,
    const TrackNavigationStripProps& rhs
) {
    return lhs.activeTrack == rhs.activeTrack &&
           lhs.previewTrack == rhs.previewTrack &&
           lhs.addTrackIndex == rhs.addTrackIndex &&
           lhs.enabledMask == rhs.enabledMask &&
           lhs.selectedMask == rhs.selectedMask &&
           lhs.focusingTrack == rhs.focusingTrack &&
           lhs.selectingTrack == rhs.selectingTrack &&
           lhs.activity == rhs.activity;
}

}  // namespace core::ui
