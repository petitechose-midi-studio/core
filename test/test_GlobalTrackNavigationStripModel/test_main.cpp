#include <cassert>
#include <cstdint>
#include <iostream>

#include "../../src/ui/common/GlobalTrackNavigationStripModel.hpp"
#include "state/project/ProjectTrackDomainOps.hpp"

namespace {

core::ui::GlobalTrackNavigationStripSource sourceFor(
    core::state::TrackNavigationState& navigation,
    core::state::StatusBarState& status,
    const core::state::project::ProjectTrackState& projectTracks,
    core::state::StructureNavigationFocus focus,
    uint16_t enabledMask,
    uint8_t activeTrack
) {
    return core::ui::GlobalTrackNavigationStripSource{
        navigation,
        focus,
        enabledMask,
        projectTracks,
        activeTrack,
        status,
    };
}

void test_uses_shared_track_state_when_not_focusing() {
    core::state::TrackNavigationState navigation;
    core::state::StatusBarState status;
    core::state::project::ProjectTrackState projectTracks;

    status.trackNoteActivity[2].set(7);
    assert(core::state::project::setProjectTrackMuted(
        projectTracks, 2U, true
    ).changed());

    const auto props = core::ui::buildGlobalTrackNavigationStripProps(
        sourceFor(
            navigation,
            status,
            projectTracks,
            core::state::StructureNavigationFocus::PAGE,
            0x0005,
            2
        )
    );

    assert(props.activeTrack == 2);
    assert(props.previewTrack == 2);
    assert(props.addTrackIndex == core::ui::TrackNavigationStripProps::TRACK_COUNT);
    assert(props.enabledMask == 0x0005);
    assert(props.explicitMutedMask == 0x0004);
    assert(props.soloMask == 0x0000);
    assert(props.inaudibleMask == 0x0004);
    assert(!props.focusingTrack);
    assert(props.activity[2] == 7);

    std::cout << "[PASS] test_uses_shared_track_state_when_not_focusing\n";
}

void test_track_focus_uses_preview_and_add_slot() {
    core::state::TrackNavigationState navigation;
    core::state::StatusBarState status;
    core::state::project::ProjectTrackState projectTracks;

    navigation.previewAddSlot.set(true);
    navigation.previewTrackIndex.set(99);

    const auto props = core::ui::buildGlobalTrackNavigationStripProps(
        sourceFor(
            navigation,
            status,
            projectTracks,
            core::state::StructureNavigationFocus::TRACK,
            0x0003,
            0
        )
    );

    assert(props.activeTrack == 0);
    assert(props.previewTrack == core::ui::TrackNavigationStripProps::TRACK_COUNT - 1);
    assert(props.addTrackIndex == core::ui::TrackNavigationStripProps::TRACK_COUNT - 1);
    assert(props.enabledMask == 0x0003);
    assert(props.focusingTrack);

    std::cout << "[PASS] test_track_focus_uses_preview_and_add_slot\n";
}

void test_solo_projects_actual_audibility_without_a_duplicate_mute_view() {
    core::state::TrackNavigationState navigation;
    core::state::StatusBarState status;
    core::state::project::ProjectTrackState projectTracks;

    assert(core::state::project::setProjectTrackSoloed(
        projectTracks, 2U, true
    ).changed());

    const auto props = core::ui::buildGlobalTrackNavigationStripProps(
        sourceFor(
            navigation,
            status,
            projectTracks,
            core::state::StructureNavigationFocus::PAGE,
            0x0007U,
            2U
        )
    );

    assert(props.enabledMask == 0x0007U);
    assert(props.explicitMutedMask == 0x0000U);
    assert(props.soloMask == 0x0004U);
    assert(props.inaudibleMask == 0x0003U);
    assert(props.activeTrack == 2U);

    std::cout << "[PASS] test_solo_projects_actual_audibility_without_a_duplicate_mute_view\n";
}

void test_track_selection_owns_cursor_and_clips_selected_slots() {
    core::state::TrackNavigationState navigation;
    core::state::StatusBarState status;
    core::state::project::ProjectTrackState projectTracks;

    navigation.previewAddSlot.set(true);
    navigation.previewTrackIndex.set(7U);
    navigation.selection.reset(
        core::state::StructureSelectionScope::TRACK,
        2U
    );
    navigation.selection.selectedMask.set(0x000DU);
    navigation.selection.active.set(true);

    const auto props = core::ui::buildGlobalTrackNavigationStripProps(
        sourceFor(
            navigation,
            status,
            projectTracks,
            core::state::StructureNavigationFocus::TRACK,
            0x0005U,
            0U
        )
    );

    assert(props.activeTrack == 0U);
    assert(props.previewTrack == 2U);
    assert(props.addTrackIndex ==
           core::ui::TrackNavigationStripProps::TRACK_COUNT);
    assert(props.selectedMask == 0x0005U);
    assert(!props.focusingTrack);
    assert(props.selectingTrack);

    std::cout << "[PASS] test_track_selection_owns_cursor_and_clips_selected_slots\n";
}

}  // namespace

int main() {
    test_uses_shared_track_state_when_not_focusing();
    test_track_focus_uses_preview_and_add_slot();
    test_solo_projects_actual_audibility_without_a_duplicate_mute_view();
    test_track_selection_owns_cursor_and_clips_selected_slots();

    std::cout << "All GlobalTrackNavigationStripModel tests passed\n";
    return 0;
}
