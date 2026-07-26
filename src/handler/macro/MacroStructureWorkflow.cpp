#include "handler/macro/MacroStructureWorkflow.hpp"

#include <algorithm>

#include <config/PlatformCompat.hpp>
#include <config/TimeCompat.hpp>

#include "handler/common/NavigationUtils.hpp"
#include "state/shared/StructureSlotOps.hpp"

namespace core::handler {

namespace structure_slots = core::state::shared;

namespace {

uint8_t currentPageCursor(const core::state::macro::MacroUiState& macroUi) {
    // previewPageIndex remains the cursor authority while the synthetic Add
    // slot is selected. The active page is musical state, not UI position.
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

core::state::macro::MacroInteractionContext MacroStructureWorkflow::interactionContext(
    bool blockingOverlay,
    bool slotPropertySelecting
) const {
    return core::state::macro::buildMacroInteractionContext(
        interactionContextSource(blockingOverlay, slotPropertySelecting)
    );
}

FLASHMEM bool MacroStructureWorkflow::commitPreviewedPageIfNeeded() {
    if (effectiveFocus() != core::state::StructureNavigationFocus::PAGE) {
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
    const auto next = current == core::state::StructureNavigationFocus::STEP
        ? core::state::StructureNavigationFocus::PAGE
        : core::state::StructureNavigationFocus::STEP;
    syncPreviewToCurrentContext();
    navigation_focus_.set(next);
}

FLASHMEM void MacroStructureWorkflow::setNavigationFocus(
    core::state::StructureNavigationFocus focus
) {
    if (focus != core::state::StructureNavigationFocus::TRACK &&
        focus != core::state::StructureNavigationFocus::PAGE &&
        focus != core::state::StructureNavigationFocus::STEP) {
        focus = core::state::StructureNavigationFocus::PAGE;
    }
    navigation_focus_.set(focus);
    syncPreviewToCurrentContext();
}

FLASHMEM void MacroStructureWorkflow::moveByFocus(float delta) {
    switch (navigation_focus_.get()) {
        case core::state::StructureNavigationFocus::TRACK:
            moveTrack(delta);
            return;
        case core::state::StructureNavigationFocus::STEP:
            moveMacroSlot(delta);
            return;
        case core::state::StructureNavigationFocus::PAGE:
        default:
            movePage(delta);
            return;
    }
}

FLASHMEM bool MacroStructureWorkflow::canRemoveCurrentStructure() const {
    return core::state::macro::macroInteractionCanRemoveStructure(interactionContextSource());
}

FLASHMEM bool MacroStructureWorkflow::canPasteCurrentStructure() const {
    return core::state::macro::macroInteractionCanPasteStructure(interactionContextSource());
}

FLASHMEM void MacroStructureWorkflow::beginHoldAction(core::state::StructureHoldAction action) {
    captureHoldTarget(action);
    if (effectiveFocus() == core::state::StructureNavigationFocus::TRACK) {
        track_ui_.hold.begin(action, core::time_compat::millis());
        return;
    }
    macro_ui_.pageHold.begin(action, core::time_compat::millis());
}

FLASHMEM bool MacroStructureWorkflow::hasHoldAction(
    core::state::StructureHoldAction action
) const {
    // Inspect both owners so a physical release still closes the gesture if
    // another input changed focus while the button was held.
    return track_ui_.hold.action.get() == action ||
           macro_ui_.pageHold.action.get() == action;
}

FLASHMEM bool MacroStructureWorkflow::commitHoldAction(
    core::state::StructureHoldAction action
) {
    if (!hasHoldAction(action) || !holdTargetStillMatches(action)) {
        clearHoldAction();
        return false;
    }

    // Validation and mutation run on the same UI thread. Clear the visual hold
    // before applying the operation, while the validated target is still the
    // effective target, so callbacks cannot reuse this physical gesture.
    clearHoldAction();
    if (action == core::state::StructureHoldAction::REMOVE) {
        removeCurrentStructure();
        return true;
    }
    if (action == core::state::StructureHoldAction::PASTE) {
        pasteCurrentStructure();
        return true;
    }
    return false;
}

FLASHMEM void MacroStructureWorkflow::clearHoldAction() {
    track_ui_.hold.clear();
    macro_ui_.pageHold.clear();
    hold_target_ = {};
}

FLASHMEM void MacroStructureWorkflow::captureHoldTarget(
    core::state::StructureHoldAction action
) {
    constexpr uint8_t kUnused = 0xFFU;
    hold_target_ = {};
    hold_target_.action = action;
    hold_target_.focus = effectiveFocus();
    hold_target_.track = pages_.currentActiveTrack();
    hold_target_.page = kUnused;
    hold_target_.macro = kUnused;

    switch (hold_target_.focus) {
        case core::state::StructureNavigationFocus::TRACK:
            hold_target_.addSlot = track_ui_.previewAddSlot.get();
            hold_target_.track = hold_target_.addSlot
                ? track_ui_.previewTrackIndex.get()
                : services_.activeTrack();
            break;
        case core::state::StructureNavigationFocus::STEP:
            hold_target_.page = pages_.currentActivePage();
            hold_target_.macro = macro_ui_.focusedMacroSlot.get();
            hold_target_.addSlot = pages_.isMacroAddSlot(hold_target_.macro);
            break;
        case core::state::StructureNavigationFocus::PAGE:
        default:
            hold_target_.page = macro_ui_.previewPageIndex.get();
            hold_target_.addSlot = macro_ui_.previewAddPageSlot.get();
            break;
    }
}

FLASHMEM bool MacroStructureWorkflow::holdTargetStillMatches(
    core::state::StructureHoldAction action
) const {
    if (action == core::state::StructureHoldAction::NONE ||
        hold_target_.action != action ||
        hold_target_.focus != effectiveFocus()) {
        return false;
    }

    switch (hold_target_.focus) {
        case core::state::StructureNavigationFocus::TRACK: {
            const bool addSlot = track_ui_.previewAddSlot.get();
            const uint8_t track = addSlot
                ? track_ui_.previewTrackIndex.get()
                : services_.activeTrack();
            return addSlot == hold_target_.addSlot && track == hold_target_.track;
        }
        case core::state::StructureNavigationFocus::STEP:
            return pages_.currentActiveTrack() == hold_target_.track &&
                   pages_.currentActivePage() == hold_target_.page &&
                   macro_ui_.focusedMacroSlot.get() == hold_target_.macro &&
                   pages_.isMacroAddSlot(hold_target_.macro) == hold_target_.addSlot;
        case core::state::StructureNavigationFocus::PAGE:
        default:
            return pages_.currentActiveTrack() == hold_target_.track &&
                   macro_ui_.previewPageIndex.get() == hold_target_.page &&
                   macro_ui_.previewAddPageSlot.get() == hold_target_.addSlot;
    }
}

FLASHMEM void MacroStructureWorkflow::eraseCurrentStructure() {
    switch (effectiveFocus()) {
        case core::state::StructureNavigationFocus::TRACK:
            if (track_ui_.previewAddSlot.get()) return;
            if (services_.eraseTrack(services_.activeTrack())) {
                syncPreviewToCurrentContext();
            }
            return;
        case core::state::StructureNavigationFocus::STEP:
            if (pages_.isMacroAddSlot(macro_ui_.focusedMacroSlot.get())) return;
            if (services_.clearMacroAutomation(macro_ui_.focusedMacroSlot.get())) {
                syncPreviewToCurrentContext();
            }
            return;
        case core::state::StructureNavigationFocus::PAGE:
        default:
            if (macro_ui_.previewAddPageSlot.get()) return;
            if (services_.erasePage(macro_ui_.previewPageIndex.get())) {
                syncPreviewToCurrentContext();
            }
            return;
    }
}

FLASHMEM void MacroStructureWorkflow::removeCurrentStructure() {
    switch (effectiveFocus()) {
        case core::state::StructureNavigationFocus::TRACK:
            if (track_ui_.previewAddSlot.get()) return;
            if (services_.deleteActiveTrack()) {
                syncPreviewToCurrentContext();
            }
            return;
        case core::state::StructureNavigationFocus::STEP:
            if (pages_.isMacroAddSlot(macro_ui_.focusedMacroSlot.get())) return;
            if (services_.removeMacroAutomation(macro_ui_.focusedMacroSlot.get())) {
                syncPreviewToCurrentContext();
            }
            return;
        case core::state::StructureNavigationFocus::PAGE:
        default:
            if (macro_ui_.previewAddPageSlot.get()) return;
            if (services_.deletePage(macro_ui_.previewPageIndex.get())) {
                syncPreviewToCurrentContext();
            }
            return;
    }
}

FLASHMEM void MacroStructureWorkflow::copyCurrentStructure() {
    switch (effectiveFocus()) {
        case core::state::StructureNavigationFocus::TRACK:
            if (track_ui_.previewAddSlot.get()) return;
            if (!structure_clipboard_.storeMacroTrack(
                pages_.tracks[services_.activeTrack()],
                pages_.control,
                services_.activeTrack()
            )) {
                return;
            }
            return;
        case core::state::StructureNavigationFocus::STEP:
            if (pages_.isMacroAddSlot(macro_ui_.focusedMacroSlot.get())) return;
            services_.copyMacroAutomation(macro_ui_.focusedMacroSlot.get(), structure_clipboard_);
            return;
        case core::state::StructureNavigationFocus::PAGE:
        default:
            if (macro_ui_.previewAddPageSlot.get()) return;
            const uint8_t page = macro_ui_.previewPageIndex.get();
            if (!structure_clipboard_.storeMacroPage(
                pages_.pageData(pages_.currentActiveTrack(), page),
                pages_.control,
                pages_.currentActiveTrack(),
                page
            )) {
                return;
            }
            return;
    }
}

FLASHMEM void MacroStructureWorkflow::pasteCurrentStructure() {
    const auto focus = effectiveFocus();
    if (focus == core::state::StructureNavigationFocus::TRACK) {
        if (!structure_clipboard_.hasMacroTrack()) return;
        const uint8_t targetIndex =
            track_ui_.previewAddSlot.get() ? track_ui_.previewTrackIndex.get() : services_.activeTrack();
        if (targetIndex >= core::state::macro::TRACK_COUNT) return;
        if (services_.pasteTrack(
                targetIndex,
                structure_clipboard_.macroTrack,
                structure_clipboard_.macroAutomationSet.get()
            )) {
            syncPreviewToCurrentContext();
        }
        track_ui_.previewAddSlot.set(false);
        return;
    }

    if (focus == core::state::StructureNavigationFocus::STEP) {
        if (services_.pasteMacroAutomation(macro_ui_.focusedMacroSlot.get(), structure_clipboard_)) {
            syncPreviewToCurrentContext();
        }
        return;
    }

    if (!structure_clipboard_.hasMacroPage()) return;
    const uint8_t targetIndex = macro_ui_.previewPageIndex.get();
    if (targetIndex >= core::state::macro::PAGE_COUNT) return;
    if (services_.pastePage(
            targetIndex,
            structure_clipboard_.macroPage,
            structure_clipboard_.macroAutomationSet.get()
        )) {
        syncPreviewToCurrentContext();
    }
    macro_ui_.previewAddPageSlot.set(false);
}

FLASHMEM void MacroStructureWorkflow::createPreviewedStructure() {
    switch (effectiveFocus()) {
        case core::state::StructureNavigationFocus::TRACK:
            if (services_.createTrack(track_ui_.previewTrackIndex.get())) {
                syncPreviewToCurrentContext();
            }
            break;
        case core::state::StructureNavigationFocus::STEP:
            if (services_.activateMacroSlot(macro_ui_.focusedMacroSlot.get())) {
                clampFocusedMacroSlot();
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
    subscriptions_.push_back(
        shared_track_active_.subscribe([this](uint8_t activeTrack) {
            track_ui_.syncPreviewTrack(activeTrack);
            macro_ui_.syncPreviewPage(pages_.currentActivePage());
            clampFocusedMacroSlot();
        })
    );

    subscriptions_.push_back(
        pages_.activePageIndexSignal().subscribe([this](uint8_t activePage) {
            macro_ui_.syncPreviewPage(activePage);
            clampFocusedMacroSlot();
        })
    );
}

FLASHMEM core::state::StructureNavigationFocus
MacroStructureWorkflow::effectiveFocus() const {
    return core::state::macro::effectiveMacroNavigationFocus(
        navigation_focus_.get()
    );
}

FLASHMEM void MacroStructureWorkflow::movePage(float delta) {
    if (!nav::hasTurnDelta(delta)) return;

    const uint16_t enabledMask = services_.pageEnabledMask();
    if (structure_slots::countEnabled(enabledMask, core::state::macro::PAGE_COUNT) == 0) return;

    const uint8_t current = currentPageCursor(macro_ui_);
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
    if (target.index != pages_.currentActivePage()) {
        services_.switchToPage(target.index);
        syncPreviewToCurrentContext();
    }
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

FLASHMEM void MacroStructureWorkflow::moveMacroSlot(float delta) {
    if (!nav::hasTurnDelta(delta)) return;

    const int step = nav::turnStep(delta);
    const int current = macro_ui_.focusedMacroSlot.get();
    const int next = std::clamp(
        current + step,
        0,
        static_cast<int>(core::state::macro::MACRO_COUNT - 1U)
    );
    macro_ui_.focusedMacroSlot.set(static_cast<uint8_t>(next));
}

core::state::macro::MacroInteractionContextSource
MacroStructureWorkflow::interactionContextSource(
    bool blockingOverlay,
    bool slotPropertySelecting
) const {
    return core::state::macro::MacroInteractionContextSource{
        .pages = pages_,
        .macroUi = macro_ui_,
        .trackNavigation = track_ui_,
        .structureClipboard = structure_clipboard_,
        .navigationFocus = navigation_focus_.get(),
        .enabledTrackMask = services_.trackEnabledMask(),
        .blockingOverlay = blockingOverlay,
        .slotPropertySelecting = slotPropertySelecting,
    };
}

FLASHMEM void MacroStructureWorkflow::syncPreviewToCurrentContext() {
    track_ui_.previewAddSlot.set(false);
    macro_ui_.previewAddPageSlot.set(false);
    track_ui_.syncPreviewTrack(services_.activeTrack());
    macro_ui_.syncPreviewPage(pages_.currentActivePage());
    clampFocusedMacroSlot();
}

FLASHMEM void MacroStructureWorkflow::clampFocusedMacroSlot() {
    const uint8_t maxIndex = static_cast<uint8_t>(
        core::state::macro::MACRO_COUNT - 1U
    );
    if (macro_ui_.focusedMacroSlot.get() > maxIndex) {
        macro_ui_.focusedMacroSlot.set(maxIndex);
    }
}

}  // namespace core::handler
