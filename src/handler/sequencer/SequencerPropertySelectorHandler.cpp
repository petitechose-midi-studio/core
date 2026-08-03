#include "SequencerPropertySelectorHandler.hpp"

#include <algorithm>

#include <config/InputIDs.hpp>
#include <config/PlatformCompat.hpp>
#include <config/Timing.hpp>

#include "handler/common/NavigationUtils.hpp"
#include "handler/sequencer/SequencerCcLaneWorkflow.hpp"
#include "handler/sequencer/SequencerInputUtils.hpp"
#include "handler/sequencer/SequencerInteractionPolicyAdapter.hpp"
#include "state/sequencer/SequencerCcLanePropertySelection.hpp"
#include "state/sequencer/SequencerContentViewOps.hpp"

namespace core::handler {
using ButtonID = Config::ButtonID;
using EncoderID = Config::EncoderID;
namespace input_utils = core::handler::sequencer::input_utils;
namespace interaction_policy = core::handler::sequencer::interaction_policy;
using StepProperty = core::state::sequencer::StepProperty;

namespace {
using PreparedBeginOutcome = core::state::sequencer::SequencerPreparedPatternEditBeginOutcome;
using PreparedCommitOutcome = core::state::sequencer::SequencerPreparedPatternEditCommitOutcome;
using PreparedOwner = core::state::sequencer::SequencerPreparedPatternEditOwner;
using PreparedSealOutcome = core::state::sequencer::SequencerPreparedPatternEditSealOutcome;
using PayloadPlan = core::state::sequencer::SequencerCoalescedPatternPayloadPlan;

constexpr uint8_t STATE_KEY_FLAG = 0x80U;
static_assert(core::state::sequencer::SequencerPatternState::MAX_STEPS <= STATE_KEY_FLAG,
              "Property Selector key must encode every Pattern step");

inline oc::type::IsActiveFn selectingPredicate(core::state::sequencer::SequencerState& sequencer) {
    return [&sequencer]() { return sequencer.stepPropertyInlineSelector.selecting.get(); };
}

inline oc::type::IsActiveFn canOpenPropertySelector(
    oc::state::ExclusiveVisibilityStack<core::ui::OverlayType>& overlays,
    core::state::sequencer::SequencerState& sequencer, core::state::TrackNavigationState& trackUi,
    oc::state::Signal<core::state::StructureNavigationFocus,
                      core::state::kStructureNavigationFocusMaxSubscribers>& navigationFocus,
    Config::ButtonID button) {
    return [&overlays, &sequencer, &trackUi, &navigationFocus, button]() {
        const auto policy = interaction_policy::build(sequencer, trackUi, navigationFocus.get(),
                                                      overlays.hasVisible());
        return button == Config::ButtonID::LEFT_CENTER
                   ? interaction_policy::canOpenMusicalPropertySelectorFromLeftCenter(policy)
                   : interaction_policy::canOpenMusicalPropertySelectorFromLeftBottom(policy);
    };
}

inline oc::type::IsActiveFn canApplyPropertySelector(
    oc::state::ExclusiveVisibilityStack<core::ui::OverlayType>& overlays,
    core::state::sequencer::SequencerState& sequencer, core::state::TrackNavigationState& trackUi,
    oc::state::Signal<core::state::StructureNavigationFocus,
                      core::state::kStructureNavigationFocusMaxSubscribers>& navigationFocus,
    Config::ButtonID button) {
    return [&overlays, &sequencer, &trackUi, &navigationFocus, button]() {
        if (!sequencer.stepPropertyInlineSelector.selecting.get()) return false;
        const auto policy = interaction_policy::build(sequencer, trackUi, navigationFocus.get(),
                                                      overlays.hasVisible());
        return button == Config::ButtonID::LEFT_CENTER
                   ? interaction_policy::canApplyMusicalPropertySelectorFromLeftCenter(policy)
                   : interaction_policy::canApplyMusicalPropertySelectorFromLeftBottom(policy);
    };
}

FLASHMEM uint8_t variationEditKey(StepProperty property) { return static_cast<uint8_t>(property); }

FLASHMEM uint8_t stateEditKey(uint8_t step) { return static_cast<uint8_t>(STATE_KEY_FLAG | step); }

FLASHMEM core::state::sequencer::SequencerHistoryDescriptor makeVariationHistoryDescriptor(
    StepProperty property, int32_t beforeValue, int32_t afterValue) {
    return {
        .kind = core::state::sequencer::SequencerHistoryActionKind::PatternVariation,
        .property = property,
        .hasValue = beforeValue != afterValue,
        .beforeValue = beforeValue,
        .afterValue = afterValue,
    };
}

FLASHMEM core::state::sequencer::SequencerHistoryDescriptor makeStateHistoryDescriptor(
    uint8_t step, int32_t beforeValue, int32_t afterValue) {
    return {
        .kind = core::state::sequencer::SequencerHistoryActionKind::StepToggle,
        .stepIndex = step,
        .property = StepProperty::NOTE,
        .hasValue = beforeValue != afterValue,
        .beforeValue = beforeValue,
        .afterValue = afterValue,
    };
}

}  // namespace

FLASHMEM SequencerPropertySelectorHandler::SequencerPropertySelectorHandler(
    StateRefs state, oc::api::EncoderAPI& encoders, oc::api::ButtonAPI& buttons,
    oc::type::ScopeID scopeId, NowProvider nowProvider, SequencerCcLaneWorkflow* ccLaneWorkflow,
    oc::context::OverlayManager<core::ui::OverlayType>* overlayManager)
    : overlays_(state.overlays), sequencer_(state.sequencer), track_ui_(state.trackNavigation),
      navigation_focus_(state.navigationFocus), history_(state.history), encoders_(encoders),
      buttons_(buttons), scope_id_(scopeId), now_provider_(nowProvider),
      cc_lane_workflow_(ccLaneWorkflow), overlay_manager_(overlayManager) {
    setupBindings();
}

FLASHMEM void SequencerPropertySelectorHandler::setupBindings() {
    buttons_.button(ButtonID::LEFT_CENTER)
        .press()
        .latch()
        .scope(scope_id_)
        .when(canOpenPropertySelector(overlays_, sequencer_, track_ui_, navigation_focus_,
                                      Config::ButtonID::LEFT_CENTER))
        .then([this]() { open(); });

    buttons_.button(ButtonID::LEFT_CENTER)
        .release()
        .scope(scope_id_)
        .when(canApplyPropertySelector(overlays_, sequencer_, track_ui_, navigation_focus_,
                                       Config::ButtonID::LEFT_CENTER))
        .then([this]() { closeApply(); });

    buttons_.button(ButtonID::LEFT_BOTTOM)
        .press()
        .latch()
        .scope(scope_id_)
        .when(canOpenPropertySelector(overlays_, sequencer_, track_ui_, navigation_focus_,
                                      Config::ButtonID::LEFT_BOTTOM))
        .then([this]() { open(); });

    buttons_.button(ButtonID::LEFT_BOTTOM)
        .release()
        .scope(scope_id_)
        .when(canApplyPropertySelector(overlays_, sequencer_, track_ui_, navigation_focus_,
                                       Config::ButtonID::LEFT_BOTTOM))
        .then([this]() { closeApply(); });

    encoders_.encoder(EncoderID::NAV)
        .turn()
        .scope(scope_id_)
        .when(selectingPredicate(sequencer_))
        .then([this](float delta) { navigate(delta); });

    encoders_.encoder(EncoderID::OPT)
        .turn()
        .scope(scope_id_)
        .when(selectingPredicate(sequencer_))
        .then([this](float normalized) { setActiveVariationRange(normalized); });

    buttons_.button(ButtonID::LEFT_TOP)
        .release()
        .scope(scope_id_)
        .when(selectingPredicate(sequencer_))
        .then([this]() { closeCancel(); });
}

FLASHMEM void SequencerPropertySelectorHandler::open() {
    const auto commitOutcome = history_.commitCoalescedPatternEditOutcome();
    if (commitOutcome == core::state::sequencer::SequencerPatternHistoryCommitOutcome::Failed) {
        sequencer_.historyFeedback.showRejection(
            core::state::sequencer::SequencerHistoryRejectionReason::HistoryUnavailable,
            now_provider_ ? now_provider_() : 0U);
        return;
    }

    auto& o = sequencer_.stepPropertyInlineSelector;
    o.reset();
    o.selecting.set(true);

    const int active = sequencer_.stepStatePropertyActive.get()
                           ? core::state::sequencer::SEQUENCER_STEP_STATE_SELECTION_INDEX
                           : core::state::sequencer::sequencerPropertySelectionIndexForProperty(
                                 sequencer_.activeStepProperty.get());
    o.snapshotIndex = active;
    o.snapshotValid = true;
    o.selectedIndex.set(active);
    restore_cc_lane_on_cancel_ = false;
    clearPreparedSegment();
    if (!sequencer_.stepStatePropertyActive.get()) {
        sequencer_.patternVariationFeedback.show(sequencer_.activeStepProperty.get(),
                                                 now_provider_ ? now_provider_() : 0);
    }
    configureOptForSelectedProperty();
}

FLASHMEM void SequencerPropertySelectorHandler::openCcLaneShortcut() {
    const auto commitOutcome = history_.commitCoalescedPatternEditOutcome();
    if (commitOutcome == core::state::sequencer::SequencerPatternHistoryCommitOutcome::Failed) {
        sequencer_.historyFeedback.showRejection(
            core::state::sequencer::SequencerHistoryRejectionReason::HistoryUnavailable,
            now_provider_ ? now_provider_() : 0U);
        return;
    }

    const auto* bank = core::state::sequencer::sequencerCcLaneView(sequencer_.pattern);
    int selected = core::state::sequencer::sequencerPropertySelectionIndexForLane(
        bank, sequencer_.ccLaneUi.focusedLane);
    if (selected < 0) { selected = core::state::sequencer::SEQUENCER_BASE_STEP_PROPERTY_COUNT; }
    restore_cc_lane_on_cancel_ =
        sequencer_.ccLaneUi.mode == core::state::sequencer::SequencerCcLaneUiMode::LANE_GRID;
    if (restore_cc_lane_on_cancel_ && cc_lane_workflow_ != nullptr) {
        cc_lane_workflow_->suspendGridForPropertySelector(now_provider_ ? now_provider_() : 0);
    }

    auto& selector = sequencer_.stepPropertyInlineSelector;
    selector.reset();
    selector.selecting.set(true);
    selector.selectedIndex.set(selected);
    selector.snapshotIndex =
        sequencer_.stepStatePropertyActive.get()
            ? core::state::sequencer::SEQUENCER_STEP_STATE_SELECTION_INDEX
            : core::state::sequencer::sequencerPropertySelectionIndexForProperty(
                  sequencer_.activeStepProperty.get());
    selector.snapshotValid = true;
    selector.suppressOpeningRelease = false;
    clearPreparedSegment();
}

FLASHMEM void SequencerPropertySelectorHandler::navigate(float delta) {
    if (!sequencer_.stepPropertyInlineSelector.selecting.get()) return;
    if (!nav::hasTurnDelta(delta)) return;

    const int current = sequencer_.stepPropertyInlineSelector.selectedIndex.get();
    const auto* bank = core::state::sequencer::sequencerCcLaneView(sequencer_.pattern);
    const int itemCount = core::state::sequencer::sequencerPropertySelectionItemCount(bank);
    const int next = nav::nextWrappedIndex(delta, current, itemCount);
    sequencer_.stepPropertyInlineSelector.selectedIndex.set(next);
    if (next >= core::state::sequencer::SEQUENCER_BASE_STEP_PROPERTY_COUNT) { return; }
    const bool stateItem = core::state::sequencer::sequencerPropertySelectionIsState(next);
    sequencer_.stepStatePropertyActive.set(stateItem);
    if (stateItem) {
        configureOptForSelectedProperty();
        return;
    }
    sequencer_.activeStepProperty.set(
        core::state::sequencer::sequencerPropertySelectionPropertyAt(next));
    if (!sequencer_.stepStatePropertyActive.get()) {
        sequencer_.patternVariationFeedback.show(sequencer_.activeStepProperty.get(),
                                                 now_provider_ ? now_provider_() : 0);
    }
    configureOptForSelectedProperty();
}

FLASHMEM void SequencerPropertySelectorHandler::closeApply() {
    if (!sequencer_.stepPropertyInlineSelector.selecting.get()) return;
    if (sequencer_.stepPropertyInlineSelector.suppressOpeningRelease) {
        sequencer_.stepPropertyInlineSelector.suppressOpeningRelease = false;
        return;
    }
    const int selectedIndex = sequencer_.stepPropertyInlineSelector.selectedIndex.get();
    const bool selectedCcLane =
        selectedIndex >= core::state::sequencer::SEQUENCER_BASE_STEP_PROPERTY_COUNT;
    if (!commitLiveEdits()) return;
    if (!selectedCcLane && !sequencer_.stepStatePropertyActive.get()) {
        sequencer_.patternVariationFeedback.show(sequencer_.activeStepProperty.get(),
                                                 now_provider_ ? now_provider_() : 0);
    }
    sequencer_.stepPropertyInlineSelector.reset();
    restore_cc_lane_on_cancel_ = false;
    if (selectedCcLane) applySelectedCcLaneProperty(selectedIndex);
}

FLASHMEM void SequencerPropertySelectorHandler::closeCancel() {
    auto& o = sequencer_.stepPropertyInlineSelector;
    if (!o.selecting.get()) return;
    if (!commitLiveEdits()) return;
    sequencer_.patternVariationFeedback.show(sequencer_.activeStepProperty.get(),
                                             now_provider_ ? now_provider_() : 0);
    o.reset();
    if (restore_cc_lane_on_cancel_ && cc_lane_workflow_ != nullptr) {
        (void)cc_lane_workflow_->openLane(sequencer_.ccLaneUi.focusedLane);
    }
    restore_cc_lane_on_cancel_ = false;
}

FLASHMEM bool SequencerPropertySelectorHandler::commitLiveEdits() {
    const auto familyOutcome = history_.commitPreparedPatternEdit(PreparedOwner::PropertySelector);
    if (familyOutcome == PreparedCommitOutcome::Failed) {
        sequencer_.historyFeedback.showRejection(
            core::state::sequencer::SequencerHistoryRejectionReason::HistoryUnavailable,
            now_provider_ ? now_provider_() : 0U);
        return false;
    }
    const auto coalescedOutcome = history_.commitCoalescedPatternEditOutcome();
    if (coalescedOutcome == core::state::sequencer::SequencerPatternHistoryCommitOutcome::Failed) {
        sequencer_.historyFeedback.showRejection(
            core::state::sequencer::SequencerHistoryRejectionReason::HistoryUnavailable,
            now_provider_ ? now_provider_() : 0U);
        return false;
    }
    clearPreparedSegment();
    sequencer_.stepPropertyInlineSelector.macroLocalVariationEditActive.set(false);
    return true;
}

FLASHMEM void SequencerPropertySelectorHandler::clearPreparedSegment() {
    prepared_segment_before_value_ = 0;
    prepared_segment_key_ = 0;
    prepared_segment_active_ = false;
}

FLASHMEM void SequencerPropertySelectorHandler::setActiveVariationRange(float normalized) {
    if (!sequencer_.stepPropertyInlineSelector.selecting.get()) return;
    if (core::state::sequencer::sequencerPropertySelectionIsState(
            sequencer_.stepPropertyInlineSelector.selectedIndex.get())) {
        const uint8_t length = core::state::sequencer::activeContentLength(sequencer_);
        if (length == 0) return;
        const uint8_t step =
            std::min<uint8_t>(sequencer_.focusedStep.get(), static_cast<uint8_t>(length - 1U));
        const bool beforeEnabled =
            core::state::sequencer::activeContentStepEnabled(sequencer_, step);
        const bool afterEnabled = normalized >= 0.5f;
        if (beforeEnabled == afterEnabled) return;

        const uint8_t key = stateEditKey(step);
        const auto payloadPlan = core::state::sequencer::isChildContentView(sequencer_)
                                     ? PayloadPlan::FullCurrentPayload
                                     : PayloadPlan::FlatOnly;
        const auto beginOutcome = history_.beginPreparedPatternEdit(
            PreparedOwner::PropertySelector, key, payloadPlan,
            makeStateHistoryDescriptor(step, beforeEnabled ? 1 : 0, afterEnabled ? 1 : 0));
        if (!core::state::sequencer::sequencerHistoryOpenAccepted(beginOutcome)) {
            sequencer_.historyFeedback.showRejection(beginOutcome,
                                                     now_provider_ ? now_provider_() : 0U);
            return;
        }
        if (beginOutcome == PreparedBeginOutcome::Started || !prepared_segment_active_ ||
            prepared_segment_key_ != key) {
            prepared_segment_before_value_ = beforeEnabled ? 1 : 0;
            prepared_segment_key_ = key;
            prepared_segment_active_ = true;
        }
        sequencer_.stepPropertyInlineSelector.macroLocalVariationEditActive.set(false);

        const bool changed =
            core::state::sequencer::setActiveContentStepEnabled(sequencer_, step, afterEnabled);
        const auto sealOutcome = history_.sealPreparedPatternEdit(
            PreparedOwner::PropertySelector, key, changed,
            makeStateHistoryDescriptor(
                step, prepared_segment_before_value_,
                core::state::sequencer::activeContentStepEnabled(sequencer_, step) ? 1 : 0));
        if (core::state::sequencer::sequencerPreparedPatternEditSealClosed(sealOutcome)) {
            clearPreparedSegment();
        }
        if (core::state::sequencer::sequencerPreparedPatternEditSealFailed(sealOutcome)) {
            sequencer_.historyFeedback.showRejection(
                core::state::sequencer::SequencerHistoryRejectionReason::HistoryUnavailable,
                now_provider_ ? now_provider_() : 0U);
            return;
        }
        return;
    }
    if (sequencer_.stepPropertyInlineSelector.selectedIndex.get() >=
        core::state::sequencer::SEQUENCER_BASE_STEP_PROPERTY_COUNT) {
        return;
    }

    const auto property = sequencer_.activeStepProperty.get();
    const uint8_t range = input_utils::normalizedToVariationRange(property, normalized);
    const uint8_t beforeRange = sequencer_.variationRangeForProperty(property);
    if (beforeRange == range) return;

    const uint8_t key = variationEditKey(property);
    const auto beginOutcome = history_.beginPreparedPatternEdit(
        PreparedOwner::PropertySelector, key, PayloadPlan::FlatOnly,
        makeVariationHistoryDescriptor(property, beforeRange, range));
    if (!core::state::sequencer::sequencerHistoryOpenAccepted(beginOutcome)) {
        sequencer_.historyFeedback.showRejection(beginOutcome,
                                                 now_provider_ ? now_provider_() : 0U);
        return;
    }
    if (beginOutcome == PreparedBeginOutcome::Started || !prepared_segment_active_ ||
        prepared_segment_key_ != key) {
        prepared_segment_before_value_ = beforeRange;
        prepared_segment_key_ = key;
        prepared_segment_active_ = true;
    }
    sequencer_.stepPropertyInlineSelector.macroLocalVariationEditActive.set(false);

    const bool changed = sequencer_.setVariationRangeForProperty(property, range);
    const auto sealOutcome = history_.sealPreparedPatternEdit(
        PreparedOwner::PropertySelector, key, changed,
        makeVariationHistoryDescriptor(property, prepared_segment_before_value_,
                                       sequencer_.variationRangeForProperty(property)));
    if (core::state::sequencer::sequencerPreparedPatternEditSealClosed(sealOutcome)) {
        clearPreparedSegment();
    }
    if (core::state::sequencer::sequencerPreparedPatternEditSealFailed(sealOutcome)) {
        sequencer_.historyFeedback.showRejection(
            core::state::sequencer::SequencerHistoryRejectionReason::HistoryUnavailable,
            now_provider_ ? now_provider_() : 0U);
        return;
    }
    if (changed) {
        sequencer_.patternVariationFeedback.show(property, now_provider_ ? now_provider_() : 0);
    }
}

FLASHMEM void SequencerPropertySelectorHandler::configureOptForSelectedProperty() {
    if (core::state::sequencer::sequencerPropertySelectionIsState(
            sequencer_.stepPropertyInlineSelector.selectedIndex.get())) {
        encoders_.setDiscreteTicksPerStep(Config::EncoderID::OPT,
                                          input_utils::DEFAULT_DISCRETE_TICKS_PER_STEP);
        encoders_.setNormalizedTurns(Config::EncoderID::OPT, input_utils::DEFAULT_NORMALIZED_TURNS);
        encoders_.setDiscreteSteps(Config::EncoderID::OPT, 2);
        const uint8_t length = core::state::sequencer::activeContentLength(sequencer_);
        const uint8_t step = length == 0 ? 0
                                         : std::min<uint8_t>(sequencer_.focusedStep.get(),
                                                             static_cast<uint8_t>(length - 1U));
        encoders_.setPosition(
            Config::EncoderID::OPT,
            length > 0 && core::state::sequencer::activeContentStepEnabled(sequencer_, step)
                ? 1.0f
                : 0.0f);
        return;
    }
    if (sequencer_.stepPropertyInlineSelector.selectedIndex.get() >=
        core::state::sequencer::SEQUENCER_BASE_STEP_PROPERTY_COUNT) {
        return;
    }
    const auto property = sequencer_.activeStepProperty.get();
    const auto config = input_utils::encoderConfigForVariationRange(property);
    encoders_.setDiscreteTicksPerStep(Config::EncoderID::OPT, config.discreteTicksPerStep);
    encoders_.setNormalizedTurns(Config::EncoderID::OPT, config.normalizedTurns);
    encoders_.setDiscreteSteps(Config::EncoderID::OPT, config.discreteSteps);
    encoders_.setPosition(Config::EncoderID::OPT,
                          input_utils::variationRangeToNormalized(
                              property, sequencer_.variationRangeForProperty(property)));
}

FLASHMEM void SequencerPropertySelectorHandler::applySelectedCcLaneProperty(int selectedIndex) {
    if (cc_lane_workflow_ == nullptr || overlay_manager_ == nullptr) return;
    const auto* bank = core::state::sequencer::sequencerCcLaneView(sequencer_.pattern);
    const int8_t lane =
        core::state::sequencer::sequencerPropertySelectionLaneAt(bank, selectedIndex);
    if (lane >= 0) {
        (void)cc_lane_workflow_->openLane(static_cast<uint8_t>(lane));
        overlay_manager_->hide();
        return;
    }
    if (core::state::sequencer::sequencerPropertySelectionIsAdd(bank, selectedIndex) &&
        cc_lane_workflow_->createDefaultLane(now_provider_ ? now_provider_() : 0U)) {
        overlay_manager_->hide();
    }
}

}  // namespace core::handler
