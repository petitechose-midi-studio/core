#include <cassert>
#include <cstdint>
#include <iostream>

#include "../../src/ui/common/GlobalTrackNavigationStripModel.hpp"

namespace {

core::ui::GlobalTrackNavigationStripSource sourceFor(
    core::state::TrackNavigationState& navigation,
    core::state::StatusBarState& status,
    core::state::StructureNavigationFocus focus,
    uint16_t enabledMask,
    uint16_t mutedMask,
    uint8_t activeTrack
) {
    return core::ui::GlobalTrackNavigationStripSource{
        navigation,
        focus,
        enabledMask,
        mutedMask,
        activeTrack,
        status,
    };
}

void test_uses_shared_track_state_when_not_focusing_or_selecting() {
    core::state::TrackNavigationState navigation;
    core::state::StatusBarState status;

    status.trackNoteActivity[2].set(7);

    const auto props = core::ui::buildGlobalTrackNavigationStripProps(
        sourceFor(
            navigation,
            status,
            core::state::StructureNavigationFocus::PAGE,
            0x0005,
            0x0004,
            2
        )
    );

    assert(props.activeTrack == 2);
    assert(props.previewTrack == 2);
    assert(props.addTrackIndex == core::ui::TrackNavigationStripProps::TRACK_COUNT);
    assert(props.enabledMask == 0x0005);
    assert(props.mutedMask == 0x0004);
    assert(props.selectedMask == 0);
    assert(!props.focusingTrack);
    assert(!props.selectingTrack);
    assert(props.activity[2] == 7);

    std::cout << "[PASS] test_uses_shared_track_state_when_not_focusing_or_selecting\n";
}

void test_track_focus_uses_preview_and_add_slot() {
    core::state::TrackNavigationState navigation;
    core::state::StatusBarState status;

    navigation.previewAddSlot.set(true);
    navigation.previewTrackIndex.set(99);

    const auto props = core::ui::buildGlobalTrackNavigationStripProps(
        sourceFor(
            navigation,
            status,
            core::state::StructureNavigationFocus::TRACK,
            0x0003,
            0,
            0
        )
    );

    assert(props.activeTrack == 0);
    assert(props.previewTrack == core::ui::TrackNavigationStripProps::TRACK_COUNT - 1);
    assert(props.addTrackIndex == core::ui::TrackNavigationStripProps::TRACK_COUNT - 1);
    assert(props.enabledMask == 0x0003);
    assert(props.focusingTrack);
    assert(!props.selectingTrack);

    std::cout << "[PASS] test_track_focus_uses_preview_and_add_slot\n";
}

void test_track_selection_takes_priority_over_preview() {
    core::state::TrackNavigationState navigation;
    core::state::StatusBarState status;

    navigation.previewAddSlot.set(true);
    navigation.previewTrackIndex.set(4);
    navigation.selection.active.set(true);
    navigation.selection.scope.set(core::state::StructureSelectionScope::TRACK);
    navigation.selection.cursorIndex.set(9);
    navigation.selection.selectedMask.set(0x0201);

    const auto props = core::ui::buildGlobalTrackNavigationStripProps(
        sourceFor(
            navigation,
            status,
            core::state::StructureNavigationFocus::TRACK,
            0x03FF,
            0xFFFF,
            1
        )
    );

    assert(props.activeTrack == 1);
    assert(props.previewTrack == 9);
    assert(props.addTrackIndex == core::ui::TrackNavigationStripProps::TRACK_COUNT);
    assert(props.enabledMask == 0x03FF);
    assert(props.mutedMask == 0x03FF);
    assert(props.selectedMask == 0x0201);
    assert(!props.focusingTrack);
    assert(props.selectingTrack);

    std::cout << "[PASS] test_track_selection_takes_priority_over_preview\n";
}

}  // namespace

int main() {
    test_uses_shared_track_state_when_not_focusing_or_selecting();
    test_track_focus_uses_preview_and_add_slot();
    test_track_selection_takes_priority_over_preview();

    std::cout << "All GlobalTrackNavigationStripModel tests passed\n";
    return 0;
}
