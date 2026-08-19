#include "SequencerStepEditHandler.hpp"

#include "SequencerChordEditOps.hpp"
#include "SequencerStepChordEditorWorkflow.hpp"
#include "SequencerStepContentDraftWorkflow.hpp"
#include "SequencerStepContextRowWorkflow.hpp"
#include "SequencerStepEditSessionWorkflow.hpp"
#include "SequencerStepValueRowWorkflow.hpp"
#include "handler/sequencer/SequencerInputUtils.hpp"

#include <algorithm>

#include <config/App.hpp>
#include <config/InputIDs.hpp>
#include <config/PlatformCompat.hpp>
#include <oc/time/Time.hpp>

#include "handler/common/NavigationUtils.hpp"
#include "state/sequencer/SequencerChordUiOps.hpp"
#include "state/sequencer/SequencerContentViewOps.hpp"
#include "state/sequencer/SequencerGraphOps.hpp"
#include "state/sequencer/SequencerStepContentDraftOps.hpp"
#include "state/sequencer/SequencerStepEditRows.hpp"
#include "state/sequencer/DrumPatternState.hpp"

namespace core::handler {
namespace step_chord_editor_workflow = core::handler::sequencer::step_chord_editor_workflow;
namespace step_context_row_workflow = core::handler::sequencer::step_context_row_workflow;
namespace step_edit_session_workflow = core::handler::sequencer::step_edit_session_workflow;
namespace step_edit_rows = core::state::sequencer::step_edit_rows;
namespace step_value_row_workflow = core::handler::sequencer::step_value_row_workflow;
namespace input_utils = core::handler::sequencer::input_utils;

namespace {

FLASHMEM oc::note::sequencer::StepSequencerScaleSettings effectiveScaleSettings(
    const core::state::sequencer::SequencerState& sequencer,
    const core::state::sequencer::SequencerTrackBankState& tracks) {
    return core::state::sequencer::resolveEffectiveScaleSettings(
        tracks.projectScaleSettings(),
        core::state::sequencer::authoringPattern(sequencer).scalePolicy,
        core::state::sequencer::authoringPattern(sequencer).scaleOverride);
}

FLASHMEM bool editedStepHasChordState(const core::state::sequencer::SequencerState& sequencer,
                                      uint8_t step) {
    const auto* graph =
        core::state::sequencer::graphView(core::state::sequencer::authoringPattern(sequencer));
    if (graph == nullptr) return false;
    const auto* node =
        graph->stepNode(core::state::sequencer::activeContentStepNodeId(sequencer, step));
    return node != nullptr && (node->has(oc::note::sequencer::STEP_NODE_CHORD_MODE) ||
                               node->has(oc::note::sequencer::STEP_NODE_CHORD_LOCAL));
}

FLASHMEM core::state::sequencer::SequencerCoalescedPatternPayloadPlan stepEditPayloadPlan(
    const core::state::sequencer::SequencerState& sequencer, bool mayGrowGraph) {
    using Plan = core::state::sequencer::SequencerCoalescedPatternPayloadPlan;
    if (mayGrowGraph) return Plan::FullWithProspectiveGraph;
    return core::state::sequencer::isChildContentView(sequencer) ? Plan::FullCurrentPayload
                                                                 : Plan::FlatOnly;
}

FLASHMEM core::state::sequencer::SequencerHistoryDescriptor stepEditDescriptor(uint8_t step) {
    return core::state::sequencer::SequencerHistoryDescriptor{
        .kind = core::state::sequencer::SequencerHistoryActionKind::StepEdit,
        .stepIndex = step,
        .property = core::state::sequencer::StepProperty::NOTE,
    };
}

FLASHMEM core::state::sequencer::SequencerCoalescedPatternPayloadPlan stepResetPayloadPlan(
    const core::state::sequencer::SequencerState& sequencer, uint8_t step) {
    using Plan = core::state::sequencer::SequencerCoalescedPatternPayloadPlan;
    if (core::state::sequencer::isChildContentView(sequencer)) { return Plan::FullCurrentPayload; }

    const uint8_t row = sequencer.stepEdit.focusedRow.get();
    if (!step_edit_rows::isProperty(row)) return Plan::FlatOnly;

    const auto* graph =
        core::state::sequencer::graphView(core::state::sequencer::authoringPattern(sequencer));
    const auto* node =
        graph == nullptr
            ? nullptr
            : graph->stepNode(core::state::sequencer::activeContentStepNodeId(sequencer, step));
    return node != nullptr && core::state::sequencer::nodeLocalVariationRange(
                                  *node, step_edit_rows::propertyForRow(row)) != 0U
               ? Plan::FullCurrentPayload
               : Plan::FlatOnly;
}

using DrumProperty = core::state::sequencer::DrumSequencerProperty;

FLASHMEM bool isDrumEditorRow(uint8_t row) {
    return std::find(
               step_edit_rows::DRUM_NAVIGATION_ORDER.begin(),
               step_edit_rows::DRUM_NAVIGATION_ORDER.end(),
               row
           ) != step_edit_rows::DRUM_NAVIGATION_ORDER.end();
}

FLASHMEM DrumProperty drumPropertyForEditorRow(uint8_t row) {
    if (step_edit_rows::isActivated(row)) return DrumProperty::STATE;
    if (!step_edit_rows::isProperty(row)) return DrumProperty::VELOCITY;
    return input_utils::drumPropertyForStepProperty(
        step_edit_rows::propertyForRow(row)
    );
}

FLASHMEM core::state::SequencerStepContentClipboardKind drumClipboardKindForRow(
    uint8_t row
) {
    using Kind = core::state::SequencerStepContentClipboardKind;
    return row == step_edit_rows::MICRO_SEQUENCE
        ? Kind::MICRO_SEQUENCE
        : Kind::CYCLE_STATES;
}

}  // namespace

FLASHMEM SequencerStepEditHandler::SequencerStepEditHandler(
    StateRefs state, oc::context::OverlayManager<core::ui::OverlayType>& overlays,
    oc::api::EncoderAPI& encoders, oc::api::ButtonAPI& buttons,
    oc::type::ScopeID sequencerViewScope, oc::type::ScopeID overlayScope,
    oc::type::ScopeID presetLibraryOverlayScope, TimeProviderFn timeProvider)
    : overlay_state_(state.overlays), sequencer_(state.sequencer), tracks_(state.tracks),
      structure_clipboard_(state.structureClipboard), track_ui_(state.trackNavigation),
      pattern_pitch_settings_(state.patternPitchSettings), navigation_focus_(state.navigationFocus),
      history_(state.history), step_presets_(state.stepPresets), chord_presets_(state.chordPresets),
      pattern_presets_(state.patternPresets),
      step_preset_library_adapter_(sequencer_, step_presets_),
      chord_preset_library_adapter_(sequencer_, chord_presets_),
      pattern_preset_library_adapter_(sequencer_, pattern_presets_),
      preset_library_(sequencer_, overlays), overlays_(overlays), encoders_(encoders),
      buttons_(buttons), sequencer_view_scope_(sequencerViewScope), overlay_scope_(overlayScope),
      preset_library_overlay_scope_(presetLibraryOverlayScope),
      time_provider_(timeProvider ? timeProvider : core::time_compat::millis) {
    setupBindings();
}

void SequencerStepEditHandler::update(uint32_t nowMs) {
    if (pitch_context_settings_open_ && pattern_pitch_settings_.flowPhase.get() ==
                                            core::state::PatternPitchSettingsFlowPhase::CLOSED) {
        pitch_context_settings_open_ = false;
        configureOptForFocusedRow();
    }

    const auto presetResult = preset_library_.update(nowMs);
    if (presetResult.outcome == SequencerPresetLibraryOutcome::LOADED ||
        presetResult.outcome == SequencerPresetLibraryOutcome::QUEUED ||
        presetResult.outcome == SequencerPresetLibraryOutcome::CANCELLED) {
        handlePresetLibraryResult(presetResult);
    }
    if (preset_library_auto_close_pending_ && !preset_library_action_press_active_ &&
        oc::time::deadlineReachedMs(nowMs, preset_library_auto_close_at_ms_)) {
        closePresetLibrary();
    }
}

FLASHMEM void SequencerStepEditHandler::openForMacroInPage(uint8_t indexInPage) {
    auto& drumUi = sequencer_.drumSequencer;
    if (core::state::sequencer::isDrumOverviewActive(sequencer_)) {
        const uint8_t step = drumUi.visibleStep(indexInPage);
        (void)openDrumStepEditor(
            drumUi.selectedLane,
            step,
            step_edit_rows::ACTIVATED
        );
        return;
    }
    if (!step_edit_session_workflow::openForMacroInPage(sequencer_, history_, overlays_,
                                                        indexInPage)) {
        return;
    }
    if (core::state::sequencer::isDrumContentView(sequencer_)) {
        auto& edit = sequencer_.stepEdit;
        edit.drumContext = true;
        edit.drumLane = sequencer_.contentView.drumOwnerLane;
        edit.drumStep = sequencer_.contentView.drumOwnerStep;
        edit.drumRootSlot = sequencer_.contentView.drumOwnerRootSlot;
    }
    step_retarget_active_ = false;
    lane_retarget_active_ = false;
    configureOptForFocusedRow();
}

FLASHMEM bool SequencerStepEditHandler::openFocusedStepAtRow(uint8_t row) {
    auto& drumUi = sequencer_.drumSequencer;
    if (core::state::sequencer::isDrumOverviewActive(sequencer_)) {
        return openDrumStepEditor(
            drumUi.selectedLane,
            drumUi.focusedStep,
            row
        );
    }
    const uint8_t length = core::state::sequencer::activeContentLength(sequencer_);
    if (length == 0) return false;
    const uint8_t focused =
        std::min<uint8_t>(sequencer_.focusedStep.get(), static_cast<uint8_t>(length - 1U));
    if (!step_edit_session_workflow::openForStep(sequencer_, history_, overlays_, focused)) {
        return false;
    }
    if (core::state::sequencer::isDrumContentView(sequencer_)) {
        if (!isDrumEditorRow(row)) {
            overlays_.hide();
            sequencer_.stepEdit.reset();
            return false;
        }
        auto& edit = sequencer_.stepEdit;
        edit.drumContext = true;
        edit.drumLane = sequencer_.contentView.drumOwnerLane;
        edit.drumStep = sequencer_.contentView.drumOwnerStep;
        edit.drumRootSlot = sequencer_.contentView.drumOwnerRootSlot;
    }
    step_retarget_active_ = false;
    lane_retarget_active_ = false;
    sequencer_.stepEdit.focusedRow.set(row);
    configureOptForFocusedRow();
    return true;
}

FLASHMEM bool SequencerStepEditHandler::openFocusedStepContentAtRow(uint8_t row) {
    if (core::state::sequencer::isDrumOverviewActive(sequencer_)) {
        if (!step_edit_rows::isContext(row)) return false;
        auto& drumUi = sequencer_.drumSequencer;
        if (!openDrumStepEditor(
                drumUi.selectedLane,
                drumUi.focusedStep,
                row
            )) {
            return false;
        }
        if (openDrumOwnedSharedContentChild()) return true;
        closeDrumStepEditor();
        return false;
    }
    if (!step_edit_rows::isChord(row) && !step_edit_rows::isContext(row)) { return false; }
    if (!openFocusedStepAtRow(row)) return false;

    if (step_edit_rows::isChord(row)) {
        openChordEditor();
        if (chordEditorActive()) return true;
        closeStepEdit();
        return false;
    }

    if (activateFocusedContextRow()) return true;
    closeStepEdit();
    return false;
}

FLASHMEM bool SequencerStepEditHandler::commitStepEditHistory() {
    if (sequencer_.stepContentDraft.active.get()) return true;
    if (drumStepEditActive()) return commitDrumStepHistory();
    return step_edit_session_workflow::commitHistory(sequencer_, history_);
}

FLASHMEM bool SequencerStepEditHandler::drumStepEditActive() const {
    return sequencer_.stepEdit.drumContext;
}

FLASHMEM bool SequencerStepEditHandler::drumChildEditActive() const {
    return drumStepEditActive() &&
        core::state::sequencer::isChildContentView(sequencer_);
}

FLASHMEM bool SequencerStepEditHandler::openDrumStepEditor(
    uint8_t lane,
    uint8_t step,
    uint8_t row
) {
    auto& drumUi = sequencer_.drumSequencer;
    if (!commitDrumStepHistory()) return false;
    if (!drumUi.focusStep(lane, step)) return false;

    sequencer_.contentView.reset();
    auto& edit = sequencer_.stepEdit;
    edit.reset();
    edit.drumContext = true;
    edit.drumLane = lane;
    edit.drumStep = step;
    const int16_t rootSlot = drumUi.drumTrack != nullptr
        ? drumUi.drumTrack->advancedRootSlot(lane, step)
        : -1;
    edit.drumRootSlot = rootSlot >= 0
        ? static_cast<uint8_t>(rootSlot)
        : 0xFFU;
    edit.stepIndex.set(step);
    edit.focusedRow.set(isDrumEditorRow(row) ? row : step_edit_rows::ACTIVATED);
    syncDrumPropertyForFocusedRow();
    step_retarget_active_ = false;
    lane_retarget_active_ = false;
    overlays_.show(core::ui::OverlayType::SEQ_STEP_EDIT);
    configureDrumOpt();
    return true;
}

FLASHMEM void SequencerStepEditHandler::closeDrumStepEditor() {
    if (drumChildEditActive() && sequencer_.stepContentDraft.active.get()) {
        context_release_latch_.clear();
        preset_open_release_latch_.clear();
        step_retarget_active_ = false;
        lane_retarget_active_ = false;
        overlays_.hide();
        sequencer_.stepEdit.reset();
        return;
    }
    if (!commitDrumStepHistory()) return;
    context_release_latch_.clear();
    preset_open_release_latch_.clear();
    step_retarget_active_ = false;
    lane_retarget_active_ = false;
    overlays_.hide();
    if (!core::state::sequencer::isDrumContentView(sequencer_)) {
        sequencer_.contentView.reset();
    }
    sequencer_.stepEdit.reset();
}

FLASHMEM bool SequencerStepEditHandler::drumEditedStepInRange(
    uint8_t& step
) const {
    if (!drumStepEditActive()) return false;
    if (drumChildEditActive()) {
        const uint8_t length = core::state::sequencer::activeContentLength(sequencer_);
        step = sequencer_.stepEdit.stepIndex.get();
        return length != 0U && step < length;
    }
    step = sequencer_.stepEdit.drumStep;
    return sequencer_.drumSequencer.stepInRange(
        sequencer_.stepEdit.drumLane,
        step
    );
}

FLASHMEM bool SequencerStepEditHandler::resolveDrumRootNodeId(
    core::state::sequencer::SequencerGraphNodeId& nodeId
) const {
    const auto& drumUi = sequencer_.drumSequencer;
    if (!drumUi.drumTrack ||
        drumUi.targetTrack != tracks_.activeTrackIndex() ||
        !drumUi.stepInRange(
            sequencer_.stepEdit.drumLane,
            sequencer_.stepEdit.drumStep
        )) {
        return false;
    }
    const int16_t slot = drumUi.drumTrack->advancedRootSlot(
        sequencer_.stepEdit.drumLane,
        sequencer_.stepEdit.drumStep
    );
    if (slot < 0) return false;
    nodeId = core::state::sequencer::rootStepNodeId(
        static_cast<uint8_t>(slot)
    );
    return nodeId != oc::note::sequencer::StepSequencerGraphLimits::INVALID_ID;
}

FLASHMEM bool SequencerStepEditHandler::ensureDrumRootNodeId(
    core::state::sequencer::SequencerGraphNodeId& nodeId,
    bool& mappingChanged
) {
    mappingChanged = false;
    if (resolveDrumRootNodeId(nodeId)) return true;

    auto& drumUi = sequencer_.drumSequencer;
    if (!drumUi.drumTrack ||
        drumUi.targetTrack != tracks_.activeTrackIndex() ||
        !drumUi.stepInRange(
            sequencer_.stepEdit.drumLane,
            sequencer_.stepEdit.drumStep
        )) {
        return false;
    }

    const int16_t freeSlot =
        core::state::sequencer::ensureDrumAdvancedRootSlot(
            *drumUi.drumTrack,
            core::state::sequencer::authoringPattern(sequencer_),
            sequencer_.stepEdit.drumLane,
            sequencer_.stepEdit.drumStep,
            mappingChanged
        );
    if (freeSlot < 0) return false;

    sequencer_.stepEdit.drumRootSlot = static_cast<uint8_t>(freeSlot);
    nodeId = core::state::sequencer::rootStepNodeId(
        static_cast<uint8_t>(freeSlot)
    );
    return nodeId != oc::note::sequencer::StepSequencerGraphLimits::INVALID_ID;
}

FLASHMEM void SequencerStepEditHandler::publishDrumAdvancedMutation(
    bool drumMappingChanged
) {
    auto& drumUi = sequencer_.drumSequencer;
    if (drumMappingChanged &&
        drumUi.targetTrack < core::state::sequencer::SequencerTrackBankState::TRACK_COUNT) {
        tracks_.publishDrumMutation(drumUi.targetTrack);
    }
    drumUi.bump();
}

FLASHMEM bool SequencerStepEditHandler::openDrumOwnedSharedContentChild() {
    if (!drumStepEditActive()) return false;
    const uint8_t row = sequencer_.stepEdit.focusedRow.get();
    if (!step_edit_rows::isContext(row) ||
        core::state::sequencer::activeContentDepth(sequencer_) >=
            oc::note::sequencer::StepSequencerGraphLimits::MAX_DEPTH - 1U) {
        return false;
    }
    const auto childKind = step_edit_rows::childKindForContextRow(row);
    const bool childContext = drumChildEditActive();
    if (!commitStepEditHistory()) return false;
    bool mappingChanged = false;
    bool historyStarted = false;
    core::state::sequencer::SequencerGraphNodeId ownerNodeId =
        oc::note::sequencer::StepSequencerGraphLimits::INVALID_ID;
    uint8_t targetStep = 0U;
    if (childContext) {
        uint8_t childStep = 0U;
        if (!drumEditedStepInRange(childStep)) {
            return false;
        }
        targetStep = childStep;
        ownerNodeId = core::state::sequencer::activeContentStepNodeId(
            sequencer_, childStep
        );
    } else {
        if (!resolveDrumRootNodeId(ownerNodeId)) {
            auto descriptor = drumStepHistoryDescriptor(
                core::state::sequencer::SequencerHistoryActionKind::DrumAdvancedContent
            );
            descriptor.property = core::state::sequencer::StepProperty::NOTE;
            if (!beginDrumStepHistory(descriptor)) return false;
            historyStarted = true;
            if (!ensureDrumRootNodeId(ownerNodeId, mappingChanged)) {
                (void)history_.abortCoalescedDrumEdit();
                return false;
            }
        }
        targetStep = sequencer_.stepEdit.drumRootSlot;
        sequencer_.focusedStep.set(targetStep);
    }
    if (ownerNodeId == oc::note::sequencer::StepSequencerGraphLimits::INVALID_ID) {
        if (historyStarted) (void)history_.abortCoalescedDrumEdit();
        return false;
    }

    const uint8_t defaultLength = childKind ==
            core::state::sequencer::StepContentChildKind::MICRO_SEQUENCE
        ? core::state::sequencer::DEFAULT_MICRO_SEQUENCE_LENGTH
        : core::state::sequencer::DEFAULT_CYCLE_STATE_COUNT;
    const auto availability =
        core::state::sequencer::activeContentChildCreationAvailability(
            sequencer_, targetStep, childKind, defaultLength
        );
    if (!availability.canCreateOrOpen) {
        if (historyStarted) (void)history_.abortCoalescedDrumEdit();
        return false;
    }
    if (!availability.opensExisting &&
        !sequencer_.stepContentDraft.active.get() && !historyStarted) {
        auto descriptor = drumStepHistoryDescriptor(
            core::state::sequencer::SequencerHistoryActionKind::DrumAdvancedContent
        );
        descriptor.property = core::state::sequencer::StepProperty::NOTE;
        if (!beginDrumStepHistory(descriptor)) return false;
        historyStarted = true;
    }

    const auto result = core::state::sequencer::openOrCreateActiveContentChild(
        sequencer_, targetStep, childKind, defaultLength
    );
    if (!result.opened) {
        if (sequencer_.stepContentDraft.active.get() && historyStarted) {
            core::state::sequencer::abandonStepContentDraft(sequencer_);
        }
        if (historyStarted) (void)history_.abortCoalescedDrumEdit();
        return false;
    }

    auto& content = sequencer_.contentView;
    content.drumOwnerActive = true;
    content.drumOwnerTrack = sequencer_.drumSequencer.targetTrack;
    content.drumOwnerLane = sequencer_.stepEdit.drumLane;
    content.drumOwnerStep = sequencer_.stepEdit.drumStep;
    content.drumOwnerRootSlot = sequencer_.stepEdit.drumRootSlot;
    if (sequencer_.activeStepProperty.get() ==
        core::state::sequencer::StepProperty::NOTE) {
        sequencer_.activeStepProperty.set(
            core::state::sequencer::StepProperty::VELOCITY
        );
    }
    sequencer_.contextSelector.reset();
    if (mappingChanged) sequencer_.drumSequencer.bump();
    overlays_.hide();
    sequencer_.stepEdit.reset();
    return true;
}

FLASHMEM bool SequencerStepEditHandler::drumFocusedContextHasChild() const {
    if (!drumStepEditActive() ||
        !step_edit_rows::isContext(sequencer_.stepEdit.focusedRow.get())) {
        return false;
    }
    const auto kind = step_edit_rows::childKindForContextRow(
        sequencer_.stepEdit.focusedRow.get()
    );
    if (drumChildEditActive()) {
        uint8_t childStep = 0U;
        return drumEditedStepInRange(childStep) &&
            core::state::sequencer::activeContentStepHasChildContent(
                sequencer_, childStep, kind
            );
    }

    core::state::sequencer::SequencerGraphNodeId nodeId =
        oc::note::sequencer::StepSequencerGraphLimits::INVALID_ID;
    if (!resolveDrumRootNodeId(nodeId)) return false;
    const auto& pattern = core::state::sequencer::authoringPattern(sequencer_);
    return kind == core::state::sequencer::StepContentChildKind::MICRO_SEQUENCE
        ? core::state::sequencer::stepNodeHasMicroSequence(pattern, nodeId)
        : core::state::sequencer::stepNodeHasCycleStateSet(pattern, nodeId);
}

FLASHMEM bool SequencerStepEditHandler::drumCanPasteFocusedContext() const {
    if (!drumStepEditActive() ||
        !step_edit_rows::isContext(sequencer_.stepEdit.focusedRow.get()) ||
        core::state::sequencer::activeContentDepth(sequencer_) >=
            oc::note::sequencer::StepSequencerGraphLimits::MAX_DEPTH - 1U) {
        return false;
    }
    return structure_clipboard_.hasSequencerStepContent(
        drumClipboardKindForRow(sequencer_.stepEdit.focusedRow.get())
    );
}

FLASHMEM core::state::sequencer::SequencerHistoryDescriptor
SequencerStepEditHandler::drumStepHistoryDescriptor(
    core::state::sequencer::SequencerHistoryActionKind kind
) const {
    const auto& drumUi = sequencer_.drumSequencer;
    const auto property = drumPropertyForEditorRow(
        sequencer_.stepEdit.focusedRow.get()
    );
    return {
        .kind = kind,
        .trackIndex = drumUi.targetTrack,
        .laneIndex = sequencer_.stepEdit.drumLane,
        .stepIndex = sequencer_.stepEdit.drumStep,
        .property = input_utils::drumStepProperty(property),
    };
}

FLASHMEM int32_t SequencerStepEditHandler::drumStepHistoryValue() const {
    const auto& drumUi = sequencer_.drumSequencer;
    const uint8_t lane = sequencer_.stepEdit.drumLane;
    const uint8_t step = sequencer_.stepEdit.drumStep;
    if (!drumUi.stepInRange(lane, step) || drumUi.drumTrack == nullptr) {
        return 0;
    }
    const auto property = drumPropertyForEditorRow(
        sequencer_.stepEdit.focusedRow.get()
    );
    const auto& authored = drumUi.drumTrack->pattern.lanes[lane];
    switch (property) {
        case DrumProperty::STATE:
            return drumUi.drumTrack->pattern.stepEnabled(lane, step) ? 1 : 0;
        case DrumProperty::PROBABILITY: return authored.probability[step];
        case DrumProperty::GATE: return authored.gate[step];
        case DrumProperty::NUDGE: return authored.nudge[step];
        case DrumProperty::VELOCITY:
        case DrumProperty::COUNT:
        default: return authored.velocity[step];
    }
}

FLASHMEM bool SequencerStepEditHandler::beginDrumStepHistory(
    core::state::sequencer::SequencerHistoryDescriptor descriptor
) {
    const auto outcome = history_.beginCoalescedDrumEdit(
        descriptor,
        time_provider_()
    );
    if (core::state::sequencer::sequencerHistoryOpenAccepted(outcome)) {
        return true;
    }
    sequencer_.historyFeedback.showRejection(outcome, time_provider_());
    return false;
}

FLASHMEM bool SequencerStepEditHandler::sealDrumStepHistory(
    bool changed,
    core::state::sequencer::SequencerHistoryDescriptor descriptor,
    bool commit
) {
    if (!history_.sealCoalescedDrumEdit(changed, descriptor)) {
        sequencer_.historyFeedback.showRejection(
            core::state::sequencer::SequencerHistoryRejectionReason::HistoryUnavailable,
            time_provider_()
        );
        return false;
    }
    return !commit || commitDrumStepHistory();
}

FLASHMEM bool SequencerStepEditHandler::commitDrumStepHistory() {
    if (sequencer_.stepContentDraft.active.get()) return true;
    const auto outcome = history_.commitCoalescedDrumEditOutcome();
    if (outcome !=
        core::state::sequencer::SequencerPatternHistoryCommitOutcome::Failed) {
        return true;
    }
    sequencer_.historyFeedback.showRejection(
        core::state::sequencer::SequencerHistoryRejectionReason::HistoryUnavailable,
        time_provider_()
    );
    return false;
}

FLASHMEM void SequencerStepEditHandler::syncDrumPropertyForFocusedRow() {
    if (!drumStepEditActive()) return;
    if (step_edit_rows::isContext(sequencer_.stepEdit.focusedRow.get())) return;
    auto& drumUi = sequencer_.drumSequencer;
    const auto next = drumPropertyForEditorRow(
        sequencer_.stepEdit.focusedRow.get()
    );
    if (drumUi.property == next) return;
    drumUi.property = next;
    drumUi.bump();
}

FLASHMEM void SequencerStepEditHandler::setDrumFocusedValue(float normalized) {
    const uint8_t row = sequencer_.stepEdit.focusedRow.get();
    if (step_edit_rows::isContext(row)) return;

    if (drumChildEditActive()) {
        uint8_t childStep = 0U;
        if (!drumEditedStepInRange(childStep)) return;
        if (sequencer_.stepContentDraft.active.get()) {
            (void)step_value_row_workflow::setFocusedRowValue(
                sequencer_,
                childStep,
                effectiveScaleSettings(sequencer_, tracks_),
                normalized
            );
            return;
        }
        auto descriptor = drumStepHistoryDescriptor(
            core::state::sequencer::SequencerHistoryActionKind::DrumAdvancedContent
        );
        descriptor.property = step_edit_rows::isProperty(row)
            ? step_edit_rows::propertyForRow(row)
            : core::state::sequencer::StepProperty::NOTE;
        if (!beginDrumStepHistory(descriptor)) return;
        const bool changed = step_value_row_workflow::setFocusedRowValue(
            sequencer_,
            childStep,
            effectiveScaleSettings(sequencer_, tracks_),
            normalized
        );
        if (!sealDrumStepHistory(changed, descriptor, false)) return;
        if (changed) publishDrumAdvancedMutation(false);
        return;
    }

    auto& drumUi = sequencer_.drumSequencer;
    const uint8_t lane = sequencer_.stepEdit.drumLane;
    const uint8_t step = sequencer_.stepEdit.drumStep;
    if (!drumUi.stepInRange(lane, step)) return;

    const auto property = drumPropertyForEditorRow(
        sequencer_.stepEdit.focusedRow.get()
    );
    auto descriptor = drumStepHistoryDescriptor(
        property == DrumProperty::STATE
            ? core::state::sequencer::SequencerHistoryActionKind::DrumStepToggle
            : core::state::sequencer::SequencerHistoryActionKind::DrumStepPropertyEdit
    );
    descriptor.hasValue = true;
    descriptor.beforeValue = drumStepHistoryValue();
    if (!beginDrumStepHistory(descriptor)) return;
    const bool changed = input_utils::applyNormalizedToDrumStep(
        drumUi,
        lane,
        step,
        property,
        normalized
    );
    descriptor.afterValue = drumStepHistoryValue();
    (void)sealDrumStepHistory(changed, descriptor, false);
}

FLASHMEM void SequencerStepEditHandler::configureDrumOpt() {
    if (!drumStepEditActive()) return;
    const uint8_t row = sequencer_.stepEdit.focusedRow.get();
    if (step_edit_rows::isContext(row)) return;

    if (drumChildEditActive()) {
        uint8_t childStep = 0U;
        if (!drumEditedStepInRange(childStep)) return;
        step_value_row_workflow::configureFocusedRowEncoder(
            encoders_,
            static_cast<oc::type::EncoderID>(Config::EncoderID::OPT),
            sequencer_,
            childStep,
            effectiveScaleSettings(sequencer_, tracks_)
        );
        return;
    }

    const auto& drumUi = sequencer_.drumSequencer;
    const uint8_t lane = sequencer_.stepEdit.drumLane;
    const uint8_t step = sequencer_.stepEdit.drumStep;
    if (!drumUi.stepInRange(lane, step)) return;

    const auto encoder = static_cast<oc::type::EncoderID>(Config::EncoderID::OPT);
    const auto property = drumPropertyForEditorRow(row);
    if (property == DrumProperty::STATE) {
        encoders_.setDiscreteTicksPerStep(encoder, 8U);
        encoders_.setNormalizedTurns(encoder, 0.25f);
        encoders_.setDiscreteSteps(encoder, 2U);
        encoders_.setPosition(
            encoder,
            drumUi.drumTrack->pattern.stepEnabled(lane, step) ? 1.0f : 0.0f
        );
        return;
    }

    const auto config = input_utils::encoderConfigForDrumProperty(property);
    encoders_.setDiscreteTicksPerStep(encoder, config.discreteTicksPerStep);
    encoders_.setNormalizedTurns(encoder, config.normalizedTurns);
    encoders_.setDiscreteSteps(encoder, config.discreteSteps);
    encoders_.setPosition(
        encoder,
        input_utils::drumStepPropertyToNormalized(
            drumUi,
            lane,
            step,
            property
        )
    );
}

FLASHMEM void SequencerStepEditHandler::resetDrumFocusedValue() {
    if (step_edit_rows::isContext(sequencer_.stepEdit.focusedRow.get())) return;
    if (drumChildEditActive()) {
        uint8_t childStep = 0U;
        if (!drumEditedStepInRange(childStep)) return;
        if (sequencer_.stepContentDraft.active.get()) {
            if (step_value_row_workflow::resetFocusedRowToDefault(
                    sequencer_, childStep)) {
                configureDrumOpt();
            }
            return;
        }
        auto descriptor = drumStepHistoryDescriptor(
            core::state::sequencer::SequencerHistoryActionKind::DrumAdvancedContent
        );
        if (!beginDrumStepHistory(descriptor)) return;
        const bool changed = step_value_row_workflow::resetFocusedRowToDefault(
            sequencer_, childStep
        );
        if (!sealDrumStepHistory(changed, descriptor, true)) return;
        if (changed) publishDrumAdvancedMutation(false);
        configureDrumOpt();
        return;
    }

    auto& drumUi = sequencer_.drumSequencer;
    const uint8_t lane = sequencer_.stepEdit.drumLane;
    const uint8_t step = sequencer_.stepEdit.drumStep;
    if (!drumUi.stepInRange(lane, step)) return;
    const auto property = drumPropertyForEditorRow(
        sequencer_.stepEdit.focusedRow.get()
    );
    auto descriptor = drumStepHistoryDescriptor(
        property == DrumProperty::STATE
            ? core::state::sequencer::SequencerHistoryActionKind::DrumStepToggle
            : core::state::sequencer::SequencerHistoryActionKind::DrumStepPropertyEdit
    );
    descriptor.hasValue = true;
    descriptor.beforeValue = drumStepHistoryValue();
    if (!beginDrumStepHistory(descriptor)) return;
    const uint32_t beforeRevision = drumUi.drumTrack->pattern.revision;
    switch (property) {
        case DrumProperty::STATE:
            (void)drumUi.setStepEnabled(lane, step, false);
            break;
        case DrumProperty::PROBABILITY:
            (void)drumUi.setStepProbability(
                lane,
                step,
                core::state::sequencer::DRUM_DEFAULT_PROBABILITY
            );
            break;
        case DrumProperty::GATE:
            (void)drumUi.setStepGate(
                lane,
                step,
                core::state::sequencer::DRUM_DEFAULT_GATE_PERCENT
            );
            break;
        case DrumProperty::NUDGE:
            (void)drumUi.setStepNudge(lane, step, 0);
            break;
        case DrumProperty::VELOCITY:
        case DrumProperty::COUNT:
        default:
            (void)drumUi.setStepVelocity(
                lane,
                step,
                core::state::sequencer::DRUM_DEFAULT_VELOCITY
            );
            break;
    }
    descriptor.afterValue = drumStepHistoryValue();
    if (!sealDrumStepHistory(
            drumUi.drumTrack->pattern.revision != beforeRevision,
            descriptor,
            true
        )) {
        return;
    }
    configureDrumOpt();
}

FLASHMEM bool SequencerStepEditHandler::beginPreparedPatternMutation(
    core::state::sequencer::SequencerPreparedPatternEditOwner owner, uint8_t key,
    core::state::sequencer::SequencerCoalescedPatternPayloadPlan payloadPlan,
    core::state::sequencer::SequencerHistoryDescriptor descriptor, bool compactGraphOnSeal) {
    if (sequencer_.stepContentDraft.active.get()) return true;
    const auto outcome =
        history_.beginPreparedPatternEdit(owner, key, payloadPlan, descriptor,
                                             compactGraphOnSeal);
    if (core::state::sequencer::sequencerHistoryOpenAccepted(outcome)) return true;
    sequencer_.historyFeedback.showRejection(outcome, oc::time::millis());
    return false;
}

FLASHMEM bool SequencerStepEditHandler::sealPreparedPatternMutation(
    core::state::sequencer::SequencerPreparedPatternEditOwner owner, uint8_t key, bool changed,
    core::state::sequencer::SequencerHistoryDescriptor descriptor) {
    if (sequencer_.stepContentDraft.active.get()) return true;
    const auto outcome = history_.sealPreparedPatternEdit(owner, key, changed, descriptor);
    if (!core::state::sequencer::sequencerPreparedPatternEditSealFailed(outcome)) return true;
    sequencer_.historyFeedback.showRejection(
        core::state::sequencer::SequencerHistoryRejectionReason::HistoryUnavailable,
        oc::time::millis());
    return false;
}

FLASHMEM bool SequencerStepEditHandler::commitPreparedPatternMutation(
    core::state::sequencer::SequencerPreparedPatternEditOwner owner) {
    if (sequencer_.stepContentDraft.active.get()) return true;
    if (history_.commitPreparedPatternEdit(owner) !=
           core::state::sequencer::SequencerPreparedPatternEditCommitOutcome::Failed) {
        return true;
    }
    sequencer_.historyFeedback.showRejection(
        core::state::sequencer::SequencerHistoryRejectionReason::HistoryUnavailable,
        oc::time::millis());
    return false;
}

FLASHMEM void SequencerStepEditHandler::backFromStepEdit() {
    if (drumStepEditActive()) {
        if (drumChildEditActive()) {
            closeDrumStepEditor();
            return;
        }
        closeDrumStepEditor();
        return;
    }
    if (sequencer_.stepContentDraft.exitPromptVisible.get()) {
        // Back from the explicit decision surface means Continue editing.
        sequencer_.stepContentDraft.hideExitPrompt();
        return;
    }

    if (chordEditorActive() && step_chord_editor_workflow::cancelSubEditor(sequencer_)) {
        configureOptForFocusedRow();
        return;
    }

    if (chordEditorActive() && sequencer_.stepContentDraft.active.get()) {
        // A Chord opened inside a new Micro/Cycle belongs to that outer draft;
        // leaving the detail editor must not discard the owning content graph.
        if (sequencer_.stepContentDraft.kind.get() !=
            core::state::sequencer::SequencerStepContentDraftKind::CHORD) {
            closeChordEditor();
            return;
        }
        const auto result =
            sequencer::step_content_draft_workflow::requestBack(sequencer_, history_);
        if (result == sequencer::step_content_draft_workflow::BackResult::DISCARDED) {
            closeChordEditor();
        }
        return;
    }

    if (chordEditorActive()) {
        closeChordEditor();
        return;
    }

    if (core::state::sequencer::isChildContentView(sequencer_) &&
        sequencer_.stepContentDraft.active.get()) {
        const auto result =
            sequencer::step_content_draft_workflow::requestBack(sequencer_, history_);
        if (result != sequencer::step_content_draft_workflow::BackResult::DISCARDED) { return; }
        if (step_edit_session_workflow::backToParentContent(sequencer_, history_)) {
            configureOptForFocusedRow();
        }
        return;
    }

    if (step_edit_session_workflow::backToParentContent(sequencer_, history_)) {
        configureOptForFocusedRow();
        return;
    }

    closeStepEdit();
}

FLASHMEM void SequencerStepEditHandler::closeStepEdit() {
    if (drumStepEditActive()) {
        closeDrumStepEditor();
        return;
    }
    if (chordEditorActive() && sequencer_.stepContentDraft.active.get()) {
        backFromStepEdit();
        if (sequencer_.stepContentDraft.active.get()) return;
    }
    step_retarget_active_ = false;
    lane_retarget_active_ = false;
    if (!step_edit_session_workflow::close(sequencer_, history_, context_release_latch_,
                                           overlays_)) {
        return;
    }
}

FLASHMEM void SequencerStepEditHandler::moveFocus(float delta) {
    if (!nav::hasTurnDelta(delta)) return;

    if (sequencer_.stepContentDraft.exitPromptVisible.get()) {
        sequencer::step_content_draft_workflow::moveExitChoice(sequencer_, delta);
        return;
    }

    if (chordEditorActive()) {
        moveChordEditorFocus(delta);
        return;
    }

    if (drumStepEditActive()) {
        if (!commitDrumStepHistory()) return;
        const int current = step_edit_rows::drumNavigationIndexForRow(
            sequencer_.stepEdit.focusedRow.get()
        );
        const int next = nav::nextWrappedIndex(
            delta,
            current,
            static_cast<int>(step_edit_rows::DRUM_NAVIGATION_ORDER.size())
        );
        sequencer_.stepEdit.contextHold.clear();
        sequencer_.stepEdit.localVariationEditActive.set(false);
        sequencer_.stepEdit.focusedRow.set(
            step_edit_rows::drumRowForNavigationIndex(next)
        );
        syncDrumPropertyForFocusedRow();
        configureDrumOpt();
        return;
    }

    const int current = step_edit_rows::navigationIndexForRow(sequencer_.stepEdit.focusedRow.get());
    const int next = nav::nextWrappedIndex(
        delta, current, static_cast<int>(step_edit_rows::NAVIGATION_ORDER.size()));
    sequencer_.stepEdit.contextHold.clear();
    sequencer_.stepEdit.localVariationEditActive.set(false);
    sequencer_.stepEdit.focusedRow.set(step_edit_rows::rowForNavigationIndex(next));

    configureOptForFocusedRow();
}

FLASHMEM void SequencerStepEditHandler::retargetEditedStep(float delta) {
    if (!nav::hasTurnDelta(delta) || chordEditorActive()) return;
    if (drumStepEditActive()) {
        if (!commitDrumStepHistory()) return;
        if (drumChildEditActive()) {
            const uint8_t length = core::state::sequencer::activeContentLength(sequencer_);
            if (length == 0U) return;
            const uint8_t current = std::min<uint8_t>(
                sequencer_.stepEdit.stepIndex.get(),
                static_cast<uint8_t>(length - 1U)
            );
            const int direction = nav::turnStep(delta);
            const uint8_t next = direction > 0
                ? static_cast<uint8_t>((static_cast<uint16_t>(current) + 1U) % length)
                : current == 0U
                ? static_cast<uint8_t>(length - 1U)
                : static_cast<uint8_t>(current - 1U);
            if (next == current) return;
            sequencer_.stepEdit.stepIndex.set(next);
            sequencer_.focusedStep.set(next);
            configureDrumOpt();
            return;
        }
        auto& drumUi = sequencer_.drumSequencer;
        const uint8_t lane = sequencer_.stepEdit.drumLane;
        if (!drumUi.drumTrack || lane >= drumUi.LANE_COUNT) return;
        const uint8_t length = drumUi.drumTrack->pattern.effectiveLength(lane);
        if (length == 0U) return;
        const uint8_t current = std::min<uint8_t>(
            sequencer_.stepEdit.stepIndex.get(),
            static_cast<uint8_t>(length - 1U)
        );
        const int direction = nav::turnStep(delta);
        const uint8_t next = direction > 0
            ? static_cast<uint8_t>((static_cast<uint16_t>(current) + 1U) % length)
            : current == 0U
            ? static_cast<uint8_t>(length - 1U)
            : static_cast<uint8_t>(current - 1U);
        if (next == current || !drumUi.focusStep(lane, next)) return;
        sequencer_.stepEdit.drumStep = next;
        sequencer_.stepEdit.stepIndex.set(next);
        const int16_t rootSlot = drumUi.drumTrack->advancedRootSlot(lane, next);
        sequencer_.stepEdit.drumRootSlot = rootSlot >= 0
            ? static_cast<uint8_t>(rootSlot)
            : 0xFFU;
        configureDrumOpt();
        return;
    }
    if (step_edit_session_workflow::retargetRootStep(sequencer_, history_, nav::turnStep(delta))) {
        configureOptForFocusedRow();
    }
}

FLASHMEM bool SequencerStepEditHandler::canRetargetEditedDrumLane() const {
    if (!drumStepEditActive() || drumChildEditActive() ||
        sequencer_.stepContentDraft.exitPromptVisible.get()) {
        return false;
    }
    uint8_t adjacentLane = 0U;
    return sequencer_.drumSequencer.adjacentLaneForStep(
        sequencer_.stepEdit.drumLane,
        sequencer_.stepEdit.drumStep,
        1,
        adjacentLane
    );
}

FLASHMEM void SequencerStepEditHandler::retargetEditedDrumLane(float delta) {
    if (!nav::hasTurnDelta(delta) || !canRetargetEditedDrumLane()) return;
    if (!commitDrumStepHistory()) return;

    auto& drumUi = sequencer_.drumSequencer;
    auto& edit = sequencer_.stepEdit;
    uint8_t nextLane = edit.drumLane;
    if (!drumUi.adjacentLaneForStep(
            edit.drumLane,
            edit.drumStep,
            nav::turnStep(delta),
            nextLane
        ) ||
        !drumUi.focusStep(nextLane, edit.drumStep)) {
        return;
    }

    edit.drumLane = nextLane;
    edit.stepIndex.set(edit.drumStep);
    const int16_t rootSlot = drumUi.drumTrack->advancedRootSlot(
        nextLane,
        edit.drumStep
    );
    edit.drumRootSlot = rootSlot >= 0
        ? static_cast<uint8_t>(rootSlot)
        : 0xFFU;
    syncDrumPropertyForFocusedRow();
    configureDrumOpt();
}

FLASHMEM void SequencerStepEditHandler::activateFocusedRowOrClose() {
    if (sequencer_.stepContentDraft.exitPromptVisible.get()) {
        confirmStepContentDraftExitChoice();
        return;
    }

    auto& edit = sequencer_.stepEdit;
    const uint8_t focusedRow = edit.focusedRow.get();

    if (drumStepEditActive()) {
        if (step_edit_rows::isContext(focusedRow)) {
            (void)openDrumOwnedSharedContentChild();
            return;
        }
        if (step_edit_rows::isActivated(focusedRow)) {
            if (drumChildEditActive()) {
                uint8_t childStep = 0U;
                if (!drumEditedStepInRange(childStep)) return;
                if (sequencer_.stepContentDraft.active.get()) {
                    if (core::state::sequencer::toggleActiveContentStep(
                            sequencer_, childStep)) {
                        configureDrumOpt();
                    }
                    return;
                }
                auto descriptor = drumStepHistoryDescriptor(
                    core::state::sequencer::SequencerHistoryActionKind::DrumAdvancedContent
                );
                if (!beginDrumStepHistory(descriptor)) return;
                const bool changed =
                    core::state::sequencer::toggleActiveContentStep(
                        sequencer_, childStep
                    );
                if (!sealDrumStepHistory(changed, descriptor, true)) return;
                if (changed) publishDrumAdvancedMutation(false);
                configureDrumOpt();
                return;
            }
            auto& drumUi = sequencer_.drumSequencer;
            const uint8_t lane = edit.drumLane;
            const uint8_t step = edit.drumStep;
            if (!drumUi.stepInRange(lane, step)) return;
            auto descriptor = drumStepHistoryDescriptor(
                core::state::sequencer::SequencerHistoryActionKind::DrumStepToggle
            );
            descriptor.hasValue = true;
            descriptor.beforeValue = drumStepHistoryValue();
            if (!beginDrumStepHistory(descriptor)) return;
            const bool changed = drumUi.setStepEnabled(
                lane,
                step,
                !drumUi.drumTrack->pattern.stepEnabled(lane, step)
            );
            descriptor.afterValue = drumStepHistoryValue();
            if (!sealDrumStepHistory(changed, descriptor, true)) return;
            configureDrumOpt();
        } else {
            (void)commitDrumStepHistory();
        }
        // Scalar Drum rows are edited live with OPT; NAV confirms harmlessly.
        return;
    }

    if (chordEditorActive()) {
        if (!step_chord_editor_workflow::formulaEditorActive(sequencer_) &&
            !step_chord_editor_workflow::sourceSelectorActive(sequencer_) &&
            sequencer_.stepEdit.chordEditor.focusedField.get() ==
                core::state::sequencer::SequencerChordEditField::PITCH_CONTEXT) {
            openPitchContextSettings();
            return;
        }
        uint8_t step = 0;
        if (editedStepInRange(step) &&
            step_chord_editor_workflow::activateFocusedItem(
                sequencer_, step, effectiveScaleSettings(sequencer_, tracks_))) {
            configureOptForFocusedRow();
            return;
        }
        backFromStepEdit();
        return;
    }

    if (step_edit_rows::isProperty(focusedRow)) {
        // Scalar rows are already edited live with OPT. NAV is deliberately a
        // harmless confirmation so the same gesture never means "close" only
        // for Note/Velocity/Gate/Nudge.
        return;
    }

    if (step_edit_rows::isChord(focusedRow)) {
        openChordEditor();
        return;
    }

    if (step_edit_rows::isActivated(focusedRow)) {
        uint8_t abs = 0;
        if (!editedStepInRange(abs)) return;
        const bool before = core::state::sequencer::activeContentStepEnabled(sequencer_, abs);
        auto descriptor = stepEditDescriptor(abs);
        descriptor.hasValue = true;
        descriptor.beforeValue = before ? 1 : 0;
        descriptor.afterValue = before ? 0 : 1;
        constexpr auto owner =
            core::state::sequencer::SequencerPreparedPatternEditOwner::StepEditSession;
        if (!beginPreparedPatternMutation(owner, abs, stepEditPayloadPlan(sequencer_, false),
                                          descriptor)) {
            return;
        }
        const bool changed = core::state::sequencer::toggleActiveContentStep(sequencer_, abs);
        if (!sealPreparedPatternMutation(owner, abs, changed, descriptor)) { return; }
        if (changed) { configureOptForFocusedRow(); }
        return;
    }

    if (step_edit_rows::isContext(focusedRow)) {
        (void)activateFocusedContextRow();
        return;
    }
}

FLASHMEM bool SequencerStepEditHandler::activateFocusedContextRow() {
    uint8_t step = 0;
    if (!editedStepInRange(step)) return false;
    if (!commitStepEditHistory()) return false;

    const auto result =
        step_context_row_workflow::openOrCreateFocusedContextChild(sequencer_, step);
    if (!result.opened) return false;

    sequencer_.contextSelector.reset();
    overlays_.hide();
    sequencer_.stepEdit.reset();
    return true;
}

FLASHMEM void SequencerStepEditHandler::setFocusedValue(float normalized) {
    if (sequencer_.stepContentDraft.exitPromptVisible.get()) return;
    if (chordEditorActive()) {
        setFocusedChordFieldValue(normalized);
        return;
    }
    if (drumStepEditActive()) {
        setDrumFocusedValue(normalized);
        return;
    }
    if (step_edit_rows::isChord(sequencer_.stepEdit.focusedRow.get())) { return; }

    uint8_t abs = 0;
    if (!editedStepInRange(abs)) return;
    auto descriptor = stepEditDescriptor(abs);
    if (step_edit_rows::isProperty(sequencer_.stepEdit.focusedRow.get())) {
        descriptor.property = step_edit_rows::propertyForRow(sequencer_.stepEdit.focusedRow.get());
    }
    const bool mayGrowGraph =
        sequencer_.stepEdit.localVariationEditActive.get() && focusedRowSupportsLocalVariation();
    constexpr auto owner =
        core::state::sequencer::SequencerPreparedPatternEditOwner::StepEditSession;
    if (!beginPreparedPatternMutation(owner, abs, stepEditPayloadPlan(sequencer_, mayGrowGraph),
                                      descriptor)) {
        return;
    }
    const bool changed = step_value_row_workflow::setFocusedRowValue(
        sequencer_, abs, effectiveScaleSettings(sequencer_, tracks_), normalized);
    if (!sealPreparedPatternMutation(owner, abs, changed, descriptor)) return;
}

FLASHMEM void SequencerStepEditHandler::configureOptForFocusedRow() {
    if (drumStepEditActive()) {
        configureDrumOpt();
        return;
    }
    if (chordEditorActive()) {
        configureOptForFocusedChordField();
        return;
    }
    if (step_edit_rows::isChord(sequencer_.stepEdit.focusedRow.get())) { return; }

    uint8_t abs = 0;
    if (!editedStepInRange(abs)) return;
    step_value_row_workflow::configureFocusedRowEncoder(
        encoders_, static_cast<oc::type::EncoderID>(Config::EncoderID::OPT), sequencer_, abs,
        effectiveScaleSettings(sequencer_, tracks_));
}

FLASHMEM void SequencerStepEditHandler::openChordEditor() {
    uint8_t step = 0;
    if (!editedStepInRange(step)) return;

    const bool existed = editedStepHasChordState(sequencer_, step);
    const auto nodeId = core::state::sequencer::activeContentStepNodeId(sequencer_, step);
    bool startedDraft = false;
    // Every Chord editor is transactional. Existing Local/Parent state is
    // copied into the same lightweight Chord draft used for creation; a Chord
    // opened inside a Micro/Cycle draft keeps using that outer draft.
    if (!sequencer_.stepContentDraft.active.get()) {
        if (!commitStepEditHistory()) return;
        startedDraft = core::state::sequencer::beginStepContentDraft(
            sequencer_, core::state::sequencer::SequencerStepContentDraftKind::CHORD, step, nodeId);
        if (!startedDraft) return;
    }

    step_chord_editor_workflow::open(sequencer_);
    if (!existed) {
        // The seeded musical default is the pristine creation baseline: Back
        // immediately after opening abandons it without a confirmation.
        if (core::state::sequencer::isRootContentView(sequencer_)) {
            const auto scale = effectiveScaleSettings(sequencer_, tracks_);
            (void)core::handler::sequencer::chord_edit_ops::createDefaultLocalChord(
                sequencer_, step,
                core::state::sequencer::pitchContextUsesScaleDegrees(
                    core::state::sequencer::authoringPattern(sequencer_).pitchEditMode, scale));
        }
        if (startedDraft) { core::state::sequencer::markStepContentDraftPristine(sequencer_); }
    }
    configureOptForFocusedRow();
}

FLASHMEM void SequencerStepEditHandler::closeChordEditor() {
    step_chord_editor_workflow::close(sequencer_);
    configureOptForFocusedRow();
}

FLASHMEM void SequencerStepEditHandler::applyStepContentDraft() {
    const bool returnToPitchRow =
        chordEditorActive() &&
        sequencer_.stepContentDraft.kind.get() ==
            core::state::sequencer::SequencerStepContentDraftKind::CHORD;
    if (!commitStepEditHistory()) return;
    if (!sequencer::step_content_draft_workflow::apply(sequencer_, tracks_, history_)) { return; }

    if (returnToPitchRow) {
        step_chord_editor_workflow::close(sequencer_);
        sequencer_.stepEdit.focusedRow.set(step_edit_rows::PROPERTY_OFFSET);
    }
    configureOptForFocusedRow();
}

FLASHMEM void SequencerStepEditHandler::confirmStepContentDraftExitChoice() {
    const bool wasChordEditor = chordEditorActive();
    const bool wasRootChordDraft =
        wasChordEditor &&
        sequencer_.stepContentDraft.kind.get() ==
            core::state::sequencer::SequencerStepContentDraftKind::CHORD;
    const bool wasChildContent = core::state::sequencer::isChildContentView(sequencer_);
    const auto result =
        sequencer::step_content_draft_workflow::applyExitChoice(sequencer_, tracks_, history_);
    using Result = sequencer::step_content_draft_workflow::BackResult;
    if (result != Result::DISCARDED && result != Result::SAVED) return;

    if (wasChordEditor) {
        if (wasRootChordDraft && result == Result::SAVED) {
            step_chord_editor_workflow::close(sequencer_);
            sequencer_.stepEdit.focusedRow.set(step_edit_rows::PROPERTY_OFFSET);
            configureOptForFocusedRow();
            return;
        }
        closeChordEditor();
        return;
    }
    if (wasChildContent && step_edit_session_workflow::backToParentContent(sequencer_, history_)) {
        configureOptForFocusedRow();
    }
}

FLASHMEM void SequencerStepEditHandler::moveChordEditorFocus(float delta) {
    uint8_t step = 0;
    if (!editedStepInRange(step)) return;
    step_chord_editor_workflow::moveFocus(sequencer_, step,
                                          effectiveScaleSettings(sequencer_, tracks_), delta);
    configureOptForFocusedRow();
}

FLASHMEM void SequencerStepEditHandler::setFocusedChordFieldValue(float normalized) {
    uint8_t step = 0;
    if (!editedStepInRange(step)) return;

    step_chord_editor_workflow::setFocusedFieldValue(
        sequencer_, step, effectiveScaleSettings(sequencer_, tracks_), normalized);
}

FLASHMEM void SequencerStepEditHandler::configureOptForFocusedChordField() {
    uint8_t step = 0;
    if (!editedStepInRange(step)) return;

    step_chord_editor_workflow::configureFocusedFieldEncoder(
        encoders_, static_cast<oc::type::EncoderID>(Config::EncoderID::OPT), sequencer_, step,
        effectiveScaleSettings(sequencer_, tracks_));
}

FLASHMEM void SequencerStepEditHandler::resetFocusedChordFieldToDefault() {
    uint8_t step = 0;
    if (!editedStepInRange(step)) return;

    if (!step_chord_editor_workflow::resetFocusedFieldToDefault(
            sequencer_, step, effectiveScaleSettings(sequencer_, tracks_)))
        return;
    configureOptForFocusedRow();
}

FLASHMEM void SequencerStepEditHandler::toggleChordSourceSelector() {
    if (!chordEditorActive() || sequencer_.stepContentDraft.exitPromptVisible.get()) { return; }
    uint8_t step = 0;
    if (!editedStepInRange(step)) return;
    step_chord_editor_workflow::toggleSourceSelector(sequencer_, step,
                                                     effectiveScaleSettings(sequencer_, tracks_));
    configureOptForFocusedRow();
}

FLASHMEM void SequencerStepEditHandler::openPitchContextSettings() {
    if (!chordEditorActive() || pitch_context_settings_open_) return;

    // Pitch Context is Pattern-global, so it remains a separate Undo record
    // even when reached from a Chord draft. The projection service also
    // adapts the current draft formula without publishing it.
    if (!sequencer_.stepContentDraft.active.get()) {
        if (!commitStepEditHistory()) return;
    }
    sequencer_.stepPropertyInlineSelector.reset();
    pattern_pitch_settings_.openOverlay();
    pattern_pitch_settings_.focusedRow.set(3U);
    overlays_.show(core::ui::OverlayType::PATTERN_PITCH_SETTINGS, true);
    pitch_context_settings_open_ = true;
}

FLASHMEM bool SequencerStepEditHandler::chordEditorActive() const {
    return step_chord_editor_workflow::active(sequencer_);
}

FLASHMEM bool SequencerStepEditHandler::editedStepInRange(uint8_t& step) const {
    if (drumStepEditActive()) {
        return drumEditedStepInRange(step);
    }
    return step_edit_session_workflow::editedStepInRange(sequencer_, step);
}

FLASHMEM void SequencerStepEditHandler::maybeCloseFromMacro(uint8_t indexInPage) {
    if (sequencer_.stepContentDraft.exitPromptVisible.get()) return;
    if (drumStepEditActive()) {
        if (drumChildEditActive()) {
            if (indexInPage == static_cast<uint8_t>(
                    sequencer_.stepEdit.stepIndex.get() %
                    core::state::sequencer::SequencerState::STEPS_PER_PAGE
                )) {
                closeDrumStepEditor();
            }
            return;
        }
        if (indexInPage == static_cast<uint8_t>(
                sequencer_.stepEdit.drumStep %
                core::state::sequencer::DrumSequencerState::STEPS_PER_PAGE
            )) {
            closeDrumStepEditor();
        }
        return;
    }
    if (step_edit_session_workflow::shouldCloseFromMacro(sequencer_, indexInPage)) {
        closeStepEdit();
    }
}

FLASHMEM bool SequencerStepEditHandler::focusedRowIsValueRow() const {
    if (chordEditorActive()) return true;
    if (drumStepEditActive()) {
        const uint8_t row = sequencer_.stepEdit.focusedRow.get();
        return step_edit_rows::isActivated(row) ||
               (step_edit_rows::isProperty(row) &&
                step_edit_rows::propertyForRow(row) !=
                    core::state::sequencer::StepProperty::NOTE);
    }
    return !step_edit_rows::isChord(sequencer_.stepEdit.focusedRow.get()) &&
           step_value_row_workflow::focusedRowIsValue(sequencer_);
}

FLASHMEM bool SequencerStepEditHandler::focusedRowIsContextRow() const {
    if (chordEditorActive()) return false;
    if (drumStepEditActive()) {
        return step_edit_rows::isContext(sequencer_.stepEdit.focusedRow.get());
    }
    return step_context_row_workflow::focusedRowIsContext(sequencer_);
}

FLASHMEM bool SequencerStepEditHandler::focusedRowSupportsLocalVariation() const {
    if (chordEditorActive()) return false;
    if (drumStepEditActive()) {
        return drumChildEditActive() &&
            step_value_row_workflow::focusedRowSupportsLocalVariation(sequencer_);
    }
    return step_value_row_workflow::focusedRowSupportsLocalVariation(sequencer_);
}

FLASHMEM bool SequencerStepEditHandler::focusedContextHasChild() const {
    if (!focusedRowIsContextRow()) return false;

    if (drumStepEditActive()) return drumFocusedContextHasChild();

    uint8_t step = 0;
    if (!editedStepInRange(step)) return false;

    return step_context_row_workflow::focusedContextHasChild(sequencer_, step);
}

FLASHMEM bool SequencerStepEditHandler::canPasteFocusedStepContent() const {
    if (drumStepEditActive()) return drumCanPasteFocusedContext();
    uint8_t step = 0;
    if (!editedStepInRange(step)) return false;

    return step_context_row_workflow::canPasteFocusedContextChild(sequencer_, step,
                                                                  structure_clipboard_);
}

FLASHMEM void SequencerStepEditHandler::resetFocusedValueRowToDefault() {
    if (sequencer_.stepContentDraft.exitPromptVisible.get()) return;
    if (!focusedRowIsValueRow()) return;

    if (chordEditorActive()) {
        resetFocusedChordFieldToDefault();
        return;
    }

    if (drumStepEditActive()) {
        resetDrumFocusedValue();
        return;
    }

    uint8_t step = 0;
    if (!editedStepInRange(step)) return;

    auto descriptor = stepEditDescriptor(step);
    if (step_edit_rows::isProperty(sequencer_.stepEdit.focusedRow.get())) {
        descriptor.property = step_edit_rows::propertyForRow(sequencer_.stepEdit.focusedRow.get());
    }
    constexpr auto owner =
        core::state::sequencer::SequencerPreparedPatternEditOwner::StepEditSession;
    if (!beginPreparedPatternMutation(owner, step, stepResetPayloadPlan(sequencer_, step),
                                      descriptor)) {
        return;
    }
    const bool changed = step_value_row_workflow::resetFocusedRowToDefault(sequencer_, step);
    if (!sealPreparedPatternMutation(owner, step, changed, descriptor)) return;
    if (!changed) return;
    configureOptForFocusedRow();
}

FLASHMEM void SequencerStepEditHandler::clearFocusedContextChild() {
    if (sequencer_.stepContentDraft.exitPromptVisible.get()) return;
    if (!focusedContextHasChild()) return;
    if (drumStepEditActive()) {
        if (drumChildEditActive() &&
            sequencer_.stepContentDraft.active.get()) {
            uint8_t childStep = 0U;
            if (drumEditedStepInRange(childStep)) {
                (void)step_context_row_workflow::clearFocusedContextChild(
                    sequencer_, childStep
                );
            }
            return;
        }
        if (!commitDrumStepHistory()) return;
        auto descriptor = drumStepHistoryDescriptor(
            core::state::sequencer::SequencerHistoryActionKind::DrumAdvancedContent
        );
        if (!beginDrumStepHistory(descriptor)) return;

        const auto kind = step_edit_rows::childKindForContextRow(
            sequencer_.stepEdit.focusedRow.get()
        );
        bool mappingChanged = false;
        bool changed = false;
        if (drumChildEditActive()) {
            uint8_t childStep = 0U;
            changed = drumEditedStepInRange(childStep) &&
                core::state::sequencer::clearActiveContentChild(
                    sequencer_, childStep, kind
                );
        } else {
            core::state::sequencer::SequencerGraphNodeId nodeId =
                oc::note::sequencer::StepSequencerGraphLimits::INVALID_ID;
            if (resolveDrumRootNodeId(nodeId)) {
                auto& pattern = core::state::sequencer::authoringPattern(sequencer_);
                changed = kind ==
                        core::state::sequencer::StepContentChildKind::MICRO_SEQUENCE
                    ? core::state::sequencer::clearNodeChildSequence(pattern, nodeId)
                    : core::state::sequencer::clearNodeCycleStateSet(pattern, nodeId);
                if (changed) {
                    (void)core::state::sequencer::compactSequencerGraph(sequencer_);
                    if (!core::state::sequencer::stepNodeHasAnyChildContent(
                            pattern, nodeId
                        )) {
                        mappingChanged = sequencer_.drumSequencer.drumTrack
                            ->releaseAdvancedRootSlot(
                                sequencer_.stepEdit.drumLane,
                                sequencer_.stepEdit.drumStep
                            );
                        if (mappingChanged) {
                            sequencer_.stepEdit.drumRootSlot = 0xFFU;
                        }
                    }
                }
            }
        }
        if (changed) publishDrumAdvancedMutation(mappingChanged);
        (void)sealDrumStepHistory(changed, descriptor, true);
        return;
    }
    uint8_t step = 0;
    if (!editedStepInRange(step)) return;
    if (!commitStepEditHistory()) return;

    constexpr auto owner = core::state::sequencer::SequencerPreparedPatternEditOwner::StepContent;
    const auto descriptor = stepEditDescriptor(step);
    if (!beginPreparedPatternMutation(
            owner, step,
            core::state::sequencer::SequencerCoalescedPatternPayloadPlan::FullCurrentPayload,
            descriptor, true)) {
        return;
    }
    const bool changed = step_context_row_workflow::clearFocusedContextChild(sequencer_, step);
    if (!sealPreparedPatternMutation(owner, step, changed, descriptor)) return;
    if (!commitPreparedPatternMutation(owner)) return;
}

FLASHMEM void SequencerStepEditHandler::copyFocusedStepContent() {
    if (sequencer_.stepContentDraft.exitPromptVisible.get()) return;
    if (!focusedContextHasChild()) return;
    if (drumStepEditActive()) {
        const auto kind = step_edit_rows::childKindForContextRow(
            sequencer_.stepEdit.focusedRow.get()
        );
        if (drumChildEditActive()) {
            uint8_t childStep = 0U;
            if (drumEditedStepInRange(childStep)) {
                (void)core::state::sequencer::copyActiveContentChildToClipboard(
                    sequencer_, childStep, kind, structure_clipboard_
                );
            }
            return;
        }
        core::state::sequencer::SequencerGraphNodeId nodeId =
            oc::note::sequencer::StepSequencerGraphLimits::INVALID_ID;
        const auto* graph = core::state::sequencer::graphView(
            core::state::sequencer::authoringPattern(sequencer_)
        );
        if (graph != nullptr && resolveDrumRootNodeId(nodeId)) {
            (void)structure_clipboard_.storeSequencerStepContent(
                *graph,
                nodeId,
                drumClipboardKindForRow(sequencer_.stepEdit.focusedRow.get())
            );
        }
        return;
    }
    uint8_t step = 0;
    if (!editedStepInRange(step)) return;

    step_context_row_workflow::copyFocusedContextChildToClipboard(sequencer_, step,
                                                                  structure_clipboard_);
}

FLASHMEM void SequencerStepEditHandler::pasteFocusedStepContent() {
    if (sequencer_.stepContentDraft.exitPromptVisible.get()) return;
    if (!canPasteFocusedStepContent()) return;
    if (drumStepEditActive()) {
        if (drumChildEditActive() &&
            sequencer_.stepContentDraft.active.get()) {
            uint8_t childStep = 0U;
            if (drumEditedStepInRange(childStep)) {
                (void)step_context_row_workflow::pasteFocusedContextChildFromClipboard(
                    sequencer_, childStep, structure_clipboard_
                );
            }
            return;
        }
        if (!commitDrumStepHistory()) return;
        auto descriptor = drumStepHistoryDescriptor(
            core::state::sequencer::SequencerHistoryActionKind::DrumAdvancedContent
        );
        if (!beginDrumStepHistory(descriptor)) return;

        const auto kind = step_edit_rows::childKindForContextRow(
            sequencer_.stepEdit.focusedRow.get()
        );
        bool mappingChanged = false;
        bool changed = false;
        if (drumChildEditActive()) {
            uint8_t childStep = 0U;
            changed = drumEditedStepInRange(childStep) &&
                core::state::sequencer::pasteActiveContentChildFromClipboard(
                    sequencer_, childStep, kind, structure_clipboard_
                );
        } else {
            core::state::sequencer::SequencerGraphNodeId nodeId =
                oc::note::sequencer::StepSequencerGraphLimits::INVALID_ID;
            if (ensureDrumRootNodeId(nodeId, mappingChanged) &&
                structure_clipboard_.sequencerGraph) {
                auto& pattern = core::state::sequencer::authoringPattern(sequencer_);
                changed = kind ==
                        core::state::sequencer::StepContentChildKind::MICRO_SEQUENCE
                    ? core::state::sequencer::copyNodeChildSequenceFromGraph(
                          pattern,
                          nodeId,
                          *structure_clipboard_.sequencerGraph,
                          structure_clipboard_.sequencerStepContentNodeId
                      )
                    : core::state::sequencer::copyNodeCycleStateSetFromGraph(
                          pattern,
                          nodeId,
                          *structure_clipboard_.sequencerGraph,
                          structure_clipboard_.sequencerStepContentNodeId
                      );
            }
        }
        if (!changed) {
            (void)history_.abortCoalescedDrumEdit();
            return;
        }
        const bool mutationChanged = true;
        if (mutationChanged) publishDrumAdvancedMutation(mappingChanged);
        (void)sealDrumStepHistory(mutationChanged, descriptor, true);
        return;
    }
    uint8_t step = 0;
    if (!editedStepInRange(step)) return;
    if (!commitStepEditHistory()) return;

    constexpr auto owner = core::state::sequencer::SequencerPreparedPatternEditOwner::StepContent;
    const auto descriptor = stepEditDescriptor(step);
    const auto payloadPlan =
        core::state::sequencer::graphView(sequencer_.pattern) == nullptr
            ? core::state::sequencer::SequencerCoalescedPatternPayloadPlan::FullWithProspectiveGraph
            : core::state::sequencer::SequencerCoalescedPatternPayloadPlan::FullCurrentPayload;
    if (!beginPreparedPatternMutation(owner, step, payloadPlan, descriptor, true)) { return; }
    const bool changed = step_context_row_workflow::pasteFocusedContextChildFromClipboard(
        sequencer_, step, structure_clipboard_);
    if (!sealPreparedPatternMutation(owner, step, changed, descriptor)) return;
    if (!commitPreparedPatternMutation(owner)) return;
}

}  // namespace core::handler
