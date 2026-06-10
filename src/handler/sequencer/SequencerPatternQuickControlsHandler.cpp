#include "SequencerPatternQuickControlsHandler.hpp"

#include <utility>

#include <config/PlatformCompat.hpp>
#include <config/InputIDs.hpp>

#include "handler/common/NavigationUtils.hpp"
#include "SequencerInputUtils.hpp"
#include "state/sequencer/SequencerContentViewOps.hpp"
#include "state/sequencer/SequencerHistory.hpp"
#include "state/sequencer/SequencerQuickControls.hpp"
#include "state/sequencer/SequencerSnapshotOps.hpp"

namespace core::handler {
using ButtonID = Config::ButtonID;
using EncoderID = Config::EncoderID;
namespace input_utils = core::handler::sequencer::input_utils;
using Item = core::state::sequencer::PatternQuickControlItem;

namespace {

constexpr int ITEM_COUNT = static_cast<int>(core::state::sequencer::QUICK_CONTROL_VISUAL_ORDER.size());
constexpr uint8_t MICRO_LENGTH_MIN = 2;
constexpr uint8_t MICRO_LENGTH_MAX =
    oc::note::sequencer::StepSequencerGraphLimits::MAX_EXPANDED_NOTES_PER_ROOT_STEP;

inline oc::type::IsActiveFn selectingPredicate(core::state::sequencer::SequencerState& sequencer) {
    return [&sequencer]() { return sequencer.patternQuickControls.selecting.get(); };
}

inline oc::type::IsActiveFn selectingWithoutPhysicalHoldPredicate(
    core::state::sequencer::SequencerState& sequencer
) {
    return [&sequencer]() {
        const auto& quick = sequencer.patternQuickControls;
        return quick.selecting.get() && !quick.physicalHoldActive.get();
    };
}

inline oc::type::IsActiveFn physicalHoldPredicate(
    core::state::sequencer::SequencerState& sequencer
) {
    return [&sequencer]() { return sequencer.patternQuickControls.physicalHoldActive.get(); };
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

FLASHMEM uint8_t normalizedToMicroLength(float normalized) {
    const int idx = input_utils::normalizedToIndex(
        normalized,
        static_cast<int>((MICRO_LENGTH_MAX - MICRO_LENGTH_MIN) + 1U)
    );
    return static_cast<uint8_t>(MICRO_LENGTH_MIN + idx);
}

FLASHMEM float microLengthToNormalized(uint8_t length) {
    const uint8_t clamped = std::clamp<uint8_t>(length, MICRO_LENGTH_MIN, MICRO_LENGTH_MAX);
    return input_utils::indexToNormalized(
        static_cast<int>(clamped - MICRO_LENGTH_MIN),
        static_cast<int>((MICRO_LENGTH_MAX - MICRO_LENGTH_MIN) + 1U)
    );
}

}  // namespace

FLASHMEM SequencerPatternQuickControlsHandler::SequencerPatternQuickControlsHandler(
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
    history_ = state.history;
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

    buttons_.button(ButtonID::LEFT_CENTER)
        .longPress()
        .scope(scope_id_)
        .when(selectingPredicate(sequencer_))
        .then([this]() { enterPhysicalHoldLayer(); });

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
        .when(physicalHoldPredicate(sequencer_))
        .then([this]() { consumeUndo(); });

    buttons_.button(ButtonID::LEFT_BOTTOM)
        .release()
        .scope(scope_id_)
        .when(physicalHoldPredicate(sequencer_))
        .then([this]() { consumeRedo(); });

    buttons_.button(ButtonID::LEFT_TOP)
        .release()
        .scope(scope_id_)
        .when(selectingWithoutPhysicalHoldPredicate(sequencer_))
        .then([this]() { closeCancel(); });
}

FLASHMEM void SequencerPatternQuickControlsHandler::open() {
    history_.commitCoalescedPatternEdit();

    auto& quick = sequencer_.patternQuickControls;
    quick.reset();
    quick.selecting.set(true);
    if (core::state::sequencer::isMicroSequenceContentView(sequencer_)) {
        quick.focusedItem.set(Item::LENGTH);
    }
    core::state::sequencer::captureSnapshot(sequencer_.pattern, cancel_snapshot_);
    core::state::sequencer::captureSnapshot(sequencer_.pattern, offset_snapshot_);
    history_snapshot_valid_ =
        core::state::sequencer::captureHistorySnapshot(sequencer_, history_snapshot_);
    history_command_consumed_ = false;
    configureOptForFocusedItem();
}

FLASHMEM void SequencerPatternQuickControlsHandler::closeApply() {
    auto& quick = sequencer_.patternQuickControls;
    if (!quick.selecting.get()) return;
    if (history_snapshot_valid_ && !history_command_consumed_) {
        core::state::sequencer::SequencerHistoryPatternSnapshot after;
        if (core::state::sequencer::captureHistorySnapshot(sequencer_, after)) {
            history_.recordPattern(
                std::move(history_snapshot_),
                std::move(after),
                core::state::sequencer::SequencerHistoryDescriptor{
                    .kind = core::state::sequencer::SequencerHistoryActionKind::QuickControls,
                }
            );
        }
    }
    history_snapshot_valid_ = false;
    history_command_consumed_ = false;
    quick.reset();
}

FLASHMEM void SequencerPatternQuickControlsHandler::closeCancel() {
    auto& quick = sequencer_.patternQuickControls;
    if (!quick.selecting.get()) return;

    core::state::sequencer::applySnapshotToEditor(sequencer_, cancel_snapshot_);
    core::state::sequencer::refreshContentView(sequencer_);
    clampFocusToLength();

    quick.reset();
    history_snapshot_valid_ = false;
    history_command_consumed_ = false;
}

FLASHMEM void SequencerPatternQuickControlsHandler::enterPhysicalHoldLayer() {
    auto& quick = sequencer_.patternQuickControls;
    if (!quick.selecting.get()) return;
    quick.physicalHoldActive.set(true);
}

FLASHMEM void SequencerPatternQuickControlsHandler::consumeUndo() {
    if (history_.undo()) {
        history_command_consumed_ = true;
        core::state::sequencer::captureSnapshot(sequencer_.pattern, cancel_snapshot_);
        core::state::sequencer::captureSnapshot(sequencer_.pattern, offset_snapshot_);
        configureOptForFocusedItem();
    }
}

FLASHMEM void SequencerPatternQuickControlsHandler::consumeRedo() {
    if (history_.redo()) {
        history_command_consumed_ = true;
        core::state::sequencer::captureSnapshot(sequencer_.pattern, cancel_snapshot_);
        core::state::sequencer::captureSnapshot(sequencer_.pattern, offset_snapshot_);
        configureOptForFocusedItem();
    }
}

FLASHMEM void SequencerPatternQuickControlsHandler::navigate(float delta) {
    if (!sequencer_.patternQuickControls.selecting.get()) return;
    if (!nav::hasTurnDelta(delta)) return;

    if (core::state::sequencer::isMicroSequenceContentView(sequencer_)) {
        sequencer_.patternQuickControls.focusedItem.set(Item::LENGTH);
        configureOptForFocusedItem();
        return;
    }

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

FLASHMEM void SequencerPatternQuickControlsHandler::setFocusedValue(float normalized) {
    auto item = sequencer_.patternQuickControls.focusedItem.get();
    if (core::state::sequencer::isMicroSequenceContentView(sequencer_)) {
        if (item == Item::LENGTH) {
            core::state::sequencer::resizeActiveMicroSequenceContent(
                sequencer_,
                normalizedToMicroLength(normalized)
            );
            clampFocusToLength();
        }
        return;
    }

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

FLASHMEM void SequencerPatternQuickControlsHandler::configureOptForFocusedItem() {
    const auto item = sequencer_.patternQuickControls.focusedItem.get();
    if (core::state::sequencer::isMicroSequenceContentView(sequencer_)) {
        input_utils::StepPropertyEncoderConfig config;
        config.discreteTicksPerStep = input_utils::DEFAULT_DISCRETE_TICKS_PER_STEP;
        config.normalizedTurns = input_utils::DEFAULT_NORMALIZED_TURNS;
        config.discreteSteps = item == Item::LENGTH
            ? static_cast<uint8_t>((MICRO_LENGTH_MAX - MICRO_LENGTH_MIN) + 1U)
            : 1U;
        encoders_.setDiscreteTicksPerStep(Config::EncoderID::OPT, config.discreteTicksPerStep);
        encoders_.setNormalizedTurns(Config::EncoderID::OPT, config.normalizedTurns);
        encoders_.setDiscreteSteps(Config::EncoderID::OPT, config.discreteSteps);
        encoders_.setPosition(
            Config::EncoderID::OPT,
            item == Item::LENGTH
                ? microLengthToNormalized(core::state::sequencer::activeContentLength(sequencer_))
                : 0.0f
        );
        return;
    }

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

FLASHMEM void SequencerPatternQuickControlsHandler::clampFocusToLength() {
    const uint8_t len = core::state::sequencer::activeContentLength(sequencer_);
    if (len == 0) return;

    uint8_t focused = sequencer_.focusedStep.get();
    if (focused >= len) {
        focused = static_cast<uint8_t>(len - 1);
        sequencer_.focusedStep.set(focused);
    }

    sequencer_.page.set(core::state::sequencer::activeContentPageForStep(focused));
}

FLASHMEM int SequencerPatternQuickControlsHandler::focusedItemOrderIndex() const {
    const auto focused = sequencer_.patternQuickControls.focusedItem.get();
    return static_cast<int>(core::state::sequencer::quickControlOrderIndex(focused));
}

FLASHMEM void SequencerPatternQuickControlsHandler::setFocusedItemByOrderIndex(int index) {
    const int clamped = std::clamp(index, 0, ITEM_COUNT - 1);
    sequencer_.patternQuickControls.focusedItem.set(
        core::state::sequencer::quickControlAtOrderIndex(static_cast<size_t>(clamped))
    );
}

FLASHMEM int SequencerPatternQuickControlsHandler::currentOffsetMax() const {
    const uint8_t len = core::state::sequencer::activeContentLength(sequencer_);
    return (len > 0) ? static_cast<int>(len - 1) : 0;
}

FLASHMEM float SequencerPatternQuickControlsHandler::offsetToNormalized(int offsetSteps) const {
    const int maxOffset = currentOffsetMax();
    if (maxOffset <= 0) return 0.5f;
    const int clamped = std::clamp(offsetSteps, -maxOffset, maxOffset);
    return static_cast<float>(clamped + maxOffset) / static_cast<float>(maxOffset * 2);
}

FLASHMEM int SequencerPatternQuickControlsHandler::normalizedToOffset(float normalized) const {
    const int maxOffset = currentOffsetMax();
    if (maxOffset <= 0) return 0;
    const int itemCount = (maxOffset * 2) + 1;
    const int index = input_utils::normalizedToIndex(normalized, itemCount);
    return index - maxOffset;
}

FLASHMEM void SequencerPatternQuickControlsHandler::applyOffsetFromSnapshot(int offsetSteps) {
    core::state::sequencer::applySnapshotToEditor(sequencer_, offset_snapshot_);
    if (offsetSteps != 0) {
        core::state::sequencer::rotatePattern(sequencer_, offsetSteps);
    }
    clampFocusToLength();
}

}  // namespace core::handler
