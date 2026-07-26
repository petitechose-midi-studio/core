#pragma once

#include <cstdint>

#include "handler/common/PressHoldTurnReleaseGesture.hpp"
#include "state/StructureNavigationState.hpp"
#include "state/sequencer/SequencerUiState.hpp"

namespace core::handler {

enum class SequencerContextSelectorAction : uint8_t {
    NONE = 0,
    APPLY_CONTEXT,
    OPEN_TRACK_EDITOR,
    OPEN_PATTERN_EDITOR,
    OPEN_STEP_EDITOR,
    EDITOR_UNAVAILABLE,
};

struct SequencerContextSelectorOutcome {
    SequencerContextSelectorAction action = SequencerContextSelectorAction::NONE;
    core::state::StructureNavigationFocus focus =
        core::state::StructureNavigationFocus::PAGE;
};

/**
 * Allocation-free NAV gesture state machine for Sequencer contexts.
 *
 * Press reveals the current context, rotation previews Track/Pattern/Step at
 * root or Pattern/Step inside child content, and a long-press without rotation
 * transfers ownership to context-local selection.
 */
class SequencerContextSelectorWorkflow {
public:
    static constexpr uint32_t UNAVAILABLE_FEEDBACK_MS = 900U;

    explicit SequencerContextSelectorWorkflow(
        core::state::sequencer::SequencerContextSelectorState& state
    );

    void press(core::state::StructureNavigationFocus current,
               bool includeTrack = true);
    /**
     * Claims an unrotated hold for context-local selection.
     *
     * On success the selector is closed and this gesture becomes inactive, so
     * the caller can enter selection immediately and consume the paired NAV
     * release. A meaningful rotation permanently keeps selector ownership.
     */
    bool holdForSelection();
    bool turn(float delta);
    SequencerContextSelectorOutcome release(uint32_t nowMs);
    void update(uint32_t nowMs);
    void cancel();

    [[nodiscard]] bool active() const { return gesture_.active(); }
    [[nodiscard]] bool rotated() const { return gesture_.turned(); }

private:
    static core::state::StructureNavigationFocus adjacent(
        core::state::StructureNavigationFocus current,
        int direction,
        bool includeTrack
    );

    core::state::sequencer::SequencerContextSelectorState& state_;
    PressHoldTurnReleaseGesture gesture_{};
    bool include_track_ = true;
};

}  // namespace core::handler
