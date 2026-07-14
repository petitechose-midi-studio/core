#include "ui/sequencer/SequencerCcLaneGridViewModelBuilder.hpp"

#include <algorithm>

#include <config/PlatformCompat.hpp>

#include "state/sequencer/SequencerCcLanePatternOps.hpp"
#include "ui/sequencer/SequencerCcLaneGridProjection.hpp"
#include "ui/theme/StandaloneTheme.hpp"

namespace core::ui::sequencer {

FLASHMEM SequencerCcLaneGridProps buildSequencerCcLaneGridProps(
    const SequencerViewModelSource& source
) {
    namespace seq = core::state::sequencer;
    const auto& ui = source.sequencer.ccLaneUi;
    if (ui.mode != seq::SequencerCcLaneUiMode::LANE_GRID) return {};

    const auto* bank = seq::sequencerCcLaneView(source.sequencer.pattern);
    if (bank == nullptr || ui.focusedLane >= bank->lanes.size()) return {};
    const auto& lane = bank->lanes[ui.focusedLane];
    if (!lane.occupied) return {};

    SequencerCcLaneGridProps props{
        .visible = true,
        .title = "",
        .meta = "",
        .hint = ui.transitionAppliedFeedback
            ? "Curve applied"
            : "Knobs value · hold+turn curve",
        .accentColor = ::standalone::theme::color::MACRO_CC_COLOR,
        .statusColor = ui.routeValid
            ? ::standalone::theme::color::MACRO_CC_COLOR
            : ::standalone::theme::color::MACRO_AUTOMATION_RECORDING,
    };
    const uint8_t length = std::max<uint8_t>(
        1U,
        source.sequencer.pattern.length.get()
    );
    const uint8_t start = static_cast<uint8_t>(
        (ui.focusedStep / seq::SequencerPatternState::STEPS_PER_PAGE) *
        seq::SequencerPatternState::STEPS_PER_PAGE
    );
    const int16_t playhead = source.sequencer.playheadStep.get();
    for (uint8_t cell = 0; cell < props.cells.size(); ++cell) {
        const uint8_t step = static_cast<uint8_t>(start + cell);
        const bool visible = step < length;
        const bool authored = visible && lane.activeMask.test(step);
        props.cells[cell] = {
            .visible = visible,
            .authored = authored,
            .focused = visible && step == ui.focusedStep,
            .playhead = visible && source.statusBar.playing.get() &&
                playhead >= 0 && step == static_cast<uint8_t>(playhead),
            .step = step,
            .value = authored ? lane.values[step] : lane.initialValue,
            .transition = authored
                ? seq::sequencerCcLaneTransition(lane, step)
                : seq::SequencerCcLaneTransition::HOLD,
        };
        if (cell + 1U < props.cells.size() && visible && step + 1U < length) {
            props.segments[cell] = projectSequencerCcLaneGridSegment(
                lane,
                step,
                length
            );
        }
    }
    if (!ui.transitionAppliedFeedback && ui.focusedStep < length &&
        lane.activeMask.test(ui.focusedStep)) {
        const auto span = sequencerCcLaneProjectionSpanAtStep(
            lane,
            ui.focusedStep,
            length
        );
        if (span.valid) {
            props.contextualHint = true;
            props.hintSourceStep = ui.focusedStep;
            props.hintTargetStep = span.target;
            props.hintTransition = seq::sequencerCcLaneTransition(
                lane,
                ui.focusedStep
            );
        }
    }
    return props;
}

}  // namespace core::ui::sequencer
