#include "SequencerRangeActionHandler.hpp"

#include <algorithm>

#include <oc/ui/lvgl/Scope.hpp>

#include <config/App.hpp>
#include <config/PlatformCompat.hpp>
#include <config/InputIDs.hpp>

#include "handler/common/NavigationUtils.hpp"
#include "state/sequencer/SequencerSnapshotOps.hpp"
#include "SequencerInputUtils.hpp"

namespace core::handler {

using oc::ui::lvgl::scope;

namespace {

using RangeSelectionKind = core::state::sequencer::RangeSelectionKind;
using RangeSelectionPhase = core::state::sequencer::RangeSelectionPhase;

inline bool isSequencerIdleForRangeActions(core::state::CoreState& state) {
    return !state.overlays.hasVisible() &&
           !state.sequencerTracks.selector.selecting.get() &&
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

FLASHMEM SequencerRangeActionHandler::SequencerRangeActionHandler(core::state::CoreState& state,
                                                                  oc::api::EncoderAPI& encoders,
                                                                  oc::api::ButtonAPI& buttons,
                                                                  lv_obj_t* sequencerViewScope)
    : state_(state)
    , encoders_(encoders)
    , buttons_(buttons)
    , scope_element_(sequencerViewScope) {
    setupBindings();
}

FLASHMEM void SequencerRangeActionHandler::setupBindings() {
    buttons_.button(Config::ButtonID::BOTTOM_LEFT)
        .release()
        .scope(scope(scope_element_))
        .when(idleRangePredicate(state_))
        .then([this]() {
            if (ignore_next_bottom_left_release_) {
                ignore_next_bottom_left_release_ = false;
                return;
            }
            clearCurrentPage();
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
            core::state::sequencer::duplicatePatternForward(state_.sequencer);
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

    buttons_.button(Config::ButtonID::BOTTOM_RIGHT)
        .release()
        .scope(scope(scope_element_))
        .then([this]() {
            if (state_.sequencer.rangeSelection.selectingPasteTarget()) {
                applyPaste();
            }
        });
}

FLASHMEM void SequencerRangeActionHandler::clearCurrentPage() {
    const uint8_t len = state_.sequencer.length.get();
    if (len == 0) return;

    const uint8_t start = currentPageStart();
    const uint8_t end = currentPageEnd();
    core::state::sequencer::clearStepRange(state_.sequencer, start, end);
}

FLASHMEM void SequencerRangeActionHandler::openClearRange() {
    ignoreNextBottomLeftRelease();
    beginRangeSelection(RangeSelectionKind::CLEAR);
}

FLASHMEM void SequencerRangeActionHandler::openCopyRange() {
    ignoreNextBottomRightRelease();
    beginRangeSelection(RangeSelectionKind::COPY);
}

FLASHMEM void SequencerRangeActionHandler::cancel() {
    restoreSnapshotFocus();
    state_.sequencer.rangeSelection.reset();
}

FLASHMEM void SequencerRangeActionHandler::moveCursor(float delta) {
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

FLASHMEM void SequencerRangeActionHandler::moveRange(float normalized) {
    auto& range = state_.sequencer.rangeSelection;
    if (!range.selectingSourceRange()) return;

    const uint8_t maxSpan = maxRangeSpan();
    const uint8_t span = static_cast<uint8_t>(
        sequencer::input_utils::normalizedToIndex(normalized, static_cast<int>(maxSpan) + 1)
    );
    setSelectedRange(range.anchorStep.get(), span);
}

FLASHMEM void SequencerRangeActionHandler::commitCursor() {
    auto& range = state_.sequencer.rangeSelection;

    switch (range.phase.get()) {
        case RangeSelectionPhase::SELECT_RANGE: {
            const uint8_t start = range.rangeStart.get();
            const uint8_t end = range.rangeEnd.get();
            if (range.kind.get() == RangeSelectionKind::CLEAR) {
                core::state::sequencer::clearStepRange(state_.sequencer, start, end);
                range.reset();
                return;
            }

            if (!core::state::sequencer::copyStepRangeToClipboard(
                    state_.sequencer,
                    start,
                    end,
                    range.clipboard
                )) {
                cancel();
                return;
            }

            range.phase.set(RangeSelectionPhase::PASTE_TARGET);
            setCursorStep(static_cast<uint8_t>(std::min<uint16_t>(
                state_.sequencer.length.get(),
                maxCursorStep()
            )));
            return;
        }
        case RangeSelectionPhase::PASTE_TARGET:
            applyPaste();
            return;
        case RangeSelectionPhase::IDLE:
        default:
            return;
    }
}

FLASHMEM void SequencerRangeActionHandler::applyPaste() {
    auto& range = state_.sequencer.rangeSelection;
    if (!range.selectingPasteTarget()) return;

    core::state::sequencer::pasteClipboardRange(
        state_.sequencer,
        range.cursorStep.get(),
        range.clipboard
    );
    range.reset();
}

FLASHMEM void SequencerRangeActionHandler::beginRangeSelection(RangeSelectionKind kind) {
    const uint8_t len = state_.sequencer.length.get();
    if (len == 0) return;

    auto& range = state_.sequencer.rangeSelection;
    range.reset();
    snapshotCurrentFocus();
    range.kind.set(kind);
    range.phase.set(RangeSelectionPhase::SELECT_RANGE);

    const uint8_t cursor = initialCursorStep();
    range.cursorStep.set(cursor);
    range.anchorStep.set(cursor);
    setSelectedRange(cursor, 0);
    setCursorStep(cursor);
    configureOptForRangeEdit();
}

FLASHMEM void SequencerRangeActionHandler::configureOptForRangeEdit() {
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

FLASHMEM void SequencerRangeActionHandler::setSelectedRange(uint8_t start, uint8_t span) {
    auto& range = state_.sequencer.rangeSelection;
    const uint8_t clampedSpan = static_cast<uint8_t>(std::min<uint16_t>(span, maxRangeSpan()));
    range.rangeStart.set(start);
    range.rangeEnd.set(static_cast<uint8_t>(start + clampedSpan));
    range.rangeValid.set(true);
}

FLASHMEM uint8_t SequencerRangeActionHandler::currentRangeSpan() const {
    const auto& range = state_.sequencer.rangeSelection;
    const uint8_t start = range.rangeStart.get();
    const uint8_t end = range.rangeEnd.get();
    if (end <= start) return 0;
    return static_cast<uint8_t>(end - start);
}

FLASHMEM uint8_t SequencerRangeActionHandler::maxRangeSpan() const {
    const auto& range = state_.sequencer.rangeSelection;
    const uint8_t start = range.anchorStep.get();
    const uint8_t maxStep = static_cast<uint8_t>(state_.sequencer.length.get() - 1);
    if (start >= maxStep) return 0;
    return static_cast<uint8_t>(maxStep - start);
}

FLASHMEM void SequencerRangeActionHandler::ignoreNextBottomLeftRelease() {
    ignore_next_bottom_left_release_ = true;
}

FLASHMEM void SequencerRangeActionHandler::ignoreNextBottomRightRelease() {
    ignore_next_bottom_right_release_ = true;
}

FLASHMEM void SequencerRangeActionHandler::setCursorStep(uint8_t step) {
    state_.sequencer.rangeSelection.cursorStep.set(step);
    state_.sequencer.focusedStep.set(step);
    state_.sequencer.page.set(state_.sequencer.pageForStep(step));
}

FLASHMEM uint8_t SequencerRangeActionHandler::initialCursorStep() const {
    const uint8_t len = state_.sequencer.length.get();
    if (len == 0) return 0;

    const uint8_t focused = state_.sequencer.focusedStep.get();
    if (focused < len) return focused;
    return state_.sequencer.pageStartStep(state_.sequencer.page.get());
}

FLASHMEM uint8_t SequencerRangeActionHandler::currentPageStart() const {
    return state_.sequencer.pageStartStepClamped(state_.sequencer.visiblePage());
}

FLASHMEM uint8_t SequencerRangeActionHandler::currentPageEnd() const {
    const uint8_t len = state_.sequencer.length.get();
    if (len == 0) return 0;

    const uint8_t start = currentPageStart();
    return static_cast<uint8_t>(std::min<uint16_t>(
        static_cast<uint16_t>(len - 1),
        static_cast<uint16_t>(start + core::state::sequencer::SequencerState::STEPS_PER_PAGE - 1)
    ));
}

FLASHMEM uint8_t SequencerRangeActionHandler::maxCursorStep() const {
    const uint8_t len = state_.sequencer.length.get();
    if (len == 0) return 0;

    const auto& range = state_.sequencer.rangeSelection;
    if (range.selectingSourceRange()) {
        return static_cast<uint8_t>(len - 1);
    }
    if (range.selectingPasteTarget() && range.clipboard.count > 0) {
        return static_cast<uint8_t>(core::state::sequencer::SequencerState::MAX_STEPS -
                                    range.clipboard.count);
    }

    return static_cast<uint8_t>(len - 1);
}

FLASHMEM void SequencerRangeActionHandler::snapshotCurrentFocus() {
    auto& range = state_.sequencer.rangeSelection;
    range.snapshotPage = state_.sequencer.visiblePage();
    range.snapshotFocusedStep = state_.sequencer.focusedStep.get();
}

FLASHMEM void SequencerRangeActionHandler::restoreSnapshotFocus() {
    const auto& range = state_.sequencer.rangeSelection;
    state_.sequencer.page.set(range.snapshotPage);
    state_.sequencer.focusedStep.set(range.snapshotFocusedStep);
}
}  // namespace core::handler
