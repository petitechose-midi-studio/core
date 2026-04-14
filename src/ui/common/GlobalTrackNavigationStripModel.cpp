#include "ui/common/GlobalTrackNavigationStripModel.hpp"

#include <algorithm>

namespace core::ui {

namespace structure_slots = core::state::shared;

namespace {

uint8_t clampTrackIndex(uint8_t index) {
    return static_cast<uint8_t>(std::min<uint16_t>(index, TrackNavigationStripProps::TRACK_COUNT - 1U));
}

uint8_t nextAddTrackIndexOrCount(uint16_t enabledMask, uint8_t count) {
    const int next = structure_slots::nextAddIndexAfterHighest(enabledMask, count);
    return next >= 0 ? static_cast<uint8_t>(next) : count;
}

TrackNavigationStripProps buildMacroGlobalTrackNavigationStripProps(const core::state::CoreState& state) {
    TrackNavigationStripProps props;
    const bool selectingTrack =
        state.macroUi.structureSelection.active.get() &&
        state.macroUi.structureSelection.scope.get() == core::state::StructureSelectionScope::TRACK;
    const bool previewAddSlot =
        !state.macroUi.structureSelection.active.get() && state.macroUi.previewAddSlot.get();
    const bool focusingTrack =
        !state.macroUi.structureSelection.active.get() &&
        state.structureNavigationFocus.get() == core::state::StructureNavigationFocus::TRACK;
    const uint16_t enabledMask = state.sharedTrackEnabledMask.get();
    const uint8_t activeTrack = state.sharedTrackActive.get();
    const uint8_t addTrackIndex =
        nextAddTrackIndexOrCount(enabledMask, TrackNavigationStripProps::TRACK_COUNT);

    props.activeTrack = activeTrack;
    props.previewTrack =
        selectingTrack
            ? state.macroUi.structureSelection.cursorIndex.get()
            : ((previewAddSlot && focusingTrack && addTrackIndex < TrackNavigationStripProps::TRACK_COUNT)
                   ? addTrackIndex
                   : (focusingTrack ? clampTrackIndex(state.macroUi.previewTrackIndex.get()) : activeTrack));
    props.addTrackIndex = addTrackIndex;
    props.enabledMask = enabledMask;
    props.selectedMask = selectingTrack ? state.macroUi.structureSelection.selectedMask.get() : 0;
    props.focusingTrack = focusingTrack;
    props.selectingTrack = selectingTrack;
    for (uint8_t i = 0; i < TrackNavigationStripProps::TRACK_COUNT; ++i) {
        props.activity[i] = state.statusBar.trackNoteActivity[i].get();
    }
    return props;
}

TrackNavigationStripProps buildSequencerGlobalTrackNavigationStripProps(const core::state::CoreState& state) {
    TrackNavigationStripProps props;
    const bool selectingTrack =
        state.sequencer.structureUi.selection.active.get() &&
        state.sequencer.structureUi.selection.scope.get() == core::state::StructureSelectionScope::TRACK;
    const bool previewAddSlot =
        !state.sequencer.structureUi.selection.active.get() && state.sequencer.structureUi.previewAddSlot.get();
    const bool focusingTrack =
        !state.sequencer.structureUi.selection.active.get() &&
        state.structureNavigationFocus.get() == core::state::StructureNavigationFocus::TRACK;
    const uint16_t enabledMask = state.sharedTrackEnabledMask.get();
    const uint8_t activeTrack = state.sharedTrackActive.get();
    const uint8_t previewAddIndex =
        previewAddSlot && focusingTrack
            ? clampTrackIndex(state.sequencer.structureUi.previewTrackIndex.get())
            : TrackNavigationStripProps::TRACK_COUNT;

    props.activeTrack = activeTrack;
    props.previewTrack =
        selectingTrack
            ? state.sequencer.structureUi.selection.cursorIndex.get()
            : ((previewAddSlot && focusingTrack && previewAddIndex < TrackNavigationStripProps::TRACK_COUNT)
                   ? previewAddIndex
                   : activeTrack);
    props.addTrackIndex = previewAddIndex;
    props.enabledMask = enabledMask;
    props.selectedMask = selectingTrack ? state.sequencer.structureUi.selection.selectedMask.get() : 0;
    props.focusingTrack = focusingTrack;
    props.selectingTrack = selectingTrack;
    for (uint8_t i = 0; i < TrackNavigationStripProps::TRACK_COUNT; ++i) {
        props.activity[i] = state.statusBar.trackNoteActivity[i].get();
    }
    return props;
}

}  // namespace

TrackNavigationStripProps buildGlobalTrackNavigationStripProps(const core::state::CoreState& state) {
    switch (state.activeView.get()) {
        case core::ui::ViewType::SEQUENCER:
            return buildSequencerGlobalTrackNavigationStripProps(state);
        case core::ui::ViewType::MACRO:
        default:
            return buildMacroGlobalTrackNavigationStripProps(state);
    }
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
