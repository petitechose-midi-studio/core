#include "SequencerStepHandler.hpp"

#include <algorithm>
#include <array>

#include <oc/log/Log.hpp>

#include <config/App.hpp>
#include <config/PlatformCompat.hpp>
#include <config/TimeCompat.hpp>
#include <config/InputIDs.hpp>
#include "handler/common/NavigationUtils.hpp"
#include "state/CoreState.hpp"
#include "state/shared/StructureSlotOps.hpp"
#include "state/sequencer/SequencerSnapshotOps.hpp"
#include "state/sequencer/SequencerTrackBankOps.hpp"

namespace core::handler {

namespace structure_slots = core::state::shared;

namespace {

inline oc::type::IsActiveFn notSelectingStepProperty(
    core::state::sequencer::SequencerState& sequencer,
    core::state::TrackNavigationState& trackUi
) {
    return [&sequencer, &trackUi]() {
        return !sequencer.structureUi.pageSelection.active.get() &&
               !trackUi.selection.active.get() &&
               !sequencer.stepPropertyInlineSelector.selecting.get() &&
               !sequencer.patternQuickControls.selecting.get() &&
               !sequencer.rangeSelection.active();
    };
}

inline oc::type::IsActiveFn selectionActive(
    core::state::sequencer::SequencerState& sequencer,
    core::state::TrackNavigationState& trackUi
) {
    return [&sequencer, &trackUi]() {
        return sequencer.structureUi.pageSelection.active.get() || trackUi.selection.active.get();
    };
}

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

void copyPersistentTrackState(core::state::sequencer::SequencerState& target,
                              const core::state::sequencer::SequencerState& source) {
    core::state::sequencer::SequencerPatternSnapshot snapshot;
    core::state::sequencer::captureSnapshot(source, snapshot);
    core::state::sequencer::applySnapshot(target, snapshot);

    const uint8_t len = target.length.get();
    const uint8_t focused =
        (len == 0)
            ? 0
            : static_cast<uint8_t>(std::min<uint16_t>(source.focusedStep.get(), len - 1U));
    target.focusedStep.set(focused);
    target.page.set(target.pageForStep(focused));
    target.activeStepProperty.set(source.activeStepProperty.get());
}

}  // namespace

SequencerStepHandler::SequencerStepHandler(StateRefs state,
                                           oc::api::EncoderAPI& encoders,
                                           oc::api::ButtonAPI& buttons,
                                           oc::type::ScopeID scopeId)
    : core_state_(state.coreState)
    , sequencer_(state.sequencer)
    , tracks_(state.tracks)
    , track_ui_(state.trackNavigation)
    , navigation_focus_(state.navigationFocus)
    , structure_clipboard_(state.structureClipboard)
    , encoders_(encoders)
    , buttons_(buttons)
    , scope_id_(scopeId) {
    track_ui_.syncPreviewTrack(currentActiveTrack());
    sequencer_.structureUi.syncPreviewPage(sequencer_.visiblePage());
    setupBindings();
    bindStateSync();
}

uint16_t SequencerStepHandler::currentTrackEnabledMask() const {
    return core_state_.currentSharedTrackEnabledMask();
}

uint8_t SequencerStepHandler::currentActiveTrack() const {
    return core_state_.currentSharedActiveTrack();
}

bool SequencerStepHandler::applyTrackState(uint16_t enabledMask, uint8_t activeTrack) {
    return core_state_.setSharedTrackState(enabledMask, activeTrack);
}

void SequencerStepHandler::setPagePreview(uint8_t pageIndex, bool addSlot) {
    const uint8_t clampedPage = sequencer_.clampPage(pageIndex);
    sequencer_.structureUi.syncPreviewPage(clampedPage);
    sequencer_.page.set(clampedPage);
    sequencer_.structureUi.previewAddPageSlot.set(addSlot);
    if (!addSlot) {
        sequencer_.focusedStep.set(sequencer_.pageStartStep(clampedPage));
    }
}

void SequencerStepHandler::setTrackPreview(uint8_t trackIndex, bool addSlot) {
    const uint8_t clampedTrack =
        core::state::sequencer::SequencerTrackBankState::clampTrackIndex(trackIndex);
    track_ui_.syncPreviewTrack(clampedTrack);
    track_ui_.previewAddSlot.set(addSlot);
    if (!addSlot) {
        applyTrackState(currentTrackEnabledMask(), clampedTrack);
    }
}

uint8_t SequencerStepHandler::cursorForSelectionScope(
    core::state::StructureSelectionScope scope
) const {
    return scope == core::state::StructureSelectionScope::TRACK
        ? currentActiveTrack()
        : sequencer_.visiblePage();
}

void SequencerStepHandler::syncPreviewToFocus(core::state::StructureNavigationFocus focus) {
    track_ui_.previewAddSlot.set(false);
    sequencer_.structureUi.previewAddPageSlot.set(false);
    track_ui_.syncPreviewTrack(currentActiveTrack());
    sequencer_.structureUi.syncPreviewPage(sequencer_.visiblePage());
}

void SequencerStepHandler::bindStateSync() {
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

FLASHMEM void SequencerStepHandler::setupBindings() {
    // Toggle step (release = future-proof vs long-press overlays)
    for (uint8_t i = 0; i < Config::MACRO_COUNT; ++i) {
        buttons_.button(Config::MACRO_BUTTONS[i])
            .release()
            .scope(scope_id_)
            .when(notSelectingStepProperty(sequencer_, track_ui_))
            .then([this, i]() { toggleStep(i); });
    }

    // Page navigation + toggle on NAV
    encoders_.encoder(Config::EncoderID::NAV)
        .turn()
        .scope(scope_id_)
        .when(selectionActive(sequencer_, track_ui_))
        .then([this](float delta) { navigateSelection(delta); });

    encoders_.encoder(Config::EncoderID::NAV)
        .turn()
        .scope(scope_id_)
        .when(notSelectingStepProperty(sequencer_, track_ui_))
        .then([this](float delta) {
            switch (navigation_focus_.get()) {
                case core::state::StructureNavigationFocus::TRACK:
                    moveTrack(delta);
                    return;
                case core::state::StructureNavigationFocus::PAGE:
                default:
                    movePage(delta);
                    return;
            }
        });

    buttons_.button(Config::ButtonID::NAV)
        .longPress(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS)
        .scope(scope_id_)
        .when(notSelectingStepProperty(sequencer_, track_ui_))
        .then([this]() {
            nav_long_press_used_ = true;
            enterSelectionMode(core::state::selectionScopeForFocus(
                navigation_focus_.get()
            ));
        });

    buttons_.button(Config::ButtonID::NAV)
        .release()
        .scope(scope_id_)
        .when(selectionActive(sequencer_, track_ui_))
        .then([this]() {
            if (nav_long_press_used_) {
                nav_long_press_used_ = false;
                return;
            }
            toggleSelectionAtCursor();
        });

    buttons_.button(Config::ButtonID::NAV)
        .release()
        .scope(scope_id_)
        .when(notSelectingStepProperty(sequencer_, track_ui_))
        .then([this]() {
            if (nav_long_press_used_) {
                nav_long_press_used_ = false;
                return;
            }
            const bool previewAddSlot =
                navigation_focus_.get() == core::state::StructureNavigationFocus::TRACK
                    ? track_ui_.previewAddSlot.get()
                    : sequencer_.structureUi.previewAddPageSlot.get();
            if (previewAddSlot) {
                createPreviewedStructure();
                return;
            }
            cycleNavigationFocus();
        });

    buttons_.button(Config::ButtonID::LEFT_TOP)
        .release()
        .scope(scope_id_)
        .when(selectionActive(sequencer_, track_ui_))
        .then([this]() { cancelSelectionMode(); });

    buttons_.button(Config::ButtonID::BOTTOM_LEFT)
        .press()
        .scope(scope_id_)
        .when(notSelectingStepProperty(sequencer_, track_ui_))
        .then([this]() {
            if (canRemoveCurrentStructure()) {
                beginHoldAction(core::state::StructureHoldAction::REMOVE);
            }
        });

    buttons_.button(Config::ButtonID::BOTTOM_LEFT)
        .release()
        .scope(scope_id_)
        .when(selectionActive(sequencer_, track_ui_))
        .then([this]() { deleteSelection(); });

    buttons_.button(Config::ButtonID::BOTTOM_LEFT)
        .release()
        .scope(scope_id_)
        .when(notSelectingStepProperty(sequencer_, track_ui_))
        .then([this]() {
            clearHoldAction();
            if (ignore_next_bottom_left_release_) {
                ignore_next_bottom_left_release_ = false;
                return;
            }
            eraseCurrentStructure();
        });

    buttons_.button(Config::ButtonID::BOTTOM_LEFT)
        .longPress(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS)
        .scope(scope_id_)
        .when(notSelectingStepProperty(sequencer_, track_ui_))
        .then([this]() {
            clearHoldAction();
            ignore_next_bottom_left_release_ = true;
            removeCurrentStructure();
        });

    buttons_.button(Config::ButtonID::BOTTOM_RIGHT)
        .press()
        .scope(scope_id_)
        .when(notSelectingStepProperty(sequencer_, track_ui_))
        .then([this]() {
            if (canPasteCurrentStructure()) {
                beginHoldAction(core::state::StructureHoldAction::PASTE);
            }
        });

    buttons_.button(Config::ButtonID::BOTTOM_RIGHT)
        .release()
        .scope(scope_id_)
        .when(selectionActive(sequencer_, track_ui_))
        .then([this]() { duplicateSelection(); });

    buttons_.button(Config::ButtonID::BOTTOM_RIGHT)
        .release()
        .scope(scope_id_)
        .when(notSelectingStepProperty(sequencer_, track_ui_))
        .then([this]() {
            clearHoldAction();
            if (ignore_next_bottom_right_release_) {
                ignore_next_bottom_right_release_ = false;
                return;
            }
            copyCurrentStructure();
        });

    buttons_.button(Config::ButtonID::BOTTOM_RIGHT)
        .longPress(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS)
        .scope(scope_id_)
        .when(notSelectingStepProperty(sequencer_, track_ui_))
        .then([this]() {
            clearHoldAction();
            ignore_next_bottom_right_release_ = true;
            pasteCurrentStructure();
        });
}

void SequencerStepHandler::toggleStep(uint8_t indexInPage) {
    uint8_t abs = 0;
    if (!sequencer_.resolveStepInPage(sequencer_.page.get(), indexInPage, abs)) return;

    sequencer_.focusedStep.set(abs);
    sequencer_.toggle(abs);
}

void SequencerStepHandler::cycleNavigationFocus() {
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

void SequencerStepHandler::movePage(float delta) {
    if (!nav::hasTurnDelta(delta)) return;
    const uint8_t next = structure_slots::wrapIndex(
        currentPageCursor(sequencer_),
        nav::turnStep(delta),
        core::state::sequencer::SequencerState::PAGE_COUNT
    );
    const bool enabled = isPageSlotEnabled(sequencer_, next);

    setPagePreview(next, !enabled);
}

void SequencerStepHandler::moveTrack(float delta) {
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

void SequencerStepHandler::eraseCurrentStructure() {
    if (navigation_focus_.get() == core::state::StructureNavigationFocus::TRACK) {
        if (track_ui_.previewAddSlot.get()) return;
        const uint8_t activeTrack = currentActiveTrack();
        sequencer_.reset();
        sequencer_.midiChannel.set(activeTrack);
        core::state::sequencer::storeActiveTrack(tracks_, sequencer_);
        return;
    }

    if (sequencer_.structureUi.previewAddPageSlot.get()) return;
    const uint8_t start = sequencer_.pageStartStepClamped(sequencer_.visiblePage());
    const uint8_t end = static_cast<uint8_t>(std::min<uint16_t>(
        core::state::sequencer::SequencerState::MAX_STEPS - 1,
        static_cast<uint16_t>(start + core::state::sequencer::SequencerState::STEPS_PER_PAGE - 1)
    ));
    core::state::sequencer::clearStepRange(sequencer_, start, end);
}

void SequencerStepHandler::removeCurrentStructure() {
    if (navigation_focus_.get() == core::state::StructureNavigationFocus::TRACK) {
        if (track_ui_.previewAddSlot.get()) return;
        const auto mutation = structure_slots::removeIndex(
            currentTrackEnabledMask(),
            currentActiveTrack(),
            core::state::sequencer::SequencerTrackBankState::TRACK_COUNT
        );
        if (!mutation.changed) return;
        applyTrackState(mutation.nextMask, mutation.nextActive);
        return;
    }

    if (sequencer_.structureUi.previewAddPageSlot.get()) return;
    const uint8_t pageIndex = sequencer_.visiblePage();
    core::state::sequencer::removePage(sequencer_, pageIndex);
}

bool SequencerStepHandler::canRemoveCurrentStructure() const {
    if (navigation_focus_.get() == core::state::StructureNavigationFocus::TRACK) {
        if (track_ui_.previewAddSlot.get()) return false;
        return structure_slots::countEnabled(
            currentTrackEnabledMask(),
            core::state::sequencer::SequencerTrackBankState::TRACK_COUNT
        ) > 1U;
    }
    if (sequencer_.structureUi.previewAddPageSlot.get()) return false;
    return sequencer_.activePageCount() > 1U;
}

void SequencerStepHandler::copyCurrentStructure() {
    if (navigation_focus_.get() == core::state::StructureNavigationFocus::TRACK) {
        if (track_ui_.previewAddSlot.get()) return;
        core::state::sequencer::SequencerPatternSnapshot snapshot;
        core::state::sequencer::captureSnapshot(sequencer_, snapshot);
        structure_clipboard_.storeSequencerTrack(snapshot);
        return;
    }

    if (sequencer_.structureUi.previewAddPageSlot.get()) return;
    core::state::SequencerPageClipboard clipboard;
    const uint8_t page = sequencer_.visiblePage();
    const uint8_t start = sequencer_.pageStartStepClamped(page);
    const uint8_t len = sequencer_.length.get();
    const uint8_t count = (start >= len)
        ? 0
        : static_cast<uint8_t>(std::min<uint16_t>(
              core::state::sequencer::SequencerState::STEPS_PER_PAGE,
              static_cast<uint16_t>(len - start)
          ));
    if (count == 0) return;

    clipboard.valid = true;
    clipboard.count = count;
    for (uint8_t i = 0; i < count; ++i) {
        const uint8_t step = static_cast<uint8_t>(start + i);
        clipboard.note[i] = sequencer_.note[step];
        clipboard.velocity[i] = sequencer_.velocity[step];
        clipboard.gate[i] = sequencer_.gate[step];
        clipboard.nudge[i] = sequencer_.nudge[step];
        clipboard.probability[i] = sequencer_.probability[step];
        if (sequencer_.isEnabled(step)) {
            clipboard.enabledMask |= static_cast<uint8_t>(1U << i);
        }
    }
    structure_clipboard_.storeSequencerPage(clipboard);
}

void SequencerStepHandler::pasteCurrentStructure() {
    if (navigation_focus_.get() == core::state::StructureNavigationFocus::TRACK) {
        if (!structure_clipboard_.hasSequencerTrack()) return;
        if (track_ui_.previewAddSlot.get() && !createTrack()) return;
        core::state::sequencer::applySnapshot(sequencer_, structure_clipboard_.sequencerTrack);
        core::state::sequencer::storeActiveTrack(tracks_, sequencer_);
        syncPreviewToFocus(core::state::StructureNavigationFocus::TRACK);
        return;
    }

    if (!structure_clipboard_.hasSequencerPage()) return;
    uint8_t targetPage = sequencer_.visiblePage();
    if (sequencer_.structureUi.previewAddPageSlot.get()) {
        targetPage = sequencer_.clampPage(sequencer_.structureUi.previewPageIndex.get());
        if (!createPage()) return;
    }

    const auto& clipboard = structure_clipboard_.sequencerPage;
    const uint8_t targetStart =
        static_cast<uint8_t>(targetPage * core::state::sequencer::SequencerState::STEPS_PER_PAGE);
    const uint8_t targetEnd = static_cast<uint8_t>(std::min<uint16_t>(
        core::state::sequencer::SequencerState::MAX_STEPS - 1,
        static_cast<uint16_t>(targetStart + core::state::sequencer::SequencerState::STEPS_PER_PAGE - 1)
    ));
    core::state::sequencer::clearStepRange(sequencer_, targetStart, targetEnd);
    const uint8_t requiredLength = static_cast<uint8_t>(std::min<uint16_t>(
        core::state::sequencer::SequencerState::MAX_STEPS,
        static_cast<uint16_t>(targetStart + std::max<uint8_t>(clipboard.count, 1))
    ));
    if (sequencer_.length.get() < requiredLength) {
        sequencer_.length.set(requiredLength);
    }
    for (uint8_t i = 0; i < clipboard.count; ++i) {
        const uint8_t step = static_cast<uint8_t>(targetStart + i);
        sequencer_.note[step] = clipboard.note[i];
        sequencer_.velocity[step] = clipboard.velocity[i];
        sequencer_.gate[step] = clipboard.gate[i];
        sequencer_.nudge[step] = clipboard.nudge[i];
        sequencer_.probability[step] = clipboard.probability[i];
        sequencer_.setEnabled(step, clipboard.isEnabled(i));
    }
    sequencer_.bumpStepDataRevision();
    setPagePreview(targetPage, false);
}

bool SequencerStepHandler::canPasteCurrentStructure() const {
    if (navigation_focus_.get() == core::state::StructureNavigationFocus::TRACK) {
        return structure_clipboard_.hasSequencerTrack();
    }
    return structure_clipboard_.hasSequencerPage();
}

void SequencerStepHandler::beginHoldAction(core::state::StructureHoldAction action) {
    if (navigation_focus_.get() == core::state::StructureNavigationFocus::TRACK) {
        track_ui_.hold.begin(action, core::time_compat::millis());
        return;
    }
    sequencer_.structureUi.pageHold.begin(action, core::time_compat::millis());
}

void SequencerStepHandler::clearHoldAction() {
    track_ui_.hold.clear();
    sequencer_.structureUi.pageHold.clear();
}

void SequencerStepHandler::createPreviewedStructure() {
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

void SequencerStepHandler::enterSelectionMode(core::state::StructureSelectionScope scope) {
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

void SequencerStepHandler::cancelSelectionMode() {
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

void SequencerStepHandler::toggleSelectionAtCursor() {
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

void SequencerStepHandler::navigateSelection(float delta) {
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

void SequencerStepHandler::deleteSelection() {
    auto& selection = track_ui_.selection.active.get() ? track_ui_.selection
                                                       : sequencer_.structureUi.pageSelection;
    if (!selection.active.get()) return;

    const uint16_t selectedMask = selection.selectedMask.get();
    if (selectedMask == 0) return;

    bool changed = false;

    if (selection.scope.get() == core::state::StructureSelectionScope::TRACK) {
        const auto mutation = structure_slots::removeSelected(
            currentTrackEnabledMask(),
            selectedMask,
            currentActiveTrack(),
            core::state::sequencer::SequencerTrackBankState::TRACK_COUNT
        );
        if (mutation.changed) {
            changed = applyTrackState(mutation.nextMask, mutation.nextActive);
        }
    } else {
        const uint8_t pageCount = sequencer_.activePageCount();
        const uint8_t deleteCount = structure_slots::countEnabled(selectedMask, pageCount);
        if (deleteCount > 0 && deleteCount < pageCount) {
            for (int page = static_cast<int>(pageCount) - 1; page >= 0; --page) {
                const uint16_t bit = structure_slots::slotBit(static_cast<uint8_t>(page));
                if ((selectedMask & bit) == 0) continue;
                changed = core::state::sequencer::removePage(
                              sequencer_,
                              static_cast<uint8_t>(page)
                          ) || changed;
            }
        }
    }

    if (!changed) return;
    cancelSelectionMode();
}

bool SequencerStepHandler::createPage() {
    const uint8_t targetPage = sequencer_.structureUi.previewAddPageSlot.get()
        ? sequencer_.clampPage(sequencer_.structureUi.previewPageIndex.get())
        : sequencer_.activePageCount();
    return core::state::sequencer::ensurePageExists(sequencer_, targetPage);
}

bool SequencerStepHandler::createTrack() {
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

void SequencerStepHandler::duplicateSelection() {
    auto& selection = track_ui_.selection.active.get() ? track_ui_.selection
                                                       : sequencer_.structureUi.pageSelection;
    if (!selection.active.get()) return;

    const uint16_t selectedMask = selection.selectedMask.get();
    if (selectedMask == 0) return;

    bool changed = false;

    if (selection.scope.get() == core::state::StructureSelectionScope::TRACK) {
        core::state::sequencer::storeActiveTrack(tracks_, sequencer_);
        const auto result = structure_slots::duplicateSelectionIntoFreeSlots(
            currentTrackEnabledMask(),
            selectedMask,
            core::state::sequencer::SequencerTrackBankState::TRACK_COUNT,
            [this](uint8_t source, uint8_t dest) {
                const auto& sourceTrack =
                    (source == currentActiveTrack()) ? sequencer_ : tracks_.track(source);
                copyPersistentTrackState(tracks_.track(dest), sourceTrack);
            }
        );

        if (result.firstDuplicated < core::state::sequencer::SequencerTrackBankState::TRACK_COUNT) {
            changed = applyTrackState(result.nextMask, result.firstDuplicated);
        }
    } else {
        const uint8_t pageCount = sequencer_.activePageCount();
        for (uint8_t page = 0; page < pageCount; ++page) {
            const uint16_t bit = static_cast<uint16_t>(1U << page);
            if ((selectedMask & bit) == 0) continue;
            changed = core::state::sequencer::duplicatePage(sequencer_, page) || changed;
            if (sequencer_.activePageCount() >= core::state::sequencer::SequencerState::PAGE_COUNT) {
                break;
            }
        }
    }

    if (!changed) return;
    cancelSelectionMode();
}

}  // namespace core::handler
