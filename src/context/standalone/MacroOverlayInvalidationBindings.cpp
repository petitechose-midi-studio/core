#include "context/standalone/MacroOverlayInvalidationBindings.hpp"

#include <config/PlatformCompat.hpp>

namespace core::context::standalone::macro_overlay_invalidation {

FLASHMEM bool Bindings::bind(StateRefs stateRefs,
                             void* callbackContext,
                             InvalidateCallback callback) {
    clear();
    if (callbackContext == nullptr || callback == nullptr) return false;

    callback_context_ = callbackContext;
    callback_ = callback;

    phase_watcher_.bind<&Bindings::requestPhaseRenders>(
        *this, 0, "MacroOverlay.phase"
    );
    clipboard_watcher_.bind<&Bindings::requestClipboardRenders>(
        *this, 1, "MacroOverlay.clipboard"
    );
    edit_watcher_.bind<&Bindings::requestEditRender>(
        *this, 2, "MacroOverlay.edit"
    );
    automation_watcher_.bind<&Bindings::requestAutomationRender>(
        *this, 3, "MacroOverlay.automation"
    );
    edit_selector_watcher_.bind<&Bindings::requestEditSelectorRender>(
        *this, 4, "MacroOverlay.editSelector"
    );
    page_selector_watcher_.bind<&Bindings::requestPageSelectorRender>(
        *this, 5, "MacroOverlay.pageSelector"
    );
    target_selector_watcher_.bind<&Bindings::requestTargetSelectorRender>(
        *this, 6, "MacroOverlay.targetSelector"
    );

    bool bound = phase_watcher_.watch(stateRefs.macroEdit.flowPhase);
    if (stateRefs.clipboard != nullptr) {
        bound = clipboard_watcher_.watch(stateRefs.clipboard->revision) && bound;
    }
    bound = edit_watcher_.watchAll(
        stateRefs.macroEdit.visible,
        stateRefs.macroEdit.editingIndex,
        stateRefs.macroEdit.tempChannel,
        stateRefs.macroEdit.tempCC,
        stateRefs.macroEdit.focusedRow,
        stateRefs.macroEdit.contextGuard,
        stateRefs.macroEdit.contextFeedback,
        stateRefs.macroEdit.contextButton,
        stateRefs.macroUi.automationRecordingRevision,
        stateRefs.macroUi.automationManualOverrideMask,
        stateRefs.macroUi.runtimeProjectionRevision,
        stateRefs.configRevision
    ) && bound;
    bound = automation_watcher_.watchAll(
        stateRefs.macroEdit.automationVisible,
        stateRefs.macroEdit.editingIndex,
        stateRefs.macroEdit.automationFocusedRow,
        stateRefs.macroEdit.modulationFocusedRow,
        stateRefs.macroEdit.modulatorNavigationFeedback,
        stateRefs.macroEdit.conversionPreview.revision,
        stateRefs.macroEdit.contextGuard,
        stateRefs.macroEdit.contextFeedback,
        stateRefs.macroEdit.contextButton,
        stateRefs.macroUi.automationRecordingRevision,
        stateRefs.macroUi.automationManualOverrideMask,
        stateRefs.configRevision
    ) && bound;
    bound = edit_selector_watcher_.watchAll(
        stateRefs.macroEdit.selector.editingRow,
        stateRefs.macroEdit.selector.selectedIndex
    ) && bound;
    bound = page_selector_watcher_.watch(
        stateRefs.pages.selector.selectedIndex
    ) && bound;
    bound = target_selector_watcher_.watch(
        stateRefs.macroEdit.macroSelector.selectedIndex
    ) && bound;

    if (!bound) clear();
    return bound;
}

FLASHMEM void Bindings::clear() {
    phase_watcher_.clear();
    clipboard_watcher_.clear();
    edit_watcher_.clear();
    automation_watcher_.clear();
    edit_selector_watcher_.clear();
    page_selector_watcher_.clear();
    target_selector_watcher_.clear();
    callback_context_ = nullptr;
    callback_ = nullptr;
}

FLASHMEM void Bindings::requestPhaseRenders() {
    invalidate(PHASE_RENDER_MASK);
}

FLASHMEM void Bindings::requestClipboardRenders() {
    invalidate(RENDER_EDIT | RENDER_AUTOMATION);
}

FLASHMEM void Bindings::requestEditRender() {
    invalidate(RENDER_EDIT);
}

FLASHMEM void Bindings::requestAutomationRender() {
    invalidate(RENDER_AUTOMATION);
}

FLASHMEM void Bindings::requestEditSelectorRender() {
    invalidate(RENDER_EDIT_SELECTOR);
}

FLASHMEM void Bindings::requestPageSelectorRender() {
    invalidate(RENDER_PAGE_SELECTOR);
}

FLASHMEM void Bindings::requestTargetSelectorRender() {
    invalidate(RENDER_TARGET_SELECTOR);
}

FLASHMEM void Bindings::invalidate(uint32_t renderFlags) {
    if (callback_ != nullptr) callback_(callback_context_, renderFlags);
}

}  // namespace core::context::standalone::macro_overlay_invalidation
