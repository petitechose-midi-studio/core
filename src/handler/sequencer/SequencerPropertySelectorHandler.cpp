#include "SequencerPropertySelectorHandler.hpp"

#include <algorithm>
#include <utility>

#include <config/PlatformCompat.hpp>
#include <config/InputIDs.hpp>
#include <config/Timing.hpp>
#include "handler/common/NavigationUtils.hpp"
#include "handler/sequencer/SequencerInteractionPolicyAdapter.hpp"
#include "handler/sequencer/SequencerInputUtils.hpp"
#include "handler/sequencer/SequencerCcLaneWorkflow.hpp"
#include "state/sequencer/SequencerCcLanePropertySelection.hpp"
#include "state/sequencer/SequencerContentViewOps.hpp"

namespace core::handler {
using ButtonID = Config::ButtonID;
using EncoderID = Config::EncoderID;
namespace input_utils = core::handler::sequencer::input_utils;
namespace interaction_policy = core::handler::sequencer::interaction_policy;
using Ranges = oc::note::sequencer::StepSequencerVariationRanges;
using StepProperty = core::state::sequencer::StepProperty;

namespace {
inline oc::type::IsActiveFn selectingPredicate(core::state::sequencer::SequencerState& sequencer) {
    return [&sequencer]() { return sequencer.stepPropertyInlineSelector.selecting.get(); };
}

inline oc::type::IsActiveFn canOpenPropertySelector(
    oc::state::ExclusiveVisibilityStack<core::ui::OverlayType>& overlays,
    core::state::sequencer::SequencerState& sequencer,
    core::state::TrackNavigationState& trackUi,
    oc::state::Signal<
        core::state::StructureNavigationFocus,
        core::state::kStructureNavigationFocusMaxSubscribers>& navigationFocus,
    Config::ButtonID button
) {
    return [&overlays, &sequencer, &trackUi, &navigationFocus, button]() {
        const auto policy = interaction_policy::build(
            sequencer,
            trackUi,
            navigationFocus.get(),
            overlays.hasVisible()
        );
        return button == Config::ButtonID::LEFT_CENTER
            ? interaction_policy::canOpenMusicalPropertySelectorFromLeftCenter(policy)
            : interaction_policy::canOpenMusicalPropertySelectorFromLeftBottom(policy);
    };
}

inline oc::type::IsActiveFn canApplyPropertySelector(
    oc::state::ExclusiveVisibilityStack<core::ui::OverlayType>& overlays,
    core::state::sequencer::SequencerState& sequencer,
    core::state::TrackNavigationState& trackUi,
    oc::state::Signal<
        core::state::StructureNavigationFocus,
        core::state::kStructureNavigationFocusMaxSubscribers>& navigationFocus,
    Config::ButtonID button
) {
    return [&overlays, &sequencer, &trackUi, &navigationFocus, button]() {
        if (!sequencer.stepPropertyInlineSelector.selecting.get()) return false;
        const auto policy = interaction_policy::build(
            sequencer,
            trackUi,
            navigationFocus.get(),
            overlays.hasVisible()
        );
        return button == Config::ButtonID::LEFT_CENTER
            ? interaction_policy::canApplyMusicalPropertySelectorFromLeftCenter(policy)
            : interaction_policy::canApplyMusicalPropertySelectorFromLeftBottom(policy);
    };
}

FLASHMEM uint8_t rangeForProperty(const Ranges& ranges, StepProperty property) {
    switch (property) {
        case StepProperty::NOTE:
            return ranges.pitchSemitones;
        case StepProperty::VELOCITY:
            return ranges.velocity;
        case StepProperty::GATE:
            return ranges.gatePercent;
        case StepProperty::NUDGE:
            return ranges.nudge;
        default:
            return 0;
    }
}

FLASHMEM bool findSingleVariationRangeChange(
    const Ranges& before,
    const Ranges& after,
    StepProperty& changedProperty,
    int32_t& beforeValue,
    int32_t& afterValue
) {
    constexpr StepProperty PROPERTIES[] = {
        StepProperty::NOTE,
        StepProperty::VELOCITY,
        StepProperty::GATE,
        StepProperty::NUDGE,
    };

    uint8_t changeCount = 0;
    for (const auto property : PROPERTIES) {
        const uint8_t beforeRange = rangeForProperty(before, property);
        const uint8_t afterRange = rangeForProperty(after, property);
        if (beforeRange == afterRange) continue;

        ++changeCount;
        changedProperty = property;
        beforeValue = beforeRange;
        afterValue = afterRange;
    }

    return changeCount == 1;
}

FLASHMEM bool sameVariationRanges(const Ranges& lhs, const Ranges& rhs) {
    return lhs.pitchSemitones == rhs.pitchSemitones &&
           lhs.velocity == rhs.velocity &&
           lhs.gatePercent == rhs.gatePercent &&
           lhs.nudge == rhs.nudge;
}

FLASHMEM core::state::sequencer::SequencerHistoryDescriptor makeVariationHistoryDescriptor(
    const Ranges& before,
    const Ranges& after,
    StepProperty fallbackProperty
) {
    auto descriptor = core::state::sequencer::SequencerHistoryDescriptor{
        .kind = core::state::sequencer::SequencerHistoryActionKind::PatternVariation,
        .property = fallbackProperty,
    };

    StepProperty changedProperty = fallbackProperty;
    int32_t beforeValue = 0;
    int32_t afterValue = 0;
    if (findSingleVariationRangeChange(before, after, changedProperty, beforeValue, afterValue)) {
        descriptor.property = changedProperty;
        descriptor.hasValue = true;
        descriptor.beforeValue = beforeValue;
        descriptor.afterValue = afterValue;
    }

    return descriptor;
}

FLASHMEM core::state::sequencer::SequencerHistoryDescriptor makeSelectorHistoryDescriptor(
    const core::state::sequencer::SequencerHistoryPatternSnapshot& before,
    const core::state::sequencer::SequencerHistoryPatternSnapshot& after,
    const Ranges& beforeRanges,
    const Ranges& afterRanges,
    StepProperty fallbackProperty,
    bool stateEditSeen
) {
    const bool variationChanged = !sameVariationRanges(beforeRanges, afterRanges);
    if (!stateEditSeen) {
        return makeVariationHistoryDescriptor(
            beforeRanges,
            afterRanges,
            fallbackProperty
        );
    }

    const uint8_t step = before.focusedStep;
    if (variationChanged) {
        return core::state::sequencer::SequencerHistoryDescriptor{
            .kind = core::state::sequencer::SequencerHistoryActionKind::StepEdit,
            .stepIndex = step,
            .property = fallbackProperty,
        };
    }

    const bool beforeEnabled = before.flat.enabledMask.test(step);
    const bool afterEnabled = after.flat.enabledMask.test(step);
    return core::state::sequencer::SequencerHistoryDescriptor{
        .kind = core::state::sequencer::SequencerHistoryActionKind::StepToggle,
        .stepIndex = step,
        .property = StepProperty::NOTE,
        .hasValue = beforeEnabled != afterEnabled,
        .beforeValue = beforeEnabled ? 1 : 0,
        .afterValue = afterEnabled ? 1 : 0,
    };
}

FLASHMEM void recordSelectorVariationHistoryIfChanged(
    core::handler::SequencerHistoryDomainServices& history,
    core::state::sequencer::SequencerState& sequencer,
    bool& snapshotValid,
    core::state::sequencer::SequencerHistoryPatternSnapshot& beforeSnapshot,
    const Ranges& beforeRanges,
    bool stateEditSeen
) {
    if (!snapshotValid) {
        return;
    }

    core::state::sequencer::SequencerHistoryPatternSnapshot after;
    if (core::state::sequencer::captureHistorySnapshot(sequencer, after)) {
        const auto descriptor = makeSelectorHistoryDescriptor(
            beforeSnapshot,
            after,
            beforeRanges,
            sequencer.pattern.variationRanges,
            sequencer.activeStepProperty.get(),
            stateEditSeen
        );
        history.recordPattern(
            std::move(beforeSnapshot),
            std::move(after),
            descriptor
        );
    }

    snapshotValid = false;
}

FLASHMEM void recordSelectorVariationHistoryFromCurrentStateIfChanged(
    core::handler::SequencerHistoryDomainServices& history,
    core::state::sequencer::SequencerState& sequencer,
    const Ranges& beforeRanges
) {
    if (sameVariationRanges(beforeRanges, sequencer.pattern.variationRanges)) {
        return;
    }

    core::state::sequencer::SequencerHistoryPatternSnapshot before;
    core::state::sequencer::SequencerHistoryPatternSnapshot after;
    if (!core::state::sequencer::captureHistorySnapshot(sequencer, before) ||
        !core::state::sequencer::captureHistorySnapshot(sequencer, after)) {
        return;
    }

    before.flat.variationRanges = beforeRanges;
    history.recordPattern(
        std::move(before),
        std::move(after),
        makeVariationHistoryDescriptor(
            beforeRanges,
            sequencer.pattern.variationRanges,
            sequencer.activeStepProperty.get()
        )
    );
}

}  // namespace

FLASHMEM SequencerPropertySelectorHandler::SequencerPropertySelectorHandler(
    StateRefs state,
    oc::api::EncoderAPI& encoders,
    oc::api::ButtonAPI& buttons,
    oc::type::ScopeID scopeId,
    NowProvider nowProvider,
    SequencerCcLaneWorkflow* ccLaneWorkflow,
    oc::context::OverlayManager<core::ui::OverlayType>* overlayManager
)
    : overlays_(state.overlays)
    , sequencer_(state.sequencer)
    , track_ui_(state.trackNavigation)
    , navigation_focus_(state.navigationFocus)
    , history_(state.history)
    , encoders_(encoders)
    , buttons_(buttons)
    , scope_id_(scopeId)
    , now_provider_(nowProvider)
    , cc_lane_workflow_(ccLaneWorkflow)
    , overlay_manager_(overlayManager) {
    setupBindings();
}

FLASHMEM void SequencerPropertySelectorHandler::setupBindings() {
    buttons_.button(ButtonID::LEFT_CENTER)
        .press()
        .latch()
        .scope(scope_id_)
        .when(canOpenPropertySelector(
            overlays_,
            sequencer_,
            track_ui_,
            navigation_focus_,
            Config::ButtonID::LEFT_CENTER
        ))
        .then([this]() { open(); });

    buttons_.button(ButtonID::LEFT_CENTER)
        .release()
        .scope(scope_id_)
        .when(canApplyPropertySelector(
            overlays_,
            sequencer_,
            track_ui_,
            navigation_focus_,
            Config::ButtonID::LEFT_CENTER
        ))
        .then([this]() { closeApply(); });

    buttons_.button(ButtonID::LEFT_BOTTOM)
        .press()
        .latch()
        .scope(scope_id_)
        .when(canOpenPropertySelector(
            overlays_,
            sequencer_,
            track_ui_,
            navigation_focus_,
            Config::ButtonID::LEFT_BOTTOM
        ))
        .then([this]() { open(); });

    buttons_.button(ButtonID::LEFT_BOTTOM)
        .release()
        .scope(scope_id_)
        .when(canApplyPropertySelector(
            overlays_,
            sequencer_,
            track_ui_,
            navigation_focus_,
            Config::ButtonID::LEFT_BOTTOM
        ))
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
    history_.commitCoalescedPatternEdit();

    auto& o = sequencer_.stepPropertyInlineSelector;
    o.reset();
    o.selecting.set(true);

    const int active = sequencer_.stepStatePropertyActive.get()
        ? core::state::sequencer::SEQUENCER_STEP_STATE_SELECTION_INDEX
        : core::state::sequencer::sequencerPropertySelectionIndexForProperty(
              sequencer_.activeStepProperty.get()
          );
    o.snapshotIndex = active;
    o.snapshotValid = true;
    o.selectedIndex.set(active);
    restore_cc_lane_on_cancel_ = false;
    snapshot_variation_ranges_ = sequencer_.pattern.variationRanges;
    history_snapshot_valid_ =
        core::state::sequencer::captureHistorySnapshot(sequencer_, history_snapshot_);
    state_edit_seen_ = false;
    if (!sequencer_.stepStatePropertyActive.get()) {
        sequencer_.patternVariationFeedback.show(
            sequencer_.activeStepProperty.get(),
            now_provider_ ? now_provider_() : 0
        );
    }
    configureOptForSelectedProperty();
}

FLASHMEM void SequencerPropertySelectorHandler::openCcLaneShortcut() {
    history_.commitCoalescedPatternEdit();

    const auto* bank = core::state::sequencer::sequencerCcLaneView(
        sequencer_.pattern
    );
    int selected = core::state::sequencer::sequencerPropertySelectionIndexForLane(
        bank,
        sequencer_.ccLaneUi.focusedLane
    );
    if (selected < 0) {
        selected = core::state::sequencer::SEQUENCER_BASE_STEP_PROPERTY_COUNT;
    }
    restore_cc_lane_on_cancel_ =
        sequencer_.ccLaneUi.mode ==
            core::state::sequencer::SequencerCcLaneUiMode::LANE_GRID;
    if (restore_cc_lane_on_cancel_ && cc_lane_workflow_ != nullptr) {
        cc_lane_workflow_->suspendGridForPropertySelector(
            now_provider_ ? now_provider_() : 0
        );
    }

    auto& selector = sequencer_.stepPropertyInlineSelector;
    selector.reset();
    selector.selecting.set(true);
    selector.selectedIndex.set(selected);
    selector.snapshotIndex = sequencer_.stepStatePropertyActive.get()
        ? core::state::sequencer::SEQUENCER_STEP_STATE_SELECTION_INDEX
        : core::state::sequencer::sequencerPropertySelectionIndexForProperty(
              sequencer_.activeStepProperty.get()
          );
    selector.snapshotValid = true;
    selector.suppressOpeningRelease = false;
    snapshot_variation_ranges_ = sequencer_.pattern.variationRanges;
    history_snapshot_valid_ =
        core::state::sequencer::captureHistorySnapshot(sequencer_, history_snapshot_);
    state_edit_seen_ = false;
}

FLASHMEM void SequencerPropertySelectorHandler::navigate(float delta) {
    if (!sequencer_.stepPropertyInlineSelector.selecting.get()) return;
    if (!nav::hasTurnDelta(delta)) return;

    const int current = sequencer_.stepPropertyInlineSelector.selectedIndex.get();
    const auto* bank = core::state::sequencer::sequencerCcLaneView(
        sequencer_.pattern
    );
    const int itemCount =
        core::state::sequencer::sequencerPropertySelectionItemCount(bank);
    const int next = nav::nextWrappedIndex(delta, current, itemCount);
    sequencer_.stepPropertyInlineSelector.selectedIndex.set(next);
    if (next >= core::state::sequencer::SEQUENCER_BASE_STEP_PROPERTY_COUNT) {
        return;
    }
    const bool stateItem =
        core::state::sequencer::sequencerPropertySelectionIsState(next);
    sequencer_.stepStatePropertyActive.set(stateItem);
    if (stateItem) {
        configureOptForSelectedProperty();
        return;
    }
    sequencer_.activeStepProperty.set(
        core::state::sequencer::sequencerPropertySelectionPropertyAt(next)
    );
    if (!sequencer_.stepStatePropertyActive.get()) {
        sequencer_.patternVariationFeedback.show(
            sequencer_.activeStepProperty.get(),
            now_provider_ ? now_provider_() : 0
        );
    }
    configureOptForSelectedProperty();
}

FLASHMEM void SequencerPropertySelectorHandler::closeApply() {
    if (!sequencer_.stepPropertyInlineSelector.selecting.get()) return;
    if (sequencer_.stepPropertyInlineSelector.suppressOpeningRelease) {
        sequencer_.stepPropertyInlineSelector.suppressOpeningRelease = false;
        return;
    }
    const int selectedIndex =
        sequencer_.stepPropertyInlineSelector.selectedIndex.get();
    const bool selectedCcLane = selectedIndex >=
        core::state::sequencer::SEQUENCER_BASE_STEP_PROPERTY_COUNT;
    const bool localVariationEdit =
        sequencer_.stepPropertyInlineSelector.macroLocalVariationEditActive.get();
    if (localVariationEdit) {
        history_.commitCoalescedPatternEdit();
        recordSelectorVariationHistoryFromCurrentStateIfChanged(
            history_,
            sequencer_,
            snapshot_variation_ranges_
        );
    } else {
        recordSelectorVariationHistoryIfChanged(
            history_,
            sequencer_,
            history_snapshot_valid_,
            history_snapshot_,
            snapshot_variation_ranges_,
            state_edit_seen_
        );
    }
    if (!selectedCcLane && !sequencer_.stepStatePropertyActive.get()) {
        sequencer_.patternVariationFeedback.show(
            sequencer_.activeStepProperty.get(),
            now_provider_ ? now_provider_() : 0
        );
    }
    history_snapshot_valid_ = false;
    state_edit_seen_ = false;
    sequencer_.stepPropertyInlineSelector.reset();
    restore_cc_lane_on_cancel_ = false;
    if (selectedCcLane) applySelectedCcLaneProperty(selectedIndex);
}

FLASHMEM void SequencerPropertySelectorHandler::closeCancel() {
    auto& o = sequencer_.stepPropertyInlineSelector;
    if (!o.selecting.get()) return;
    if (o.macroLocalVariationEditActive.get()) {
        history_.commitCoalescedPatternEdit();
        recordSelectorVariationHistoryFromCurrentStateIfChanged(
            history_,
            sequencer_,
            snapshot_variation_ranges_
        );
    } else {
        recordSelectorVariationHistoryIfChanged(
            history_,
            sequencer_,
            history_snapshot_valid_,
            history_snapshot_,
            snapshot_variation_ranges_,
            state_edit_seen_
        );
    }
    history_snapshot_valid_ = false;
    state_edit_seen_ = false;
    sequencer_.patternVariationFeedback.show(
        sequencer_.activeStepProperty.get(),
        now_provider_ ? now_provider_() : 0
    );
    o.reset();
    if (restore_cc_lane_on_cancel_ && cc_lane_workflow_ != nullptr) {
        (void)cc_lane_workflow_->openLane(sequencer_.ccLaneUi.focusedLane);
    }
    restore_cc_lane_on_cancel_ = false;
}

FLASHMEM void SequencerPropertySelectorHandler::setActiveVariationRange(float normalized) {
    if (!sequencer_.stepPropertyInlineSelector.selecting.get()) return;
    if (core::state::sequencer::sequencerPropertySelectionIsState(
            sequencer_.stepPropertyInlineSelector.selectedIndex.get()
        )) {
        const uint8_t length = core::state::sequencer::activeContentLength(sequencer_);
        if (length == 0) return;
        const uint8_t step = std::min<uint8_t>(
            sequencer_.focusedStep.get(),
            static_cast<uint8_t>(length - 1U)
        );
        if (core::state::sequencer::setActiveContentStepEnabled(
            sequencer_,
            step,
            normalized >= 0.5f
        )) {
            state_edit_seen_ = true;
        }
        return;
    }
    if (sequencer_.stepPropertyInlineSelector.selectedIndex.get() >=
        core::state::sequencer::SEQUENCER_BASE_STEP_PROPERTY_COUNT) {
        return;
    }

    const auto property = sequencer_.activeStepProperty.get();
    const uint8_t range = input_utils::normalizedToVariationRange(property, normalized);
    if (sequencer_.setVariationRangeForProperty(property, range)) {
        sequencer_.patternVariationFeedback.show(property, now_provider_ ? now_provider_() : 0);
    }
}

FLASHMEM void SequencerPropertySelectorHandler::configureOptForSelectedProperty() {
    if (core::state::sequencer::sequencerPropertySelectionIsState(
            sequencer_.stepPropertyInlineSelector.selectedIndex.get()
        )) {
        encoders_.setDiscreteTicksPerStep(
            Config::EncoderID::OPT,
            input_utils::DEFAULT_DISCRETE_TICKS_PER_STEP
        );
        encoders_.setNormalizedTurns(
            Config::EncoderID::OPT,
            input_utils::DEFAULT_NORMALIZED_TURNS
        );
        encoders_.setDiscreteSteps(Config::EncoderID::OPT, 2);
        const uint8_t length = core::state::sequencer::activeContentLength(sequencer_);
        const uint8_t step = length == 0
            ? 0
            : std::min<uint8_t>(
                  sequencer_.focusedStep.get(),
                  static_cast<uint8_t>(length - 1U)
              );
        encoders_.setPosition(
            Config::EncoderID::OPT,
            length > 0 && core::state::sequencer::activeContentStepEnabled(
                sequencer_,
                step
            ) ? 1.0f : 0.0f
        );
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
    encoders_.setPosition(
        Config::EncoderID::OPT,
        input_utils::variationRangeToNormalized(
            property,
            sequencer_.variationRangeForProperty(property)
        )
    );
}

FLASHMEM void SequencerPropertySelectorHandler::applySelectedCcLaneProperty(
    int selectedIndex
) {
    if (cc_lane_workflow_ == nullptr || overlay_manager_ == nullptr) return;
    const auto* bank = core::state::sequencer::sequencerCcLaneView(
        sequencer_.pattern
    );
    const int8_t lane =
        core::state::sequencer::sequencerPropertySelectionLaneAt(
            bank,
            selectedIndex
        );
    if (lane >= 0) {
        (void)cc_lane_workflow_->openLane(static_cast<uint8_t>(lane));
        overlay_manager_->hide();
        return;
    }
    if (core::state::sequencer::sequencerPropertySelectionIsAdd(
            bank,
            selectedIndex
        ) && cc_lane_workflow_->createDefaultLane(
            now_provider_ ? now_provider_() : 0U
        )) {
        overlay_manager_->hide();
    }
}

}  // namespace core::handler
