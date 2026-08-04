#include "SequencerStepEditHandler.hpp"

#include <config/PlatformCompat.hpp>

#include "config/Timing.hpp"

namespace core::handler {

FLASHMEM void SequencerStepEditHandler::openStepPresetLibrary() {
    if (sequencer_.stepContentDraft.active.get() ||
        sequencer_.stepContentDraft.exitPromptVisible.get()) {
        return;
    }
    preset_library_auto_close_pending_ = false;
    preset_library_auto_close_at_ms_ = 0U;
    (void)preset_library_.open(step_preset_library_adapter_.operations());
}

FLASHMEM void SequencerStepEditHandler::openChordPresetLibrary() {
    if (!chordEditorActive() || !sequencer_.stepContentDraft.active.get() ||
        sequencer_.stepContentDraft.exitPromptVisible.get()) {
        return;
    }
    const auto subEditor = sequencer_.stepEdit.chordEditor.subEditor.get();
    if (subEditor.formulaEditorActive || subEditor.sourceSelectorActive) { return; }
    preset_library_auto_close_pending_ = false;
    preset_library_auto_close_at_ms_ = 0U;
    (void)preset_library_.open(chord_preset_library_adapter_.operations());
}

FLASHMEM void SequencerStepEditHandler::closePresetLibrary() {
    preset_library_action_press_active_ = false;
    preset_library_auto_close_pending_ = false;
    preset_library_auto_close_at_ms_ = 0U;
    preset_library_.close();
    configureOptForFocusedRow();
}

FLASHMEM void SequencerStepEditHandler::backFromPresetLibrary() {
    if (preset_library_auto_close_pending_) {
        closePresetLibrary();
        configureOptForFocusedRow();
        return;
    }
    preset_library_action_press_active_ = false;
    preset_library_auto_close_pending_ = false;
    preset_library_auto_close_at_ms_ = 0U;
    if (preset_library_.back(time_provider_())) { configureOptForFocusedRow(); }
}

FLASHMEM void SequencerStepEditHandler::movePresetLibraryItem(float delta) {
    if (preset_library_auto_close_pending_) return;
    preset_library_.move(delta, time_provider_());
}

FLASHMEM void SequencerStepEditHandler::adjustPresetLibraryDetail(float delta) {
    if (preset_library_auto_close_pending_) return;
    preset_library_.adjustFocusedDetail(delta);
}

FLASHMEM void SequencerStepEditHandler::enterPresetLibraryDetail() {
    if (preset_library_auto_close_pending_) return;
    preset_library_.enterDetail();
}

FLASHMEM void SequencerStepEditHandler::togglePresetLibraryMode() {
    if (preset_library_auto_close_pending_) return;
    preset_library_.toggleMode();
}

FLASHMEM void SequencerStepEditHandler::beginPresetLibraryActionGuard() {
    if (preset_library_auto_close_pending_) return;
    preset_library_action_press_active_ = preset_library_.beginActionGuard(time_provider_());
}

FLASHMEM void SequencerStepEditHandler::releasePresetLibraryAction() {
    if (preset_library_action_press_active_) {
        preset_library_action_press_active_ = false;
        (void)preset_library_.cancelActionGuard(time_provider_());
        return;
    }
    if (preset_library_auto_close_pending_) return;

    if (preset_library_.shouldCommitBeforeLoad(false)) {
        if (!commitStepEditHistory()) return;
    }
    handlePresetLibraryResult(preset_library_.executeTap(time_provider_()));
}

FLASHMEM void SequencerStepEditHandler::commitPresetLibraryActionGuard() {
    if (preset_library_auto_close_pending_) return;
    if (!preset_library_action_press_active_) return;

    if (preset_library_.shouldCommitBeforeLoad(true)) {
        if (!commitStepEditHistory()) return;
    }
    handlePresetLibraryResult(preset_library_.commitActionGuard(time_provider_()));
}

FLASHMEM void SequencerStepEditHandler::handlePresetLibraryResult(
    const SequencerPresetLibraryResult& result) {
    if (result.outcome == SequencerPresetLibraryOutcome::SAVED) {
        // Saved assets remain visible and selected so Save never feels like
        // a silent modal dismissal.
        return;
    }
    if (result.outcome != SequencerPresetLibraryOutcome::LOADED &&
        result.outcome != SequencerPresetLibraryOutcome::QUEUED &&
        result.outcome != SequencerPresetLibraryOutcome::CANCELLED) {
        return;
    }

    if (result.refreshPublishedState) { sequencer_.invalidateVariationTelemetry(); }
    if (result.outcome == SequencerPresetLibraryOutcome::QUEUED) { return; }

    preset_library_auto_close_at_ms_ =
        time_provider_() + (result.outcome == SequencerPresetLibraryOutcome::CANCELLED
                                ? Config::Timing::CONTEXT_CANCELLED_FEEDBACK_MS
                                : Config::Timing::CONTEXT_APPLIED_FEEDBACK_MS);
    preset_library_auto_close_pending_ = true;
}

}  // namespace core::handler
