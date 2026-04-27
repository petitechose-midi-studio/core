#include "handler/sequencer/SequencerStructureNavigationWorkflow.hpp"

#include <algorithm>

#include "handler/common/NavigationUtils.hpp"
#include "state/shared/StructureSlotOps.hpp"
#include "state/sequencer/SequencerSnapshotOps.hpp"
#include "state/sequencer/SequencerTrackBankOps.hpp"

namespace core::handler {

namespace structure_slots = core::state::shared;

namespace {

bool isPageSlotEnabled(const core::state::sequencer::SequencerState& sequencer, uint8_t index) {
    return index < sequencer.activePageCount();
}

uint8_t currentPageCursor(const core::state::sequencer::SequencerState& sequencer) {
    if (sequencer.structureUi.previewAddPageSlot.get()) {
        return sequencer.clampPage(sequencer.structureUi.previewPageIndex.get());
    }
    return sequencer.visiblePage();
}

uint8_t currentTrackCursor(const core::state::TrackNavigationState& trackUi) {
    return core::state::sequencer::SequencerTrackBankState::clampTrackIndex(
        trackUi.previewTrackIndex.get()
    );
}

uint8_t currentExistingPage(const core::state::sequencer::SequencerState& sequencer) {
    const uint8_t pageCount = sequencer.activePageCount();
    if (pageCount == 0) return 0;
    return static_cast<uint8_t>(std::min<uint16_t>(
        sequencer.clampPage(sequencer.page.get()),
        static_cast<uint16_t>(pageCount - 1U)
    ));
}

uint8_t nextVisiblePage(const core::state::sequencer::SequencerState& sequencer,
                        uint8_t current,
                        int direction) {
    const uint8_t pageCount = sequencer.activePageCount();
    if (pageCount == 0) return 0;
    const int next =
        (static_cast<int>(current) + direction + static_cast<int>(pageCount)) %
        static_cast<int>(pageCount);
    return static_cast<uint8_t>(next);
}

}  // namespace

SequencerStructureNavigationWorkflow::SequencerStructureNavigationWorkflow(StateRefs state)
    : sequencer_(state.sequencer)
    , tracks_(state.tracks)
    , navigation_focus_(state.navigationFocus)
    , track_ui_(state.trackNavigation)
    , shared_tracks_(state.sharedTracks) {
    track_ui_.syncPreviewTrack(currentActiveTrack());
    sequencer_.structureUi.syncPreviewPage(sequencer_.visiblePage());
    bindStateSync();
}

bool SequencerStructureNavigationWorkflow::allowsMainBindings() const {
    return !sequencer_.structureUi.pageSelection.active.get() &&
           !track_ui_.selection.active.get() &&
           !sequencer_.stepPropertyInlineSelector.selecting.get() &&
           !sequencer_.patternQuickControls.selecting.get();
}

bool SequencerStructureNavigationWorkflow::selectionActive() const {
    return sequencer_.structureUi.pageSelection.active.get() || track_ui_.selection.active.get();
}

bool SequencerStructureNavigationWorkflow::previewingAddSlot() const {
    return navigation_focus_.get() == core::state::StructureNavigationFocus::TRACK
        ? track_ui_.previewAddSlot.get()
        : sequencer_.structureUi.previewAddPageSlot.get();
}

void SequencerStructureNavigationWorkflow::moveByFocus(float delta) {
    switch (navigation_focus_.get()) {
        case core::state::StructureNavigationFocus::TRACK:
            moveTrack(delta);
            return;
        case core::state::StructureNavigationFocus::PAGE:
        default:
            movePage(delta);
            return;
    }
}

void SequencerStructureNavigationWorkflow::cycleNavigationFocus() {
    const auto current = navigation_focus_.get();
    const auto next = (current == core::state::StructureNavigationFocus::PAGE)
        ? core::state::StructureNavigationFocus::TRACK
        : core::state::StructureNavigationFocus::PAGE;
    if (current == core::state::StructureNavigationFocus::PAGE &&
        sequencer_.structureUi.previewAddPageSlot.get()) {
        const uint8_t page = currentExistingPage(sequencer_);
        sequencer_.page.set(page);
        sequencer_.focusedStep.set(sequencer_.pageStartStepClamped(page));
    }
    syncPreviewToFocus(next);
    if (next == core::state::StructureNavigationFocus::PAGE) {
        sequencer_.structureUi.syncPreviewPage(currentExistingPage(sequencer_));
    }
    navigation_focus_.set(next);
}

void SequencerStructureNavigationWorkflow::enterSelectionModeForCurrentFocus() {
    const auto scope = core::state::selectionScopeForFocus(navigation_focus_.get());
    auto& selection = scope == core::state::StructureSelectionScope::TRACK
        ? track_ui_.selection
        : sequencer_.structureUi.pageSelection;
    if (selection.active.get()) return;
    if (scope == core::state::StructureSelectionScope::PAGE &&
        sequencer_.structureUi.previewAddPageSlot.get()) {
        const uint8_t page = currentExistingPage(sequencer_);
        sequencer_.page.set(page);
        sequencer_.focusedStep.set(sequencer_.pageStartStepClamped(page));
    }
    track_ui_.previewAddSlot.set(false);
    sequencer_.structureUi.previewAddPageSlot.set(false);

    const uint8_t cursor = cursorForSelectionScope(scope);

    selection.active.set(true);
    selection.scope.set(scope);
    selection.cursorIndex.set(cursor);
    selection.selectedMask.set(0);
    navigation_focus_.set(
        scope == core::state::StructureSelectionScope::TRACK
            ? core::state::StructureNavigationFocus::TRACK
            : core::state::StructureNavigationFocus::PAGE
    );
}

void SequencerStructureNavigationWorkflow::cancelSelectionMode() {
    auto& selection = track_ui_.selection.active.get() ? track_ui_.selection
                                                       : sequencer_.structureUi.pageSelection;
    const auto scope = selection.scope.get();
    const uint8_t cursor = cursorForSelectionScope(scope);
    selection.reset(scope, cursor);
    syncPreviewToFocus(
        scope == core::state::StructureSelectionScope::TRACK
            ? core::state::StructureNavigationFocus::TRACK
            : core::state::StructureNavigationFocus::PAGE
    );
}

void SequencerStructureNavigationWorkflow::toggleSelectionAtCursor() {
    auto& selection = track_ui_.selection.active.get() ? track_ui_.selection
                                                       : sequencer_.structureUi.pageSelection;
    if (!selection.active.get()) return;

    const uint8_t cursor = selection.cursorIndex.get();
    bool selectable = false;
    if (selection.scope.get() == core::state::StructureSelectionScope::TRACK) {
        selectable = (currentTrackEnabledMask() & static_cast<uint16_t>(1U << cursor)) != 0;
    } else {
        selectable = cursor < sequencer_.activePageCount();
    }
    if (!selectable) return;

    const uint16_t bit = static_cast<uint16_t>(1U << cursor);
    uint16_t selectedMask = selection.selectedMask.get();
    if ((selectedMask & bit) != 0) {
        selectedMask &= static_cast<uint16_t>(~bit);
    } else {
        selectedMask |= bit;
    }
    selection.selectedMask.set(selectedMask);
}

void SequencerStructureNavigationWorkflow::navigateSelection(float delta) {
    auto& selection = track_ui_.selection.active.get() ? track_ui_.selection
                                                       : sequencer_.structureUi.pageSelection;
    if (!selection.active.get()) return;
    if (!nav::hasTurnDelta(delta)) return;

    const int direction = nav::turnStep(delta);
    const uint8_t current = selection.cursorIndex.get();
    uint8_t next = current;

    if (selection.scope.get() == core::state::StructureSelectionScope::TRACK) {
        next = structure_slots::nextEnabledIndex(
            currentTrackEnabledMask(),
            current,
            core::state::sequencer::SequencerTrackBankState::TRACK_COUNT,
            direction
        );
    } else {
        next = nextVisiblePage(sequencer_, current, direction);
    }

    selection.cursorIndex.set(next);
}

void SequencerStructureNavigationWorkflow::createPreviewedStructure() {
    const auto focus = navigation_focus_.get();
    switch (focus) {
        case core::state::StructureNavigationFocus::TRACK:
            createTrack();
            break;
        case core::state::StructureNavigationFocus::PAGE:
        default:
            createPage();
            break;
    }

    syncPreviewToFocus(focus);
}

void SequencerStructureNavigationWorkflow::bindStateSync() {
    subscriptions_.reserve(2);

    subscriptions_.push_back(
        tracks_.activeTrackSignal().subscribe([this](uint8_t activeTrack) {
            track_ui_.syncPreviewTrack(activeTrack);
        })
    );

    subscriptions_.push_back(
        sequencer_.page.subscribe([this](uint8_t pageIndex) {
            sequencer_.structureUi.syncPreviewPage(sequencer_.clampPage(pageIndex));
        })
    );
}

void SequencerStructureNavigationWorkflow::movePage(float delta) {
    if (!nav::hasTurnDelta(delta)) return;
    const uint8_t next = structure_slots::wrapIndex(
        currentPageCursor(sequencer_),
        nav::turnStep(delta),
        core::state::sequencer::SequencerState::PAGE_COUNT
    );
    const bool enabled = isPageSlotEnabled(sequencer_, next);

    setPagePreview(next, !enabled);
}

void SequencerStructureNavigationWorkflow::moveTrack(float delta) {
    if (!nav::hasTurnDelta(delta)) return;
    const uint16_t enabledMask = currentTrackEnabledMask();
    const uint8_t next = structure_slots::wrapIndex(
        currentTrackCursor(track_ui_),
        nav::turnStep(delta),
        core::state::sequencer::SequencerTrackBankState::TRACK_COUNT
    );
    const bool enabled = structure_slots::isEnabled(enabledMask, next);

    setTrackPreview(next, !enabled);
}

bool SequencerStructureNavigationWorkflow::createPage() {
    const uint8_t targetPage = sequencer_.structureUi.previewAddPageSlot.get()
        ? sequencer_.clampPage(sequencer_.structureUi.previewPageIndex.get())
        : sequencer_.activePageCount();
    return core::state::sequencer::ensurePageExists(sequencer_, targetPage);
}

bool SequencerStructureNavigationWorkflow::createTrack() {
    const uint8_t index = track_ui_.previewAddSlot.get()
        ? core::state::sequencer::SequencerTrackBankState::clampTrackIndex(
              track_ui_.previewTrackIndex.get()
          )
        : currentActiveTrack();
    if ((currentTrackEnabledMask() & structure_slots::slotBit(index)) != 0) {
        return false;
    }

    core::state::sequencer::storeActiveTrack(tracks_, sequencer_);
    tracks_.track(index).reset();
    tracks_.track(index).midiChannel.set(index);
    return applyTrackState(
        static_cast<uint16_t>(currentTrackEnabledMask() | structure_slots::slotBit(index)),
        index
    );
}

void SequencerStructureNavigationWorkflow::setPagePreview(uint8_t pageIndex, bool addSlot) {
    const uint8_t clampedPage = sequencer_.clampPage(pageIndex);
    sequencer_.structureUi.syncPreviewPage(clampedPage);
    sequencer_.page.set(clampedPage);
    sequencer_.structureUi.previewAddPageSlot.set(addSlot);
    if (!addSlot) {
        sequencer_.focusedStep.set(sequencer_.pageStartStep(clampedPage));
    }
}

void SequencerStructureNavigationWorkflow::setTrackPreview(uint8_t trackIndex, bool addSlot) {
    const uint8_t clampedTrack =
        core::state::sequencer::SequencerTrackBankState::clampTrackIndex(trackIndex);
    track_ui_.syncPreviewTrack(clampedTrack);
    track_ui_.previewAddSlot.set(addSlot);
    if (!addSlot) {
        applyTrackState(currentTrackEnabledMask(), clampedTrack);
    }
}

uint8_t SequencerStructureNavigationWorkflow::cursorForSelectionScope(
    core::state::StructureSelectionScope scope
) const {
    return scope == core::state::StructureSelectionScope::TRACK
        ? currentActiveTrack()
        : sequencer_.visiblePage();
}

void SequencerStructureNavigationWorkflow::syncPreviewToFocus(
    core::state::StructureNavigationFocus /*focus*/
) {
    track_ui_.previewAddSlot.set(false);
    sequencer_.structureUi.previewAddPageSlot.set(false);
    track_ui_.syncPreviewTrack(currentActiveTrack());
    sequencer_.structureUi.syncPreviewPage(sequencer_.visiblePage());
}

uint16_t SequencerStructureNavigationWorkflow::currentTrackEnabledMask() const {
    return shared_tracks_.enabledMask();
}

uint8_t SequencerStructureNavigationWorkflow::currentActiveTrack() const {
    return shared_tracks_.activeTrack();
}

bool SequencerStructureNavigationWorkflow::applyTrackState(uint16_t enabledMask, uint8_t activeTrack) {
    return shared_tracks_.setState(enabledMask, activeTrack);
}

}  // namespace core::handler
