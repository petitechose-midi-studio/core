#pragma once

#include <oc/state/SignalWatcher.hpp>

#include "state/sequencer/SequencerState.hpp"
#include "state/sequencer/SequencerTrackBankState.hpp"
#include "state/StructureClipboardState.hpp"
#include "ui/strip/ContextActionStrip.hpp"

namespace ms::ui {
class VirtualListKeyValueOverlay;
}

namespace core::ui {
class SequencerStepEditOverlay;
}

namespace core::context::standalone {

/**
 * Projects sequencer step-edit state into the step edit overlay.
 *
 * The presenter formats current step values and watches step-edit signals; it
 * does not apply edits or manage input bindings.
 */
class SequencerOverlayPresenter {
public:
    struct StateRefs {
        core::state::sequencer::SequencerState& sequencer;
        core::state::sequencer::SequencerTrackBankState& tracks;
        core::state::StructureClipboardState& structureClipboard;
    };

    SequencerOverlayPresenter(StateRefs stateRefs,
                              core::ui::SequencerStepEditOverlay& stepEditOverlay,
                              core::ui::ContextActionStrip& stepEditActionStrip);

    void bind();
    void renderStepEdit();
    void renderStepEditActionStrip();

private:
    StateRefs state_refs_;
    core::ui::SequencerStepEditOverlay& step_edit_overlay_;
    core::ui::ContextActionStrip& step_edit_action_strip_;
    oc::state::SignalWatcher step_edit_watcher_;
    oc::state::SignalWatcher step_edit_action_watcher_;
};

}  // namespace core::context::standalone
