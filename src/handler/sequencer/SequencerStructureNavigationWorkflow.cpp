#include "handler/sequencer/SequencerStructureNavigationWorkflow.hpp"

#include <algorithm>

#include <config/PlatformCompat.hpp>

#include "handler/common/NavigationUtils.hpp"
#include "handler/sequencer/SequencerInteractionPolicyAdapter.hpp"
#include "handler/sequencer/SequencerStructurePageOps.hpp"
#include "state/shared/StructureSlotOps.hpp"
#include "state/sequencer/SequencerContentViewOps.hpp"

namespace core::handler {

namespace structure_slots = core::state::shared;
namespace interaction_policy = core::handler::sequencer::interaction_policy;

namespace {

FLASHMEM uint8_t currentTrackCursor(const core::state::TrackNavigationState& trackUi) {
    return core::state::sequencer::SequencerTrackBankState::clampTrackIndex(
        trackUi.previewTrackIndex.get()
    );
}

FLASHMEM uint8_t currentExistingPage(
    const core::state::sequencer::SequencerState& sequencer
) {
    const uint8_t pageCount = sequencer.activePageCount();
    if (pageCount == 0) return 0;
    return static_cast<uint8_t>(std::min<uint16_t>(
        sequencer.clampPage(sequencer.page.get()),
        static_cast<uint16_t>(pageCount - 1U)
    ));
}

FLASHMEM uint8_t currentActiveContentPage(
    const core::state::sequencer::SequencerState& sequencer
) {
    const uint8_t pageCount =
        core::state::sequencer::activeContentPageCount(sequencer);
    if (pageCount == 0U) return 0U;
    return static_cast<uint8_t>(std::min<uint16_t>(
        core::state::sequencer::normalizeActiveContentPage(
            sequencer,
            sequencer.page.get()
        ),
        static_cast<uint16_t>(pageCount - 1U)
    ));
}

}  // namespace

FLASHMEM SequencerStructureNavigationWorkflow::SequencerStructureNavigationWorkflow(StateRefs state)
    : sequencer_(state.sequencer)
    , tracks_(state.tracks)
    , navigation_focus_(state.navigationFocus)
    , track_ui_(state.trackNavigation)
    , shared_tracks_(state.sharedTracks.get()) {
    track_ui_.syncPreviewTrack(currentActiveTrack());
    sequencer_.structureUi.syncPreviewPage(sequencer_.visiblePage());
    bindStateSync();
}

FLASHMEM bool SequencerStructureNavigationWorkflow::allowsMainBindings() const {
    return interaction_policy::allowsMainSurface(
        sequencer_,
        track_ui_,
        navigation_focus_.get()
    );
}

FLASHMEM bool SequencerStructureNavigationWorkflow::selectionActive() const {
    return track_ui_.selection.active.get() ||
           sequencer_.structureUi.pageSelection.active.get() ||
           sequencer_.structureUi.stepSelection.active.get() ||
           sequencer_.drumSequencer.laneSelection.active;
}

FLASHMEM bool SequencerStructureNavigationWorkflow::selectedItemsAvailable() const {
    if (track_ui_.selection.active.get()) {
        return (
            track_ui_.selection.selectedMask.get() &
            currentTrackEnabledMask()
        ) != 0U;
    }
    if (sequencer_.structureUi.pageSelection.active.get()) {
        const uint8_t pageCount =
            core::state::sequencer::activeContentPageCount(sequencer_);
        return (
            sequencer_.structureUi.pageSelection.selectedMask.get() &
            structure_slots::prefixMask(pageCount)
        ) != 0U;
    }
    if (sequencer_.structureUi.stepSelection.active.get()) {
        const uint8_t length = core::state::sequencer::activeContentLength(sequencer_);
        for (uint16_t step = 0; step < length; ++step) {
            if (sequencer_.structureUi.stepSelection.selected(
                    static_cast<uint8_t>(step)
                )) {
                return true;
            }
        }
        return false;
    }
    if (sequencer_.drumSequencer.laneSelection.active) {
        const auto& drumUi = sequencer_.drumSequencer;
        const uint8_t laneCount = drumUi.drumTrack == nullptr
            ? 0U
            : std::min<uint8_t>(
                  drumUi.drumTrack->kit.laneCount,
                  core::state::sequencer::DRUM_MAX_LANES
              );
        const uint16_t activeMask = laneCount >= 16U
            ? 0xFFFFU
            : laneCount == 0U
                ? 0U
                : static_cast<uint16_t>((uint16_t{1U} << laneCount) - 1U);
        return (drumUi.laneSelection.selectedMask & activeMask) != 0U;
    }
    return false;
}

FLASHMEM bool SequencerStructureNavigationWorkflow::stepFocusActive() const {
    return navigation_focus_.get() == core::state::StructureNavigationFocus::STEP;
}

FLASHMEM void SequencerStructureNavigationWorkflow::moveByFocus(float delta) {
    switch (navigation_focus_.get()) {
        case core::state::StructureNavigationFocus::TRACK:
            moveTrack(delta);
            return;
        case core::state::StructureNavigationFocus::STEP:
            moveStep(delta);
            return;
        case core::state::StructureNavigationFocus::PAGE:
        default:
            movePage(delta);
            return;
    }
}

FLASHMEM void SequencerStructureNavigationWorkflow::setNavigationFocus(
    core::state::StructureNavigationFocus focus
) {
    if (selectionActive()) return;
    syncPreviewToFocus(focus);
    navigation_focus_.set(focus);
}

FLASHMEM void SequencerStructureNavigationWorkflow::enterSelectionModeForCurrentFocus() {
    if (selectionActive()) return;

    track_ui_.previewAddSlot.set(false);

    switch (navigation_focus_.get()) {
        case core::state::StructureNavigationFocus::TRACK: {
            auto& selection = track_ui_.selection;
            const uint8_t cursor = currentActiveTrack();
            selection.reset(core::state::StructureSelectionScope::TRACK, cursor);
            selection.active.set(true);
            return;
        }
        case core::state::StructureNavigationFocus::PAGE: {
            auto& drumUi = sequencer_.drumSequencer;
            if (core::state::sequencer::isDrumOverviewActive(sequencer_) &&
                drumUi.drumTrack != nullptr &&
                drumUi.drumTrack->kit.laneCount > 0U &&
                !drumUi.laneAddSlotFocused()) {
                auto& selection = drumUi.laneSelection;
                selection.reset(drumUi.selectedLane);
                selection.active = true;
                drumUi.bump();
                return;
            }
            auto& selection = sequencer_.structureUi.pageSelection;
            const uint8_t cursor = currentActiveContentPage(sequencer_);
            selection.reset(core::state::StructureSelectionScope::PAGE, cursor);
            selection.active.set(true);
            return;
        }
        case core::state::StructureNavigationFocus::STEP:
        default: {
            auto& selection = sequencer_.structureUi.stepSelection;
            const uint8_t cursor = std::min<uint8_t>(
                sequencer_.focusedStep.get(),
                maxStepCursor()
            );
            selection.reset(cursor);
            selection.active.set(true);
            return;
        }
    }
}

FLASHMEM bool SequencerStructureNavigationWorkflow::backSelectionMode() {
    const auto focus = navigation_focus_.get();
    if (sequencer_.drumSequencer.laneSelection.active) {
        auto& drumUi = sequencer_.drumSequencer;
        auto& selection = drumUi.laneSelection;
        if (selection.moving) {
            selection.moving = false;
            selection.pasteBlocked = false;
            selection.destinationMask = 0U;
            selection.overwriteMask = 0U;
            const uint16_t selectedMask = selection.selectedMask;
            for (uint8_t lane = 0U;
                 lane < core::state::sequencer::DRUM_MAX_LANES;
                 ++lane) {
                if ((selectedMask & static_cast<uint16_t>(1U << lane)) == 0U) {
                    continue;
                }
                selection.cursorLane = lane;
                drumUi.selectedLane = lane;
                break;
            }
            drumUi.ensureSelectedLaneVisible();
        } else if (selection.placing) {
            selection.placing = false;
            selection.pasteBlocked = false;
            selection.destinationMask = 0U;
            selection.overwriteMask = 0U;
            selection.clipboardRevision = 0U;
            selection.clipboardCount = 0U;
        } else if (selection.anySelected()) {
            selection.clearCurrent();
        } else {
            const uint8_t cursor = drumUi.selectedLane;
            selection.reset(cursor);
        }
        drumUi.bump();
        return true;
    }
    if (track_ui_.selection.active.get()) {
        auto& selection = track_ui_.selection;
        if (selection.placing.get() || selection.anySelected()) {
            selection.clearCurrent();
            track_ui_.syncPreviewTrack(selection.cursorIndex.get());
        } else {
            selection.reset(
                core::state::StructureSelectionScope::TRACK,
                currentActiveTrack()
            );
            syncPreviewToFocus(focus);
        }
        return true;
    }
    if (sequencer_.structureUi.pageSelection.active.get()) {
        auto& selection = sequencer_.structureUi.pageSelection;
        if (selection.placing.get() || selection.anySelected()) {
            selection.clearCurrent();
            sequencer_.structureUi.syncPreviewPage(
                selection.cursorIndex.get()
            );
        } else {
            selection.reset(
                core::state::StructureSelectionScope::PAGE,
                currentActiveContentPage(sequencer_)
            );
            syncPreviewToFocus(focus);
        }
        return true;
    }
    if (sequencer_.structureUi.stepSelection.active.get()) {
        auto& selection = sequencer_.structureUi.stepSelection;
        if (selection.placementActive() ||
            selection.anySelected() ||
            selection.pastePreviewActive.get()) {
            selection.clearCurrent();
        } else {
            const uint8_t cursor = std::min<uint8_t>(
                sequencer_.focusedStep.get(),
                maxStepCursor()
            );
            selection.reset(cursor);
            syncPreviewToFocus(focus);
        }
        return true;
    }
    return false;
}

FLASHMEM void SequencerStructureNavigationWorkflow::toggleSelectionAtCursor() {
    if (sequencer_.drumSequencer.laneSelection.active) {
        auto& drumUi = sequencer_.drumSequencer;
        auto& selection = drumUi.laneSelection;
        if (selection.placing || selection.moving ||
            drumUi.drumTrack == nullptr) return;
        const uint8_t laneCount = std::min<uint8_t>(
            drumUi.drumTrack->kit.laneCount,
            core::state::sequencer::DRUM_MAX_LANES
        );
        if (selection.cursorLane >= laneCount) return;
        selection.selectedMask = static_cast<uint16_t>(
            selection.selectedMask ^
            static_cast<uint16_t>(1U << selection.cursorLane)
        );
        drumUi.bump();
        return;
    }
    if (track_ui_.selection.active.get()) {
        auto& selection = track_ui_.selection;
        if (selection.placing.get()) return;
        const uint8_t cursor = selection.cursorIndex.get();
        if (!structure_slots::isEnabled(currentTrackEnabledMask(), cursor)) return;
        selection.selectedMask.set(static_cast<uint16_t>(
            selection.selectedMask.get() ^ structure_slots::slotBit(cursor)
        ));
        return;
    }
    if (sequencer_.structureUi.pageSelection.active.get()) {
        auto& selection = sequencer_.structureUi.pageSelection;
        if (selection.placing.get()) return;
        const uint8_t cursor = selection.cursorIndex.get();
        const uint8_t pageCount =
            core::state::sequencer::activeContentPageCount(sequencer_);
        if (cursor >= pageCount) return;
        selection.selectedMask.set(static_cast<uint16_t>(
            selection.selectedMask.get() ^ structure_slots::slotBit(cursor)
        ));
        return;
    }
    if (sequencer_.structureUi.stepSelection.active.get()) {
        auto& selection = sequencer_.structureUi.stepSelection;
        if (selection.placementActive()) return;
        const uint8_t cursor = selection.cursorStep.get();
        if (!stepSelectable(cursor)) return;
        selection.setSelected(cursor, !selection.selected(cursor));
    }
}

FLASHMEM void SequencerStructureNavigationWorkflow::toggleStepSelectionAtVisibleIndex(
    uint8_t indexInPage
) {
    auto& selection = sequencer_.structureUi.stepSelection;
    if (!selection.active.get() || indexInPage >= core::state::sequencer::SequencerState::STEPS_PER_PAGE) {
        return;
    }
    if (selection.placementActive()) return;

    const uint16_t step = static_cast<uint16_t>(
        sequencer_.page.get() * core::state::sequencer::SequencerState::STEPS_PER_PAGE +
        indexInPage
    );
    if (step > maxStepCursor()) return;
    const auto target = static_cast<uint8_t>(step);
    if (!stepSelectable(target)) return;

    selection.cursorStep.set(target);
    selection.setSelected(target, !selection.selected(target));
}

FLASHMEM void SequencerStructureNavigationWorkflow::navigateSelection(float delta) {
    if (!nav::hasTurnDelta(delta)) return;

    const int direction = nav::turnStep(delta);
    if (sequencer_.drumSequencer.laneSelection.active) {
        auto& drumUi = sequencer_.drumSequencer;
        auto& selection = drumUi.laneSelection;
        if (drumUi.drumTrack == nullptr) return;
        const uint8_t laneCount = std::min<uint8_t>(
            drumUi.drumTrack->kit.laneCount,
            core::state::sequencer::DRUM_MAX_LANES
        );
        if (laneCount == 0U) return;
        selection.cursorLane = structure_slots::wrapIndex(
            selection.cursorLane,
            direction,
            laneCount
        );
        drumUi.selectedLane = selection.cursorLane;
        drumUi.laneAddSlotSelected = false;
        if (selection.moving) {
            selection.destinationMask = static_cast<uint16_t>(
                1U << selection.cursorLane
            );
            selection.overwriteMask = 0U;
            selection.pasteBlocked = false;
        }
        drumUi.ensureSelectedLaneVisible();
        drumUi.bump();
        return;
    }
    if (track_ui_.selection.active.get()) {
        auto& selection = track_ui_.selection;
        if (selection.placing.get()) {
            const uint8_t next = structure_slots::wrapIndex(
                selection.cursorIndex.get(),
                direction,
                core::state::sequencer::SequencerTrackBankState::TRACK_COUNT
            );
            selection.cursorIndex.set(next);
            track_ui_.syncPreviewTrack(next);
            return;
        }
        const uint16_t enabledMask = currentTrackEnabledMask();
        if (enabledMask == 0U) return;
        const uint8_t next = structure_slots::nextEnabledIndex(
            enabledMask,
            selection.cursorIndex.get(),
            core::state::sequencer::SequencerTrackBankState::TRACK_COUNT,
            direction
        );
        selection.cursorIndex.set(next);
        return;
    }
    if (sequencer_.structureUi.pageSelection.active.get()) {
        auto& selection = sequencer_.structureUi.pageSelection;
        if (selection.placing.get()) {
            const uint8_t next = structure_slots::wrapIndex(
                selection.cursorIndex.get(),
                direction,
                core::state::sequencer::SequencerPatternState::PAGE_COUNT
            );
            selection.cursorIndex.set(next);
            sequencer_.structureUi.syncPreviewPage(next);
            const uint8_t pageCount =
                core::state::sequencer::activeContentPageCount(sequencer_);
            if (next < pageCount) {
                sequencer_.page.set(next);
                sequencer_.focusedStep.set(
                    core::state::sequencer::activeContentPageStartStep(
                        sequencer_,
                        next
                    )
                );
            }
            return;
        }
        const uint8_t pageCount =
            core::state::sequencer::activeContentPageCount(sequencer_);
        if (pageCount == 0U) return;
        const uint8_t next = structure_slots::wrapIndex(
            selection.cursorIndex.get(),
            direction,
            pageCount
        );
        selection.cursorIndex.set(next);
        sequencer_.page.set(next);
        sequencer_.focusedStep.set(
            core::state::sequencer::activeContentPageStartStep(sequencer_, next)
        );
        return;
    }
    if (sequencer_.structureUi.stepSelection.active.get()) {
        auto& selection = sequencer_.structureUi.stepSelection;
        const uint8_t current = selection.cursorStep.get();
        const uint8_t maxCursor = maxStepCursor();
        const int next = static_cast<int>(current) + direction;
        uint8_t wrapped = 0;
        if (next < 0) {
            wrapped = maxCursor;
        } else if (next > static_cast<int>(maxCursor)) {
            wrapped = 0;
        } else {
            wrapped = static_cast<uint8_t>(next);
        }
        selection.cursorStep.set(wrapped);
        sequencer_.focusedStep.set(wrapped);
        sequencer_.page.set(
            core::state::sequencer::activeContentPageForStep(wrapped)
        );
    }
}

FLASHMEM void SequencerStructureNavigationWorkflow::bindStateSync() {
    subscriptions_.push_back(
        tracks_.activeTrackSignal().subscribe([this](uint8_t activeTrack) {
            syncTrackPreviewFromActive(activeTrack);
        })
    );

    subscriptions_.push_back(
        sequencer_.page.subscribe([this](uint8_t pageIndex) {
            if (sequencer_.structureUi.pageSelection.placementActive()) {
                return;
            }
            sequencer_.structureUi.syncPreviewPage(sequencer_.clampPage(pageIndex));
        })
    );
}

FLASHMEM void SequencerStructureNavigationWorkflow::syncTrackPreviewFromActive(
    uint8_t activeTrack
) {
    if (track_ui_.hold.active()) return;
    track_ui_.syncPreviewTrack(activeTrack);
}

FLASHMEM void SequencerStructureNavigationWorkflow::movePage(float delta) {
    if (!nav::hasTurnDelta(delta)) return;
    const uint8_t pageCount = sequencer_.activePageCount();
    if (pageCount == 0) return;
    const uint8_t next = structure_slots::wrapIndex(
        currentExistingPage(sequencer_),
        nav::turnStep(delta),
        pageCount
    );
    setPagePreview(next);
}

FLASHMEM void SequencerStructureNavigationWorkflow::moveTrack(float delta) {
    if (!nav::hasTurnDelta(delta)) return;
    const uint16_t enabledMask = currentTrackEnabledMask();
    if (enabledMask == 0U) return;
    const uint8_t next = structure_slots::wrapIndex(
        currentTrackCursor(track_ui_),
        nav::turnStep(delta),
        core::state::sequencer::SequencerTrackBankState::TRACK_COUNT
    );
    setTrackPreview(
        next,
        !structure_slots::isEnabled(enabledMask, next)
    );
}

FLASHMEM void SequencerStructureNavigationWorkflow::moveStep(float delta) {
    if (!nav::hasTurnDelta(delta)) return;

    const uint8_t length = core::state::sequencer::activeContentLength(sequencer_);
    if (length == 0) return;

    const uint8_t current = std::min<uint8_t>(
        sequencer_.focusedStep.get(),
        static_cast<uint8_t>(length - 1U)
    );
    const int next = static_cast<int>(current) + nav::turnStep(delta);
    uint8_t wrapped = 0;
    if (next < 0) {
        wrapped = static_cast<uint8_t>(length - 1U);
    } else if (next >= length) {
        wrapped = 0;
    } else {
        wrapped = static_cast<uint8_t>(next);
    }

    sequencer_.focusedStep.set(wrapped);
    sequencer_.page.set(core::state::sequencer::activeContentPageForStep(wrapped));
}

FLASHMEM void SequencerStructureNavigationWorkflow::setPagePreview(
    uint8_t pageIndex
) {
    const uint8_t clampedPage = sequencer_.clampPage(pageIndex);
    sequencer_.structureUi.syncPreviewPage(clampedPage);
    sequencer_.page.set(clampedPage);
    sequencer_.focusedStep.set(sequencer_.pageStartStep(clampedPage));
}

FLASHMEM void SequencerStructureNavigationWorkflow::setTrackPreview(
    uint8_t trackIndex,
    bool addSlot
) {
    const uint8_t clampedTrack =
        core::state::sequencer::SequencerTrackBankState::clampTrackIndex(trackIndex);
    track_ui_.syncPreviewTrack(clampedTrack);
    track_ui_.previewAddSlot.set(addSlot);
    if (!addSlot) {
        applyTrackState(currentTrackEnabledMask(), clampedTrack);
    }
}

FLASHMEM uint8_t SequencerStructureNavigationWorkflow::maxStepCursor() const {
    if (core::state::sequencer::isMicroSequenceContentView(sequencer_)) {
        return static_cast<uint8_t>(
            oc::note::sequencer::StepSequencerGraphLimits::MAX_EXPANDED_NOTES_PER_ROOT_STEP - 1U
        );
    }
    if (core::state::sequencer::isCycleStatesContentView(sequencer_)) {
        return static_cast<uint8_t>(
            oc::note::sequencer::StepSequencerGraphLimits::MAX_CYCLE_STATES_PER_SET - 1U
        );
    }
    return static_cast<uint8_t>(core::state::sequencer::SequencerState::MAX_STEPS - 1U);
}

FLASHMEM uint8_t SequencerStructureNavigationWorkflow::maxStepPage() const {
    return core::state::sequencer::activeContentPageForStep(maxStepCursor());
}

FLASHMEM bool SequencerStructureNavigationWorkflow::stepSelectable(uint8_t step) const {
    return step < core::state::sequencer::activeContentLength(sequencer_);
}

FLASHMEM void SequencerStructureNavigationWorkflow::syncPreviewToFocus(
    core::state::StructureNavigationFocus focus
) {
    track_ui_.previewAddSlot.set(false);
    track_ui_.syncPreviewTrack(currentActiveTrack());
    if (focus == core::state::StructureNavigationFocus::STEP) {
        sequencer_.structureUi.syncPreviewPage(
            std::min<uint8_t>(sequencer_.page.get(), maxStepPage())
        );
        return;
    }
    syncSequencerPagePreviewToVisible(sequencer_, false);
}

FLASHMEM uint16_t SequencerStructureNavigationWorkflow::currentTrackEnabledMask() const {
    return shared_tracks_.enabledMask();
}

FLASHMEM uint8_t SequencerStructureNavigationWorkflow::currentActiveTrack() const {
    return shared_tracks_.activeTrack();
}

FLASHMEM bool SequencerStructureNavigationWorkflow::applyTrackState(
    uint16_t enabledMask,
    uint8_t activeTrack
) {
    return shared_tracks_.setState(enabledMask, activeTrack);
}

}  // namespace core::handler
