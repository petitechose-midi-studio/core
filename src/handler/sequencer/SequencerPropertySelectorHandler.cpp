#include "SequencerPropertySelectorHandler.hpp"

#include <algorithm>
#include <utility>

#include <config/PlatformCompat.hpp>
#include <config/InputIDs.hpp>
#include "handler/common/NavigationUtils.hpp"
#include "handler/sequencer/SequencerInputUtils.hpp"

namespace core::handler {
using ButtonID = Config::ButtonID;
using EncoderID = Config::EncoderID;
namespace input_utils = core::handler::sequencer::input_utils;
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
    core::state::TrackNavigationState& trackUi
) {
    return [&overlays, &sequencer, &trackUi]() {
        return !overlays.hasVisible() &&
               !sequencer.structureUi.pageSelection.active.get() &&
               !trackUi.selection.active.get() &&
               !sequencer.patternQuickControls.selecting.get();
    };
}

uint8_t rangeForProperty(const Ranges& ranges, StepProperty property) {
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

bool findSingleVariationRangeChange(
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

core::state::sequencer::SequencerHistoryDescriptor makeVariationHistoryDescriptor(
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

}  // namespace

SequencerPropertySelectorHandler::SequencerPropertySelectorHandler(
    StateRefs state,
    oc::api::EncoderAPI& encoders,
    oc::api::ButtonAPI& buttons,
    oc::type::ScopeID scopeId,
    NowProvider nowProvider
)
    : overlays_(state.overlays)
    , sequencer_(state.sequencer)
    , track_ui_(state.trackNavigation)
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
        .when(canOpenPropertySelector(overlays_, sequencer_, track_ui_))
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

void SequencerPropertySelectorHandler::open() {
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

void SequencerPropertySelectorHandler::navigate(float delta) {
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

void SequencerPropertySelectorHandler::closeApply() {
    if (!sequencer_.stepPropertyInlineSelector.selecting.get()) return;
    if (history_snapshot_valid_) {
        core::state::sequencer::SequencerHistoryPatternSnapshot after;
        if (core::state::sequencer::captureHistorySnapshot(sequencer_, after)) {
            history_.recordPattern(
                std::move(history_snapshot_),
                std::move(after),
                makeVariationHistoryDescriptor(
                    snapshot_variation_ranges_,
                    sequencer_.pattern.variationRanges,
                    sequencer_.activeStepProperty.get()
                )
            );
        }
    }
    sequencer_.patternVariationFeedback.show(
        sequencer_.activeStepProperty.get(),
        now_provider_ ? now_provider_() : 0
    );
    history_snapshot_valid_ = false;
    sequencer_.stepPropertyInlineSelector.reset();
}

void SequencerPropertySelectorHandler::closeCancel() {
    auto& o = sequencer_.stepPropertyInlineSelector;
    if (!o.selecting.get()) return;
    if (o.snapshotValid) {
        const int restored = std::clamp(o.snapshotIndex, 0, PROPERTY_COUNT - 1);
        sequencer_.activeStepProperty.set(static_cast<core::state::sequencer::StepProperty>(restored));
    }
    sequencer_.setPatternVariationRanges(snapshot_variation_ranges_);
    sequencer_.patternVariationFeedback.reset();
    history_snapshot_valid_ = false;
    o.reset();
}

void SequencerPropertySelectorHandler::setActiveVariationRange(float normalized) {
    if (!sequencer_.stepPropertyInlineSelector.selecting.get()) return;

    const auto property = sequencer_.activeStepProperty.get();
    const uint8_t range = input_utils::normalizedToVariationRange(property, normalized);
    if (sequencer_.setVariationRangeForProperty(property, range)) {
        sequencer_.patternVariationFeedback.show(property, now_provider_ ? now_provider_() : 0);
    }
}

void SequencerPropertySelectorHandler::configureOptForSelectedProperty() {
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
