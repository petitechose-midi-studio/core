#include "handler/sequencer/SequencerStructureNavigationWorkflow.hpp"

#include <algorithm>
#include <utility>

#include <config/PlatformCompat.hpp>

#include "handler/common/NavigationUtils.hpp"
#include "handler/sequencer/SequencerInteractionPolicyAdapter.hpp"
#include "handler/sequencer/SequencerStructureHistoryUtils.hpp"
#include "handler/sequencer/SequencerStructurePageOps.hpp"
#include "handler/sequencer/SequencerStructureTrackOps.hpp"
#include "state/shared/StructureSlotOps.hpp"
#include "state/sequencer/SequencerContentViewOps.hpp"
#include "state/sequencer/SequencerHistory.hpp"

namespace core::handler {

namespace structure_slots = core::state::shared;
namespace interaction_policy = core::handler::sequencer::interaction_policy;

namespace {

FLASHMEM uint8_t currentPageCursor(const core::state::sequencer::SequencerState& sequencer) {
    if (sequencer.structureUi.previewAddPageSlot.get()) {
        return sequencer.clampPage(sequencer.structureUi.previewPageIndex.get());
    }
    return sequencer.visiblePage();
}

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

}  // namespace

FLASHMEM SequencerStructureNavigationWorkflow::SequencerStructureNavigationWorkflow(StateRefs state)
    : sequencer_(state.sequencer)
    , tracks_(state.tracks)
    , navigation_focus_(state.navigationFocus)
    , track_ui_(state.trackNavigation)
    , shared_tracks_(state.sharedTracks)
    , history_(state.history) {
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
    return interaction_policy::selectionActive(
        sequencer_,
        track_ui_,
        navigation_focus_.get()
    );
}

FLASHMEM bool SequencerStructureNavigationWorkflow::selectedItemsAvailable() const {
    if (sequencer_.structureUi.stepSelection.active.get()) {
        const uint8_t length = core::state::sequencer::activeContentLength(sequencer_);
        for (uint8_t step = 0; step < length; ++step) {
            if (sequencer_.structureUi.stepSelection.selected(step)) return true;
        }
        return false;
    }
    if (track_ui_.selection.active.get()) {
        return (track_ui_.selection.selectedMask.get() & currentTrackEnabledMask()) != 0;
    }
    if (sequencer_.structureUi.pageSelection.active.get()) {
        return (
            sequencer_.structureUi.pageSelection.selectedMask.get() &
            structure_slots::prefixMask(sequencer_.activePageCount())
        ) != 0;
    }
    return false;
}

FLASHMEM bool SequencerStructureNavigationWorkflow::stepFocusActive() const {
    return !structureWorkspaceActive() &&
           navigation_focus_.get() == core::state::StructureNavigationFocus::STEP;
}

FLASHMEM bool SequencerStructureNavigationWorkflow::structureWorkspaceActive() const {
    return sequencer_.structureUi.workspace.active.get();
}

FLASHMEM bool SequencerStructureNavigationWorkflow::previewingAddSlot() const {
    const auto focus = !structureWorkspaceActive() &&
            navigation_focus_.get() == core::state::StructureNavigationFocus::TRACK
        ? core::state::StructureNavigationFocus::PAGE
        : navigation_focus_.get();
    if (focus == core::state::StructureNavigationFocus::STEP) return false;
    return focus == core::state::StructureNavigationFocus::TRACK
        ? track_ui_.previewAddSlot.get()
        : sequencer_.structureUi.previewAddPageSlot.get();
}

FLASHMEM void SequencerStructureNavigationWorkflow::moveByFocus(float delta) {
    if (structureWorkspaceActive()) {
        if (sequencer_.structureUi.workspace.level.get() ==
            core::state::sequencer::SequencerStructureWorkspaceLevel::TRACKS) {
            moveWorkspaceTrack(delta);
        } else {
            moveWorkspacePage(delta);
        }
        return;
    }
    switch (navigation_focus_.get()) {
        case core::state::StructureNavigationFocus::TRACK:
            movePage(delta);
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

FLASHMEM void SequencerStructureNavigationWorkflow::cycleNavigationFocus() {
    const auto current = navigation_focus_.get();
    const auto next = current == core::state::StructureNavigationFocus::STEP
        ? core::state::StructureNavigationFocus::PAGE
        : core::state::StructureNavigationFocus::STEP;
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

FLASHMEM void SequencerStructureNavigationWorkflow::openStructureWorkspace() {
    if (structureWorkspaceActive()) return;

    auto& workspace = sequencer_.structureUi.workspace;
    workspace.callerFocus =
        navigation_focus_.get() == core::state::StructureNavigationFocus::STEP
        ? core::state::StructureNavigationFocus::STEP
        : core::state::StructureNavigationFocus::PAGE;
    workspace.callerTrack = currentActiveTrack();
    workspace.callerPage = sequencer_.visiblePage();
    workspace.callerStep = sequencer_.focusedStep.get();
    workspace.level.set(
        core::state::sequencer::SequencerStructureWorkspaceLevel::TRACKS
    );
    workspace.active.set(true);
    sequencer_.structureUi.previewAddPageSlot.set(false);
    track_ui_.previewAddSlot.set(false);
    track_ui_.syncPreviewTrack(currentActiveTrack());
    navigation_focus_.set(core::state::StructureNavigationFocus::TRACK);
}

FLASHMEM void SequencerStructureNavigationWorkflow::confirmStructureWorkspace() {
    if (!structureWorkspaceActive()) return;

    if (sequencer_.structureUi.workspace.level.get() ==
        core::state::sequencer::SequencerStructureWorkspaceLevel::TRACKS) {
        if (track_ui_.previewAddSlot.get()) {
            createPreviewedStructure();
        }
        enterWorkspacePatternLevel();
        return;
    }

    if (sequencer_.structureUi.previewAddPageSlot.get()) {
        createPreviewedStructure();
    }
    closeStructureWorkspace(false);
    navigation_focus_.set(core::state::StructureNavigationFocus::PAGE);
}

FLASHMEM void SequencerStructureNavigationWorkflow::backStructureWorkspace() {
    if (!structureWorkspaceActive()) return;

    if (sequencer_.structureUi.workspace.level.get() ==
        core::state::sequencer::SequencerStructureWorkspaceLevel::PATTERNS) {
        const uint8_t page = currentExistingPage(sequencer_);
        sequencer_.page.set(page);
        sequencer_.focusedStep.set(sequencer_.pageStartStepClamped(page));
        sequencer_.structureUi.previewAddPageSlot.set(false);
        sequencer_.structureUi.workspace.level.set(
            core::state::sequencer::SequencerStructureWorkspaceLevel::TRACKS
        );
        track_ui_.previewAddSlot.set(false);
        track_ui_.syncPreviewTrack(currentActiveTrack());
        navigation_focus_.set(core::state::StructureNavigationFocus::TRACK);
        return;
    }

    closeStructureWorkspace(true);
}

FLASHMEM void SequencerStructureNavigationWorkflow::enterSelectionModeForCurrentFocus() {
    const auto effectiveFocus = !structureWorkspaceActive() &&
            navigation_focus_.get() == core::state::StructureNavigationFocus::TRACK
        ? core::state::StructureNavigationFocus::PAGE
        : navigation_focus_.get();
    auto scope = core::state::selectionScopeForFocus(effectiveFocus);
    if (core::state::sequencer::isChildContentView(sequencer_)) {
        scope = core::state::StructureSelectionScope::STEP;
    }
    if (scope == core::state::StructureSelectionScope::STEP) {
        auto& selection = sequencer_.structureUi.stepSelection;
        if (selection.active.get()) return;
        track_ui_.previewAddSlot.set(false);
        sequencer_.structureUi.previewAddPageSlot.set(false);
        const uint8_t cursor = cursorForSelectionScope(scope);
        selection.active.set(true);
        selection.cursorStep.set(cursor);
        selection.selectedMask.set({});
        selection.pastePreviewActive.set(false);
        selection.pastePreview.set(core::state::sequencer::SequencerStepPastePreview::NONE);
        navigation_focus_.set(core::state::StructureNavigationFocus::STEP);
        return;
    }

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

FLASHMEM void SequencerStructureNavigationWorkflow::cancelSelectionMode() {
    if (sequencer_.structureUi.stepSelection.active.get()) {
        const uint8_t cursor = cursorForSelectionScope(core::state::StructureSelectionScope::STEP);
        sequencer_.structureUi.stepSelection.reset(cursor);
        syncPreviewToFocus(core::state::StructureNavigationFocus::STEP);
        return;
    }

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

FLASHMEM void SequencerStructureNavigationWorkflow::toggleSelectionAtCursor() {
    if (sequencer_.structureUi.stepSelection.active.get()) {
        auto& selection = sequencer_.structureUi.stepSelection;
        const uint8_t cursor = selection.cursorStep.get();
        if (!stepSelectable(cursor)) return;
        selection.setSelected(cursor, !selection.selected(cursor));
        return;
    }

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

FLASHMEM void SequencerStructureNavigationWorkflow::toggleStepSelectionAtVisibleIndex(
    uint8_t indexInPage
) {
    auto& selection = sequencer_.structureUi.stepSelection;
    if (!selection.active.get() || indexInPage >= core::state::sequencer::SequencerState::STEPS_PER_PAGE) {
        return;
    }

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
    if (sequencer_.structureUi.stepSelection.active.get()) {
        auto& selection = sequencer_.structureUi.stepSelection;
        if (!nav::hasTurnDelta(delta)) return;

        const uint8_t current = selection.cursorStep.get();
        const uint8_t maxCursor = maxStepCursor();
        const int next = static_cast<int>(current) + nav::turnStep(delta);
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
        sequencer_.page.set(core::state::sequencer::activeContentPageForStep(wrapped));
        return;
    }

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
        next = structure_slots::wrapIndex(
            current,
            direction,
            core::state::sequencer::SequencerState::PAGE_COUNT
        );
    }

    selection.cursorIndex.set(next);
}

FLASHMEM void SequencerStructureNavigationWorkflow::createPreviewedStructure() {
    const auto focus = navigation_focus_.get();

    switch (focus) {
        case core::state::StructureNavigationFocus::TRACK: {
            const uint8_t targetTrack = currentTrackCursor(track_ui_);
            const uint16_t historyMask = static_cast<uint16_t>(
                sequencerStructureHistoryTrackBit(currentActiveTrack()) |
                sequencerStructureHistoryTrackBit(targetTrack)
            );
            auto change = captureSequencerTrackStructureHistoryBefore(
                tracks_,
                sequencer_,
                historyMask
            );
            if (!change) break;
            const bool changed =
                createSequencerStructureTrack(sequencer_, tracks_, track_ui_, shared_tracks_);
            if (changed &&
                captureSequencerTrackStructureHistoryAfter(
                    tracks_,
                    sequencer_,
                    historyMask,
                    *change
                )) {
                recordSequencerTrackStructureHistoryChange(history_, std::move(change));
            }
            break;
        }
        case core::state::StructureNavigationFocus::PAGE:
        default: {
            auto change = captureSequencerPageStructureHistoryBefore(sequencer_);
            if (!change) break;
            const bool changed = createSequencerStructurePage(sequencer_);
            if (changed) {
                recordSequencerPageStructureHistoryChange(
                    history_,
                    sequencer_,
                    std::move(change),
                    currentActiveTrack()
                );
            }
            break;
        }
    }

    syncPreviewToFocus(focus);
}

FLASHMEM void SequencerStructureNavigationWorkflow::bindStateSync() {
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

FLASHMEM void SequencerStructureNavigationWorkflow::movePage(float delta) {
    if (!nav::hasTurnDelta(delta)) return;
    const uint8_t pageCount = sequencer_.activePageCount();
    if (pageCount == 0) return;
    const uint8_t next = structure_slots::wrapIndex(
        currentExistingPage(sequencer_),
        nav::turnStep(delta),
        pageCount
    );
    setPagePreview(next, false);
}

FLASHMEM void SequencerStructureNavigationWorkflow::moveWorkspacePage(float delta) {
    if (!nav::hasTurnDelta(delta)) return;
    const uint8_t pageCount = sequencer_.activePageCount();
    const uint16_t enabledMask = structure_slots::prefixMask(pageCount);
    const auto target = structure_slots::nextNavigationTarget(
        enabledMask,
        currentPageCursor(sequencer_),
        core::state::sequencer::SequencerState::PAGE_COUNT,
        sequencer_.structureUi.previewAddPageSlot.get(),
        nav::turnStep(delta)
    );
    if (!target.valid) return;
    setPagePreview(target.index, target.addSlot);
}

FLASHMEM void SequencerStructureNavigationWorkflow::moveTrack(float delta) {
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

FLASHMEM void SequencerStructureNavigationWorkflow::moveWorkspaceTrack(float delta) {
    if (!nav::hasTurnDelta(delta)) return;
    const uint16_t enabledMask = currentTrackEnabledMask();
    const int addIndex = structure_slots::firstDisabledIndex(
        enabledMask,
        core::state::sequencer::SequencerTrackBankState::TRACK_COUNT
    );
    const uint16_t visibleMask = addIndex < 0
        ? enabledMask
        : static_cast<uint16_t>(
              enabledMask |
              structure_slots::slotBit(static_cast<uint8_t>(addIndex))
          );
    const uint8_t target = structure_slots::nextEnabledIndex(
        visibleMask,
        currentTrackCursor(track_ui_),
        core::state::sequencer::SequencerTrackBankState::TRACK_COUNT,
        nav::turnStep(delta)
    );
    setTrackPreview(
        target,
        addIndex >= 0 && target == static_cast<uint8_t>(addIndex)
    );
}

FLASHMEM void SequencerStructureNavigationWorkflow::enterWorkspacePatternLevel() {
    if (!structureWorkspaceActive()) return;
    if (track_ui_.previewAddSlot.get()) return;

    sequencer_.structureUi.workspace.level.set(
        core::state::sequencer::SequencerStructureWorkspaceLevel::PATTERNS
    );
    navigation_focus_.set(core::state::StructureNavigationFocus::PAGE);
    const uint8_t page = currentExistingPage(sequencer_);
    sequencer_.page.set(page);
    sequencer_.focusedStep.set(sequencer_.pageStartStepClamped(page));
    sequencer_.structureUi.syncPreviewPage(page);
    sequencer_.structureUi.previewAddPageSlot.set(false);
}

FLASHMEM void SequencerStructureNavigationWorkflow::closeStructureWorkspace(
    bool restoreCaller
) {
    auto& workspace = sequencer_.structureUi.workspace;
    if (!workspace.active.get()) return;

    const auto callerFocus = workspace.callerFocus;
    const uint8_t callerTrack = workspace.callerTrack;
    const uint8_t callerPage = workspace.callerPage;
    const uint8_t callerStep = workspace.callerStep;

    workspace.active.set(false);
    workspace.level.set(
        core::state::sequencer::SequencerStructureWorkspaceLevel::TRACKS
    );
    track_ui_.previewAddSlot.set(false);
    sequencer_.structureUi.previewAddPageSlot.set(false);

    if (restoreCaller && structure_slots::isEnabled(
            currentTrackEnabledMask(),
            callerTrack
        )) {
        applyTrackState(currentTrackEnabledMask(), callerTrack);
        const uint8_t restoredPage = std::min<uint8_t>(
            callerPage,
            static_cast<uint8_t>(sequencer_.activePageCount() - 1U)
        );
        sequencer_.page.set(restoredPage);
        const uint8_t length = core::state::sequencer::activeContentLength(sequencer_);
        const uint8_t restoredStep = length == 0
            ? 0
            : std::min<uint8_t>(callerStep, static_cast<uint8_t>(length - 1U));
        sequencer_.focusedStep.set(restoredStep);
        sequencer_.structureUi.syncPreviewPage(restoredPage);
        track_ui_.syncPreviewTrack(callerTrack);
        navigation_focus_.set(callerFocus);
        return;
    }

    syncPreviewToFocus(core::state::StructureNavigationFocus::PAGE);
    navigation_focus_.set(core::state::StructureNavigationFocus::PAGE);
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
    sequencer_.structureUi.previewAddPageSlot.set(false);
}

FLASHMEM void SequencerStructureNavigationWorkflow::setPagePreview(
    uint8_t pageIndex,
    bool addSlot
) {
    const uint8_t clampedPage = sequencer_.clampPage(pageIndex);
    sequencer_.structureUi.syncPreviewPage(clampedPage);
    sequencer_.page.set(clampedPage);
    sequencer_.structureUi.previewAddPageSlot.set(addSlot);
    if (!addSlot) {
        sequencer_.focusedStep.set(sequencer_.pageStartStep(clampedPage));
    }
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

FLASHMEM uint8_t SequencerStructureNavigationWorkflow::cursorForSelectionScope(
    core::state::StructureSelectionScope scope
) const {
    switch (scope) {
        case core::state::StructureSelectionScope::TRACK:
            return currentActiveTrack();
        case core::state::StructureSelectionScope::STEP:
            return std::min<uint8_t>(sequencer_.focusedStep.get(), maxStepCursor());
        case core::state::StructureSelectionScope::PAGE:
        default:
            return sequencer_.visiblePage();
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
        sequencer_.structureUi.previewAddPageSlot.set(false);
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
