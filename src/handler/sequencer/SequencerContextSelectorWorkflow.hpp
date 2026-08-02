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
};

struct SequencerContextSelectorOutcome {
    SequencerContextSelectorAction action = SequencerContextSelectorAction::NONE;
    core::state::StructureNavigationFocus focus =
        core::state::StructureNavigationFocus::PAGE;
    uint8_t previewTarget = 0U;
    bool previewAddSlot = false;
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
    explicit SequencerContextSelectorWorkflow(
        core::state::sequencer::SequencerContextSelectorState& state
    );

    void press(core::state::StructureNavigationFocus current,
               bool includeTrack = true,
               uint8_t previewTarget = 0U,
               bool previewAddSlot = false);
    /**
     * Claims an unrotated hold for context-local selection.
     *
     * On success the selector is closed and this gesture becomes inactive, so
     * the caller can enter selection immediately and consume the paired NAV
     * release. A meaningful rotation permanently keeps selector ownership.
     */
    bool holdForSelection(core::state::StructureNavigationFocus current,
                          uint8_t previewTarget,
                          bool previewAddSlot);
    bool turn(float delta);
    SequencerContextSelectorOutcome release();
    void update();
    void cancel();

    [[nodiscard]] bool active() const { return gesture_.active(); }

private:
    static core::state::StructureNavigationFocus adjacent(
        core::state::StructureNavigationFocus current,
        int direction,
        bool includeTrack
    );

    core::state::sequencer::SequencerContextSelectorState& state_;
    PressHoldTurnReleaseGesture gesture_{};
    // Two compact bytes preserve exact press provenance without growing the
    // ARM workflow: origin focus[0..1], add intent[2], Track availability[3],
    // plus the complete Track/Page/Step target.
    uint8_t press_context_ = 0U;
    uint8_t press_target_ = 0U;
};

static_assert(
    sizeof(void*) != 4U || sizeof(SequencerContextSelectorWorkflow) == 8U,
    "Sequencer context selector exceeds its ARM RAM contract"
);

}  // namespace core::handler
