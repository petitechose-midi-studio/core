#include "SequencerStepEditHandler.hpp"

#include <config/PlatformCompat.hpp>

#include "config/Timing.hpp"

namespace core::handler {

FLASHMEM void SequencerStepEditHandler::openStepPresetPicker() {
    if (sequencer_.stepContentDraft.active.get() ||
        sequencer_.stepContentDraft.exitPromptVisible.get()) {
        return;
    }
    step_preset_auto_close_pending_ = false;
    step_preset_auto_close_at_ms_ = 0;
    step_preset_picker_.open();
}

FLASHMEM void SequencerStepEditHandler::closeStepPresetPicker() {
    step_preset_action_press_active_ = false;
    step_preset_auto_close_pending_ = false;
    step_preset_auto_close_at_ms_ = 0;
    step_preset_picker_.close();
    configureOptForFocusedRow();
}

FLASHMEM void SequencerStepEditHandler::moveStepPresetItem(float delta) {
    step_preset_auto_close_pending_ = false;
    step_preset_picker_.move(delta);
}

FLASHMEM void SequencerStepEditHandler::moveStepPresetPreviewState(float delta) {
    step_preset_auto_close_pending_ = false;
    step_preset_picker_.movePreviewState(delta);
}

FLASHMEM void SequencerStepEditHandler::toggleStepPresetDetail() {
    step_preset_auto_close_pending_ = false;
    step_preset_picker_.toggleDetail();
}

FLASHMEM void SequencerStepEditHandler::toggleStepPresetMode() {
    step_preset_auto_close_pending_ = false;
    step_preset_picker_.toggleMode();
}

FLASHMEM void SequencerStepEditHandler::beginStepPresetActionGuard() {
    step_preset_auto_close_pending_ = false;
    step_preset_action_press_active_ =
        step_preset_picker_.beginActionGuard(time_provider_());
}

FLASHMEM void SequencerStepEditHandler::releaseStepPresetAction() {
    if (step_preset_action_press_active_) {
        step_preset_action_press_active_ = false;
        (void)step_preset_picker_.cancelActionGuard(time_provider_());
        return;
    }

    if (step_preset_picker_.shouldCommitBeforeLoad()) {
        commitStepEditHistory();
        history_.commitCoalescedPatternEdit();
    }

    handleStepPresetOutcome(step_preset_picker_.executeTap(time_provider_()));
}

FLASHMEM void SequencerStepEditHandler::commitStepPresetActionGuard() {
    if (!step_preset_action_press_active_) return;

    if (step_preset_picker_.shouldCommitBeforeLoad()) {
        commitStepEditHistory();
        history_.commitCoalescedPatternEdit();
    }

    handleStepPresetOutcome(
        step_preset_picker_.commitActionGuard(time_provider_())
    );
}

FLASHMEM void SequencerStepEditHandler::handleStepPresetOutcome(
    SequencerStepPresetPickerOutcome outcome
) {
    if (outcome == SequencerStepPresetPickerOutcome::SAVED) {
        // Save confirmation and the new semantic asset row remain visible in
        // the temporary picker until the user dismisses it.
        return;
    }
    if (outcome != SequencerStepPresetPickerOutcome::APPLIED &&
        outcome != SequencerStepPresetPickerOutcome::QUEUED &&
        outcome != SequencerStepPresetPickerOutcome::CANCELLED) {
        return;
    }

    sequencer_.invalidateVariationTelemetry();
    history_snapshot_valid_ =
        core::state::sequencer::captureHistorySnapshot(sequencer_, history_snapshot_);
    if (outcome == SequencerStepPresetPickerOutcome::QUEUED) return;

    // Keep the result visible just long enough to be perceived, then return to
    // the Step Editor automatically. Any deliberate picker navigation cancels
    // this deadline so the user remains in control without an extra dismiss.
    step_preset_auto_close_at_ms_ =
        time_provider_() + (
            outcome == SequencerStepPresetPickerOutcome::CANCELLED
                ? Config::Timing::CONTEXT_CANCELLED_FEEDBACK_MS
                : Config::Timing::CONTEXT_APPLIED_FEEDBACK_MS
        );
    step_preset_auto_close_pending_ = true;
}

}  // namespace core::handler
