#include "SequencerStepHandler.hpp"

#include <config/App.hpp>
#include <config/Timing.hpp>
#include <config/InputIDs.hpp>
#include <config/PlatformCompat.hpp>
#include <config/TimeCompat.hpp>

#include "handler/sequencer/ProjectTrackEditorHandler.hpp"
#include "handler/sequencer/DrumLaneEditorHandler.hpp"
#include "handler/sequencer/SequencerInputUtils.hpp"
#include "handler/sequencer/SequencerPatternEditorHandler.hpp"
#include "handler/sequencer/SequencerStepContentDraftWorkflow.hpp"
#include "handler/sequencer/SequencerStepEditHandler.hpp"
#include "state/sequencer/SequencerContentViewOps.hpp"
#include "state/sequencer/SequencerStepEditRows.hpp"

#if defined(MS_UX_RECORDER)
#include "validation/ux/SemanticUxTraceState.hpp"
#endif

namespace core::handler {

namespace input_utils = core::handler::sequencer::input_utils;

namespace {

using SelectionAction = core::state::StructureSelectionInteractionAction;
namespace seq = core::state::sequencer;

constexpr auto kStepToggleOwner = seq::SequencerPreparedPatternEditOwner::StepToggle;

FLASHMEM bool commitPatternHistoryBarrier(
    const seq::SequencerState& sequencer,
    SequencerHistoryDomainServices& history
) {
    // A new Drum Micro/Cycle is one transactional draft. Its prepared Drum
    // history entry intentionally remains unsealed until Apply, while moving
    // around the child is presentation-only. Trying to commit it here would
    // reject ordinary navigation as "History unavailable".
    if (sequencer.stepContentDraft.active.get() &&
        seq::isDrumContentView(sequencer)) {
        return true;
    }
    return history.commitCoalescedPatternEditOutcome() !=
           seq::SequencerPatternHistoryCommitOutcome::Failed;
}

FLASHMEM bool publishPatternHistoryBarrier(seq::SequencerState& sequencer, bool committed) {
    if (committed) return true;
    sequencer.historyFeedback.showRejection(
        seq::SequencerHistoryRejectionReason::HistoryUnavailable, core::time_compat::millis());
    return false;
}

FLASHMEM seq::SequencerHistoryDescriptor stepToggleDescriptor(uint8_t step, bool beforeEnabled,
                                                              bool afterEnabled) {
    return {
        .kind = seq::SequencerHistoryActionKind::StepToggle,
        .stepIndex = step,
        .property = seq::StepProperty::NOTE,
        .hasValue = beforeEnabled != afterEnabled,
        .beforeValue = beforeEnabled ? 1 : 0,
        .afterValue = afterEnabled ? 1 : 0,
    };
}

FLASHMEM seq::SequencerHistoryDescriptor drumHistoryDescriptor(
    const seq::DrumSequencerState& drumUi,
    seq::SequencerHistoryActionKind kind,
    uint8_t lane = seq::SequencerHistoryDescriptor::INVALID_INDEX,
    uint8_t step = seq::SequencerHistoryDescriptor::INVALID_INDEX,
    seq::StepProperty property = seq::StepProperty::NOTE
) {
    return {
        .kind = kind,
        .trackIndex = drumUi.targetTrack,
        .laneIndex = lane,
        .stepIndex = step,
        .property = property,
    };
}

FLASHMEM int32_t drumStepHistoryValue(
    const seq::DrumSequencerState& drumUi,
    uint8_t lane,
    uint8_t step,
    seq::DrumSequencerProperty property
) {
    if (!drumUi.stepInRange(lane, step) || drumUi.drumTrack == nullptr) {
        return 0;
    }
    const auto& pattern = drumUi.drumTrack->pattern.lanes[lane];
    switch (property) {
        case seq::DrumSequencerProperty::STATE:
            return drumUi.drumTrack->pattern.stepEnabled(lane, step) ? 1 : 0;
        case seq::DrumSequencerProperty::PROBABILITY:
            return pattern.probability[step];
        case seq::DrumSequencerProperty::GATE:
            return pattern.gate[step];
        case seq::DrumSequencerProperty::NUDGE:
            return pattern.nudge[step];
        case seq::DrumSequencerProperty::VELOCITY:
        case seq::DrumSequencerProperty::COUNT:
        default:
            return pattern.velocity[step];
    }
}

}  // namespace

FLASHMEM SequencerStepHandler::SequencerStepHandler(
    StateRefs state, oc::api::EncoderAPI& encoders, oc::api::ButtonAPI& buttons,
    oc::type::ScopeID scopeId
#if defined(MS_UX_RECORDER)
    ,
    core::validation::ux::StructureUxTraceState* uxTraceState
#endif
    )
    : sequencer_(state.sequencer), tracks_(state.tracks),
      structure_clipboard_(state.structureClipboard),
      navigation_focus_(state.navigationFocus),
      track_ui_(state.trackNavigation),
      edit_workflow_(SequencerStructureEditWorkflow::StateRefs{
          state.sequencer,
          state.tracks,
          state.navigationFocus,
          state.trackNavigation,
          state.projectNavigation,
          state.projectTracks,
          state.projectTrackDomain,
          state.structureClipboard,
          state.sharedTracks,
          state.history,
          state.macroPages,
          state.trackActivations,
          state.statusBar,
      }),
      navigation_workflow_(SequencerStructureNavigationWorkflow::StateRefs{
          state.sequencer,
          state.tracks,
          state.navigationFocus,
          state.trackNavigation,
          edit_workflow_.sharedTrackServices(),
      }),
      history_(state.history), context_selector_workflow_(state.sequencer.contextSelector),
      encoders_(encoders), buttons_(buttons), scope_id_(scopeId)
#if defined(MS_UX_RECORDER)
      ,
      ux_trace_state_(uxTraceState)
#endif
{
    setupBindings();
}

FLASHMEM SequencerStepHandler::~SequencerStepHandler() = default;

FLASHMEM void SequencerStepHandler::update(uint32_t nowMs) {
    syncDrumSequencerToActiveTrack();
    edit_workflow_.update(nowMs);
    context_selector_workflow_.update();
}

FLASHMEM void SequencerStepHandler::attachStepEditHandler(SequencerStepEditHandler& handler) {
    step_edit_handler_ = &handler;
}

FLASHMEM void SequencerStepHandler::attachPatternEditorHandler(
    SequencerPatternEditorHandler& handler) {
    pattern_editor_handler_ = &handler;
}

FLASHMEM void SequencerStepHandler::attachTrackEditorHandler(ProjectTrackEditorHandler& handler) {
    track_editor_handler_ = &handler;
}

FLASHMEM void SequencerStepHandler::attachDrumLaneEditorHandler(
    DrumLaneEditorHandler& handler
) {
    drum_lane_editor_handler_ = &handler;
}

FLASHMEM void SequencerStepHandler::syncDrumSequencerToActiveTrack() {
    auto& drumUi = sequencer_.drumSequencer;
    if (drumUi.pickerVisible()) return;

    const uint8_t activeTrack = tracks_.activeTrackIndex();
    if (!tracks_.isDrumTrack(activeTrack)) {
        drumUi.unbindTrack();
        return;
    }

    auto& authored = tracks_.drumTrack(activeTrack);
    const bool needsEntry = !drumUi.gridVisible() ||
        drumUi.targetTrack != activeTrack || drumUi.drumTrack != &authored;
    drumUi.bindTrack(activeTrack, authored, tracks_);
    if (needsEntry) {
        drumUi.enterGrid();
        if (navigation_focus_.get() !=
            core::state::StructureNavigationFocus::TRACK) {
            navigation_focus_.set(core::state::StructureNavigationFocus::PAGE);
        }
    }
}

FLASHMEM void SequencerStepHandler::confirmDrumSequencerType() {
    auto& drumUi = sequencer_.drumSequencer;
    if (!drumUi.pickerVisible()) return;

    const uint8_t requestedTrack = drumUi.targetTrack;
    const bool drum = drumUi.typePickerVisible()
        ? drumUi.selectedKind == seq::DrumSequencerKind::DRUM
        : true;
    if (drumUi.typePickerVisible() && drum) {
        drumUi.openKitPicker();
        return;
    }
    const auto result = edit_workflow_.createPreviewedTrackStructure(
        drum
            ? seq::SequencerTrackKind::DRUM
            : seq::SequencerTrackKind::INSTRUMENT,
        drumUi.selectedKitPreset
    );
    if (!result.settled()) return;

    const uint8_t createdTrack = requestedTrack < seq::SequencerTrackBankState::TRACK_COUNT
        ? requestedTrack
        : tracks_.activeTrackIndex();
    if (drum) {
        drumUi.bindTrack(createdTrack, tracks_.drumTrack(createdTrack), tracks_);
        drumUi.enterGrid();
        navigation_focus_.set(core::state::StructureNavigationFocus::PAGE);
    } else {
        drumUi.unbindTrack();
    }
}

FLASHMEM void SequencerStepHandler::handleDrumSequencerNavTurn(
    float delta
) {
    auto& drumUi = sequencer_.drumSequencer;
    if (!drumUi.gridVisible() || delta == 0.0f) return;
    if (context_selector_workflow_.ownsGesture()) {
        (void)context_selector_workflow_.turn(delta);
        return;
    }
    if (drumUi.selectorVisible()) {
        if (drumUi.selector ==
            seq::DrumSequencerSelector::LANE_EDITOR) {
            return;
        }
        drumUi.moveSelector(delta);
        return;
    }
    if (history_.commitCoalescedDrumEditOutcome() ==
        seq::SequencerPatternHistoryCommitOutcome::Failed) {
        sequencer_.historyFeedback.showRejection(
            seq::SequencerHistoryRejectionReason::HistoryUnavailable,
            core::time_compat::millis()
        );
        return;
    }
    switch (navigation_focus_.get()) {
        case core::state::StructureNavigationFocus::TRACK:
            navigation_workflow_.moveByFocus(delta);
            syncDrumSequencerToActiveTrack();
            return;
        case core::state::StructureNavigationFocus::STEP:
            drumUi.moveFocusedStep(delta);
            return;
        case core::state::StructureNavigationFocus::PAGE:
        default:
            drumUi.moveLane(delta);
            return;
    }
}

FLASHMEM void SequencerStepHandler::handleDrumSequencerNavPress() {
    auto& drumUi = sequencer_.drumSequencer;
    if (!drumUi.gridVisible() || drumUi.selectorVisible() ||
        context_selector_workflow_.ownsGesture()) {
        return;
    }
    const auto focus = navigation_focus_.get();
    const bool trackFocus = focus == core::state::StructureNavigationFocus::TRACK;
    const uint8_t previewTarget = trackFocus
        ? track_ui_.previewTrackIndex.get()
        : (focus == core::state::StructureNavigationFocus::STEP
            ? drumUi.focusedStep
            : (drumUi.laneAddSlotFocused()
                ? drumUi.drumTrack->kit.laneCount
                : drumUi.selectedLane));
    context_selector_workflow_.press(
        focus,
        true,
        previewTarget,
        trackFocus && track_ui_.previewAddSlot.get()
    );
}

FLASHMEM void SequencerStepHandler::handleDrumSequencerNavRelease() {
    auto& drumUi = sequencer_.drumSequencer;
    if (!drumUi.gridVisible() || !context_selector_workflow_.ownsGesture()) {
        return;
    }
    const auto outcome = context_selector_workflow_.release();
    if (outcome.action == SequencerContextSelectorAction::OPEN_STEP_EDITOR) {
        if (history_.commitCoalescedDrumEditOutcome() ==
                seq::SequencerPatternHistoryCommitOutcome::Failed ||
            step_edit_handler_ == nullptr) {
            return;
        }
        (void)step_edit_handler_->openFocusedStepAtRow(
            core::state::sequencer::step_edit_rows::ACTIVATED
        );
        return;
    }
    if (outcome.action == SequencerContextSelectorAction::OPEN_PATTERN_EDITOR) {
        if (drum_lane_editor_handler_ == nullptr) return;
        (void)drum_lane_editor_handler_->open(drumUi.laneAddSlotFocused());
        return;
    }
    if (outcome.action == SequencerContextSelectorAction::OPEN_TRACK_EDITOR) {
        if (history_.commitCoalescedDrumEditOutcome() ==
            seq::SequencerPatternHistoryCommitOutcome::Failed) {
            return;
        }
        if (outcome.previewAddSlot) {
            drumUi.openTypePicker(outcome.previewTarget);
        } else if (track_editor_handler_ != nullptr) {
            (void)track_editor_handler_->openActiveTrack();
        }
        return;
    }
    if (outcome.action != SequencerContextSelectorAction::APPLY_CONTEXT) return;
    if (outcome.focus == core::state::StructureNavigationFocus::STEP &&
        !drumUi.focusAuthoredLane()) {
        return;
    }
    navigation_workflow_.setNavigationFocus(outcome.focus);
    drumUi.bump();
}

FLASHMEM bool SequencerStepHandler::drumBackActionAvailable() const {
    const auto& drumUi = sequencer_.drumSequencer;
    if (!drumUi.active()) return false;
    if (core::state::sequencer::isDrumContentView(sequencer_)) return false;

    // Pickers and local transient contexts still own Back. At the Track root,
    // however, LEFT_TOP belongs to the controller-wide View Selector. Do not
    // capture the gesture here when the Drum surface has nowhere left to back
    // into locally.
    if (!drumUi.gridVisible() || drumUi.selectorVisible() ||
        context_selector_workflow_.ownsGesture()) {
        return true;
    }
    return navigation_focus_.get() !=
        core::state::StructureNavigationFocus::TRACK;
}

FLASHMEM void SequencerStepHandler::handleDrumSequencerBack() {
    auto& drumUi = sequencer_.drumSequencer;
    if (sequencer_.stepContentSelector.selecting.get()) {
        sequencer_.stepContentSelector.selecting.set(false);
        drumUi.bump();
        return;
    }
    if (drumUi.laneSelection.active) {
        (void)navigation_workflow_.backSelectionMode();
        return;
    }
    if (drumUi.kitPickerVisible()) {
        drumUi.returnToTypePicker();
        return;
    }
    if (drumUi.typePickerVisible()) {
        drumUi.close();
        return;
    }
    if (drumUi.selectorVisible()) {
        cancelDrumSelector();
        return;
    }
    if (context_selector_workflow_.ownsGesture()) {
        context_selector_workflow_.cancel();
        drumUi.bump();
        return;
    }
    switch (navigation_focus_.get()) {
        case core::state::StructureNavigationFocus::STEP:
            navigation_workflow_.setNavigationFocus(
                core::state::StructureNavigationFocus::PAGE
            );
            break;
        case core::state::StructureNavigationFocus::PAGE:
            navigation_workflow_.setNavigationFocus(
                core::state::StructureNavigationFocus::TRACK
            );
            break;
        case core::state::StructureNavigationFocus::TRACK:
        default:
            break;
    }
    (void)history_.commitCoalescedDrumEditOutcome();
    drumUi.bump();
}

FLASHMEM void SequencerStepHandler::editDrumSequencerStepProperty(
    uint8_t indexInPage,
    float normalized
) {
    auto& drumUi = sequencer_.drumSequencer;
    if (drumUi.laneAddSlotFocused() ||
        indexInPage >= drumUi.STEPS_PER_PAGE) return;
    const uint8_t lane = drumUi.selectedLane;
    const uint8_t step = drumUi.visibleStep(indexInPage);
    auto descriptor = drumHistoryDescriptor(
        drumUi,
        seq::SequencerHistoryActionKind::DrumStepPropertyEdit,
        lane,
        step,
        input_utils::drumStepProperty(drumUi.property)
    );
    descriptor.hasValue = true;
    descriptor.beforeValue = drumStepHistoryValue(
        drumUi, lane, step, drumUi.property);
    if (!beginDrumHistory(descriptor)) return;
    const bool changed = input_utils::applyNormalizedToDrumStep(
        drumUi,
        lane,
        step,
        drumUi.property,
        normalized
    );
    descriptor.afterValue = drumStepHistoryValue(
        drumUi, lane, step, drumUi.property);
    (void)sealDrumHistory(changed, descriptor, false);
}

FLASHMEM bool SequencerStepHandler::beginDrumHistory(
    seq::SequencerHistoryDescriptor descriptor
) {
    const auto outcome = history_.beginCoalescedDrumEdit(
        descriptor,
        core::time_compat::millis()
    );
    if (seq::sequencerHistoryOpenAccepted(outcome)) return true;
    sequencer_.historyFeedback.showRejection(outcome, core::time_compat::millis());
    return false;
}

FLASHMEM bool SequencerStepHandler::sealDrumHistory(
    bool changed,
    seq::SequencerHistoryDescriptor descriptor,
    bool commit
) {
    if (!history_.sealCoalescedDrumEdit(changed, descriptor)) {
        sequencer_.historyFeedback.showRejection(
            seq::SequencerHistoryRejectionReason::HistoryUnavailable,
            core::time_compat::millis()
        );
        return false;
    }
    if (!commit) return true;
    const auto outcome = history_.commitCoalescedDrumEditOutcome();
    if (outcome != seq::SequencerPatternHistoryCommitOutcome::Failed) {
        return true;
    }
    sequencer_.historyFeedback.showRejection(
        seq::SequencerHistoryRejectionReason::HistoryUnavailable,
        core::time_compat::millis()
    );
    return false;
}

FLASHMEM void SequencerStepHandler::cancelDrumSelector() {
    auto& drumUi = sequencer_.drumSequencer;
    if (drumUi.selector == seq::DrumSequencerSelector::DIMENSION ||
        drumUi.selector == seq::DrumSequencerSelector::PATTERN_DEFAULTS) {
        (void)history_.abortCoalescedDrumEdit();
    }
    drumUi.cancelSelector();
}

FLASHMEM void SequencerStepHandler::applyDrumSelector() {
    auto& drumUi = sequencer_.drumSequencer;
    const bool commitsLiveEdit =
        drumUi.selector == seq::DrumSequencerSelector::DIMENSION ||
        drumUi.selector == seq::DrumSequencerSelector::PATTERN_DEFAULTS;
    drumUi.applySelector();
    if (commitsLiveEdit) {
        if (history_.commitCoalescedDrumEditOutcome() ==
            seq::SequencerPatternHistoryCommitOutcome::Failed) {
            sequencer_.historyFeedback.showRejection(
                seq::SequencerHistoryRejectionReason::HistoryUnavailable,
                core::time_compat::millis()
            );
        }
    }
}

FLASHMEM void SequencerStepHandler::editDrumSequencerOpt(
    float normalized
) {
    auto& drumUi = sequencer_.drumSequencer;
    if (drumUi.selector == seq::DrumSequencerSelector::LANE_EDITOR) {
        return;
    }
    if (drumUi.selector ==
        seq::DrumSequencerSelector::PATTERN_DEFAULTS) {
        const auto field = drumUi.patternDefaultField;
        auto descriptor = drumHistoryDescriptor(
            drumUi,
            seq::SequencerHistoryActionKind::DrumPatternSettings,
            seq::SequencerHistoryDescriptor::INVALID_INDEX,
            static_cast<uint8_t>(field)
        );
        descriptor.hasValue = true;
        descriptor.beforeValue = field == seq::DrumPatternDefaultField::LENGTH
            ? drumUi.drumTrack->pattern.defaultLength
            : drumUi.drumTrack->pattern.defaultStepsPerBeat;
        if (!beginDrumHistory(descriptor)) return;
        const uint32_t beforeRevision = drumUi.drumTrack->pattern.revision;
        drumUi.editPatternDefaultValue(normalized);
        descriptor.afterValue = field == seq::DrumPatternDefaultField::LENGTH
            ? drumUi.drumTrack->pattern.defaultLength
            : drumUi.drumTrack->pattern.defaultStepsPerBeat;
        (void)sealDrumHistory(
            drumUi.drumTrack->pattern.revision != beforeRevision,
            descriptor,
            false
        );
        return;
    }
    if (!drumUi.gridVisible() ||
        drumUi.selector == seq::DrumSequencerSelector::PROPERTY ||
        drumUi.laneAddSlotFocused()) {
        return;
    }
    if (navigation_focus_.get() ==
            core::state::StructureNavigationFocus::STEP &&
        !drumUi.selectorVisible()) {
        editDrumSequencerStepProperty(
            static_cast<uint8_t>(
                drumUi.focusedStep % drumUi.STEPS_PER_PAGE
            ),
            normalized
        );
        return;
    }

    switch (drumUi.dimension) {
        case seq::DrumSequencerDimension::MODE: {
            auto descriptor = drumHistoryDescriptor(
                drumUi,
                seq::SequencerHistoryActionKind::DrumPatternSettings,
                drumUi.selectedLane,
                static_cast<uint8_t>(drumUi.dimension)
            );
            descriptor.hasValue = true;
            descriptor.beforeValue = static_cast<int32_t>(
                drumUi.drumTrack->pattern.lanes[drumUi.selectedLane]
                    .timing.mode);
            if (!beginDrumHistory(descriptor)) return;
            const uint32_t beforeRevision = drumUi.drumTrack->pattern.revision;
            drumUi.setSelectedLaneTimingCustom(
                input_utils::clampNormalized(normalized) >= 0.5f
            );
            descriptor.afterValue = static_cast<int32_t>(
                drumUi.drumTrack->pattern.lanes[drumUi.selectedLane]
                    .timing.mode);
            (void)sealDrumHistory(
                drumUi.drumTrack->pattern.revision != beforeRevision,
                descriptor,
                false
            );
            return;
        }
        case seq::DrumSequencerDimension::DIVISION: {
            auto descriptor = drumHistoryDescriptor(
                drumUi,
                seq::SequencerHistoryActionKind::DrumPatternSettings,
                drumUi.selectedLane,
                static_cast<uint8_t>(drumUi.dimension)
            );
            descriptor.hasValue = true;
            descriptor.beforeValue = drumUi.drumTrack->pattern
                .effectiveStepsPerBeat(drumUi.selectedLane);
            if (!beginDrumHistory(descriptor)) return;
            const uint32_t beforeRevision = drumUi.drumTrack->pattern.revision;
            const int index = input_utils::normalizedToIndex(
                normalized,
                static_cast<int>(input_utils::STEPS_PER_BEAT_CHOICES.size())
            );
            drumUi.setSelectedLaneStepsPerBeat(
                input_utils::STEPS_PER_BEAT_CHOICES[
                    static_cast<size_t>(index)
                ]
            );
            descriptor.afterValue = drumUi.drumTrack->pattern
                .effectiveStepsPerBeat(drumUi.selectedLane);
            (void)sealDrumHistory(
                drumUi.drumTrack->pattern.revision != beforeRevision,
                descriptor,
                false
            );
            return;
        }
        case seq::DrumSequencerDimension::LENGTH:
        case seq::DrumSequencerDimension::COUNT:
        default: {
            auto descriptor = drumHistoryDescriptor(
                drumUi,
                seq::SequencerHistoryActionKind::DrumPatternSettings,
                drumUi.selectedLane,
                static_cast<uint8_t>(drumUi.dimension)
            );
            descriptor.hasValue = true;
            descriptor.beforeValue = drumUi.drumTrack->pattern
                .effectiveLength(drumUi.selectedLane);
            if (!beginDrumHistory(descriptor)) return;
            const uint32_t beforeRevision = drumUi.drumTrack->pattern.revision;
            drumUi.setSelectedLaneLength(static_cast<uint8_t>(
                input_utils::normalizedToInclusiveInt(
                    normalized,
                    drumUi.MAX_STEPS - 1U
                ) + 1
            ));
            descriptor.afterValue = drumUi.drumTrack->pattern
                .effectiveLength(drumUi.selectedLane);
            (void)sealDrumHistory(
                drumUi.drumTrack->pattern.revision != beforeRevision,
                descriptor,
                false
            );
            return;
        }
    }
}

FLASHMEM void SequencerStepHandler::handleContextSelectorRelease() {
    const auto outcome = context_selector_workflow_.release();
    switch (outcome.action) {
        case SequencerContextSelectorAction::APPLY_CONTEXT:
            if (!publishPatternHistoryBarrier(
                    sequencer_, commitPatternHistoryBarrier(sequencer_, history_))) {
                return;
            }
            navigation_workflow_.setNavigationFocus(outcome.focus);
            return;
        case SequencerContextSelectorAction::OPEN_STEP_EDITOR:
            if (outcome.focus != core::state::StructureNavigationFocus::STEP ||
                navigation_focus_.get() !=
                    core::state::StructureNavigationFocus::STEP ||
                sequencer_.focusedStep.get() != outcome.previewTarget) {
                return;
            }
            if (step_edit_handler_ != nullptr) {
                (void)step_edit_handler_->openFocusedStepAtRow(
                    core::state::sequencer::step_edit_rows::ACTIVATED);
            }
            return;
        case SequencerContextSelectorAction::OPEN_PATTERN_EDITOR:
            if (outcome.focus != core::state::StructureNavigationFocus::PAGE ||
                navigation_focus_.get() !=
                    core::state::StructureNavigationFocus::PAGE ||
                sequencer_.structureUi.previewPageIndex.get() !=
                    outcome.previewTarget || outcome.previewAddSlot) {
                return;
            }
            if (pattern_editor_handler_ != nullptr &&
                core::state::sequencer::isRootContentView(sequencer_)) {
                (void)pattern_editor_handler_->openFromCurrentPage();
            }
            return;
        case SequencerContextSelectorAction::OPEN_TRACK_EDITOR:
            if (outcome.focus != core::state::StructureNavigationFocus::TRACK ||
                !trackFocusActive() ||
                track_ui_.previewTrackIndex.get() != outcome.previewTarget) {
                return;
            }
            if (outcome.previewAddSlot) {
                if (!track_ui_.previewAddSlot.get() ||
                    track_ui_.previewTrackIndex.get() !=
                        outcome.previewTarget) {
                    return;
                }
                sequencer_.drumSequencer.openTypePicker(
                    outcome.previewTarget
                );
                return;
            }
            if (track_ui_.previewAddSlot.get()) return;
            if (track_editor_handler_ != nullptr &&
                core::state::sequencer::isRootContentView(sequencer_)) {
                (void)track_editor_handler_->openActiveTrack();
            }
            return;
        case SequencerContextSelectorAction::NONE:
        default: return;
    }
}

FLASHMEM bool SequencerStepHandler::trackFocusActive() const {
    return navigation_focus_.get() == core::state::StructureNavigationFocus::TRACK;
}

FLASHMEM void SequencerStepHandler::enterSelectionModeForCurrentFocus() {
    if (core::state::sequencer::isDrumOverviewActive(sequencer_)) {
        if (history_.commitCoalescedDrumEditOutcome() ==
            seq::SequencerPatternHistoryCommitOutcome::Failed) {
            sequencer_.historyFeedback.showRejection(
                seq::SequencerHistoryRejectionReason::HistoryUnavailable,
                core::time_compat::millis()
            );
            return;
        }
    } else if (!publishPatternHistoryBarrier(
                   sequencer_,
                   commitPatternHistoryBarrier(sequencer_, history_))) {
        return;
    }
    navigation_workflow_.enterSelectionModeForCurrentFocus();
}

FLASHMEM void SequencerStepHandler::setupDrumBindings() {
    // Drum owns its lane navigation and momentary property surfaces. Track and
    // Step structure actions deliberately fall through to the common workflow
    // registered below; only Pattern paging remains a prioritized exception.
    encoders_.encoder(Config::EncoderID::NAV)
        .turn()
        .scope(scope_id_)
        .when([this]() { return sequencer_.drumSequencer.typePickerVisible(); })
        .then([this](float delta) {
            sequencer_.drumSequencer.moveKind(delta);
        });

    encoders_.encoder(Config::EncoderID::NAV)
        .turn()
        .scope(scope_id_)
        .when([this]() { return sequencer_.drumSequencer.kitPickerVisible(); })
        .then([this](float delta) {
            sequencer_.drumSequencer.moveKitPreset(delta);
        });

    encoders_.encoder(Config::EncoderID::NAV)
        .turn()
        .scope(scope_id_)
        .when([this]() {
            return core::state::sequencer::isDrumOverviewActive(sequencer_) &&
                !sequencer_.stepContentSelector.selecting.get() &&
                !sequencer_.drumSequencer.laneSelection.active;
        })
        .then([this](float delta) { handleDrumSequencerNavTurn(delta); });

    encoders_.encoder(Config::EncoderID::OPT)
        .turn()
        .scope(scope_id_)
        .when([this]() {
            return core::state::sequencer::isDrumOverviewActive(sequencer_) &&
                !sequencer_.drumSequencer.laneSelection.active;
        })
        .then([this](float normalized) {
            editDrumSequencerOpt(normalized);
        });

    buttons_.button(Config::ButtonID::NAV)
        .press()
        .scope(scope_id_)
        .priority(100)
        .when([this]() {
            return sequencer_.drumSequencer.active() &&
                !sequencer_.drumSequencer.laneSelection.active &&
                (!sequencer_.drumSequencer.gridVisible() ||
                 core::state::sequencer::isDrumOverviewActive(sequencer_));
        })
        .then([this]() {
            if (core::state::sequencer::isDrumOverviewActive(sequencer_)) {
                handleDrumSequencerNavPress();
            }
        });

    buttons_.button(Config::ButtonID::NAV)
        .release()
        .scope(scope_id_)
        .priority(100)
        .when([this]() { return sequencer_.drumSequencer.pickerVisible(); })
        .then([this]() { confirmDrumSequencerType(); });

    buttons_.button(Config::ButtonID::NAV)
        .release()
        .scope(scope_id_)
        .priority(100)
        .when([this]() {
            return core::state::sequencer::isDrumOverviewActive(sequencer_) &&
                !sequencer_.drumSequencer.laneSelection.active;
        })
        .then([this]() { handleDrumSequencerNavRelease(); });

    buttons_.button(Config::ButtonID::NAV)
        .longPress(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS)
        .scope(scope_id_)
        .priority(100)
        .when([this]() {
            const auto& drumUi = sequencer_.drumSequencer;
            return core::state::sequencer::isDrumOverviewActive(sequencer_) &&
                !drumUi.laneSelection.active &&
                !drumUi.laneAddSlotFocused() &&
                context_selector_workflow_.ownsGesture() &&
                navigation_focus_.get() ==
                    core::state::StructureNavigationFocus::PAGE;
        })
        .then([this]() {
            const auto& drumUi = sequencer_.drumSequencer;
            if (!context_selector_workflow_.holdForSelection(
                    core::state::StructureNavigationFocus::PAGE,
                    drumUi.selectedLane,
                    false)) {
                return;
            }
            enterSelectionModeForCurrentFocus();
        });

    // Pattern Lane selection already owns the cursor and source identity.
    // LEFT_CENTER enters a lightweight destination phase; NAV previews the
    // target and BOTTOM_RIGHT applies one undoable structural move.
    buttons_.button(Config::ButtonID::LEFT_CENTER)
        .release()
        .scope(scope_id_)
        .priority(110)
        .when([this]() {
            return edit_workflow_.canMoveDrumLaneSelection();
        })
        .then([this]() { edit_workflow_.beginDrumLaneMove(); });

    buttons_.button(Config::ButtonID::BOTTOM_RIGHT)
        .release()
        .scope(scope_id_)
        .priority(110)
        .when([this]() {
            return sequencer_.drumSequencer.laneSelection.moveActive();
        })
        .then([this]() { edit_workflow_.applyDrumLaneMove(); });

    buttons_.button(Config::ButtonID::LEFT_CENTER)
        .press()
        .latch()
        .scope(scope_id_)
        .priority(100)
        .when([this]() {
            const auto& drumUi = sequencer_.drumSequencer;
            return core::state::sequencer::isDrumOverviewActive(sequencer_) &&
                !drumUi.selectorVisible() &&
                !drumUi.laneSelection.active &&
                !context_selector_workflow_.ownsGesture();
        })
        .then([this]() {
            auto& drumUi = sequencer_.drumSequencer;
            const auto focus = navigation_focus_.get();
            if (focus == core::state::StructureNavigationFocus::STEP) {
                drumUi.openPropertySelector();
            } else if (focus == core::state::StructureNavigationFocus::TRACK) {
                if (history_.commitCoalescedDrumEditOutcome() ==
                    seq::SequencerPatternHistoryCommitOutcome::Failed) {
                    return;
                }
                drumUi.openPatternDefaults();
            } else {
                drumUi.openDimensionSelector();
            }
        });

    buttons_.button(Config::ButtonID::LEFT_CENTER)
        .release()
        .scope(scope_id_)
        .priority(100)
        .when([this]() {
            const auto selector = sequencer_.drumSequencer.selector;
            return selector == seq::DrumSequencerSelector::DIMENSION ||
                selector == seq::DrumSequencerSelector::PATTERN_DEFAULTS ||
                (selector == seq::DrumSequencerSelector::PROPERTY &&
                 navigation_focus_.get() ==
                     core::state::StructureNavigationFocus::STEP);
        })
        .then([this]() { applyDrumSelector(); });

    buttons_.button(Config::ButtonID::LEFT_BOTTOM)
        .press()
        .latch()
        .scope(scope_id_)
        .priority(100)
        .when([this]() {
            const auto& drumUi = sequencer_.drumSequencer;
            return core::state::sequencer::isDrumOverviewActive(sequencer_) &&
                !drumUi.selectorVisible() &&
                !drumUi.laneSelection.active &&
                !context_selector_workflow_.ownsGesture() &&
                navigation_focus_.get() ==
                    core::state::StructureNavigationFocus::PAGE;
        })
        .then([this]() {
            sequencer_.drumSequencer.openPropertySelector();
        });

    buttons_.button(Config::ButtonID::LEFT_BOTTOM)
        .release()
        .scope(scope_id_)
        .priority(100)
        .when([this]() {
            return sequencer_.drumSequencer.selector ==
                    seq::DrumSequencerSelector::PROPERTY &&
                navigation_focus_.get() ==
                    core::state::StructureNavigationFocus::PAGE;
        })
        .then([this]() { applyDrumSelector(); });

    buttons_.button(Config::ButtonID::LEFT_TOP)
        .press()
        .scope(scope_id_)
        .priority(100)
        .when([this]() { return drumBackActionAvailable(); })
        .then([]() {});

    buttons_.button(Config::ButtonID::LEFT_TOP)
        .release()
        .scope(scope_id_)
        .priority(100)
        .when([this]() { return drumBackActionAvailable(); })
        .then([this]() { handleDrumSequencerBack(); });

    buttons_.button(Config::ButtonID::BOTTOM_LEFT)
        .release()
        .scope(scope_id_)
        .priority(100)
        .when([this]() {
            return core::state::sequencer::isDrumOverviewActive(sequencer_) &&
                !sequencer_.drumSequencer.selectorVisible() &&
                !sequencer_.drumSequencer.laneSelection.active &&
                !context_selector_workflow_.ownsGesture() &&
                navigation_focus_.get() ==
                    core::state::StructureNavigationFocus::PAGE;
        })
        .then([this]() { sequencer_.drumSequencer.movePage(-1); });

    buttons_.button(Config::ButtonID::BOTTOM_RIGHT)
        .release()
        .scope(scope_id_)
        .priority(100)
        .when([this]() {
            return core::state::sequencer::isDrumOverviewActive(sequencer_) &&
                !sequencer_.drumSequencer.selectorVisible() &&
                !sequencer_.drumSequencer.laneSelection.active &&
                !context_selector_workflow_.ownsGesture() &&
                navigation_focus_.get() ==
                    core::state::StructureNavigationFocus::PAGE;
        })
        .then([this]() { sequencer_.drumSequencer.movePage(1); });

    for (uint8_t i = 0; i < Config::MACRO_COUNT; ++i) {
        buttons_.button(Config::MACRO_BUTTONS[i])
            .release()
            .scope(scope_id_)
            .priority(100)
            .when([this]() {
                return core::state::sequencer::isDrumOverviewActive(sequencer_) &&
                    !sequencer_.drumSequencer.selectorVisible() &&
                    !sequencer_.drumSequencer.laneSelection.active &&
                    !context_selector_workflow_.ownsGesture();
            })
            .then([this, i]() {
                auto& drumUi = sequencer_.drumSequencer;
                if (drumUi.laneAddSlotFocused()) return;
                const uint8_t lane = drumUi.selectedLane;
                const uint8_t step = drumUi.visibleStep(i);
                auto descriptor = drumHistoryDescriptor(
                    drumUi,
                    seq::SequencerHistoryActionKind::DrumStepToggle,
                    lane,
                    step,
                    seq::StepProperty::NOTE
                );
                descriptor.hasValue = true;
                descriptor.beforeValue = drumStepHistoryValue(
                    drumUi,
                    lane,
                    step,
                    seq::DrumSequencerProperty::STATE
                );
                if (!beginDrumHistory(descriptor)) return;
                const uint32_t beforeRevision = drumUi.drumTrack->pattern.revision;
                drumUi.toggleVisibleStep(i);
                descriptor.afterValue = drumStepHistoryValue(
                    drumUi,
                    lane,
                    step,
                    seq::DrumSequencerProperty::STATE
                );
                (void)sealDrumHistory(
                    drumUi.drumTrack->pattern.revision != beforeRevision,
                    descriptor,
                    true
                );
            });

        encoders_.encoder(Config::MACRO_ENCODERS[i])
            .turn()
            .scope(scope_id_)
            .when([this]() {
                return core::state::sequencer::isDrumOverviewActive(sequencer_) &&
                    !sequencer_.drumSequencer.selectorVisible() &&
                    !sequencer_.drumSequencer.laneSelection.active &&
                    !context_selector_workflow_.ownsGesture();
            })
            .then([this, i](float normalized) {
                editDrumSequencerStepProperty(i, normalized);
            });
    }
}

FLASHMEM void SequencerStepHandler::setupBindings() {
    setupDrumBindings();

    encoders_.encoder(Config::EncoderID::NAV)
        .turn()
        .scope(scope_id_)
        .when([this]() { return sequencer_.stepContentDraft.exitPromptVisible.get(); })
        .then([this](float delta) { moveStepContentDraftExitChoice(delta); });

    buttons_.button(Config::ButtonID::NAV)
        .release()
        .scope(scope_id_)
        .when([this]() { return sequencer_.stepContentDraft.exitPromptVisible.get(); })
        .then([this]() { confirmStepContentDraftExitChoice(); });

    buttons_.button(Config::ButtonID::LEFT_TOP)
        .release()
        .scope(scope_id_)
        .when([this]() { return sequencer_.stepContentDraft.exitPromptVisible.get(); })
        .then([this]() { continueStepContentDraft(); });

    buttons_.button(Config::ButtonID::BOTTOM_RIGHT)
        .release()
        .scope(scope_id_)
        .when([this]() {
            return core::state::sequencer::isChildContentView(sequencer_) &&
                   sequencer_.stepContentDraft.active.get() &&
                   !sequencer_.stepContentDraft.exitPromptVisible.get();
        })
        .then([this]() { applyStepContentDraft(); });

    buttons_.button(Config::ButtonID::LEFT_CENTER)
        .release()
        .scope(scope_id_)
        .when([this]() { return edit_workflow_.trackPastePlanInspectable(); })
        .then([this]() { edit_workflow_.toggleTrackPasteDetails(); });

    buttons_.button(Config::ButtonID::LEFT_TOP)
        .release()
        .scope(scope_id_)
        .when([this]() { return edit_workflow_.trackPasteNavigationBlocked(); })
        .then([this]() { edit_workflow_.cancelTrackPasteAction(core::time_compat::millis()); });

    for (uint8_t i = 0; i < Config::MACRO_COUNT; ++i) {
        buttons_.button(Config::MACRO_BUTTONS[i])
            .longPress(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS)
            .scope(scope_id_)
            .when([this]() { return sequencer_.structureUi.stepSelection.active.get(); })
            .then(
                [this, i]() { step_selection_macro_release_latch_.arm(Config::MACRO_BUTTONS[i]); });

        buttons_.button(Config::MACRO_BUTTONS[i])
            .release()
            .scope(scope_id_)
            .when([this]() { return sequencer_.structureUi.stepSelection.active.get(); })
            .then([this, i]() {
                if (step_selection_macro_release_latch_.consume(Config::MACRO_BUTTONS[i])) {
                    return;
                }
                navigation_workflow_.toggleStepSelectionAtVisibleIndex(i);
            });

        buttons_.button(Config::MACRO_BUTTONS[i])
            .release()
            .scope(scope_id_)
            .when([this]() { return navigation_workflow_.allowsMainBindings(); })
            .then([this, i]() {
                if (step_selection_macro_release_latch_.consume(Config::MACRO_BUTTONS[i])) {
                    return;
                }
                toggleStep(i);
            });
    }

    encoders_.encoder(Config::EncoderID::NAV)
        .turn()
        .scope(scope_id_)
        .when([this]() { return context_selector_workflow_.ownsGesture(); })
        .then([this](float delta) { (void)context_selector_workflow_.turn(delta); });

    encoders_.encoder(Config::EncoderID::NAV)
        .turn()
        .scope(scope_id_)
        .when([this]() {
            return core::state::sequencer::isChildContentView(sequencer_) &&
                   !context_selector_workflow_.ownsGesture() &&
                   navigation_workflow_.allowsMainBindings() &&
                   !edit_workflow_.trackPasteNavigationBlocked() &&
                   !navigation_workflow_.stepFocusActive();
        })
        .then([this](float delta) {
            if (delta == 0.0f) return;
            const uint8_t pages = core::state::sequencer::activeContentPageCount(sequencer_);
            if (pages <= 1U) return;
            const int direction = delta > 0.0f ? 1 : -1;
            const int next = static_cast<int>(sequencer_.page.get()) + direction;
            sequencer_.page.set(core::state::sequencer::normalizeActiveContentPage(
                sequencer_, static_cast<uint8_t>((next + pages) % pages)));
            sequencer_.focusedStep.set(core::state::sequencer::activeContentPageStartStep(
                sequencer_, sequencer_.page.get()));
        });

    encoders_.encoder(Config::EncoderID::NAV)
        .turn()
        .scope(scope_id_)
        .when([this]() {
            return selectionInteractionPolicy().navTurn == SelectionAction::MOVE_CURSOR &&
                   !context_selector_workflow_.ownsGesture() &&
                   !edit_workflow_.trackPasteNavigationBlocked() &&
                   !edit_workflow_.trackRemoveNavigationBlocked();
        })
        .then([this](float delta) { navigation_workflow_.navigateSelection(delta); });

    encoders_.encoder(Config::EncoderID::NAV)
        .turn()
        .scope(scope_id_)
        .when([this]() {
            return core::state::sequencer::isChildContentView(sequencer_) &&
                   !context_selector_workflow_.ownsGesture() &&
                   navigation_workflow_.allowsMainBindings() &&
                   !edit_workflow_.trackPasteNavigationBlocked() &&
                   !edit_workflow_.trackRemoveNavigationBlocked() &&
                   navigation_workflow_.stepFocusActive();
        })
        .then([this](float delta) {
            if (!publishPatternHistoryBarrier(
                    sequencer_, commitPatternHistoryBarrier(sequencer_, history_))) {
                return;
            }
            navigation_workflow_.moveByFocus(delta);
        });

    encoders_.encoder(Config::EncoderID::NAV)
        .turn()
        .scope(scope_id_)
        .when([this]() {
            return core::state::sequencer::isRootContentView(sequencer_) &&
                   !context_selector_workflow_.ownsGesture() &&
                   navigation_workflow_.allowsMainBindings() &&
                   !edit_workflow_.trackPasteNavigationBlocked() &&
                   !edit_workflow_.trackRemoveNavigationBlocked();
        })
        .then([this](float delta) {
            if (!publishPatternHistoryBarrier(
                    sequencer_, commitPatternHistoryBarrier(sequencer_, history_))) {
                return;
            }
            navigation_workflow_.moveByFocus(delta);
        });

    buttons_.button(Config::ButtonID::NAV)
        .press()
        .scope(scope_id_)
        .when([this]() {
            return navigation_workflow_.allowsMainBindings() &&
                   !navigation_workflow_.selectionActive() &&
                   !edit_workflow_.trackPasteNavigationBlocked() &&
                   !edit_workflow_.trackRemoveNavigationBlocked();
        })
        .then([this]() {
            const auto focus = navigation_focus_.get();
            const bool previewAddSlot =
                focus == core::state::StructureNavigationFocus::TRACK &&
                track_ui_.previewAddSlot.get();
            const uint8_t previewTarget =
                focus == core::state::StructureNavigationFocus::TRACK
                    ? track_ui_.previewTrackIndex.get()
                    : focus == core::state::StructureNavigationFocus::STEP
                        ? sequencer_.focusedStep.get()
                        : sequencer_.structureUi.previewPageIndex.get();
            context_selector_workflow_.press(
                focus,
                core::state::sequencer::isRootContentView(sequencer_),
                previewTarget,
                previewAddSlot
            );
        });

    buttons_.button(Config::ButtonID::NAV)
        .longPress(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS)
        .scope(scope_id_)
        .when([this]() {
            return !core::state::sequencer::isDrumOverviewActive(sequencer_) &&
                context_selector_workflow_.ownsGesture();
        })
        .then([this]() {
            const auto focus = navigation_focus_.get();
            const bool previewAddSlot =
                focus == core::state::StructureNavigationFocus::TRACK &&
                track_ui_.previewAddSlot.get();
            const uint8_t previewTarget =
                focus == core::state::StructureNavigationFocus::TRACK
                    ? track_ui_.previewTrackIndex.get()
                    : focus == core::state::StructureNavigationFocus::STEP
                        ? sequencer_.focusedStep.get()
                        : sequencer_.structureUi.previewPageIndex.get();
            if (!context_selector_workflow_.holdForSelection(
                    focus,
                    previewTarget,
                    previewAddSlot
                )) {
                return;
            }
            enterSelectionModeForCurrentFocus();
        });

    buttons_.button(Config::ButtonID::NAV)
        .release()
        .scope(scope_id_)
        .when([this]() {
            return context_selector_workflow_.ownsGesture() &&
                   !edit_workflow_.trackPasteNavigationBlocked();
        })
        .then([this]() {
            if (context_selector_workflow_.ownsGesture()) {
                handleContextSelectorRelease();
                return;
            }
        });

    buttons_.button(Config::ButtonID::NAV)
        .release()
        .scope(scope_id_)
        .when([this]() {
            return selectionInteractionPolicy().navRelease == SelectionAction::TOGGLE_ITEM &&
                   !edit_workflow_.trackPasteNavigationBlocked();
        })
        .then([this]() { navigation_workflow_.toggleSelectionAtCursor(); });

    buttons_.button(Config::ButtonID::LEFT_TOP)
        .release()
        .scope(scope_id_)
        .when([this]() {
            return navigation_workflow_.allowsMainBindings() &&
                   core::state::sequencer::isChildContentView(sequencer_) &&
                   !edit_workflow_.trackPasteNavigationBlocked();
        })
        .then([this]() {
            if (core::state::sequencer::isChildContentView(sequencer_)) {
                if (sequencer_.stepContentDraft.active.get()) {
                    backFromStepContentDraft();
                    return;
                }
                if (!publishPatternHistoryBarrier(sequencer_,
                                                  commitPatternHistoryBarrier(sequencer_, history_))) {
                    return;
                }
                core::state::sequencer::leaveContentView(sequencer_);
                return;
            }
        });

    buttons_.button(Config::ButtonID::BOTTOM_LEFT)
        .release()
        .scope(scope_id_)
        .when([this]() { return childPatternContentActionsAvailable(); })
        .then([this]() {
            if (bottom_action_release_latch_.consume(Config::ButtonID::BOTTOM_LEFT)) { return; }
            clearFocusedStepContent();
        });

    buttons_.button(Config::ButtonID::BOTTOM_LEFT)
        .longPress(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS)
        .scope(scope_id_)
        .priority(100)
        .when([this]() {
            return sequencer_.drumSequencer.laneSelection.active;
        })
        .then([this]() {
            bottom_action_release_latch_.arm(Config::ButtonID::BOTTOM_LEFT);
        });

    buttons_.button(Config::ButtonID::BOTTOM_LEFT)
        .longPress(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS)
        .scope(scope_id_)
        .when([this]() {
            return childPatternContentActionsAvailable() && focusedStepHasChildContent();
        })
        .then([this]() {
            bottom_action_release_latch_.arm(Config::ButtonID::BOTTOM_LEFT);
            clearFocusedStepContent();
        });

    buttons_.button(Config::ButtonID::BOTTOM_RIGHT)
        .release()
        .scope(scope_id_)
        .when([this]() { return childPatternContentActionsAvailable(); })
        .then([this]() {
            if (bottom_action_release_latch_.consume(Config::ButtonID::BOTTOM_RIGHT)) { return; }
            copyFocusedStepContent();
        });

    buttons_.button(Config::ButtonID::BOTTOM_RIGHT)
        .longPress(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS)
        .scope(scope_id_)
        .when([this]() {
            return childPatternContentActionsAvailable() && canPasteFocusedStepContent();
        })
        .then([this]() {
            bottom_action_release_latch_.arm(Config::ButtonID::BOTTOM_RIGHT);
            pasteFocusedStepContent();
        });

    buttons_.button(Config::ButtonID::LEFT_TOP)
        .release()
        .scope(scope_id_)
        .when([this]() {
            return selectionInteractionPolicy().leftTopRelease != SelectionAction::NONE &&
                   !edit_workflow_.trackPasteNavigationBlocked();
        })
        .then([this]() {
            edit_workflow_.clearHoldAction();
            (void)navigation_workflow_.backSelectionMode();
        });

    buttons_.button(Config::ButtonID::BOTTOM_LEFT)
        .press()
        .scope(scope_id_)
        .when([this]() { return navigation_workflow_.selectionActive(); })
        .then([this]() {
            if (edit_workflow_.selectionHoldActionAvailable()) {
                edit_workflow_.beginSelectionHoldAction(
                    core::state::StructureHoldAction::REMOVE
                );
            }
        });

    buttons_.button(Config::ButtonID::BOTTOM_LEFT)
        .longPress(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS)
        .scope(scope_id_)
        .when([this]() {
            if (edit_workflow_.selectionTrackRemoveHoldPending()) return true;
            return navigation_workflow_.selectionActive() &&
                   !edit_workflow_.currentTrackRemoveHoldPending() &&
                   edit_workflow_.selectionHoldActionAvailable();
        })
        .then([this]() {
            bottom_action_release_latch_.arm(Config::ButtonID::BOTTOM_LEFT);
#if defined(MS_UX_RECORDER)
            if (ux_trace_state_) ux_trace_state_->ignoreNextBottomLeftRelease = true;
#endif
            if (edit_workflow_.selectionTrackRemoveHoldPending()) {
                edit_workflow_.applyLatchedTrackSelectionLongPress();
                return;
            }
            if (track_ui_.selection.active.get()) {
                edit_workflow_.clearHoldAction();
            }
            edit_workflow_.applySelectionBottomLeftHold();
        });

    buttons_.button(Config::ButtonID::BOTTOM_LEFT)
        .press()
        .scope(scope_id_)
        .when([this]() {
            return currentStructureBottomActionsAvailable() &&
                   !navigation_workflow_.selectionActive();
        })
        .then([this]() {
#if defined(MS_UX_RECORDER)
            if (ux_trace_state_) ux_trace_state_->ignoreNextBottomLeftRelease = false;
#endif
            if (edit_workflow_.canRemoveCurrentStructure()) {
                edit_workflow_.beginHoldAction(core::state::StructureHoldAction::REMOVE);
            }
        });

    buttons_.button(Config::ButtonID::BOTTOM_LEFT)
        .release()
        .scope(scope_id_)
        .when([this]() {
            return bottom_action_release_latch_.isArmed(
                       Config::ButtonID::BOTTOM_LEFT
                   ) ||
                   edit_workflow_.selectionTrackRemoveHoldPending() ||
                   (navigation_workflow_.selectionActive() &&
                    !edit_workflow_.currentTrackRemoveHoldPending());
        })
        .then([this]() {
            if (bottom_action_release_latch_.consume(Config::ButtonID::BOTTOM_LEFT)) {
                edit_workflow_.settleConsumedBottomLeftRelease();
#if defined(MS_UX_RECORDER)
                if (ux_trace_state_) ux_trace_state_->ignoreNextBottomLeftRelease = false;
#endif
                return;
            }
            if (edit_workflow_.selectionTrackRemoveHoldPending()) {
                edit_workflow_.applyLatchedTrackSelectionShortPress();
                return;
            }
            if (track_ui_.selection.active.get()) {
                edit_workflow_.clearHoldAction();
                if (!publishPatternHistoryBarrier(sequencer_,
                                                  commitPatternHistoryBarrier(sequencer_, history_))) {
                    return;
                }
            }
            edit_workflow_.applySelectionBottomLeftTap();
        });

    buttons_.button(Config::ButtonID::BOTTOM_LEFT)
        .release()
        .scope(scope_id_)
        .when([this]() {
            if (bottom_action_release_latch_.isArmed(
                    Config::ButtonID::BOTTOM_LEFT
                )) {
                return true;
            }
            const bool selectionActive =
                navigation_workflow_.selectionActive();
            if (edit_workflow_.currentTrackRemoveHoldPending()) return true;
            if (edit_workflow_.trackRemoveHoldPending()) {
                return !selectionActive;
            }
            return !selectionActive &&
                   currentStructureBottomActionsAvailable();
        })
        .then([this]() {
            if (bottom_action_release_latch_.consume(Config::ButtonID::BOTTOM_LEFT)) {
                edit_workflow_.settleConsumedBottomLeftRelease();
#if defined(MS_UX_RECORDER)
                if (ux_trace_state_) ux_trace_state_->ignoreNextBottomLeftRelease = false;
#endif
                return;
            }
            if (edit_workflow_.currentTrackRemoveHoldPending()) {
                edit_workflow_.applyLatchedCurrentTrackShortPress();
                return;
            }
            if (edit_workflow_.trackRemoveHoldPending()) {
                edit_workflow_.clearHoldAction();
                return;
            }
            // A physical release always terminates the STEP/PAGE hold, even
            // when the short action is a no-op (for example an empty step).
            edit_workflow_.clearHoldAction();
            if (trackFocusActive()) {
                if (!publishPatternHistoryBarrier(sequencer_,
                                                  commitPatternHistoryBarrier(sequencer_, history_))) {
                    return;
                }
            }
            edit_workflow_.applyCurrentStructureShortPress();
        });

    buttons_.button(Config::ButtonID::BOTTOM_LEFT)
        .longPress(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS)
        .scope(scope_id_)
        .when([this]() {
            const bool selectionActive =
                navigation_workflow_.selectionActive();
            if (edit_workflow_.currentTrackRemoveHoldPending()) return true;
            if (edit_workflow_.trackRemoveHoldPending()) {
                return !selectionActive;
            }
            return !selectionActive &&
                   currentStructureBottomActionsAvailable() &&
                   edit_workflow_.canRemoveCurrentStructure();
        })
        .then([this]() {
            bottom_action_release_latch_.arm(Config::ButtonID::BOTTOM_LEFT);
#if defined(MS_UX_RECORDER)
            if (ux_trace_state_) ux_trace_state_->ignoreNextBottomLeftRelease = true;
#endif
            edit_workflow_.applyCurrentStructureLongPress();
        });

    buttons_.button(Config::ButtonID::BOTTOM_RIGHT)
        .press()
        .scope(scope_id_)
        .when([this]() {
            return navigation_workflow_.selectionActive() &&
                   !sequencer_.structureUi.stepSelection.active.get();
        })
        .then([this]() {
#if defined(MS_UX_RECORDER)
            if (ux_trace_state_) { ux_trace_state_->ignoreNextBottomRightRelease = false; }
#endif
            const bool placing = track_ui_.selection.placementActive() ||
                sequencer_.structureUi.pageSelection.placementActive() ||
                sequencer_.drumSequencer.laneSelection.placementActive();
            if (placing && selectionInteractionPolicy().bottomRightLongPress ==
                               SelectionAction::PASTE_SELECTION) {
                edit_workflow_.beginHoldAction(core::state::StructureHoldAction::PASTE);
            }
        });

    buttons_.button(Config::ButtonID::BOTTOM_RIGHT)
        .press()
        .scope(scope_id_)
        .when([this]() { return sequencer_.structureUi.stepSelection.active.get(); })
        .then([this]() {
            if (selectionInteractionPolicy().bottomRightLongPress ==
                SelectionAction::PASTE_SELECTION) {
                edit_workflow_.beginHoldAction(core::state::StructureHoldAction::PASTE);
            }
            edit_workflow_.beginStepPastePreview();
        });

    buttons_.button(Config::ButtonID::BOTTOM_RIGHT)
        .press()
        .scope(scope_id_)
        .when([this]() { return currentStructureBottomActionsAvailable(); })
        .then([this]() {
#if defined(MS_UX_RECORDER)
            if (ux_trace_state_) ux_trace_state_->ignoreNextBottomRightRelease = false;
#endif
            if (edit_workflow_.canPasteCurrentStructure()) {
                edit_workflow_.beginHoldAction(core::state::StructureHoldAction::PASTE);
            }
        });

    buttons_.button(Config::ButtonID::BOTTOM_RIGHT)
        .release()
        .scope(scope_id_)
        .when([this]() {
            return navigation_workflow_.selectionActive() &&
                   !sequencer_.structureUi.stepSelection.active.get();
        })
        .then([this]() {
            const bool trackSelection = track_ui_.selection.active.get();
            const bool drumLaneSelection =
                sequencer_.drumSequencer.laneSelection.active;
            const bool placing = trackSelection
                ? track_ui_.selection.placementActive()
                : drumLaneSelection
                    ? sequencer_.drumSequencer.laneSelection.placementActive()
                    : sequencer_.structureUi.pageSelection.placementActive();
            if (trackSelection && placing) {
                (void)edit_workflow_.releaseTrackPasteAction(core::time_compat::millis());
                return;
            }

            edit_workflow_.clearHoldAction();
            if (bottom_action_release_latch_.consume(Config::ButtonID::BOTTOM_RIGHT)) {
#if defined(MS_UX_RECORDER)
                if (ux_trace_state_) { ux_trace_state_->ignoreNextBottomRightRelease = false; }
#endif
                return;
            }
            if (selectionInteractionPolicy().bottomRightRelease ==
                SelectionAction::COPY_SELECTION) {
                edit_workflow_.copyStructureSelection();
            }
        });

    buttons_.button(Config::ButtonID::BOTTOM_RIGHT)
        .release()
        .scope(scope_id_)
        .when([this]() { return sequencer_.structureUi.stepSelection.active.get(); })
        .then([this]() {
            edit_workflow_.clearStepPastePreview();
            edit_workflow_.clearHoldAction();
            if (bottom_action_release_latch_.consume(Config::ButtonID::BOTTOM_RIGHT)) {
#if defined(MS_UX_RECORDER)
                if (ux_trace_state_) ux_trace_state_->ignoreNextBottomRightRelease = false;
#endif
                return;
            }
            if (selectionInteractionPolicy().bottomRightRelease ==
                SelectionAction::COPY_SELECTION) {
                edit_workflow_.copyStepSelection();
            }
        });

    buttons_.button(Config::ButtonID::BOTTOM_RIGHT)
        .release()
        .scope(scope_id_)
        .when([this]() { return currentStructureBottomActionsAvailable(); })
        .then([this]() {
            if (trackFocusActive()) {
                const auto release =
                    edit_workflow_.releaseTrackPasteAction(core::time_compat::millis());
                if (release == core::state::contextual::GuardedActionRelease::TAP) {
                    edit_workflow_.copyCurrentStructure();
                }
                return;
            }
            edit_workflow_.clearHoldAction();
            if (bottom_action_release_latch_.consume(Config::ButtonID::BOTTOM_RIGHT)) {
#if defined(MS_UX_RECORDER)
                if (ux_trace_state_) ux_trace_state_->ignoreNextBottomRightRelease = false;
#endif
                return;
            }
            edit_workflow_.copyCurrentStructure();
        });

    buttons_.button(Config::ButtonID::BOTTOM_RIGHT)
        .longPress(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS)
        .scope(scope_id_)
        .when([this]() {
            const bool structureSelection =
                sequencer_.drumSequencer.laneSelection.active ||
                sequencer_.structureUi.pageSelection.active.get();
            return structureSelection &&
                   selectionInteractionPolicy().bottomRightLongPress ==
                       SelectionAction::PASTE_SELECTION;
        })
        .then([this]() {
            bottom_action_release_latch_.arm(Config::ButtonID::BOTTOM_RIGHT);
#if defined(MS_UX_RECORDER)
            if (ux_trace_state_) { ux_trace_state_->ignoreNextBottomRightRelease = true; }
#endif
            edit_workflow_.pasteStructureSelection();
        });

    buttons_.button(Config::ButtonID::BOTTOM_RIGHT)
        .longPress(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS)
        .scope(scope_id_)
        .when([this]() {
            return sequencer_.structureUi.stepSelection.active.get() &&
                   selectionInteractionPolicy().bottomRightLongPress ==
                       SelectionAction::PASTE_SELECTION;
        })
        .then([this]() {
            bottom_action_release_latch_.arm(Config::ButtonID::BOTTOM_RIGHT);
#if defined(MS_UX_RECORDER)
            if (ux_trace_state_) ux_trace_state_->ignoreNextBottomRightRelease = true;
#endif
            edit_workflow_.pasteStepSelection();
        });

    buttons_.button(Config::ButtonID::BOTTOM_RIGHT)
        .longPress(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS)
        .scope(scope_id_)
        .when([this]() { return currentStructureBottomActionsAvailable() && !trackFocusActive(); })
        .then([this]() {
            bottom_action_release_latch_.arm(Config::ButtonID::BOTTOM_RIGHT);
#if defined(MS_UX_RECORDER)
            if (ux_trace_state_) ux_trace_state_->ignoreNextBottomRightRelease = true;
#endif
            edit_workflow_.pasteCurrentStructure();
        });
}

FLASHMEM bool SequencerStepHandler::selectionHasItems() const {
    return navigation_workflow_.selectedItemsAvailable();
}

FLASHMEM core::state::StructureSelectionInteractionPolicy
SequencerStepHandler::selectionInteractionPolicy() const {
    const bool stepSelection = sequencer_.structureUi.stepSelection.active.get();
    const bool drumLaneSelection =
        sequencer_.drumSequencer.laneSelection.active;
    const bool placement = stepSelection ? sequencer_.structureUi.stepSelection.placementActive()
                           : drumLaneSelection
                               ? sequencer_.drumSequencer.laneSelection.placementActive()
                           : track_ui_.selection.active.get()
                               ? track_ui_.selection.placementActive()
                               : sequencer_.structureUi.pageSelection.placementActive();
    const bool pasteAvailable = stepSelection
        ? edit_workflow_.canPasteStepSelection()
        : edit_workflow_.canPasteStructureSelection();

    return core::state::buildStructureSelectionInteractionPolicy({
        .entryAvailable = false,
        .active = navigation_workflow_.selectionActive(),
        .placing = placement,
        .selectedItemsAvailable = selectionHasItems(),
        .pasteAvailable = pasteAvailable,
    });
}

FLASHMEM void SequencerStepHandler::applyStepContentDraft() {
    if (!core::state::sequencer::isDrumContentView(sequencer_) &&
        !publishPatternHistoryBarrier(
            sequencer_, commitPatternHistoryBarrier(sequencer_, history_))) {
        return;
    }
    (void)sequencer::step_content_draft_workflow::apply(sequencer_, tracks_, history_);
}

FLASHMEM void SequencerStepHandler::backFromStepContentDraft() {
    using Result = sequencer::step_content_draft_workflow::BackResult;
    if (!core::state::sequencer::isDrumContentView(sequencer_) &&
        !publishPatternHistoryBarrier(
            sequencer_, commitPatternHistoryBarrier(sequencer_, history_))) {
        return;
    }
    const auto result =
        sequencer::step_content_draft_workflow::requestBack(sequencer_, history_);
    if (result == Result::DISCARDED || result == Result::SAVED) {
        (void)core::state::sequencer::leaveContentView(sequencer_);
    }
}

FLASHMEM void SequencerStepHandler::moveStepContentDraftExitChoice(float delta) {
    sequencer::step_content_draft_workflow::moveExitChoice(sequencer_, delta);
}

FLASHMEM void SequencerStepHandler::confirmStepContentDraftExitChoice() {
    using Result = sequencer::step_content_draft_workflow::BackResult;
    const auto result =
        sequencer::step_content_draft_workflow::applyExitChoice(sequencer_, tracks_, history_);
    if (result == Result::DISCARDED || result == Result::SAVED) {
        (void)core::state::sequencer::leaveContentView(sequencer_);
    }
}

FLASHMEM void SequencerStepHandler::continueStepContentDraft() {
    sequencer_.stepContentDraft.exitChoice.set(
        core::state::sequencer::SequencerStepContentDraftExitChoice::CONTINUE);
    confirmStepContentDraftExitChoice();
}

FLASHMEM bool SequencerStepHandler::childPatternContentActionsAvailable() const {
    return core::state::sequencer::isChildContentView(sequencer_) &&
           !sequencer_.stepContentDraft.active.get() && navigation_workflow_.allowsMainBindings() &&
           !navigation_workflow_.stepFocusActive();
}

FLASHMEM bool SequencerStepHandler::currentStructureBottomActionsAvailable() const {
    // A child-content draft owns the bottom strip exclusively: only Apply is
    // admissible until the unpublished authoring session is resolved. Do not
    // let hidden structure bindings mutate, copy, or paste behind it.
    if (sequencer_.stepContentDraft.active.get()) return false;
    if (core::state::sequencer::isDrumOverviewActive(sequencer_)) {
        return
               !sequencer_.drumSequencer.selectorVisible() &&
               !context_selector_workflow_.ownsGesture() &&
               (navigation_focus_.get() ==
                    core::state::StructureNavigationFocus::TRACK ||
                navigation_focus_.get() ==
                    core::state::StructureNavigationFocus::STEP);
    }
    if (!navigation_workflow_.allowsMainBindings()) return false;
    if (core::state::sequencer::isRootContentView(sequencer_)) return true;
    return core::state::sequencer::isChildContentView(sequencer_) &&
           navigation_workflow_.stepFocusActive();
}

FLASHMEM void SequencerStepHandler::toggleStep(uint8_t indexInPage) {
    uint8_t abs = 0;
    if (!seq::resolveActiveContentStepInPage(sequencer_, sequencer_.page.get(), indexInPage, abs)) {
        return;
    }

    const bool rootContext = seq::isRootContentView(sequencer_);

    // Micro/Cycle drafts publish one prepared entry when the draft is applied.
    // Their scratch-only toggles must remain allocation-free and must not open
    // an independent Pattern transaction.
    if (!rootContext && sequencer_.stepContentDraft.pattern() != nullptr) {
        sequencer_.focusedStep.set(abs);
        (void)seq::toggleActiveContentStep(sequencer_, abs);
        return;
    }

    if (seq::isDrumContentView(sequencer_)) {
        const auto& owner = sequencer_.contentView;
        auto descriptor = seq::SequencerHistoryDescriptor{
            .kind = seq::SequencerHistoryActionKind::DrumAdvancedContent,
            .trackIndex = owner.drumOwnerTrack,
            .laneIndex = owner.drumOwnerLane,
            .stepIndex = owner.drumOwnerStep,
            .property = seq::StepProperty::NOTE,
        };
        if (!beginDrumHistory(descriptor)) return;
        sequencer_.focusedStep.set(abs);
        const bool changed = seq::toggleActiveContentStep(sequencer_, abs);
        if (changed) {
            tracks_.publishDrumMutation(owner.drumOwnerTrack);
            sequencer_.drumSequencer.bump();
        }
        (void)sealDrumHistory(changed, descriptor, true);
        return;
    }

    const bool beforeEnabled = seq::activeContentStepEnabled(sequencer_, abs);
    auto descriptor = stepToggleDescriptor(abs, beforeEnabled, !beforeEnabled);
    const auto payloadPlan = rootContext
                                 ? seq::SequencerCoalescedPatternPayloadPlan::FlatOnly
                                 : seq::SequencerCoalescedPatternPayloadPlan::FullCurrentPayload;
    const auto begin =
        history_.beginPreparedPatternEdit(kStepToggleOwner, abs, payloadPlan, descriptor);
    if (!seq::sequencerHistoryOpenAccepted(begin)) {
        sequencer_.historyFeedback.showRejection(begin, core::time_compat::millis());
        return;
    }

    sequencer_.focusedStep.set(abs);
    const bool changed = seq::toggleActiveContentStep(sequencer_, abs);
    descriptor =
        stepToggleDescriptor(abs, beforeEnabled, seq::activeContentStepEnabled(sequencer_, abs));

    const auto seal = history_.sealPreparedPatternEdit(kStepToggleOwner, abs, changed, descriptor);
    if (seq::sequencerPreparedPatternEditSealFailed(seal)) {
        sequencer_.historyFeedback.showRejection(
            seq::SequencerHistoryRejectionReason::HistoryUnavailable, core::time_compat::millis());
        return;
    }
    if (seal != seq::SequencerPreparedPatternEditSealOutcome::Sealed) return;

    const auto commit = history_.commitPreparedPatternEdit(kStepToggleOwner);
    if (commit != seq::SequencerPreparedPatternEditCommitOutcome::Committed) {
        sequencer_.historyFeedback.showRejection(
            seq::SequencerHistoryRejectionReason::HistoryUnavailable, core::time_compat::millis());
        return; }
}

}  // namespace core::handler
