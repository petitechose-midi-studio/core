#include "SequencerRangeActionHandler.hpp"

#include <algorithm>

#include <config/App.hpp>
#include <config/PlatformCompat.hpp>
#include <config/InputIDs.hpp>

#include "handler/common/NavigationUtils.hpp"
#include "state/sequencer/SequencerSnapshotOps.hpp"
#include "SequencerInputUtils.hpp"

namespace core::handler {

namespace {

using RangeSelectionKind = core::state::sequencer::RangeSelectionKind;
using RangeSelectionPhase = core::state::sequencer::RangeSelectionPhase;

inline bool isSequencerIdleForRangeActions(
    oc::state::ExclusiveVisibilityStack<core::ui::OverlayType>& overlays,
    core::state::sequencer::SequencerState& sequencer,
    core::state::sequencer::SequencerTrackBankState& tracks
) {
    return !overlays.hasVisible() &&
           !tracks.selector.selecting.get() &&
           !sequencer.stepPropertyInlineSelector.selecting.get() &&
           !sequencer.patternQuickControls.selecting.get() &&
           !sequencer.rangeSelection.active();
}

inline oc::type::IsActiveFn idleRangePredicate(
    oc::state::ExclusiveVisibilityStack<core::ui::OverlayType>& overlays,
    core::state::sequencer::SequencerState& sequencer,
    core::state::sequencer::SequencerTrackBankState& tracks
) {
    return [&overlays, &sequencer, &tracks]() {
        return isSequencerIdleForRangeActions(overlays, sequencer, tracks);
    };
}

inline oc::type::IsActiveFn activeRangePredicate(core::state::sequencer::SequencerState& sequencer) {
    return [&sequencer]() { return sequencer.rangeSelection.active(); };
}

}  // namespace

FLASHMEM SequencerRangeActionHandler::SequencerRangeActionHandler(StateRefs state,
                                                                  oc::api::EncoderAPI& encoders,
                                                                  oc::api::ButtonAPI& buttons,
                                                                  oc::type::ScopeID scopeId)
    : overlays_(state.overlays)
    , sequencer_(state.sequencer)
    , tracks_(state.tracks)
    , encoders_(encoders)
    , buttons_(buttons)
    , scope_id_(scopeId) {
    setupBindings();
}

FLASHMEM void SequencerRangeActionHandler::setupBindings() {
    buttons_.button(Config::ButtonID::BOTTOM_LEFT)
        .release()
        .scope(scope_id_)
        .when(idleRangePredicate(overlays_, sequencer_, tracks_))
        .then([this]() {
            if (ignore_next_bottom_left_release_) {
                ignore_next_bottom_left_release_ = false;
                return;
            }
            clearCurrentPage();
        });

    buttons_.button(Config::ButtonID::BOTTOM_LEFT)
        .longPress(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS)
        .scope(scope_id_)
        .when(idleRangePredicate(overlays_, sequencer_, tracks_))
        .then([this]() { openClearRange(); });

    buttons_.button(Config::ButtonID::BOTTOM_RIGHT)
        .release()
        .scope(scope_id_)
        .when(idleRangePredicate(overlays_, sequencer_, tracks_))
        .then([this]() {
            if (ignore_next_bottom_right_release_) {
                ignore_next_bottom_right_release_ = false;
                return;
            }
            core::state::sequencer::duplicatePatternForward(sequencer_);
        });

    buttons_.button(Config::ButtonID::BOTTOM_RIGHT)
        .longPress(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS)
        .scope(scope_id_)
        .when(idleRangePredicate(overlays_, sequencer_, tracks_))
        .then([this]() { openCopyRange(); });

    encoders_.encoder(Config::EncoderID::NAV)
        .turn()
        .scope(scope_id_)
        .when(activeRangePredicate(sequencer_))
        .then([this](float delta) { moveCursor(delta); });

    encoders_.encoder(Config::EncoderID::OPT)
        .turn()
        .scope(scope_id_)
        .when(activeRangePredicate(sequencer_))
        .then([this](float normalized) { moveRange(normalized); });

    buttons_.button(Config::ButtonID::NAV)
        .release()
        .scope(scope_id_)
        .when(activeRangePredicate(sequencer_))
        .then([this]() { commitCursor(); });

    buttons_.button(Config::ButtonID::LEFT_TOP)
        .press()
        .latch()
        .scope(scope_id_)
        .when(activeRangePredicate(sequencer_))
        .then([]() {});

    buttons_.button(Config::ButtonID::LEFT_TOP)
        .release()
        .scope(scope_id_)
        .when(activeRangePredicate(sequencer_))
        .then([this]() { cancel(); });

    buttons_.button(Config::ButtonID::BOTTOM_RIGHT)
        .release()
        .scope(scope_id_)
        .then([this]() {
            if (sequencer_.rangeSelection.selectingPasteTarget()) {
                applyPaste();
            }
        });
}

FLASHMEM void SequencerRangeActionHandler::clearCurrentPage() {
    const uint8_t len = sequencer_.length.get();
    if (len == 0) return;

    const uint8_t start = currentPageStart();
    const uint8_t end = currentPageEnd();
    core::state::sequencer::clearStepRange(sequencer_, start, end);
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
    sequencer_.rangeSelection.reset();
}

FLASHMEM void SequencerRangeActionHandler::moveCursor(float delta) {
    if (!nav::hasTurnDelta(delta)) return;

    auto& range = sequencer_.rangeSelection;
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
    auto& range = sequencer_.rangeSelection;
    if (!range.selectingSourceRange()) return;

    const uint8_t maxSpan = maxRangeSpan();
    const uint8_t span = static_cast<uint8_t>(
        sequencer::input_utils::normalizedToIndex(normalized, static_cast<int>(maxSpan) + 1)
    );
    setSelectedRange(range.anchorStep.get(), span);
}

FLASHMEM void SequencerRangeActionHandler::commitCursor() {
    auto& range = sequencer_.rangeSelection;

    switch (range.phase.get()) {
        case RangeSelectionPhase::SELECT_RANGE: {
            const uint8_t start = range.rangeStart.get();
            const uint8_t end = range.rangeEnd.get();
            if (range.kind.get() == RangeSelectionKind::CLEAR) {
                core::state::sequencer::clearStepRange(sequencer_, start, end);
                range.reset();
                return;
            }

            if (!core::state::sequencer::copyStepRangeToClipboard(
                    sequencer_,
                    start,
                    end,
                    range.clipboard
                )) {
                cancel();
                return;
            }

            range.phase.set(RangeSelectionPhase::PASTE_TARGET);
            setCursorStep(static_cast<uint8_t>(std::min<uint16_t>(
                sequencer_.length.get(),
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
    auto& range = sequencer_.rangeSelection;
    if (!range.selectingPasteTarget()) return;

    core::state::sequencer::pasteClipboardRange(
        sequencer_,
        range.cursorStep.get(),
        range.clipboard
    );
    range.reset();
}

FLASHMEM void SequencerRangeActionHandler::beginRangeSelection(RangeSelectionKind kind) {
    const uint8_t len = sequencer_.length.get();
    if (len == 0) return;

    auto& range = sequencer_.rangeSelection;
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
    auto& range = sequencer_.rangeSelection;
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
    auto& range = sequencer_.rangeSelection;
    const uint8_t clampedSpan = static_cast<uint8_t>(std::min<uint16_t>(span, maxRangeSpan()));
    range.rangeStart.set(start);
    range.rangeEnd.set(static_cast<uint8_t>(start + clampedSpan));
    range.rangeValid.set(true);
}

FLASHMEM uint8_t SequencerRangeActionHandler::currentRangeSpan() const {
    const auto& range = sequencer_.rangeSelection;
    const uint8_t start = range.rangeStart.get();
    const uint8_t end = range.rangeEnd.get();
    if (end <= start) return 0;
    return static_cast<uint8_t>(end - start);
}

FLASHMEM uint8_t SequencerRangeActionHandler::maxRangeSpan() const {
    const auto& range = sequencer_.rangeSelection;
    const uint8_t start = range.anchorStep.get();
    const uint8_t maxStep = static_cast<uint8_t>(sequencer_.length.get() - 1);
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
    sequencer_.rangeSelection.cursorStep.set(step);
    sequencer_.focusedStep.set(step);
    sequencer_.page.set(sequencer_.pageForStep(step));
}

FLASHMEM uint8_t SequencerRangeActionHandler::initialCursorStep() const {
    const uint8_t len = sequencer_.length.get();
    if (len == 0) return 0;

    const uint8_t focused = sequencer_.focusedStep.get();
    if (focused < len) return focused;
    return sequencer_.pageStartStep(sequencer_.page.get());
}

FLASHMEM uint8_t SequencerRangeActionHandler::currentPageStart() const {
    return sequencer_.pageStartStepClamped(sequencer_.visiblePage());
}

FLASHMEM uint8_t SequencerRangeActionHandler::currentPageEnd() const {
    const uint8_t len = sequencer_.length.get();
    if (len == 0) return 0;

    const uint8_t start = currentPageStart();
    return static_cast<uint8_t>(std::min<uint16_t>(
        static_cast<uint16_t>(len - 1),
        static_cast<uint16_t>(start + core::state::sequencer::SequencerState::STEPS_PER_PAGE - 1)
    ));
}

FLASHMEM uint8_t SequencerRangeActionHandler::maxCursorStep() const {
    const uint8_t len = sequencer_.length.get();
    if (len == 0) return 0;

    const auto& range = sequencer_.rangeSelection;
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
    auto& range = sequencer_.rangeSelection;
    range.snapshotPage = sequencer_.visiblePage();
    range.snapshotFocusedStep = sequencer_.focusedStep.get();
}

FLASHMEM void SequencerRangeActionHandler::restoreSnapshotFocus() {
    const auto& range = sequencer_.rangeSelection;
    sequencer_.page.set(range.snapshotPage);
    sequencer_.focusedStep.set(range.snapshotFocusedStep);
}
}  // namespace core::handler
