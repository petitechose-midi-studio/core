#include "SequencerRangeActionHandler.hpp"

#include <algorithm>

#include <oc/ui/lvgl/Scope.hpp>

#include <config/App.hpp>
#include <config/InputIDs.hpp>

#include "handler/common/NavigationUtils.hpp"
#include "SequencerInputUtils.hpp"

namespace core::handler {

using oc::ui::lvgl::scope;

namespace {

using RangeSelectionKind = core::state::sequencer::RangeSelectionKind;
using RangeSelectionPhase = core::state::sequencer::RangeSelectionPhase;

inline bool isSequencerIdleForRangeActions(core::state::CoreState& state) {
    return !state.overlays.hasVisible() &&
           !state.sequencer.stepPropertyInlineSelector.selecting.get() &&
           !state.sequencer.patternQuickControls.selecting.get() &&
           !state.sequencer.rangeSelection.active();
}

inline oc::type::IsActiveFn idleRangePredicate(core::state::CoreState& state) {
    return [&state]() { return isSequencerIdleForRangeActions(state); };
}

inline oc::type::IsActiveFn activeRangePredicate(core::state::CoreState& state) {
    return [&state]() { return state.sequencer.rangeSelection.active(); };
}

}  // namespace

SequencerRangeActionHandler::SequencerRangeActionHandler(core::state::CoreState& state,
                                                         oc::api::EncoderAPI& encoders,
                                                         oc::api::ButtonAPI& buttons,
                                                         lv_obj_t* sequencerViewScope)
    : state_(state)
    , encoders_(encoders)
    , buttons_(buttons)
    , scope_element_(sequencerViewScope) {
    setupBindings();
}

void SequencerRangeActionHandler::setupBindings() {
    buttons_.button(Config::ButtonID::BOTTOM_LEFT)
        .release()
        .scope(scope(scope_element_))
        .when(idleRangePredicate(state_))
        .then([this]() {
            if (ignore_next_bottom_left_release_) {
                ignore_next_bottom_left_release_ = false;
                return;
            }
            armClearPage();
        });

    buttons_.button(Config::ButtonID::BOTTOM_LEFT)
        .longPress(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS)
        .scope(scope(scope_element_))
        .when(idleRangePredicate(state_))
        .then([this]() { openClearRange(); });

    buttons_.button(Config::ButtonID::BOTTOM_RIGHT)
        .release()
        .scope(scope(scope_element_))
        .when(idleRangePredicate(state_))
        .then([this]() {
            if (ignore_next_bottom_right_release_) {
                ignore_next_bottom_right_release_ = false;
                return;
            }
            duplicatePageForward();
        });

    buttons_.button(Config::ButtonID::BOTTOM_RIGHT)
        .longPress(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS)
        .scope(scope(scope_element_))
        .when(idleRangePredicate(state_))
        .then([this]() { openCopyRange(); });

    encoders_.encoder(Config::EncoderID::NAV)
        .turn()
        .scope(scope(scope_element_))
        .when(activeRangePredicate(state_))
        .then([this](float delta) { moveCursor(delta); });

    encoders_.encoder(Config::EncoderID::OPT)
        .turn()
        .scope(scope(scope_element_))
        .when(activeRangePredicate(state_))
        .then([this](float normalized) { moveRange(normalized); });

    buttons_.button(Config::ButtonID::NAV)
        .release()
        .scope(scope(scope_element_))
        .when(activeRangePredicate(state_))
        .then([this]() { commitCursor(); });

    buttons_.button(Config::ButtonID::LEFT_TOP)
        .press()
        .latch()
        .scope(scope(scope_element_))
        .when(activeRangePredicate(state_))
        .then([]() {});

    buttons_.button(Config::ButtonID::LEFT_TOP)
        .release()
        .scope(scope(scope_element_))
        .when(activeRangePredicate(state_))
        .then([this]() { cancel(); });

    buttons_.button(Config::ButtonID::BOTTOM_LEFT)
        .release()
        .scope(scope(scope_element_))
        .then([this]() {
            if (state_.sequencer.rangeSelection.confirmingClearPage()) {
                applyClear();
            }
        });

    buttons_.button(Config::ButtonID::BOTTOM_RIGHT)
        .release()
        .scope(scope(scope_element_))
        .then([this]() {
            if (state_.sequencer.rangeSelection.selectingPasteTarget()) {
                applyPaste();
            }
        });
}

void SequencerRangeActionHandler::armClearPage() {
    const uint8_t len = state_.sequencer.length.get();
    if (len == 0) return;

    auto& range = state_.sequencer.rangeSelection;
    range.reset();
    range.kind.set(RangeSelectionKind::CLEAR);
    range.phase.set(RangeSelectionPhase::CONFIRM_CLEAR);

    const uint8_t start = currentPageStart();
    const uint8_t end = currentPageEnd();
    range.anchorStep.set(start);
    range.cursorStep.set(start);
    range.rangeStart.set(start);
    range.rangeEnd.set(end);
    range.rangeValid.set(true);
    setCursorStep(start);
}

void SequencerRangeActionHandler::openClearRange() {
    ignoreNextBottomLeftRelease();
    beginRangeSelection(RangeSelectionKind::CLEAR);
}

void SequencerRangeActionHandler::openCopyRange() {
    ignoreNextBottomRightRelease();
    beginRangeSelection(RangeSelectionKind::COPY);
}

void SequencerRangeActionHandler::cancel() {
    state_.sequencer.rangeSelection.reset();
}

void SequencerRangeActionHandler::moveCursor(float delta) {
    if (!nav::hasTurnDelta(delta)) return;

    auto& range = state_.sequencer.rangeSelection;
    const auto phase = range.phase.get();
    if (!range.selectingSourceRange() && phase != RangeSelectionPhase::PASTE_TARGET) {
        return;
    }

    const int step = nav::turnStep(delta);
    const uint8_t current = range.cursorStep.get();
    const uint8_t next = static_cast<uint8_t>(
        std::clamp<int>(static_cast<int>(current) + step, 0, maxCursorStep())
    );
    if (next == current) return;

    if (range.selectingSourceRange()) {
        const uint8_t span = currentRangeSpan();
        range.anchorStep.set(next);
        setSelectedRange(next, span);
        configureOptForRangeEdit();
    }

    setCursorStep(next);
}

void SequencerRangeActionHandler::moveRange(float normalized) {
    auto& range = state_.sequencer.rangeSelection;
    if (!range.selectingSourceRange()) return;

    const uint8_t maxSpan = maxRangeSpan();
    const uint8_t span = static_cast<uint8_t>(
        sequencer::input_utils::normalizedToIndex(normalized, static_cast<int>(maxSpan) + 1)
    );
    setSelectedRange(range.anchorStep.get(), span);
}

void SequencerRangeActionHandler::commitCursor() {
    auto& range = state_.sequencer.rangeSelection;

    switch (range.phase.get()) {
        case RangeSelectionPhase::SELECT_RANGE: {
            const uint8_t start = range.rangeStart.get();
            const uint8_t end = range.rangeEnd.get();
            if (range.kind.get() == RangeSelectionKind::CLEAR) {
                state_.sequencer.clearStepRange(start, end);
                range.reset();
                return;
            }

            if (!state_.sequencer.copyStepRangeToClipboard(start, end, range.clipboard)) {
                cancel();
                return;
            }

            range.phase.set(RangeSelectionPhase::PASTE_TARGET);
            setCursorStep(start);
            return;
        }
        case RangeSelectionPhase::CONFIRM_CLEAR:
            applyClear();
            return;
        case RangeSelectionPhase::PASTE_TARGET:
            applyPaste();
            return;
        case RangeSelectionPhase::IDLE:
        default:
            return;
    }
}

void SequencerRangeActionHandler::applyClear() {
    auto& range = state_.sequencer.rangeSelection;
    if (!range.confirmingClearPage()) return;

    state_.sequencer.clearStepRange(range.rangeStart.get(), range.rangeEnd.get());
    range.reset();
}

void SequencerRangeActionHandler::applyPaste() {
    auto& range = state_.sequencer.rangeSelection;
    if (!range.selectingPasteTarget()) return;

    state_.sequencer.pasteClipboardRange(range.cursorStep.get(), range.clipboard);
    range.reset();
}

void SequencerRangeActionHandler::duplicatePageForward() {
    state_.sequencer.duplicatePageForward(state_.sequencer.page.get());
}

void SequencerRangeActionHandler::beginRangeSelection(RangeSelectionKind kind) {
    const uint8_t len = state_.sequencer.length.get();
    if (len == 0) return;

    auto& range = state_.sequencer.rangeSelection;
    range.reset();
    range.kind.set(kind);
    range.phase.set(RangeSelectionPhase::SELECT_RANGE);

    const uint8_t cursor = initialCursorStep();
    range.cursorStep.set(cursor);
    range.anchorStep.set(cursor);
    setSelectedRange(cursor, 0);
    setCursorStep(cursor);
    configureOptForRangeEdit();
}

void SequencerRangeActionHandler::configureOptForRangeEdit() {
    auto& range = state_.sequencer.rangeSelection;
    if (!range.selectingSourceRange()) return;

    const uint8_t maxSpan = maxRangeSpan();
    encoders_.setDiscreteTicksPerStep(
        Config::EncoderID::OPT,
        sequencer::input_utils::DEFAULT_DISCRETE_TICKS_PER_STEP
    );
    encoders_.setNormalizedTurns(
        Config::EncoderID::OPT,
        sequencer::input_utils::DEFAULT_NORMALIZED_TURNS
    );
    encoders_.setDiscreteSteps(Config::EncoderID::OPT, static_cast<uint8_t>(maxSpan + 1));
    encoders_.setPosition(
        Config::EncoderID::OPT,
        sequencer::input_utils::indexToNormalized(currentRangeSpan(), static_cast<int>(maxSpan) + 1)
    );
}

void SequencerRangeActionHandler::setSelectedRange(uint8_t start, uint8_t span) {
    auto& range = state_.sequencer.rangeSelection;
    const uint8_t clampedSpan = static_cast<uint8_t>(std::min<uint16_t>(span, maxRangeSpan()));
    range.rangeStart.set(start);
    range.rangeEnd.set(static_cast<uint8_t>(start + clampedSpan));
    range.rangeValid.set(true);
}

uint8_t SequencerRangeActionHandler::currentRangeSpan() const {
    const auto& range = state_.sequencer.rangeSelection;
    const uint8_t start = range.rangeStart.get();
    const uint8_t end = range.rangeEnd.get();
    if (end <= start) return 0;
    return static_cast<uint8_t>(end - start);
}

uint8_t SequencerRangeActionHandler::maxRangeSpan() const {
    const auto& range = state_.sequencer.rangeSelection;
    const uint8_t start = range.anchorStep.get();
    const uint8_t maxStep = static_cast<uint8_t>(state_.sequencer.length.get() - 1);
    if (start >= maxStep) return 0;
    return static_cast<uint8_t>(maxStep - start);
}

void SequencerRangeActionHandler::ignoreNextBottomLeftRelease() {
    ignore_next_bottom_left_release_ = true;
}

void SequencerRangeActionHandler::ignoreNextBottomRightRelease() {
    ignore_next_bottom_right_release_ = true;
}

void SequencerRangeActionHandler::setCursorStep(uint8_t step) {
    state_.sequencer.rangeSelection.cursorStep.set(step);
    state_.sequencer.focusedStep.set(step);
    state_.sequencer.page.set(state_.sequencer.pageForStep(step));
}

uint8_t SequencerRangeActionHandler::currentPageStart() const {
    return state_.sequencer.pageStartStep(state_.sequencer.page.get());
}

uint8_t SequencerRangeActionHandler::currentPageEnd() const {
    const uint8_t len = state_.sequencer.length.get();
    if (len == 0) return 0;

    const uint8_t start = currentPageStart();
    return static_cast<uint8_t>(std::min<uint16_t>(
        static_cast<uint16_t>(len - 1),
        static_cast<uint16_t>(start + core::state::sequencer::SequencerState::STEPS_PER_PAGE - 1)
    ));
}

uint8_t SequencerRangeActionHandler::initialCursorStep() const {
    const uint8_t len = state_.sequencer.length.get();
    if (len == 0) return 0;

    const uint8_t focused = state_.sequencer.focusedStep.get();
    if (focused < len) return focused;
    return state_.sequencer.pageStartStep(state_.sequencer.page.get());
}

uint8_t SequencerRangeActionHandler::maxCursorStep() const {
    const uint8_t len = state_.sequencer.length.get();
    if (len == 0) return 0;

    const auto& range = state_.sequencer.rangeSelection;
    if (range.selectingSourceRange()) {
        return static_cast<uint8_t>(len - 1);
    }
    if (range.selectingPasteTarget() && range.clipboard.count > 0) {
        // Keep V1 paste fully inside the existing pattern length so the
        // cursor always stays representable by the current page model.
        if (len <= range.clipboard.count) {
            return 0;
        }
        return static_cast<uint8_t>(len - range.clipboard.count);
    }

    return static_cast<uint8_t>(len - 1);
}
}  // namespace core::handler
