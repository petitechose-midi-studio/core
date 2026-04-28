#pragma once

#include <oc/state/SignalWatcher.hpp>

#include "state/sequencer/SequencerState.hpp"

namespace ms::ui {
class VirtualListKeyValueOverlay;
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
    };

    SequencerOverlayPresenter(StateRefs stateRefs,
                              ms::ui::VirtualListKeyValueOverlay& stepEditOverlay);

    void bind();
    void renderStepEdit();

private:
    StateRefs state_refs_;
    ms::ui::VirtualListKeyValueOverlay& step_edit_overlay_;
    oc::state::SignalWatcher step_edit_watcher_;
};

}  // namespace core::context::standalone
