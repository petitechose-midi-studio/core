#include "ui/common/GlobalTrackNavigationStripModel.hpp"

#include <algorithm>

#include <config/PlatformCompat.hpp>

#include "state/project/ProjectTrackDomainOps.hpp"

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
        trackNavigation.selection.scope.get() ==
            core::state::StructureSelectionScope::TRACK;
    const bool previewAddSlot =
        !selectingTrack && trackNavigation.previewAddSlot.get();
    const bool focusingTrack =
        !selectingTrack &&
        source.structureNavigationFocus ==
            core::state::StructureNavigationFocus::TRACK;
    const uint16_t enabledMask = source.sharedTrackEnabledMask;
    const uint16_t audible = core::state::project::audibleMask(
        source.projectTracks,
        enabledMask
    );
    const uint16_t inaudibleMask = static_cast<uint16_t>(
        enabledMask & static_cast<uint16_t>(~audible)
    );
    const uint16_t explicitMutedMask = static_cast<uint16_t>(
        enabledMask & source.projectTracks.authored.mutedMask
    );
    const uint16_t soloMask = static_cast<uint16_t>(
        enabledMask & source.projectTracks.authored.soloMask
    );
    const uint8_t activeTrack = source.sharedTrackActive;
    const uint8_t previewAddIndex =
        previewAddSlot && focusingTrack
            ? clampTrackIndex(trackNavigation.previewTrackIndex.get())
            : TrackNavigationStripProps::TRACK_COUNT;

    props.activeTrack = activeTrack;
    props.previewTrack = selectingTrack
        ? clampTrackIndex(trackNavigation.selection.cursorIndex.get())
        : (focusingTrack
            ? clampTrackIndex(trackNavigation.previewTrackIndex.get())
            : activeTrack);
    props.addTrackIndex = previewAddIndex;
    props.enabledMask = enabledMask;
    props.explicitMutedMask = explicitMutedMask;
    props.soloMask = soloMask;
    props.inaudibleMask = inaudibleMask;
    props.selectedMask = selectingTrack
        ? static_cast<uint16_t>(
            trackNavigation.selection.selectedMask.get() & enabledMask
        )
        : 0U;
    const bool placing =
        selectingTrack && trackNavigation.selection.placing.get();
    props.destinationPreviewMask = placing
        ? trackNavigation.selection.destinationMask.get()
        : 0U;
    props.destinationOverwriteMask = placing
        ? trackNavigation.selection.overwriteMask.get()
        : 0U;
    props.destinationBlockedMask =
        placing && trackNavigation.selection.pasteBlocked.get()
            ? props.destinationPreviewMask
            : 0U;
    props.focusingTrack = focusingTrack;
    props.selectingTrack = selectingTrack;
    for (uint8_t i = 0; i < TrackNavigationStripProps::TRACK_COUNT; ++i) {
        props.activity[i] = source.statusBar.trackNoteActivity[i].get();
    }
    return props;
}

}  // namespace

FLASHMEM TrackNavigationStripProps buildGlobalTrackNavigationStripProps(
    const GlobalTrackNavigationStripSource& source
) {
    return buildTrackNavigationStripProps(source);
}

FLASHMEM bool globalTrackNavigationStripPropsEqual(
    const TrackNavigationStripProps& lhs,
    const TrackNavigationStripProps& rhs
) {
    return lhs.activeTrack == rhs.activeTrack &&
           lhs.previewTrack == rhs.previewTrack &&
           lhs.addTrackIndex == rhs.addTrackIndex &&
           lhs.enabledMask == rhs.enabledMask &&
           lhs.explicitMutedMask == rhs.explicitMutedMask &&
           lhs.soloMask == rhs.soloMask &&
           lhs.inaudibleMask == rhs.inaudibleMask &&
           lhs.selectedMask == rhs.selectedMask &&
           lhs.destinationPreviewMask == rhs.destinationPreviewMask &&
           lhs.destinationOverwriteMask == rhs.destinationOverwriteMask &&
           lhs.destinationBlockedMask == rhs.destinationBlockedMask &&
           lhs.focusingTrack == rhs.focusingTrack &&
           lhs.selectingTrack == rhs.selectingTrack &&
           lhs.activity == rhs.activity;
}

}  // namespace core::ui
