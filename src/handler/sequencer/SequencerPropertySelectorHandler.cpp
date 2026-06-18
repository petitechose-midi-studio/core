#include "SequencerPropertySelectorHandler.hpp"

#include <algorithm>
#include <utility>

#include <config/PlatformCompat.hpp>
#include <config/InputIDs.hpp>
#include "handler/common/NavigationUtils.hpp"
#include "handler/sequencer/SequencerInteractionPolicyAdapter.hpp"
#include "handler/sequencer/SequencerInputUtils.hpp"

namespace core::handler {
using ButtonID = Config::ButtonID;
using EncoderID = Config::EncoderID;
namespace input_utils = core::handler::sequencer::input_utils;
namespace interaction_policy = core::handler::sequencer::interaction_policy;
using Ranges = oc::note::sequencer::StepSequencerVariationRanges;
using StepProperty = core::state::sequencer::StepProperty;

namespace {
constexpr int PROPERTY_COUNT =
    static_cast<int>(core::state::sequencer::StepProperty::PROBABILITY) + 1;

inline oc::type::IsActiveFn selectingPredicate(core::state::sequencer::SequencerState& sequencer) {
    return [&sequencer]() { return sequencer.stepPropertyInlineSelector.selecting.get(); };
}

inline oc::type::IsActiveFn canOpenPropertySelector(
    oc::state::ExclusiveVisibilityStack<core::ui::OverlayType>& overlays,
    core::state::sequencer::SequencerState& sequencer,
    core::state::TrackNavigationState& trackUi,
    oc::state::Signal<
        core::state::StructureNavigationFocus,
        core::state::kStructureNavigationFocusMaxSubscribers>& navigationFocus
) {
    return [&overlays, &sequencer, &trackUi, &navigationFocus]() {
        const auto policy = interaction_policy::build(
            sequencer,
            trackUi,
            navigationFocus.get(),
            overlays.hasVisible()
        );
        return interaction_policy::canOpenMusicalPropertySelector(policy);
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

FLASHMEM void recordSelectorVariationHistoryIfChanged(
    core::handler::SequencerHistoryDomainServices& history,
    core::state::sequencer::SequencerState& sequencer,
    bool& snapshotValid,
    core::state::sequencer::SequencerHistoryPatternSnapshot& beforeSnapshot,
    const Ranges& beforeRanges
) {
    if (!snapshotValid) {
        return;
    }

    core::state::sequencer::SequencerHistoryPatternSnapshot after;
    if (core::state::sequencer::captureHistorySnapshot(sequencer, after)) {
        history.recordPattern(
            std::move(beforeSnapshot),
            std::move(after),
            makeVariationHistoryDescriptor(
                beforeRanges,
                sequencer.pattern.variationRanges,
                sequencer.activeStepProperty.get()
            )
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
    NowProvider nowProvider
)
    : overlays_(state.overlays)
    , sequencer_(state.sequencer)
    , track_ui_(state.trackNavigation)
    , navigation_focus_(state.navigationFocus)
    , history_(state.history)
    , encoders_(encoders)
    , buttons_(buttons)
    , scope_id_(scopeId)
    , now_provider_(nowProvider) {
    setupBindings();
}

FLASHMEM void SequencerPropertySelectorHandler::setupBindings() {
    buttons_.button(ButtonID::LEFT_BOTTOM)
        .press()
        .latch()
        .scope(scope_id_)
        .when(canOpenPropertySelector(overlays_, sequencer_, track_ui_, navigation_focus_))
        .then([this]() { open(); });

    buttons_.button(ButtonID::LEFT_BOTTOM)
        .release()
        .scope(scope_id_)
        .when(selectingPredicate(sequencer_))
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

    const int active = static_cast<int>(sequencer_.activeStepProperty.get());
    o.snapshotIndex = active;
    o.snapshotValid = true;
    o.selectedIndex.set(active);
    snapshot_variation_ranges_ = sequencer_.pattern.variationRanges;
    history_snapshot_valid_ =
        core::state::sequencer::captureHistorySnapshot(sequencer_, history_snapshot_);
    sequencer_.patternVariationFeedback.show(
        sequencer_.activeStepProperty.get(),
        now_provider_ ? now_provider_() : 0
    );
    configureOptForSelectedProperty();
}

FLASHMEM void SequencerPropertySelectorHandler::navigate(float delta) {
    if (!sequencer_.stepPropertyInlineSelector.selecting.get()) return;
    if (!nav::hasTurnDelta(delta)) return;

    const int current = sequencer_.stepPropertyInlineSelector.selectedIndex.get();
    const int next = nav::nextWrappedIndex(delta, current, PROPERTY_COUNT);
    sequencer_.stepPropertyInlineSelector.selectedIndex.set(next);
    sequencer_.activeStepProperty.set(
        static_cast<core::state::sequencer::StepProperty>(next)
    );
    sequencer_.patternVariationFeedback.show(
        sequencer_.activeStepProperty.get(),
        now_provider_ ? now_provider_() : 0
    );
    configureOptForSelectedProperty();
}

FLASHMEM void SequencerPropertySelectorHandler::closeApply() {
    if (!sequencer_.stepPropertyInlineSelector.selecting.get()) return;
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
            snapshot_variation_ranges_
        );
    }
    sequencer_.patternVariationFeedback.show(
        sequencer_.activeStepProperty.get(),
        now_provider_ ? now_provider_() : 0
    );
    history_snapshot_valid_ = false;
    sequencer_.stepPropertyInlineSelector.reset();
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
            snapshot_variation_ranges_
        );
    }
    history_snapshot_valid_ = false;
    sequencer_.patternVariationFeedback.show(
        sequencer_.activeStepProperty.get(),
        now_provider_ ? now_provider_() : 0
    );
    o.reset();
}

FLASHMEM void SequencerPropertySelectorHandler::setActiveVariationRange(float normalized) {
    if (!sequencer_.stepPropertyInlineSelector.selecting.get()) return;

    const auto property = sequencer_.activeStepProperty.get();
    const uint8_t range = input_utils::normalizedToVariationRange(property, normalized);
    if (sequencer_.setVariationRangeForProperty(property, range)) {
        sequencer_.patternVariationFeedback.show(property, now_provider_ ? now_provider_() : 0);
    }
}

FLASHMEM void SequencerPropertySelectorHandler::configureOptForSelectedProperty() {
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

}  // namespace core::handler
