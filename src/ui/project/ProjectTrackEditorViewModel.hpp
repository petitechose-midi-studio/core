#pragma once

#include <array>
#include <cstdint>
#include <type_traits>

#include "state/project/ProjectTrackEditorState.hpp"
#include "state/project/ProjectTrackState.hpp"

namespace core::ui::project {

/** Allocation-free semantic projection consumed by the retained Track view. */
struct ProjectTrackEditorViewModel {
    // Enough for the full uint8_t diagnostic range ("TRACK 255\0"), even
    // though the canonical product domain is currently limited to 16 Tracks.
    static constexpr uint8_t TITLE_CAPACITY = 10U;
    static constexpr uint8_t PORT_CAPACITY = 4U;

    std::array<char, TITLE_CAPACITY> title{};
    std::array<char, PORT_CAPACITY> port{};
    int16_t delayMs = 0;
    uint8_t trackIndex = 0U;
    uint8_t trackNumber = 1U;
    uint8_t midiChannel = 1U;
    core::state::project::ProjectTrackEditorProperty selectedProperty =
        core::state::project::ProjectTrackEditorProperty::CHANNEL;
    bool visible = false;
    bool trackEnabled = false;
    bool canSwitchTrack = false;
    bool muted = false;
    bool soloed = false;
    bool portEditable = false;
    bool drum = false;
    bool draftDrum = false;
    bool typeChangePending = false;

    friend bool operator==(
        const ProjectTrackEditorViewModel& lhs,
        const ProjectTrackEditorViewModel& rhs
    ) {
        return lhs.title == rhs.title && lhs.port == rhs.port &&
               lhs.delayMs == rhs.delayMs &&
               lhs.trackIndex == rhs.trackIndex &&
               lhs.trackNumber == rhs.trackNumber &&
               lhs.midiChannel == rhs.midiChannel &&
               lhs.selectedProperty == rhs.selectedProperty &&
               lhs.visible == rhs.visible &&
               lhs.trackEnabled == rhs.trackEnabled &&
               lhs.canSwitchTrack == rhs.canSwitchTrack &&
               lhs.muted == rhs.muted && lhs.soloed == rhs.soloed &&
               lhs.portEditable == rhs.portEditable &&
               lhs.drum == rhs.drum && lhs.draftDrum == rhs.draftDrum &&
               lhs.typeChangePending == rhs.typeChangePending;
    }
};

static_assert(sizeof(ProjectTrackEditorViewModel) <= 40U);
static_assert(std::is_trivially_copyable_v<ProjectTrackEditorViewModel>);

ProjectTrackEditorViewModel buildProjectTrackEditorViewModel(
    const core::state::project::ProjectTrackEditorState& editor,
    const core::state::project::ProjectTrackState& tracks,
    uint16_t enabledMask
);

}  // namespace core::ui::project
