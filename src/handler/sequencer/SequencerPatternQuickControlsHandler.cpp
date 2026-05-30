#include "SequencerPatternQuickControlsHandler.hpp"

#include <config/PlatformCompat.hpp>
#include <config/InputIDs.hpp>

#include "handler/common/NavigationUtils.hpp"
#include "SequencerInputUtils.hpp"
#include "state/sequencer/SequencerQuickControls.hpp"
#include "state/sequencer/SequencerSnapshotOps.hpp"

namespace core::handler {
using ButtonID = Config::ButtonID;
using EncoderID = Config::EncoderID;
namespace input_utils = core::handler::sequencer::input_utils;
using Item = core::state::sequencer::PatternQuickControlItem;

namespace {

constexpr int ITEM_COUNT = static_cast<int>(core::state::sequencer::QUICK_CONTROL_VISUAL_ORDER.size());

inline oc::type::IsActiveFn selectingPredicate(core::state::sequencer::SequencerState& sequencer) {
    return [&sequencer]() { return sequencer.patternQuickControls.selecting.get(); };
}

inline oc::type::IsActiveFn canOpenQuickControls(
    oc::state::ExclusiveVisibilityStack<core::ui::OverlayType>& overlays,
    core::state::sequencer::SequencerState& sequencer,
    core::state::TrackNavigationState& trackUi
) {
    return [&overlays, &sequencer, &trackUi]() {
        return !overlays.hasVisible() &&
               !sequencer.structureUi.pageSelection.active.get() &&
               !trackUi.selection.active.get() &&
               !sequencer.stepPropertyInlineSelector.selecting.get();
    };
}

}  // namespace

SequencerPatternQuickControlsHandler::SequencerPatternQuickControlsHandler(
    StateRefs state,
    oc::api::EncoderAPI& encoders,
    oc::api::ButtonAPI& buttons,
    oc::type::ScopeID scopeId
)
    : overlays_(state.overlays)
    , sequencer_(state.sequencer)
    , track_ui_(state.trackNavigation)
    , encoders_(encoders)
    , buttons_(buttons)
    , scope_id_(scopeId) {
    setupBindings();
}

FLASHMEM void SequencerPatternQuickControlsHandler::setupBindings() {
    buttons_.button(ButtonID::LEFT_CENTER)
        .press()
        .latch()
        .scope(scope_id_)
        .when(canOpenQuickControls(overlays_, sequencer_, track_ui_))
        .then([this]() { open(); });

    buttons_.button(ButtonID::LEFT_CENTER)
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
        .then([this](float normalized) { setFocusedValue(normalized); });

    buttons_.button(ButtonID::LEFT_TOP)
        .release()
        .scope(scope_id_)
        .when(selectingPredicate(sequencer_))
        .then([this]() { closeCancel(); });
}

void SequencerPatternQuickControlsHandler::open() {
    auto& quick = sequencer_.patternQuickControls;
    quick.reset();
    quick.selecting.set(true);
    core::state::sequencer::captureSnapshot(sequencer_.pattern, cancel_snapshot_);
    core::state::sequencer::captureSnapshot(sequencer_.pattern, offset_snapshot_);
    configureOptForFocusedItem();
}

void SequencerPatternQuickControlsHandler::closeApply() {
    auto& quick = sequencer_.patternQuickControls;
    if (!quick.selecting.get()) return;
    quick.reset();
}

void SequencerPatternQuickControlsHandler::closeCancel() {
    auto& quick = sequencer_.patternQuickControls;
    if (!quick.selecting.get()) return;

    core::state::sequencer::applySnapshotToEditor(sequencer_, cancel_snapshot_);
    clampFocusToLength();

    quick.reset();
}

void SequencerPatternQuickControlsHandler::navigate(float delta) {
    if (!sequencer_.patternQuickControls.selecting.get()) return;
    if (!nav::hasTurnDelta(delta)) return;

    const int current = focusedItemOrderIndex();
    const int next = nav::nextWrappedIndex(delta, current, ITEM_COUNT);
    const auto nextItem = core::state::sequencer::quickControlAtOrderIndex(static_cast<size_t>(next));
    setFocusedItemByOrderIndex(next);
    if (nextItem == Item::OFFSET) {
        core::state::sequencer::captureSnapshot(sequencer_.pattern, offset_snapshot_);
        sequencer_.patternQuickControls.offsetSteps.set(0);
    }
    configureOptForFocusedItem();
}

void SequencerPatternQuickControlsHandler::setFocusedValue(float normalized) {
    auto item = sequencer_.patternQuickControls.focusedItem.get();
    if (item == Item::OFFSET) {
        const int offsetSteps = normalizedToOffset(normalized);
        if (sequencer_.patternQuickControls.offsetSteps.get() == offsetSteps) {
            return;
        }
        sequencer_.patternQuickControls.offsetSteps.set(static_cast<int8_t>(offsetSteps));
        applyOffsetFromSnapshot(offsetSteps);
        return;
    }

    input_utils::applyNormalizedToQuickControl(sequencer_, item, normalized);
    core::state::sequencer::captureSnapshot(sequencer_.pattern, offset_snapshot_);
    sequencer_.patternQuickControls.offsetSteps.set(0);
    if (item == Item::LENGTH) {
        clampFocusToLength();
    }
}

void SequencerPatternQuickControlsHandler::configureOptForFocusedItem() {
    const auto item = sequencer_.patternQuickControls.focusedItem.get();
    if (item == Item::OFFSET) {
        input_utils::StepPropertyEncoderConfig config;
        config.discreteSteps = static_cast<uint8_t>((currentOffsetMax() * 2) + 1);
        config.discreteTicksPerStep = input_utils::DEFAULT_DISCRETE_TICKS_PER_STEP;
        config.normalizedTurns = input_utils::DEFAULT_NORMALIZED_TURNS;
        encoders_.setDiscreteTicksPerStep(Config::EncoderID::OPT, config.discreteTicksPerStep);
        encoders_.setNormalizedTurns(Config::EncoderID::OPT, config.normalizedTurns);
        encoders_.setDiscreteSteps(Config::EncoderID::OPT, config.discreteSteps);
        encoders_.setPosition(
            Config::EncoderID::OPT,
            offsetToNormalized(sequencer_.patternQuickControls.offsetSteps.get())
        );
        return;
    }

    const auto config = input_utils::encoderConfigForQuickControl(item);
    encoders_.setDiscreteTicksPerStep(Config::EncoderID::OPT, config.discreteTicksPerStep);
    encoders_.setNormalizedTurns(Config::EncoderID::OPT, config.normalizedTurns);
    encoders_.setDiscreteSteps(Config::EncoderID::OPT, config.discreteSteps);
    encoders_.setPosition(
        Config::EncoderID::OPT,
        input_utils::quickControlToNormalized(sequencer_, item)
    );
}

void SequencerPatternQuickControlsHandler::clampFocusToLength() {
    const uint8_t len = sequencer_.pattern.length.get();
    if (len == 0) return;

    uint8_t focused = sequencer_.focusedStep.get();
    if (focused >= len) {
        focused = static_cast<uint8_t>(len - 1);
        sequencer_.focusedStep.set(focused);
    }

    sequencer_.page.set(sequencer_.pageForStep(focused));
}

int SequencerPatternQuickControlsHandler::focusedItemOrderIndex() const {
    const auto focused = sequencer_.patternQuickControls.focusedItem.get();
    return static_cast<int>(core::state::sequencer::quickControlOrderIndex(focused));
}

void SequencerPatternQuickControlsHandler::setFocusedItemByOrderIndex(int index) {
    const int clamped = std::clamp(index, 0, ITEM_COUNT - 1);
    sequencer_.patternQuickControls.focusedItem.set(
        core::state::sequencer::quickControlAtOrderIndex(static_cast<size_t>(clamped))
    );
}

int SequencerPatternQuickControlsHandler::currentOffsetMax() const {
    const uint8_t len = sequencer_.pattern.length.get();
    return (len > 0) ? static_cast<int>(len - 1) : 0;
}

float SequencerPatternQuickControlsHandler::offsetToNormalized(int offsetSteps) const {
    const int maxOffset = currentOffsetMax();
    if (maxOffset <= 0) return 0.5f;
    const int clamped = std::clamp(offsetSteps, -maxOffset, maxOffset);
    return static_cast<float>(clamped + maxOffset) / static_cast<float>(maxOffset * 2);
}

int SequencerPatternQuickControlsHandler::normalizedToOffset(float normalized) const {
    const int maxOffset = currentOffsetMax();
    if (maxOffset <= 0) return 0;
    const int itemCount = (maxOffset * 2) + 1;
    const int index = input_utils::normalizedToIndex(normalized, itemCount);
    return index - maxOffset;
}

void SequencerPatternQuickControlsHandler::applyOffsetFromSnapshot(int offsetSteps) {
    core::state::sequencer::applySnapshotToEditor(sequencer_, offset_snapshot_);
    if (offsetSteps != 0) {
        core::state::sequencer::rotatePattern(sequencer_, offsetSteps);
    }
    clampFocusToLength();
}

}  // namespace core::handler
