#include "ui/project/ProjectTrackEditorViewModel.hpp"

#include <algorithm>
#include <cstdio>

#include <config/PlatformCompat.hpp>

#include "state/project/ProjectTrackDomainOps.hpp"
#include "state/project/ProjectTrackEditorOps.hpp"

namespace core::ui::project {
namespace track = core::state::project;

FLASHMEM ProjectTrackEditorViewModel buildProjectTrackEditorViewModel(
    const track::ProjectTrackEditorState& editor,
    const track::ProjectTrackState& tracks,
    uint16_t enabledMask
) {
    ProjectTrackEditorViewModel out{};
    out.port = {'U', 'S', 'B', '\0'};
    out.selectedProperty = editor.selectedProperty;
    if (!editor.active || !track::validProjectTrackIndex(editor.trackIndex)) {
        return out;
    }

    const uint8_t index = editor.trackIndex;
    out.visible = true;
    out.trackIndex = index;
    out.trackNumber = static_cast<uint8_t>(index + 1U);
    out.trackEnabled = track::projectTrackEditorTrackEnabled(
        enabledMask,
        index
    );
    out.canSwitchTrack =
        (enabledMask & static_cast<uint16_t>(enabledMask - 1U)) != 0U;
    out.muted = track::projectTrackMuted(tracks, index);
    out.soloed = track::projectTrackSoloed(tracks, index);
    out.midiChannel = static_cast<uint8_t>(
        std::min<uint8_t>(
            track::projectTrackMidiChannel(tracks, index),
            track::PROJECT_TRACK_MIDI_CHANNEL_MAX_0BASED
        ) + 1U
    );
    out.delayMs = static_cast<int16_t>(std::clamp<int32_t>(
        track::projectTrackDelayMs(tracks, index),
        track::PROJECT_TRACK_DELAY_MIN_MS,
        track::PROJECT_TRACK_DELAY_MAX_MS
    ));
    std::snprintf(
        out.title.data(),
        out.title.size(),
        "TRACK %u",
        static_cast<unsigned>(out.trackNumber)
    );
    return out;
}

}  // namespace core::ui::project
