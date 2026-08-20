#include "SequencerStepEditHandler.hpp"

#include <cstdio>

#include <config/PlatformCompat.hpp>

#include "config/Timing.hpp"
#include "state/sequencer/SequencerPatternEditorOps.hpp"

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

FLASHMEM void SequencerStepEditHandler::openPatternPresetLibrary() {
    if (!sequencer_.patternEditor.active.get() &&
        !sequencer_.drumSequencer.laneEditor.active) {
        return;
    }
    if (sequencer_.drumSequencer.laneEditor.active &&
        sequencer_.drumSequencer.laneEditor.dirty) {
        sequencer_.historyFeedback.show(
            "Pattern presets",
            "Apply lane first",
            "Draft unchanged",
            time_provider_()
        );
        return;
    }
    preset_library_auto_close_pending_ = false;
    preset_library_auto_close_at_ms_ = 0U;
    preset_open_release_latch_.arm(Config::ButtonID::NAV);
    (void)preset_library_.open(
        pattern_preset_library_adapter_.operations()
    );
}

FLASHMEM void SequencerStepEditHandler::closePresetLibrary() {
    preset_library_action_press_active_ = false;
    preset_library_auto_close_pending_ = false;
    preset_library_auto_close_at_ms_ = 0U;
    preset_library_.close();
    if (sequencer_.stepEdit.visible.get()) configureOptForFocusedRow();
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
    if (preset_library_.back(time_provider_()) &&
        sequencer_.stepEdit.visible.get()) {
        configureOptForFocusedRow();
    }
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

FLASHMEM void SequencerStepEditHandler::openPresetLibraryManagement() {
    if (preset_library_auto_close_pending_) return;
    if (preset_library_.openFocusedManagement()) {
        preset_open_release_latch_.arm(Config::ButtonID::NAV);
    }
}

FLASHMEM void SequencerStepEditHandler::togglePresetLibraryMode() {
    if (preset_library_auto_close_pending_) return;
    preset_library_.toggleMode();
}

FLASHMEM void
SequencerStepEditHandler::cyclePatternPresetLibrarySource() {
    if (preset_library_auto_close_pending_) return;
    preset_library_.cyclePatternSourceFilter();
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
        if (!result.returnToParent) return;
        const auto& pattern = sequencer_.presetLibrary.pattern();
        const char* name = result.displayName[0] != '\0'
            ? result.displayName
            : (pattern.descriptor.valid
                ? pattern.descriptor.metadata.semanticName
                : result.assetId);
        char savedName[
            core::state::sequencer::SEQUENCER_PRESET_SEMANTIC_NAME_SIZE
        ]{};
        std::snprintf(savedName, sizeof(savedName), "%s", name ? name : "Pattern");
        returnPatternPresetWorkflowToGrid();
        sequencer_.historyFeedback.show(
            "Saved",
            savedName,
            "",
            time_provider_()
        );
        return;
    }
    if (result.outcome != SequencerPresetLibraryOutcome::LOADED &&
        result.outcome != SequencerPresetLibraryOutcome::QUEUED &&
        result.outcome != SequencerPresetLibraryOutcome::CANCELLED) {
        return;
    }

    if (result.refreshPublishedState) { sequencer_.invalidateVariationTelemetry(); }
    if (pattern_preset_library_adapter_.previewActive()) {
        returnPatternPresetWorkflowToGrid();
        return;
    }
    if (result.outcome == SequencerPresetLibraryOutcome::QUEUED) { return; }

    preset_library_auto_close_at_ms_ =
        time_provider_() + (result.outcome == SequencerPresetLibraryOutcome::CANCELLED
                                ? Config::Timing::CONTEXT_CANCELLED_FEEDBACK_MS
                                : Config::Timing::CONTEXT_APPLIED_FEEDBACK_MS);
    preset_library_auto_close_pending_ = true;
}

FLASHMEM bool SequencerStepEditHandler::patternPresetPreviewActive() const {
    return pattern_preset_library_adapter_.previewActive();
}

FLASHMEM void SequencerStepEditHandler::confirmPatternPresetPreview() {
    if (!pattern_preset_library_adapter_.previewActive()) return;
    const auto result = pattern_preset_library_adapter_.confirmPreview();
    if (result.outcome == SequencerPresetLibraryOutcome::LOADED) {
        sequencer_.invalidateVariationTelemetry();
        return;
    }
    sequencer_.historyFeedback.show(
        "Preset",
        "Apply unavailable",
        "State unchanged",
        time_provider_()
    );
}

FLASHMEM void SequencerStepEditHandler::cancelPatternPresetPreview() {
    if (!pattern_preset_library_adapter_.previewActive()) return;
    const auto result = pattern_preset_library_adapter_.cancelPreview();
    if (result.outcome == SequencerPresetLibraryOutcome::CANCELLED) {
        sequencer_.invalidateVariationTelemetry();
        return;
    }
    sequencer_.historyFeedback.show(
        "Preset",
        "Cancel unavailable",
        "Preview unchanged",
        time_provider_()
    );
}

FLASHMEM void SequencerStepEditHandler::returnPatternPresetWorkflowToGrid() {
    closePresetLibrary();
    core::state::sequencer::closePatternEditor(sequencer_);
    if (sequencer_.drumSequencer.laneEditor.active) {
        sequencer_.drumSequencer.cancelLaneEditor();
    }
    if (overlays_.isCurrent(core::ui::OverlayType::SEQ_PATTERN_EDIT)) {
        overlays_.hide();
    }
    if (overlays_.isCurrent(core::ui::OverlayType::SEQ_DRUM_LANE_EDIT)) {
        overlays_.hide();
    }
    navigation_focus_.set(core::state::StructureNavigationFocus::PAGE);
}

}  // namespace core::handler
