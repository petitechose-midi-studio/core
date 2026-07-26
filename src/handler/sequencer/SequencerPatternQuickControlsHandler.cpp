#include "SequencerPatternQuickControlsHandler.hpp"

#include <utility>

#include <config/PlatformCompat.hpp>
#include <config/InputIDs.hpp>
#include <config/TimeCompat.hpp>

#include "handler/common/NavigationUtils.hpp"
#include "SequencerInputUtils.hpp"
#include "SequencerInteractionPolicyAdapter.hpp"
#include "state/sequencer/SequencerContentViewOps.hpp"
#include "state/sequencer/SequencerHistory.hpp"
#include "state/sequencer/SequencerQuickControls.hpp"
#include "state/sequencer/SequencerSnapshotOps.hpp"

namespace core::handler {
using ButtonID = Config::ButtonID;
using EncoderID = Config::EncoderID;
namespace input_utils = core::handler::sequencer::input_utils;
namespace interaction_policy = core::handler::sequencer::interaction_policy;
using Item = core::state::sequencer::PatternQuickControlItem;

namespace {

constexpr int ITEM_COUNT = static_cast<int>(core::state::sequencer::QUICK_CONTROL_VISUAL_ORDER.size());
constexpr uint8_t MICRO_LENGTH_MIN = 2;
constexpr uint8_t MICRO_LENGTH_MAX =
    oc::note::sequencer::StepSequencerGraphLimits::MAX_EXPANDED_NOTES_PER_ROOT_STEP;
constexpr uint8_t CYCLE_LENGTH_MIN = 1;
constexpr uint8_t CYCLE_LENGTH_MAX =
    oc::note::sequencer::StepSequencerGraphLimits::MAX_CYCLE_STATES_PER_SET;
constexpr int CHILD_ITEM_COUNT = 2;

inline oc::type::IsActiveFn selectingPredicate(core::state::sequencer::SequencerState& sequencer) {
    return [&sequencer]() { return sequencer.patternQuickControls.selecting.get(); };
}

inline oc::type::IsActiveFn canOpenQuickControls(
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
        return interaction_policy::canOpenPatternDimensionSelector(policy);
    };
}

inline oc::type::IsActiveFn canDirectEditPatternQuickControl(
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
        return interaction_policy::canOptEditPatternDimension(policy);
    };
}

struct ChildLengthRange {
    uint8_t min = 1;
    uint8_t max = 1;
};

FLASHMEM ChildLengthRange activeChildLengthRange(
    const core::state::sequencer::SequencerState& sequencer
) {
    if (core::state::sequencer::isCycleStatesContentView(sequencer)) {
        return {.min = CYCLE_LENGTH_MIN, .max = CYCLE_LENGTH_MAX};
    }
    return {.min = MICRO_LENGTH_MIN, .max = MICRO_LENGTH_MAX};
}

FLASHMEM uint8_t normalizedToChildLength(
    const core::state::sequencer::SequencerState& sequencer,
    float normalized
) {
    const auto range = activeChildLengthRange(sequencer);
    const int count = static_cast<int>((range.max - range.min) + 1U);
    const int idx = input_utils::normalizedToIndex(normalized, count);
    return static_cast<uint8_t>(range.min + idx);
}

FLASHMEM float childLengthToNormalized(
    const core::state::sequencer::SequencerState& sequencer,
    uint8_t length
) {
    const auto range = activeChildLengthRange(sequencer);
    const uint8_t clamped = std::clamp<uint8_t>(length, range.min, range.max);
    return input_utils::indexToNormalized(
        static_cast<int>(clamped - range.min),
        static_cast<int>((range.max - range.min) + 1U)
    );
}

FLASHMEM uint8_t activeChildLengthStepCount(
    const core::state::sequencer::SequencerState& sequencer
) {
    const auto range = activeChildLengthRange(sequencer);
    return static_cast<uint8_t>((range.max - range.min) + 1U);
}

FLASHMEM int childFocusedItemOrderIndex(
    const core::state::sequencer::SequencerState& sequencer
) {
    return sequencer.patternQuickControls.focusedItem.get() == Item::OFFSET ? 1 : 0;
}

FLASHMEM Item childQuickControlAtOrderIndex(int index) {
    return index == 1 ? Item::OFFSET : Item::LENGTH;
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
    , navigation_focus_(state.navigationFocus)
    , encoders_(encoders)
    , buttons_(buttons)
    , scope_id_(scopeId)
    , history_(state.history) {
    setupBindings();
}

FLASHMEM void SequencerPatternQuickControlsHandler::setupBindings() {
    buttons_.button(ButtonID::LEFT_CENTER)
        .press()
        .latch()
        .scope(scope_id_)
        .when(canOpenQuickControls(overlays_, sequencer_, track_ui_, navigation_focus_))
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

    encoders_.encoder(EncoderID::OPT)
        .turn()
        .scope(scope_id_)
        .when(canDirectEditPatternQuickControl(overlays_, sequencer_, track_ui_, navigation_focus_))
        .then([this](float normalized) { setFocusedValueDirect(normalized); });

    buttons_.button(ButtonID::LEFT_TOP)
        .release()
        .scope(scope_id_)
        .when(selectingPredicate(sequencer_))
        .then([this]() { closeCancel(); });
}

FLASHMEM void SequencerPatternQuickControlsHandler::open() {
    history_.commitCoalescedPatternEdit();

    auto& quick = sequencer_.patternQuickControls;
    prepareQuickControlsForOpen();
    quick.selecting.set(true);
    core::state::sequencer::captureHistorySnapshot(sequencer_, cancel_snapshot_);
    core::state::sequencer::captureHistorySnapshot(sequencer_, offset_snapshot_);
    history_snapshot_valid_ =
        core::state::sequencer::captureHistorySnapshot(sequencer_, history_snapshot_);
    configureOptForFocusedItem();
}

FLASHMEM void SequencerPatternQuickControlsHandler::closeApply() {
    auto& quick = sequencer_.patternQuickControls;
    if (!quick.selecting.get()) return;
    if (history_snapshot_valid_) {
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
    closeTransientQuickControlsState();
}

FLASHMEM void SequencerPatternQuickControlsHandler::closeCancel() {
    auto& quick = sequencer_.patternQuickControls;
    if (!quick.selecting.get()) return;

    core::state::sequencer::applyHistorySnapshotToEditor(sequencer_, cancel_snapshot_);
    core::state::sequencer::refreshContentView(sequencer_);
    clampFocusToLength();

    closeTransientQuickControlsState();
    history_snapshot_valid_ = false;
}

FLASHMEM void SequencerPatternQuickControlsHandler::navigate(float delta) {
    if (!sequencer_.patternQuickControls.selecting.get()) return;
    if (!nav::hasTurnDelta(delta)) return;

    if (core::state::sequencer::isChildContentView(sequencer_)) {
        const int current = childFocusedItemOrderIndex(sequencer_);
        const int next = nav::nextWrappedIndex(delta, current, CHILD_ITEM_COUNT);
        sequencer_.patternQuickControls.focusedItem.set(childQuickControlAtOrderIndex(next));
        if (sequencer_.patternQuickControls.focusedItem.get() == Item::OFFSET) {
            core::state::sequencer::captureHistorySnapshot(sequencer_, offset_snapshot_);
            sequencer_.patternQuickControls.offsetSteps.set(0);
        }
        configureOptForFocusedItem();
        return;
    }

    const int current = focusedItemOrderIndex();
    const int next = nav::nextWrappedIndex(delta, current, ITEM_COUNT);
    const auto nextItem = core::state::sequencer::quickControlAtOrderIndex(static_cast<size_t>(next));
    setFocusedItemByOrderIndex(next);
    if (nextItem == Item::OFFSET) {
        core::state::sequencer::captureHistorySnapshot(sequencer_, offset_snapshot_);
        sequencer_.patternQuickControls.offsetSteps.set(0);
    }
    configureOptForFocusedItem();
}

FLASHMEM void SequencerPatternQuickControlsHandler::setFocusedValue(float normalized) {
    auto item = sequencer_.patternQuickControls.focusedItem.get();
    if (core::state::sequencer::isChildContentView(sequencer_)) {
        if (item == Item::LENGTH) {
            const uint8_t length = normalizedToChildLength(sequencer_, normalized);
            if (core::state::sequencer::isCycleStatesContentView(sequencer_)) {
                core::state::sequencer::resizeActiveCycleStatesContent(sequencer_, length);
            } else {
                core::state::sequencer::resizeActiveMicroSequenceContent(sequencer_, length);
            }
            clampFocusToLength();
            return;
        }

        if (item == Item::OFFSET) {
            const int offsetSteps = normalizedToOffset(normalized);
            if (sequencer_.patternQuickControls.offsetSteps.get() == offsetSteps) {
                return;
            }
            sequencer_.patternQuickControls.offsetSteps.set(static_cast<int8_t>(offsetSteps));
            applyOffsetFromSnapshot(offsetSteps);
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
    sequencer_.patternQuickControls.offsetSteps.set(0);
    if (item == Item::LENGTH) {
        clampFocusToLength();
    }
}

FLASHMEM void SequencerPatternQuickControlsHandler::setFocusedValueDirect(float normalized) {
    auto item = sequencer_.patternQuickControls.focusedItem.get();
    if (core::state::sequencer::isChildContentView(sequencer_) &&
        item != Item::LENGTH &&
        item != Item::OFFSET) {
        item = Item::LENGTH;
        sequencer_.patternQuickControls.focusedItem.set(item);
    }

    const uint8_t len = core::state::sequencer::activeContentLength(sequencer_);
    const uint8_t focusedStep = len == 0
        ? 0
        : std::min<uint8_t>(
              sequencer_.focusedStep.get(),
              static_cast<uint8_t>(len - 1U)
          );
    const uint32_t nowMs = core::time_compat::millis();
    history_.beginCoalescedPatternEdit(
        focusedStep,
        core::state::sequencer::StepProperty::NOTE,
        nowMs
    );
    sequencer_.patternQuickControls.showFeedback(nowMs);

    if (item == Item::OFFSET) {
        const int nextOffset = normalizedToOffset(normalized);
        const int currentOffset = sequencer_.patternQuickControls.offsetSteps.get();
        if (nextOffset == currentOffset) {
            return;
        }
        sequencer_.patternQuickControls.offsetSteps.set(static_cast<int8_t>(nextOffset));
        applyOffsetDelta(nextOffset - currentOffset);
        return;
    }

    setFocusedValue(normalized);
}

FLASHMEM void SequencerPatternQuickControlsHandler::configureOptForFocusedItem() {
    const auto item = sequencer_.patternQuickControls.focusedItem.get();
    if (core::state::sequencer::isChildContentView(sequencer_)) {
        input_utils::StepPropertyEncoderConfig config;
        config.discreteTicksPerStep = input_utils::DEFAULT_DISCRETE_TICKS_PER_STEP;
        config.normalizedTurns = input_utils::DEFAULT_NORMALIZED_TURNS;
        config.discreteSteps = item == Item::LENGTH
            ? activeChildLengthStepCount(sequencer_)
            : static_cast<uint8_t>((currentOffsetMax() * 2) + 1);
        encoders_.setDiscreteTicksPerStep(Config::EncoderID::OPT, config.discreteTicksPerStep);
        encoders_.setNormalizedTurns(Config::EncoderID::OPT, config.normalizedTurns);
        encoders_.setDiscreteSteps(Config::EncoderID::OPT, config.discreteSteps);
        encoders_.setPosition(
            Config::EncoderID::OPT,
            item == Item::LENGTH
                ? childLengthToNormalized(
                      sequencer_,
                      core::state::sequencer::activeContentLength(sequencer_)
                  )
                : offsetToNormalized(sequencer_.patternQuickControls.offsetSteps.get())
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

FLASHMEM void SequencerPatternQuickControlsHandler::prepareQuickControlsForOpen() {
    auto& quick = sequencer_.patternQuickControls;
    quick.offsetSteps.set(0);
    if (core::state::sequencer::isChildContentView(sequencer_)) {
        const auto item = quick.focusedItem.get();
        if (item != Item::LENGTH && item != Item::OFFSET) {
            quick.focusedItem.set(Item::LENGTH);
        }
    }
}

FLASHMEM void SequencerPatternQuickControlsHandler::closeTransientQuickControlsState() {
    auto& quick = sequencer_.patternQuickControls;
    quick.selecting.set(false);
    quick.offsetSteps.set(0);
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
    core::state::sequencer::applyHistorySnapshotToEditor(sequencer_, offset_snapshot_);
    if (offsetSteps != 0) {
        if (core::state::sequencer::isChildContentView(sequencer_)) {
            core::state::sequencer::rotateActiveContentSteps(sequencer_, offsetSteps);
        } else {
            core::state::sequencer::rotatePattern(sequencer_, offsetSteps);
        }
    }
    core::state::sequencer::refreshContentView(sequencer_);
    clampFocusToLength();
}

FLASHMEM void SequencerPatternQuickControlsHandler::applyOffsetDelta(int offsetSteps) {
    if (offsetSteps == 0) return;
    if (core::state::sequencer::isChildContentView(sequencer_)) {
        core::state::sequencer::rotateActiveContentSteps(sequencer_, offsetSteps);
    } else {
        core::state::sequencer::rotatePattern(sequencer_, offsetSteps);
    }
    core::state::sequencer::refreshContentView(sequencer_);
    clampFocusToLength();
}

}  // namespace core::handler
