#include "ui/common/GlobalTrackNavigationStripModel.hpp"

#include <algorithm>

namespace core::ui {

namespace {

uint8_t clampTrackIndex(uint8_t index) {
    return static_cast<uint8_t>(std::min<uint16_t>(index, TrackNavigationStripProps::TRACK_COUNT - 1U));
}

TrackNavigationStripProps buildTrackNavigationStripProps(
    const GlobalTrackNavigationStripSource& source
) {
    TrackNavigationStripProps props;
    const auto& trackNavigation = source.trackNavigation;
    const bool selectingTrack =
        trackNavigation.selection.active.get() &&
        trackNavigation.selection.scope.get() == core::state::StructureSelectionScope::TRACK;
    const bool previewAddSlot =
        !trackNavigation.selection.active.get() && trackNavigation.previewAddSlot.get();
    const bool focusingTrack = !trackNavigation.selection.active.get() &&
        source.structureNavigationFocus == core::state::StructureNavigationFocus::TRACK;
    const uint16_t enabledMask = source.sharedTrackEnabledMask;
    const uint16_t mutedMask = static_cast<uint16_t>(source.sharedTrackMutedMask & enabledMask);
    const uint8_t activeTrack = source.sharedTrackActive;
    const uint8_t previewAddIndex =
        previewAddSlot && focusingTrack
            ? clampTrackIndex(trackNavigation.previewTrackIndex.get())
            : TrackNavigationStripProps::TRACK_COUNT;

    props.activeTrack = activeTrack;
    props.previewTrack =
        selectingTrack
            ? trackNavigation.selection.cursorIndex.get()
            : (focusingTrack ? clampTrackIndex(trackNavigation.previewTrackIndex.get())
                             : activeTrack);
    props.addTrackIndex = previewAddIndex;
    props.enabledMask = enabledMask;
    props.mutedMask = mutedMask;
    props.selectedMask = selectingTrack ? trackNavigation.selection.selectedMask.get() : 0;
    props.focusingTrack = focusingTrack;
    props.selectingTrack = selectingTrack;
    for (uint8_t i = 0; i < TrackNavigationStripProps::TRACK_COUNT; ++i) {
        props.activity[i] = source.statusBar.trackNoteActivity[i].get();
    }
    return props;
}

}  // namespace

TrackNavigationStripProps buildGlobalTrackNavigationStripProps(
    const GlobalTrackNavigationStripSource& source
) {
    return buildTrackNavigationStripProps(source);
}

bool globalTrackNavigationStripPropsEqual(
    const TrackNavigationStripProps& lhs,
    const TrackNavigationStripProps& rhs
) {
    return lhs.activeTrack == rhs.activeTrack &&
           lhs.previewTrack == rhs.previewTrack &&
           lhs.addTrackIndex == rhs.addTrackIndex &&
           lhs.enabledMask == rhs.enabledMask &&
           lhs.mutedMask == rhs.mutedMask &&
           lhs.selectedMask == rhs.selectedMask &&
           lhs.focusingTrack == rhs.focusingTrack &&
           lhs.selectingTrack == rhs.selectingTrack &&
           lhs.activity == rhs.activity;
}

}  // namespace core::ui
