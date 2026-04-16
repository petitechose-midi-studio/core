#include "handler/macro/MacroStructureWorkflow.hpp"

#include <algorithm>

#include <config/PlatformCompat.hpp>
#include <config/TimeCompat.hpp>

#include "handler/common/NavigationUtils.hpp"
#include "state/shared/StructureSlotOps.hpp"

namespace core::handler {

namespace structure_slots = core::state::shared;

namespace {

uint8_t currentPageCursor(const core::state::macro::MacroUiState& macroUi,
                          const core::state::macro::MacroPagesState& pages) {
    if (macroUi.previewAddPageSlot.get()) {
        return pages.currentActivePage();
    }
    return macroUi.previewPageIndex.get();
}

uint8_t currentTrackCursor(const core::state::TrackNavigationState& trackUi) {
    return trackUi.previewTrackIndex.get();
}

}  // namespace

FLASHMEM MacroStructureWorkflow::MacroStructureWorkflow(
    StateRefs state,
    MacroStructureDomainServices services
)
    : macro_ui_(state.macroUi)
    , pages_(state.pages)
    , track_ui_(state.trackNavigation)
    , shared_track_active_(state.sharedTrackActive)
    , navigation_focus_(state.navigationFocus)
    , structure_clipboard_(state.structureClipboard)
    , services_(services) {
    track_ui_.syncPreviewTrack(services_.activeTrack());
    macro_ui_.syncPreviewPage(pages_.currentActivePage());
    bindStateSync();
}

bool MacroStructureWorkflow::selectionActive() const {
    return macro_ui_.pageSelection.active.get() || track_ui_.selection.active.get();
}

bool MacroStructureWorkflow::previewingAddSlot() const {
    return navigation_focus_.get() == core::state::StructureNavigationFocus::TRACK
        ? track_ui_.previewAddSlot.get()
        : macro_ui_.previewAddPageSlot.get();
}

FLASHMEM bool MacroStructureWorkflow::commitPreviewedPageIfNeeded() {
    if (navigation_focus_.get() != core::state::StructureNavigationFocus::PAGE) {
        return false;
    }

    if (macro_ui_.previewAddPageSlot.get()) return false;
    const uint8_t previewPage = macro_ui_.previewPageIndex.get();
    if (previewPage == pages_.currentActivePage()) {
        return false;
    }
    services_.switchToPage(previewPage);
    macro_ui_.syncPreviewPage(pages_.currentActivePage());
    return true;
}

FLASHMEM void MacroStructureWorkflow::cycleNavigationFocus() {
    const auto current = navigation_focus_.get();
    const auto next = (current == core::state::StructureNavigationFocus::PAGE)
        ? core::state::StructureNavigationFocus::TRACK
        : core::state::StructureNavigationFocus::PAGE;
    syncPreviewToCurrentContext();
    navigation_focus_.set(next);
}

FLASHMEM void MacroStructureWorkflow::moveByFocus(float delta) {
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

FLASHMEM void MacroStructureWorkflow::enterSelectionModeForCurrentFocus() {
    const auto scope = core::state::selectionScopeForFocus(navigation_focus_.get());
    auto& selection = scope == core::state::StructureSelectionScope::TRACK
        ? track_ui_.selection
        : macro_ui_.pageSelection;
    if (selection.active.get()) return;

    track_ui_.previewAddSlot.set(false);
    macro_ui_.previewAddPageSlot.set(false);

    const uint8_t cursor =
        (scope == core::state::StructureSelectionScope::TRACK)
            ? currentTrackCursor(track_ui_)
            : currentPageCursor(macro_ui_, pages_);

    selection.active.set(true);
    selection.scope.set(scope);
    selection.cursorIndex.set(cursor);
    selection.selectedMask.set(0);
    navigation_focus_.set(
        scope == core::state::StructureSelectionScope::TRACK
            ? core::state::StructureNavigationFocus::TRACK
            : core::state::StructureNavigationFocus::PAGE
    );
    track_ui_.syncPreviewTrack(services_.activeTrack());
    macro_ui_.syncPreviewPage(pages_.currentActivePage());
}

FLASHMEM void MacroStructureWorkflow::cancelSelectionMode() {
    track_ui_.previewAddSlot.set(false);
    macro_ui_.previewAddPageSlot.set(false);
    auto& selection = track_ui_.selection.active.get() ? track_ui_.selection : macro_ui_.pageSelection;
    const auto scope = selection.scope.get();
    const uint8_t cursor =
        (scope == core::state::StructureSelectionScope::TRACK)
            ? services_.activeTrack()
            : pages_.currentActivePage();
    selection.reset(scope, cursor);
    track_ui_.syncPreviewTrack(services_.activeTrack());
    macro_ui_.syncPreviewPage(pages_.currentActivePage());
}

FLASHMEM void MacroStructureWorkflow::toggleSelectionAtCursor() {
    auto& selection = track_ui_.selection.active.get() ? track_ui_.selection : macro_ui_.pageSelection;
    if (!selection.active.get()) return;

    const uint8_t cursor = selection.cursorIndex.get();
    const bool trackScope = selection.scope.get() == core::state::StructureSelectionScope::TRACK;
    const uint16_t enabledMask =
        trackScope ? services_.trackEnabledMask() : services_.pageEnabledMask();
    const uint16_t bit = static_cast<uint16_t>(1U << cursor);
    if ((enabledMask & bit) == 0) return;

    uint16_t selectedMask = selection.selectedMask.get();
    if ((selectedMask & bit) != 0) {
        selectedMask &= static_cast<uint16_t>(~bit);
    } else {
        selectedMask |= bit;
    }
    selection.selectedMask.set(selectedMask);
}

FLASHMEM void MacroStructureWorkflow::navigateSelection(float delta) {
    auto& selection = track_ui_.selection.active.get() ? track_ui_.selection : macro_ui_.pageSelection;
    if (!selection.active.get()) return;
    if (!nav::hasTurnDelta(delta)) return;

    const bool trackScope = selection.scope.get() == core::state::StructureSelectionScope::TRACK;
    const uint16_t enabledMask =
        trackScope ? services_.trackEnabledMask() : services_.pageEnabledMask();
    const uint8_t count =
        trackScope ? core::state::macro::TRACK_COUNT : core::state::macro::PAGE_COUNT;
    if (structure_slots::countEnabled(enabledMask, count) == 0) return;

    const uint8_t current = selection.cursorIndex.get();
    const uint8_t next = structure_slots::nextEnabledIndex(
        enabledMask,
        current,
        count,
        nav::turnStep(delta)
    );
    selection.cursorIndex.set(next);
}

FLASHMEM bool MacroStructureWorkflow::canRemoveCurrentStructure() const {
    switch (navigation_focus_.get()) {
        case core::state::StructureNavigationFocus::TRACK:
            if (track_ui_.previewAddSlot.get()) return false;
            return structure_slots::countEnabled(
                services_.trackEnabledMask(),
                core::state::macro::TRACK_COUNT
            ) > 1U;
        case core::state::StructureNavigationFocus::PAGE:
        default:
            if (macro_ui_.previewAddPageSlot.get()) return false;
            return structure_slots::countEnabled(
                services_.pageEnabledMask(),
                core::state::macro::PAGE_COUNT
            ) > 1U;
    }
}

FLASHMEM bool MacroStructureWorkflow::canPasteCurrentStructure() const {
    if (navigation_focus_.get() == core::state::StructureNavigationFocus::TRACK) {
        return structure_clipboard_.hasMacroTrack();
    }
    return structure_clipboard_.hasMacroPage();
}

FLASHMEM void MacroStructureWorkflow::beginHoldAction(core::state::StructureHoldAction action) {
    if (navigation_focus_.get() == core::state::StructureNavigationFocus::TRACK) {
        track_ui_.hold.begin(action, core::time_compat::millis());
        return;
    }
    macro_ui_.pageHold.begin(action, core::time_compat::millis());
}

FLASHMEM void MacroStructureWorkflow::clearHoldAction() {
    track_ui_.hold.clear();
    macro_ui_.pageHold.clear();
}

FLASHMEM void MacroStructureWorkflow::eraseCurrentStructure() {
    switch (navigation_focus_.get()) {
        case core::state::StructureNavigationFocus::TRACK:
            if (track_ui_.previewAddSlot.get()) return;
            if (services_.eraseTrack(services_.activeTrack())) {
                syncPreviewToCurrentContext();
            }
            return;
        case core::state::StructureNavigationFocus::PAGE:
        default:
            if (macro_ui_.previewAddPageSlot.get()) return;
            if (services_.erasePage(pages_.currentActivePage())) {
                syncPreviewToCurrentContext();
            }
            return;
    }
}

FLASHMEM void MacroStructureWorkflow::removeCurrentStructure() {
    switch (navigation_focus_.get()) {
        case core::state::StructureNavigationFocus::TRACK:
            if (track_ui_.previewAddSlot.get()) return;
            if (services_.deleteActiveTrack()) {
                syncPreviewToCurrentContext();
            }
            return;
        case core::state::StructureNavigationFocus::PAGE:
        default:
            if (macro_ui_.previewAddPageSlot.get()) return;
            if (services_.deleteActivePage()) {
                syncPreviewToCurrentContext();
            }
            return;
    }
}

FLASHMEM void MacroStructureWorkflow::copyCurrentStructure() {
    switch (navigation_focus_.get()) {
        case core::state::StructureNavigationFocus::TRACK:
            if (track_ui_.previewAddSlot.get()) return;
            structure_clipboard_.storeMacroTrack(pages_.tracks[services_.activeTrack()]);
            return;
        case core::state::StructureNavigationFocus::PAGE:
        default:
            if (macro_ui_.previewAddPageSlot.get()) return;
            structure_clipboard_.storeMacroPage(pages_.activePageData());
            return;
    }
}

FLASHMEM void MacroStructureWorkflow::pasteCurrentStructure() {
    if (navigation_focus_.get() == core::state::StructureNavigationFocus::TRACK) {
        if (!structure_clipboard_.hasMacroTrack()) return;
        const uint8_t targetIndex =
            track_ui_.previewAddSlot.get() ? track_ui_.previewTrackIndex.get() : services_.activeTrack();
        if (targetIndex >= core::state::macro::TRACK_COUNT) return;
        if (services_.pasteTrack(targetIndex, structure_clipboard_.macroTrack)) {
            syncPreviewToCurrentContext();
        }
        track_ui_.previewAddSlot.set(false);
        return;
    }

    if (!structure_clipboard_.hasMacroPage()) return;
    const uint8_t addPageIndex = static_cast<uint8_t>(std::max(
        0,
        structure_slots::nextAddIndexAfterHighest(
            services_.pageEnabledMask(),
            core::state::macro::PAGE_COUNT
        )
    ));
    const uint8_t targetIndex =
        macro_ui_.previewAddPageSlot.get() ? addPageIndex : pages_.currentActivePage();
    if (targetIndex >= core::state::macro::PAGE_COUNT) return;
    if (services_.pastePage(targetIndex, structure_clipboard_.macroPage)) {
        syncPreviewToCurrentContext();
    }
    macro_ui_.previewAddPageSlot.set(false);
}

FLASHMEM void MacroStructureWorkflow::deleteSelection() {
    auto& selection = track_ui_.selection.active.get() ? track_ui_.selection : macro_ui_.pageSelection;
    if (!selection.active.get()) return;

    const bool trackScope = selection.scope.get() == core::state::StructureSelectionScope::TRACK;
    const uint16_t selectedMask = selection.selectedMask.get();
    if (selectedMask == 0) return;

    const bool changed = trackScope
        ? services_.deleteSelectedTracks(selectedMask)
        : services_.deleteSelectedPages(selectedMask);
    if (!changed) return;

    cancelSelectionMode();
    syncPreviewToCurrentContext();
}

FLASHMEM void MacroStructureWorkflow::duplicateSelection() {
    auto& selection = track_ui_.selection.active.get() ? track_ui_.selection : macro_ui_.pageSelection;
    if (!selection.active.get()) return;

    const bool trackScope = selection.scope.get() == core::state::StructureSelectionScope::TRACK;
    const uint16_t selectedMask = selection.selectedMask.get();
    if (selectedMask == 0) return;

    const bool changed = trackScope
        ? services_.duplicateSelectedTracks(selectedMask)
        : services_.duplicateSelectedPages(selectedMask);
    if (!changed) return;

    cancelSelectionMode();
    syncPreviewToCurrentContext();
}

FLASHMEM void MacroStructureWorkflow::createPreviewedStructure() {
    switch (navigation_focus_.get()) {
        case core::state::StructureNavigationFocus::TRACK:
            if (services_.createTrack(track_ui_.previewTrackIndex.get())) {
                syncPreviewToCurrentContext();
            }
            break;
        case core::state::StructureNavigationFocus::PAGE:
        default:
            if (services_.createNextPage()) {
                syncPreviewToCurrentContext();
            }
            break;
    }

    track_ui_.previewAddSlot.set(false);
    macro_ui_.previewAddPageSlot.set(false);
}

FLASHMEM void MacroStructureWorkflow::bindStateSync() {
    subscriptions_.reserve(2);

    subscriptions_.push_back(
        shared_track_active_.subscribe([this](uint8_t activeTrack) {
            track_ui_.syncPreviewTrack(activeTrack);
            macro_ui_.syncPreviewPage(pages_.currentActivePage());
        })
    );

    subscriptions_.push_back(
        pages_.activePageIndexSignal().subscribe([this](uint8_t activePage) {
            macro_ui_.syncPreviewPage(activePage);
        })
    );
}

FLASHMEM void MacroStructureWorkflow::movePage(float delta) {
    if (!nav::hasTurnDelta(delta)) return;

    const uint16_t enabledMask = services_.pageEnabledMask();
    if (structure_slots::countEnabled(enabledMask, core::state::macro::PAGE_COUNT) == 0) return;

    const uint8_t current = currentPageCursor(macro_ui_, pages_);
    const bool currentAddSlot = macro_ui_.previewAddPageSlot.get();
    const auto target = structure_slots::nextNavigationTarget(
        enabledMask,
        current,
        core::state::macro::PAGE_COUNT,
        currentAddSlot,
        nav::turnStep(delta)
    );
    if (!target.valid) return;
    macro_ui_.syncPreviewPage(target.index);
    if (target.addSlot) {
        macro_ui_.previewAddPageSlot.set(true);
        return;
    }

    macro_ui_.previewAddPageSlot.set(false);
}

FLASHMEM void MacroStructureWorkflow::moveTrack(float delta) {
    if (!nav::hasTurnDelta(delta)) return;

    const uint16_t enabledMask = services_.trackEnabledMask();
    if (structure_slots::countEnabled(enabledMask, core::state::macro::TRACK_COUNT) == 0) return;

    const uint8_t next = structure_slots::wrapIndex(
        currentTrackCursor(track_ui_),
        nav::turnStep(delta),
        core::state::macro::TRACK_COUNT
    );
    track_ui_.syncPreviewTrack(next);
    const bool enabled = structure_slots::isEnabled(enabledMask, next);
    track_ui_.previewAddSlot.set(!enabled);
    if (enabled && next != services_.activeTrack()) {
        services_.switchToTrack(next);
        syncPreviewToCurrentContext();
    }
}

FLASHMEM void MacroStructureWorkflow::syncPreviewToCurrentContext() {
    track_ui_.previewAddSlot.set(false);
    macro_ui_.previewAddPageSlot.set(false);
    track_ui_.syncPreviewTrack(services_.activeTrack());
    macro_ui_.syncPreviewPage(pages_.currentActivePage());
}

}  // namespace core::handler
