#include "SequencerStepEditHandler.hpp"

#include <algorithm>
#include <config/App.hpp>
#include <config/PlatformCompat.hpp>

#include <utility>

#include "handler/common/NavigationUtils.hpp"
#include "SequencerInputUtils.hpp"

namespace core::handler {
namespace input_utils = core::handler::sequencer::input_utils;

namespace {

constexpr uint8_t PROPERTY_ROW_COUNT =
    static_cast<uint8_t>(core::state::sequencer::StepProperty::PROBABILITY) + 1;
constexpr uint8_t ROW_COUNT = PROPERTY_ROW_COUNT + 2;
constexpr uint8_t MICRO_SEQUENCE_ROW = PROPERTY_ROW_COUNT;
constexpr uint8_t CYCLE_STATES_ROW = PROPERTY_ROW_COUNT + 1U;
constexpr uint8_t MICRO_STEP_ROW = PROPERTY_ROW_COUNT;
constexpr uint8_t MICRO_LENGTH_ROW = PROPERTY_ROW_COUNT + 1U;
constexpr int NOTE_OFFSET_MIN = -24;
constexpr int NOTE_OFFSET_MAX = 24;
constexpr int VELOCITY_OFFSET_MIN = -127;
constexpr int VELOCITY_OFFSET_MAX = 127;
constexpr int GATE_OFFSET_MIN = -100;
constexpr int GATE_OFFSET_MAX = 100;
constexpr int PROBABILITY_OFFSET_MIN = -100;
constexpr int PROBABILITY_OFFSET_MAX = 100;
constexpr uint8_t MICRO_LENGTH_MIN = 2;
constexpr uint8_t MICRO_LENGTH_MAX =
    oc::note::sequencer::StepSequencerGraphLimits::MAX_EXPANDED_NOTES_PER_ROOT_STEP;

struct SignedRange {
    int min = 0;
    int max = 0;
};

FLASHMEM bool isPropertyRow(uint8_t row) {
    return row < PROPERTY_ROW_COUNT;
}

FLASHMEM bool isRootContext(
    const core::state::sequencer::StepContentContextView& context
) {
    return !context.active ||
           context.kind == core::state::sequencer::StepContentContextKind::ROOT_STEP;
}

FLASHMEM bool isMicroContext(
    const core::state::sequencer::StepContentContextView& context
) {
    return context.active &&
           context.kind == core::state::sequencer::StepContentContextKind::MICRO_SEQUENCE;
}

FLASHMEM SignedRange offsetRangeForProperty(core::state::sequencer::StepProperty property) {
    switch (property) {
        case core::state::sequencer::StepProperty::NOTE:
            return {NOTE_OFFSET_MIN, NOTE_OFFSET_MAX};
        case core::state::sequencer::StepProperty::VELOCITY:
            return {VELOCITY_OFFSET_MIN, VELOCITY_OFFSET_MAX};
        case core::state::sequencer::StepProperty::GATE:
            return {GATE_OFFSET_MIN, GATE_OFFSET_MAX};
        case core::state::sequencer::StepProperty::NUDGE:
            return {input_utils::NUDGE_MIN, input_utils::NUDGE_MAX};
        case core::state::sequencer::StepProperty::PROBABILITY:
            return {PROBABILITY_OFFSET_MIN, PROBABILITY_OFFSET_MAX};
    }
    return {};
}

FLASHMEM int normalizedToSignedRange(float normalized, SignedRange range) {
    const int width = std::max(0, range.max - range.min);
    return range.min + input_utils::normalizedToInclusiveInt(normalized, width);
}

FLASHMEM float signedRangeToNormalized(int value, SignedRange range) {
    const int width = std::max(0, range.max - range.min);
    return input_utils::indexToNormalized(std::clamp(value, range.min, range.max) - range.min,
                                          width + 1);
}

FLASHMEM uint8_t normalizedToMicroLength(float normalized) {
    const int idx = input_utils::normalizedToInclusiveInt(
        normalized,
        static_cast<int>(MICRO_LENGTH_MAX - MICRO_LENGTH_MIN)
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

FLASHMEM int focusedOffsetValue(
    const core::state::sequencer::StepContentFocusedValues& values,
    core::state::sequencer::StepProperty property
) {
    switch (property) {
        case core::state::sequencer::StepProperty::NOTE:
            return values.noteOffset;
        case core::state::sequencer::StepProperty::VELOCITY:
            return values.velocityOffset;
        case core::state::sequencer::StepProperty::GATE:
            return values.gateOffset;
        case core::state::sequencer::StepProperty::NUDGE:
            return values.nudgeOffset;
        case core::state::sequencer::StepProperty::PROBABILITY:
            return values.probabilityOffset;
    }
    return 0;
}

template <typename EncoderIdT>
FLASHMEM void configureStepEditEncoder(
    oc::api::EncoderAPI& encoders,
    EncoderIdT encoderId,
    core::state::sequencer::StepProperty property,
    const core::state::sequencer::SequencerState& sequencer,
    uint8_t step
) {
    const auto config = input_utils::encoderConfigForProperty(property);
    encoders.setDiscreteTicksPerStep(encoderId, config.discreteTicksPerStep);
    encoders.setNormalizedTurns(encoderId, config.normalizedTurns);
    encoders.setDiscreteSteps(encoderId, config.discreteSteps);
    encoders.setPosition(encoderId, input_utils::stepPropertyToNormalized(sequencer, step, property));
}

template <typename EncoderIdT>
FLASHMEM void configureSignedOffsetEncoder(
    oc::api::EncoderAPI& encoders,
    EncoderIdT encoderId,
    core::state::sequencer::StepProperty property,
    int value
) {
    const auto range = offsetRangeForProperty(property);
    encoders.setDiscreteTicksPerStep(encoderId, input_utils::DEFAULT_DISCRETE_TICKS_PER_STEP);
    encoders.setNormalizedTurns(encoderId, input_utils::DEFAULT_NORMALIZED_TURNS);
    encoders.setDiscreteSteps(
        encoderId,
        static_cast<uint8_t>(std::min(range.max - range.min + 1, 255))
    );
    encoders.setPosition(encoderId, signedRangeToNormalized(value, range));
}

template <typename EncoderIdT>
FLASHMEM void configureMicroLengthEncoder(
    oc::api::EncoderAPI& encoders,
    EncoderIdT encoderId,
    uint8_t length
) {
    encoders.setDiscreteTicksPerStep(encoderId, input_utils::DEFAULT_DISCRETE_TICKS_PER_STEP);
    encoders.setNormalizedTurns(encoderId, input_utils::DEFAULT_NORMALIZED_TURNS);
    encoders.setDiscreteSteps(
        encoderId,
        static_cast<uint8_t>((MICRO_LENGTH_MAX - MICRO_LENGTH_MIN) + 1U)
    );
    encoders.setPosition(encoderId, microLengthToNormalized(length));
}

template <typename EncoderIdT>
FLASHMEM void configureMicroStepEncoder(
    oc::api::EncoderAPI& encoders,
    EncoderIdT encoderId,
    uint8_t localIndex,
    uint8_t length
) {
    const uint8_t safeLength = std::max<uint8_t>(length, 1U);
    encoders.setDiscreteTicksPerStep(encoderId, input_utils::DEFAULT_DISCRETE_TICKS_PER_STEP);
    encoders.setNormalizedTurns(encoderId, input_utils::DEFAULT_NORMALIZED_TURNS);
    encoders.setDiscreteSteps(encoderId, safeLength);
    encoders.setPosition(
        encoderId,
        input_utils::indexToNormalized(localIndex, static_cast<int>(safeLength))
    );
}

inline oc::type::IsActiveFn canOpenStepEdit(
    oc::state::ExclusiveVisibilityStack<core::ui::OverlayType>& overlays,
    core::state::sequencer::SequencerState& sequencer,
    core::state::TrackNavigationState& trackUi
) {
    return [&overlays, &sequencer, &trackUi]() {
        return !overlays.hasVisible() &&
               !sequencer.structureUi.pageSelection.active.get() &&
               !trackUi.selection.active.get() &&
               !sequencer.patternQuickControls.selecting.get() &&
               !sequencer.stepPropertyInlineSelector.selecting.get();
    };
}

}  // namespace

FLASHMEM SequencerStepEditHandler::SequencerStepEditHandler(
    StateRefs state,
    oc::context::OverlayManager<core::ui::OverlayType>& overlays,
    oc::api::EncoderAPI& encoders,
    oc::api::ButtonAPI& buttons,
    oc::type::ScopeID sequencerViewScope,
    oc::type::ScopeID overlayScope
)
    : overlay_state_(state.overlays)
    , sequencer_(state.sequencer)
    , track_ui_(state.trackNavigation)
    , history_(state.history)
    , overlays_(overlays)
    , encoders_(encoders)
    , buttons_(buttons)
    , sequencer_view_scope_(sequencerViewScope)
    , overlay_scope_(overlayScope)
{
    setupBindings();
}

FLASHMEM void SequencerStepEditHandler::setupBindings() {
    // ===== SEQUENCER VIEW SCOPE =====
    // MACRO_i long press: open STEP EDIT for step i in the current page.
    for (uint8_t i = 0; i < Config::MACRO_COUNT; ++i) {
        auto btn = static_cast<oc::type::ButtonID>(Config::MACRO_BUTTONS[i]);
        buttons_.button(btn)
            .longPress(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS)
            .scope(sequencer_view_scope_)
            .when(canOpenStepEdit(overlay_state_, sequencer_, track_ui_))
            .then([this, i]() { openForMacroInPage(i); });
    }

    // ===== OVERLAY SCOPE =====
    // NAV encoder: focus row
    encoders_.encoder(Config::EncoderID::NAV)
        .turn()
        .scope(overlay_scope_)
        .then([this](float delta) { moveFocus(delta); });

    // OPT encoder: edit focused value
    encoders_.encoder(Config::EncoderID::OPT)
        .turn()
        .scope(overlay_scope_)
        .then([this](float value) { setFocusedValue(value); });

    // Pressing the currently edited step closes + applies
    for (uint8_t i = 0; i < Config::MACRO_COUNT; ++i) {
        auto btn = static_cast<oc::type::ButtonID>(Config::MACRO_BUTTONS[i]);
        buttons_.button(btn)
            .release()
            .scope(overlay_scope_)
            .then([this, i]() { maybeCloseApplyFromMacro(i); });
    }

    // Apply + close
    buttons_.button(Config::ButtonID::NAV)
        .release()
        .scope(overlay_scope_)
        .then([this]() { activateFocusedRowOrApply(); });

    // Cancel + close
    buttons_.button(Config::ButtonID::LEFT_TOP)
        .release()
        .scope(overlay_scope_)
        .then([this]() {
            auto& edit = sequencer_.stepEdit;
            if (edit.contentSession.leaveChildContext()) {
                edit.focusedRow.set(0);
                bumpContentRevision();
                configureOptForFocusedRow();
                return;
            }
            closeCancel();
        });

}

FLASHMEM void SequencerStepEditHandler::openForMacroInPage(uint8_t indexInPage) {
    history_.commitCoalescedPatternEdit();

    uint8_t abs = 0;
    if (!sequencer_.resolveStepInPage(sequencer_.page.get(), indexInPage, abs)) return;

    history_snapshot_valid_ =
        core::state::sequencer::captureHistorySnapshot(sequencer_, history_snapshot_);

    sequencer_.focusedStep.set(abs);

    auto& o = sequencer_.stepEdit;
    o.reset();
    o.stepIndex.set(abs);
    o.contentSession.openRootStepContext(abs);
    bumpContentRevision();

    o.snapshotNote = sequencer_.pattern.note[abs];
    o.snapshotVelocity = sequencer_.pattern.velocity[abs];
    o.snapshotGate = sequencer_.pattern.gate[abs];
    o.snapshotNudge = sequencer_.pattern.nudge[abs];
    o.snapshotProbability = sequencer_.pattern.probability[abs];
    o.snapshotValid = true;

    // longPress() fires while button is still pressed; don't immediately close on release.
    ignore_open_release_ = true;
    ignore_open_macro_index_in_page_ = indexInPage;

    overlays_.show(core::ui::OverlayType::SEQ_STEP_EDIT);

    configureOptForFocusedRow();
}

FLASHMEM void SequencerStepEditHandler::closeApply() {
    if (history_snapshot_valid_) {
        core::state::sequencer::SequencerHistoryPatternSnapshot after;
        if (core::state::sequencer::captureHistorySnapshot(sequencer_, after)) {
            history_.recordPattern(
                std::move(history_snapshot_),
                std::move(after),
                core::state::sequencer::SequencerHistoryDescriptor{
                    .kind = core::state::sequencer::SequencerHistoryActionKind::StepEdit,
                    .stepIndex = sequencer_.stepEdit.stepIndex.get(),
                }
            );
        }
    }

    history_snapshot_valid_ = false;
    ignore_open_release_ = false;
    overlays_.hide();
    sequencer_.stepEdit.reset();
}

FLASHMEM void SequencerStepEditHandler::closeCancel() {
    auto& o = sequencer_.stepEdit;

    if (history_snapshot_valid_) {
        core::state::sequencer::applyHistorySnapshotToEditor(sequencer_, history_snapshot_);
    } else {
        const uint8_t abs = o.stepIndex.get();
        if (o.snapshotValid && abs < core::state::sequencer::SequencerState::MAX_STEPS) {
            sequencer_.setStepDataAt(
                abs,
                o.snapshotNote,
                o.snapshotVelocity,
                o.snapshotGate,
                o.snapshotNudge,
                o.snapshotProbability
            );
        }
    }

    history_snapshot_valid_ = false;
    ignore_open_release_ = false;
    overlays_.hide();
    o.reset();
}

FLASHMEM void SequencerStepEditHandler::moveFocus(float delta) {
    if (!nav::hasTurnDelta(delta)) return;

    const int current = static_cast<int>(sequencer_.stepEdit.focusedRow.get());
    const int next = nav::nextWrappedIndex(delta, current, ROW_COUNT);
    sequencer_.stepEdit.focusedRow.set(static_cast<uint8_t>(next));

    configureOptForFocusedRow();
}

FLASHMEM void SequencerStepEditHandler::activateFocusedRowOrApply() {
    auto& edit = sequencer_.stepEdit;
    const auto context = edit.contentSession.current();
    const uint8_t focusedRow = edit.focusedRow.get();

    if (isPropertyRow(focusedRow)) {
        closeApply();
        return;
    }

    if (isRootContext(context) && focusedRow == MICRO_SEQUENCE_ROW) {
        const auto result = edit.contentSession.createOrOpenMicroSequence(sequencer_.pattern);
        if (result.ok) {
            edit.focusedRow.set(0);
            bumpContentRevision();
            configureOptForFocusedRow();
        }
        return;
    }

    if (isRootContext(context) && focusedRow == CYCLE_STATES_ROW) {
        return;
    }

    if (isMicroContext(context) &&
        (focusedRow == MICRO_STEP_ROW || focusedRow == MICRO_LENGTH_ROW)) {
        return;
    }
}

FLASHMEM void SequencerStepEditHandler::setFocusedValue(float normalized) {
    auto& edit = sequencer_.stepEdit;
    const auto context = edit.contentSession.current();

    const uint8_t focusedRow = edit.focusedRow.get();

    if (isMicroContext(context) && focusedRow == MICRO_STEP_ROW) {
        const int index = input_utils::normalizedToIndex(normalized, context.length);
        if (edit.contentSession.focusLocalStep(static_cast<uint8_t>(index))) {
            bumpContentRevision();
            configureOptForFocusedRow();
        }
        return;
    }

    if (isMicroContext(context) && focusedRow == MICRO_LENGTH_ROW) {
        if (edit.contentSession.resizeCurrentMicroSequence(
                sequencer_.pattern,
                normalizedToMicroLength(normalized)
            )) {
            bumpContentRevision();
            configureOptForFocusedRow();
        }
        return;
    }

    if (!isPropertyRow(focusedRow)) return;

    const auto property = input_utils::stepEditRowToProperty(focusedRow);
    if (!isRootContext(context)) {
        const int next = normalizedToSignedRange(normalized, offsetRangeForProperty(property));
        switch (property) {
            case core::state::sequencer::StepProperty::NOTE:
                edit.contentSession.setFocusedNoteOffset(sequencer_.pattern, static_cast<int8_t>(next));
                return;
            case core::state::sequencer::StepProperty::VELOCITY:
                edit.contentSession.setFocusedVelocityOffset(sequencer_.pattern, next);
                return;
            case core::state::sequencer::StepProperty::GATE:
                edit.contentSession.setFocusedGateOffset(sequencer_.pattern, next);
                return;
            case core::state::sequencer::StepProperty::NUDGE:
                edit.contentSession.setFocusedNudgeOffset(sequencer_.pattern, static_cast<int8_t>(next));
                return;
            case core::state::sequencer::StepProperty::PROBABILITY:
                edit.contentSession.setFocusedProbabilityOffset(sequencer_.pattern, next);
                return;
        }
    }

    const uint8_t len = sequencer_.pattern.length.get();
    if (len == 0) return;

    const uint8_t abs = edit.stepIndex.get();
    if (abs >= len) return;
    if (abs >= core::state::sequencer::SequencerState::MAX_STEPS) return;

    input_utils::applyNormalizedToStep(
        sequencer_,
        abs,
        property,
        normalized
    );
}

FLASHMEM void SequencerStepEditHandler::configureOptForFocusedRow() {
    auto& edit = sequencer_.stepEdit;
    const auto context = edit.contentSession.current();
    const uint8_t focusedRow = edit.focusedRow.get();

    if (isMicroContext(context) && focusedRow == MICRO_STEP_ROW) {
        configureMicroStepEncoder(encoders_, Config::EncoderID::OPT, context.localIndex, context.length);
        return;
    }

    if (isMicroContext(context) && focusedRow == MICRO_LENGTH_ROW) {
        configureMicroLengthEncoder(encoders_, Config::EncoderID::OPT, context.length);
        return;
    }

    if (!isPropertyRow(focusedRow)) return;

    const auto property = input_utils::stepEditRowToProperty(focusedRow);
    if (!isRootContext(context)) {
        const auto values = edit.contentSession.focusedValues(sequencer_.pattern);
        if (!values.valid) return;
        configureSignedOffsetEncoder(
            encoders_,
            Config::EncoderID::OPT,
            property,
            focusedOffsetValue(values, property)
        );
        return;
    }

    const uint8_t len = sequencer_.pattern.length.get();
    if (len == 0) return;

    const uint8_t abs = edit.stepIndex.get();
    if (abs >= len) return;
    if (abs >= core::state::sequencer::SequencerState::MAX_STEPS) return;

    configureStepEditEncoder(
        encoders_,
        Config::EncoderID::OPT,
        property,
        sequencer_,
        abs
    );
}

FLASHMEM void SequencerStepEditHandler::maybeCloseApplyFromMacro(uint8_t indexInPage) {
    if (ignore_open_release_ && indexInPage == ignore_open_macro_index_in_page_) {
        ignore_open_release_ = false;
        return;
    }

    auto& edit = sequencer_.stepEdit;
    const auto context = edit.contentSession.current();
    if (!isRootContext(context)) {
        if (indexInPage < context.length && edit.contentSession.focusLocalStep(indexInPage)) {
            bumpContentRevision();
            configureOptForFocusedRow();
        }
        return;
    }

    constexpr uint8_t stepsPerPage = core::state::sequencer::SequencerState::STEPS_PER_PAGE;
    const uint8_t abs = edit.stepIndex.get();
    const uint8_t currentIndexInPage = static_cast<uint8_t>(abs % stepsPerPage);

    if (indexInPage != currentIndexInPage) return;
    closeApply();
}

FLASHMEM void SequencerStepEditHandler::bumpContentRevision() {
    auto& revision = sequencer_.stepEdit.contentRevision;
    revision.set(revision.get() + 1U);
}

}  // namespace core::handler
