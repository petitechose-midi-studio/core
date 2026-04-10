#include "SequencerStepHandler.hpp"

#include <algorithm>
#include <array>

#include <oc/log/Log.hpp>

#include <config/App.hpp>
#include <config/PlatformCompat.hpp>
#include <config/TimeCompat.hpp>
#include <config/InputIDs.hpp>
#include "handler/common/NavigationUtils.hpp"
#include "state/sequencer/SequencerSnapshotOps.hpp"
#include "state/sequencer/SequencerTrackBankOps.hpp"

namespace core::handler {

namespace {

inline oc::type::IsActiveFn notSelectingStepProperty(
    core::state::sequencer::SequencerState& sequencer,
    core::state::sequencer::SequencerTrackBankState& tracks
) {
    return [&sequencer, &tracks]() {
        return !sequencer.structureUi.selection.active.get() &&
               !sequencer.stepPropertyInlineSelector.selecting.get() &&
               !sequencer.patternQuickControls.selecting.get() &&
               !sequencer.rangeSelection.active();
    };
}

inline oc::type::IsActiveFn selectionActive(core::state::sequencer::SequencerState& sequencer) {
    return [&sequencer]() { return sequencer.structureUi.selection.active.get(); };
}

uint8_t countEnabledTracks(uint16_t enabledMask) {
    uint8_t count = 0;
    for (uint8_t i = 0; i < core::state::sequencer::SequencerTrackBankState::TRACK_COUNT; ++i) {
        if ((enabledMask & static_cast<uint16_t>(1U << i)) != 0) {
            ++count;
        }
    }
    return count;
}

uint8_t nextEnabledTrack(uint16_t enabledMask, uint8_t current, int direction) {
    for (uint8_t offset = 1; offset < core::state::sequencer::SequencerTrackBankState::TRACK_COUNT;
         ++offset) {
        const int candidate =
            (static_cast<int>(current) +
             direction * static_cast<int>(offset) +
             core::state::sequencer::SequencerTrackBankState::TRACK_COUNT) %
            core::state::sequencer::SequencerTrackBankState::TRACK_COUNT;
        const uint16_t bit = static_cast<uint16_t>(1U << static_cast<uint8_t>(candidate));
        if ((enabledMask & bit) != 0) {
            return static_cast<uint8_t>(candidate);
        }
    }
    return current;
}

int nextAvailableIndexAfterHighest(uint16_t enabledMask, uint8_t count) {
    for (int index = static_cast<int>(count) - 1; index >= 0; --index) {
        if ((enabledMask & static_cast<uint16_t>(1U << static_cast<uint8_t>(index))) == 0) {
            continue;
        }
        const int next = index + 1;
        return (next < count) ? next : -1;
    }
    return (count > 0) ? 0 : -1;
}

struct StructureNavTarget {
    uint8_t index = 0;
    bool addSlot = false;
    bool valid = false;
};

StructureNavTarget nextTrackTarget(
    uint16_t enabledMask,
    uint8_t current,
    bool currentAddSlot,
    int direction
) {
    const int addIndex = nextAvailableIndexAfterHighest(
        enabledMask,
        core::state::sequencer::SequencerTrackBankState::TRACK_COUNT
    );
    const uint8_t firstEnabled = nextEnabledTrack(enabledMask, static_cast<uint8_t>(
        core::state::sequencer::SequencerTrackBankState::TRACK_COUNT - 1
    ), 1);
    const uint8_t lastEnabled = nextEnabledTrack(enabledMask, 0, -1);

    if (currentAddSlot) {
        if (direction < 0) {
            return {.index = lastEnabled, .addSlot = false, .valid = true};
        }
        return {
            .index = (addIndex >= 0) ? static_cast<uint8_t>(addIndex) : lastEnabled,
            .addSlot = (addIndex >= 0),
            .valid = true,
        };
    }

    if (direction > 0) {
        for (uint8_t candidate = static_cast<uint8_t>(current + 1);
             candidate < core::state::sequencer::SequencerTrackBankState::TRACK_COUNT;
             ++candidate) {
            if ((enabledMask & static_cast<uint16_t>(1U << candidate)) != 0) {
                return {.index = candidate, .addSlot = false, .valid = true};
            }
        }

        if (addIndex >= 0 && current == lastEnabled) {
            return {
                .index = static_cast<uint8_t>(addIndex),
                .addSlot = true,
                .valid = true,
            };
        }

        return {.index = firstEnabled, .addSlot = false, .valid = true};
    }

    for (int candidate = static_cast<int>(current) - 1; candidate >= 0; --candidate) {
        const auto index = static_cast<uint8_t>(candidate);
        if ((enabledMask & static_cast<uint16_t>(1U << index)) != 0) {
            return {.index = index, .addSlot = false, .valid = true};
        }
    }

    return {.index = lastEnabled, .addSlot = false, .valid = true};
}

StructureNavTarget nextPageTarget(
    uint8_t pageCount,
    uint8_t current,
    bool currentAddSlot,
    int direction
) {
    if (pageCount == 0) return {};

    const bool hasAddSlot = pageCount < core::state::sequencer::SequencerState::PAGE_COUNT;

    if (currentAddSlot) {
        if (direction < 0) {
            return {
                .index = static_cast<uint8_t>(pageCount - 1),
                .addSlot = false,
                .valid = true,
            };
        }
        return {
            .index = pageCount,
            .addSlot = hasAddSlot,
            .valid = true,
        };
    }

    if (direction > 0) {
        if (current + 1 < pageCount) {
            return {
                .index = static_cast<uint8_t>(current + 1),
                .addSlot = false,
                .valid = true,
            };
        }
        if (hasAddSlot) {
            return {
                .index = pageCount,
                .addSlot = true,
                .valid = true,
            };
        }
        return {.index = 0, .addSlot = false, .valid = true};
    }

    if (current > 0) {
        return {
            .index = static_cast<uint8_t>(current - 1),
            .addSlot = false,
            .valid = true,
        };
    }

    return {
        .index = static_cast<uint8_t>(pageCount - 1),
        .addSlot = false,
        .valid = true,
    };
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

uint8_t countSelectedPages(uint16_t mask, uint8_t pageCount) {
    uint8_t count = 0;
    for (uint8_t i = 0; i < pageCount; ++i) {
        if ((mask & static_cast<uint16_t>(1U << i)) != 0) {
            ++count;
        }
    }
    return count;
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
    : sequencer_(state.sequencer)
    , tracks_(state.tracks)
    , navigation_focus_(state.navigationFocus)
    , structure_clipboard_(state.structureClipboard)
    , encoders_(encoders)
    , buttons_(buttons)
    , scope_id_(scopeId) {
    setupBindings();
}

FLASHMEM void SequencerStepHandler::setupBindings() {
    // Toggle step (release = future-proof vs long-press overlays)
    for (uint8_t i = 0; i < Config::MACRO_COUNT; ++i) {
        buttons_.button(Config::MACRO_BUTTONS[i])
            .release()
            .scope(scope_id_)
            .when(notSelectingStepProperty(sequencer_, tracks_))
            .then([this, i]() { toggleStep(i); });
    }

    // Page navigation + toggle on NAV
    encoders_.encoder(Config::EncoderID::NAV)
        .turn()
        .scope(scope_id_)
        .when(selectionActive(sequencer_))
        .then([this](float delta) { navigateSelection(delta); });

    encoders_.encoder(Config::EncoderID::NAV)
        .turn()
        .scope(scope_id_)
        .when(notSelectingStepProperty(sequencer_, tracks_))
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
        .when(notSelectingStepProperty(sequencer_, tracks_))
        .then([this]() {
            nav_long_press_used_ = true;
            enterSelectionMode(core::state::selectionScopeForFocus(
                navigation_focus_.get()
            ));
        });

    buttons_.button(Config::ButtonID::NAV)
        .release()
        .scope(scope_id_)
        .when(selectionActive(sequencer_))
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
        .when(notSelectingStepProperty(sequencer_, tracks_))
        .then([this]() {
            if (nav_long_press_used_) {
                nav_long_press_used_ = false;
                return;
            }
            if (sequencer_.structureUi.previewAddSlot.get()) {
                createPreviewedStructure();
                return;
            }
            cycleNavigationFocus();
        });

    buttons_.button(Config::ButtonID::LEFT_TOP)
        .release()
        .scope(scope_id_)
        .when(selectionActive(sequencer_))
        .then([this]() { cancelSelectionMode(); });

    buttons_.button(Config::ButtonID::BOTTOM_LEFT)
        .press()
        .scope(scope_id_)
        .when(notSelectingStepProperty(sequencer_, tracks_))
        .then([this]() {
            if (canRemoveCurrentStructure()) {
                beginHoldAction(core::state::StructureHoldAction::REMOVE);
            }
        });

    buttons_.button(Config::ButtonID::BOTTOM_LEFT)
        .release()
        .scope(scope_id_)
        .when(selectionActive(sequencer_))
        .then([this]() { deleteSelection(); });

    buttons_.button(Config::ButtonID::BOTTOM_LEFT)
        .release()
        .scope(scope_id_)
        .when(notSelectingStepProperty(sequencer_, tracks_))
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
        .when(notSelectingStepProperty(sequencer_, tracks_))
        .then([this]() {
            clearHoldAction();
            ignore_next_bottom_left_release_ = true;
            removeCurrentStructure();
        });

    buttons_.button(Config::ButtonID::BOTTOM_RIGHT)
        .press()
        .scope(scope_id_)
        .when(notSelectingStepProperty(sequencer_, tracks_))
        .then([this]() {
            if (canPasteCurrentStructure()) {
                beginHoldAction(core::state::StructureHoldAction::PASTE);
            }
        });

    buttons_.button(Config::ButtonID::BOTTOM_RIGHT)
        .release()
        .scope(scope_id_)
        .when(selectionActive(sequencer_))
        .then([this]() { duplicateSelection(); });

    buttons_.button(Config::ButtonID::BOTTOM_RIGHT)
        .release()
        .scope(scope_id_)
        .when(notSelectingStepProperty(sequencer_, tracks_))
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
        .when(notSelectingStepProperty(sequencer_, tracks_))
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
    sequencer_.structureUi.previewAddSlot.set(false);
    navigation_focus_.set(next);
}

void SequencerStepHandler::movePage(float delta) {
    if (!nav::hasTurnDelta(delta)) return;
    const uint8_t pageCount = sequencer_.activePageCount();
    if (pageCount == 0) return;

    const bool currentAddSlot = sequencer_.structureUi.previewAddSlot.get();
    const auto target = nextPageTarget(
        pageCount,
        sequencer_.visiblePage(),
        currentAddSlot,
        nav::turnStep(delta)
    );
    if (!target.valid) return;

    if (target.addSlot) {
        sequencer_.structureUi.previewAddSlot.set(true);
        return;
    }

    sequencer_.structureUi.previewAddSlot.set(false);
    sequencer_.page.set(target.index);
    sequencer_.focusedStep.set(sequencer_.pageStartStep(target.index));
}

void SequencerStepHandler::moveTrack(float delta) {
    if (!nav::hasTurnDelta(delta)) return;
    const uint8_t current = tracks_.activeTrack.get();
    const uint16_t enabledMask = tracks_.enabledMask.get();
    if (countEnabledTracks(enabledMask) == 0) return;

    const bool currentAddSlot = sequencer_.structureUi.previewAddSlot.get();
    const auto target = nextTrackTarget(
        enabledMask,
        current,
        currentAddSlot,
        nav::turnStep(delta)
    );
    if (!target.valid) return;

    if (target.addSlot) {
        sequencer_.structureUi.previewAddSlot.set(true);
        return;
    }

    sequencer_.structureUi.previewAddSlot.set(false);
    core::state::sequencer::switchActiveTrack(
        tracks_,
        sequencer_,
        target.index
    );
}

void SequencerStepHandler::eraseCurrentStructure() {
    if (sequencer_.structureUi.previewAddSlot.get()) return;

    if (navigation_focus_.get() == core::state::StructureNavigationFocus::TRACK) {
        const uint8_t activeTrack = tracks_.activeTrack.get();
        sequencer_.reset();
        sequencer_.midiChannel.set(activeTrack);
        core::state::sequencer::storeActiveTrack(tracks_, sequencer_);
        return;
    }

    const uint8_t start = sequencer_.pageStartStepClamped(sequencer_.visiblePage());
    const uint8_t end = static_cast<uint8_t>(std::min<uint16_t>(
        core::state::sequencer::SequencerState::MAX_STEPS - 1,
        static_cast<uint16_t>(start + core::state::sequencer::SequencerState::STEPS_PER_PAGE - 1)
    ));
    core::state::sequencer::clearStepRange(sequencer_, start, end);
}

void SequencerStepHandler::removeCurrentStructure() {
    if (sequencer_.structureUi.previewAddSlot.get()) return;

    if (navigation_focus_.get() == core::state::StructureNavigationFocus::TRACK) {
        const uint16_t enabledMask = tracks_.enabledMask.get();
        const uint8_t activeTrack = tracks_.activeTrack.get();
        const uint16_t bit = static_cast<uint16_t>(1U << activeTrack);
        if ((enabledMask & bit) == 0 || countEnabledTracks(enabledMask) <= 1U) return;

        uint16_t nextMask = enabledMask & static_cast<uint16_t>(~bit);
        uint8_t nextTrack = activeTrack;
        if ((nextMask & static_cast<uint16_t>(1U << activeTrack)) == 0) {
            nextTrack = nextEnabledTrack(nextMask, activeTrack, 1);
        }
        tracks_.enabledMask.set(nextMask);
        core::state::sequencer::switchActiveTrack(tracks_, sequencer_, nextTrack);
        return;
    }

    const uint8_t pageIndex = sequencer_.visiblePage();
    core::state::sequencer::removePage(sequencer_, pageIndex);
}

bool SequencerStepHandler::canRemoveCurrentStructure() const {
    if (sequencer_.structureUi.previewAddSlot.get()) return false;
    if (navigation_focus_.get() == core::state::StructureNavigationFocus::TRACK) {
        return countEnabledTracks(tracks_.enabledMask.get()) > 1U;
    }
    return sequencer_.activePageCount() > 1U;
}

void SequencerStepHandler::copyCurrentStructure() {
    if (sequencer_.structureUi.previewAddSlot.get()) return;

    if (navigation_focus_.get() == core::state::StructureNavigationFocus::TRACK) {
        core::state::sequencer::SequencerPatternSnapshot snapshot;
        core::state::sequencer::captureSnapshot(sequencer_, snapshot);
        structure_clipboard_.storeSequencerTrack(snapshot);
        return;
    }

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
        if (sequencer_.structureUi.previewAddSlot.get() && !createTrack()) return;
        core::state::sequencer::applySnapshot(sequencer_, structure_clipboard_.sequencerTrack);
        core::state::sequencer::storeActiveTrack(tracks_, sequencer_);
        sequencer_.structureUi.previewAddSlot.set(false);
        return;
    }

    if (!structure_clipboard_.hasSequencerPage()) return;
    uint8_t targetPage = sequencer_.visiblePage();
    if (sequencer_.structureUi.previewAddSlot.get()) {
        targetPage = sequencer_.activePageCount();
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
    sequencer_.page.set(targetPage);
    sequencer_.focusedStep.set(targetStart);
    sequencer_.structureUi.previewAddSlot.set(false);
}

bool SequencerStepHandler::canPasteCurrentStructure() const {
    if (navigation_focus_.get() == core::state::StructureNavigationFocus::TRACK) {
        return structure_clipboard_.hasSequencerTrack();
    }
    return structure_clipboard_.hasSequencerPage();
}

void SequencerStepHandler::beginHoldAction(core::state::StructureHoldAction action) {
    sequencer_.structureUi.hold.begin(action, core::time_compat::millis());
}

void SequencerStepHandler::clearHoldAction() {
    sequencer_.structureUi.hold.clear();
}

void SequencerStepHandler::createPreviewedStructure() {
    switch (navigation_focus_.get()) {
        case core::state::StructureNavigationFocus::TRACK:
            createTrack();
            break;
        case core::state::StructureNavigationFocus::PAGE:
        default:
            createPage();
            break;
    }

    sequencer_.structureUi.previewAddSlot.set(false);
}

void SequencerStepHandler::enterSelectionMode(core::state::StructureSelectionScope scope) {
    auto& selection = sequencer_.structureUi.selection;
    if (selection.active.get()) return;
    sequencer_.structureUi.previewAddSlot.set(false);

    const uint8_t cursor =
        (scope == core::state::StructureSelectionScope::TRACK)
            ? tracks_.activeTrack.get()
            : sequencer_.visiblePage();

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
    sequencer_.structureUi.previewAddSlot.set(false);
    const auto scope = sequencer_.structureUi.selection.scope.get();
    const uint8_t cursor =
        (scope == core::state::StructureSelectionScope::TRACK)
            ? tracks_.activeTrack.get()
            : sequencer_.visiblePage();
    sequencer_.structureUi.selection.reset(scope, cursor);
}

void SequencerStepHandler::toggleSelectionAtCursor() {
    auto& selection = sequencer_.structureUi.selection;
    if (!selection.active.get()) return;

    const uint8_t cursor = selection.cursorIndex.get();
    bool selectable = false;
    if (selection.scope.get() == core::state::StructureSelectionScope::TRACK) {
        selectable = tracks_.isTrackEnabled(cursor);
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
    auto& selection = sequencer_.structureUi.selection;
    if (!selection.active.get()) return;
    if (!nav::hasTurnDelta(delta)) return;

    const int direction = nav::turnStep(delta);
    const uint8_t current = selection.cursorIndex.get();
    uint8_t next = current;

    if (selection.scope.get() == core::state::StructureSelectionScope::TRACK) {
        next = nextEnabledTrack(tracks_.enabledMask.get(), current, direction);
    } else {
        next = nextVisiblePage(sequencer_, current, direction);
    }

    selection.cursorIndex.set(next);
}

void SequencerStepHandler::deleteSelection() {
    auto& selection = sequencer_.structureUi.selection;
    if (!selection.active.get()) return;

    const uint16_t selectedMask = selection.selectedMask.get();
    if (selectedMask == 0) return;

    bool changed = false;

    if (selection.scope.get() == core::state::StructureSelectionScope::TRACK) {
        const uint16_t enabledMask = tracks_.enabledMask.get();
        const uint16_t deleteMask = enabledMask & selectedMask;
        const uint8_t enabledCount = countEnabledTracks(enabledMask);
        const uint8_t deleteCount =
            countEnabledTracks(deleteMask);
        if (deleteMask != 0 && deleteCount < enabledCount) {
            const uint8_t activeTrack = tracks_.activeTrack.get();
            uint16_t nextMask = enabledMask & static_cast<uint16_t>(~deleteMask);
            uint8_t nextTrack = activeTrack;
            if ((nextMask & static_cast<uint16_t>(1U << activeTrack)) == 0) {
                nextTrack = nextEnabledTrack(nextMask, activeTrack, 1);
            }

            tracks_.enabledMask.set(nextMask);
            core::state::sequencer::switchActiveTrack(tracks_, sequencer_, nextTrack);
            changed = true;
        }
    } else {
        const uint8_t pageCount = sequencer_.activePageCount();
        const uint8_t deleteCount = countSelectedPages(selectedMask, pageCount);
        if (deleteCount > 0 && deleteCount < pageCount) {
            for (int page = static_cast<int>(pageCount) - 1; page >= 0; --page) {
                const uint16_t bit = static_cast<uint16_t>(1U << static_cast<uint8_t>(page));
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
    return core::state::sequencer::appendPage(sequencer_);
}

bool SequencerStepHandler::createTrack() {
    const int nextTrack = nextAvailableIndexAfterHighest(
        tracks_.enabledMask.get(),
        core::state::sequencer::SequencerTrackBankState::TRACK_COUNT
    );
    if (nextTrack < 0) return false;

    const uint8_t index = static_cast<uint8_t>(nextTrack);
    core::state::sequencer::storeActiveTrack(tracks_, sequencer_);
    tracks_.track(index).reset();
    tracks_.track(index).midiChannel.set(index);
    tracks_.enabledMask.set(
        tracks_.enabledMask.get() | static_cast<uint16_t>(1U << index)
    );
    return core::state::sequencer::switchActiveTrack(tracks_, sequencer_, index);
}

void SequencerStepHandler::duplicateSelection() {
    auto& selection = sequencer_.structureUi.selection;
    if (!selection.active.get()) return;

    const uint16_t selectedMask = selection.selectedMask.get();
    if (selectedMask == 0) return;

    bool changed = false;

    if (selection.scope.get() == core::state::StructureSelectionScope::TRACK) {
        core::state::sequencer::storeActiveTrack(tracks_, sequencer_);
        uint16_t nextMask = tracks_.enabledMask.get();
        uint8_t firstDuplicatedTrack = core::state::sequencer::SequencerTrackBankState::TRACK_COUNT;

        for (uint8_t source = 0; source < core::state::sequencer::SequencerTrackBankState::TRACK_COUNT;
             ++source) {
            const uint16_t sourceBit = static_cast<uint16_t>(1U << source);
            if ((selectedMask & sourceBit) == 0 || (nextMask & sourceBit) == 0) continue;

            uint8_t dest = core::state::sequencer::SequencerTrackBankState::TRACK_COUNT;
            for (uint8_t candidate = 0;
                 candidate < core::state::sequencer::SequencerTrackBankState::TRACK_COUNT;
                 ++candidate) {
                const uint16_t candidateBit = static_cast<uint16_t>(1U << candidate);
                if ((nextMask & candidateBit) == 0) {
                    dest = candidate;
                    break;
                }
            }
            if (dest >= core::state::sequencer::SequencerTrackBankState::TRACK_COUNT) break;

            const auto& sourceTrack =
                (source == tracks_.activeTrack.get()) ? sequencer_ : tracks_.track(source);
            copyPersistentTrackState(tracks_.track(dest), sourceTrack);
            nextMask |= static_cast<uint16_t>(1U << dest);
            if (firstDuplicatedTrack >= core::state::sequencer::SequencerTrackBankState::TRACK_COUNT) {
                firstDuplicatedTrack = dest;
            }
        }

        if (firstDuplicatedTrack < core::state::sequencer::SequencerTrackBankState::TRACK_COUNT) {
            tracks_.enabledMask.set(nextMask);
            core::state::sequencer::switchActiveTrack(tracks_, sequencer_, firstDuplicatedTrack);
            changed = true;
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

void SequencerStepHandler::prevPage() {
    const uint8_t pageCount = sequencer_.activePageCount();
    if (pageCount <= 1) return;

    const uint8_t current = sequencer_.normalizePage(sequencer_.page.get());
    const uint8_t next = (current == 0) ? (pageCount - 1) : (current - 1);
    sequencer_.page.set(next);
    sequencer_.focusedStep.set(sequencer_.pageStartStep(next));
}

void SequencerStepHandler::nextPage() {
    const uint8_t pageCount = sequencer_.activePageCount();
    if (pageCount <= 1) return;

    const uint8_t current = sequencer_.normalizePage(sequencer_.page.get());
    const uint8_t next = static_cast<uint8_t>((current + 1) % pageCount);
    sequencer_.page.set(next);
    sequencer_.focusedStep.set(sequencer_.pageStartStep(next));
}

}  // namespace core::handler
