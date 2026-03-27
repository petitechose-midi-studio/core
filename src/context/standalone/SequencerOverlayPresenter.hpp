#pragma once

#include <oc/state/SignalWatcher.hpp>

#include "state/CoreState.hpp"

namespace ms::ui {
class VirtualListKeyValueOverlay;
}

namespace core::context::standalone {

class SequencerOverlayPresenter {
public:
    SequencerOverlayPresenter(core::state::CoreState& state,
                              ms::ui::VirtualListKeyValueOverlay& stepEditOverlay);

    void bind();
    void renderStepEdit();

private:
    core::state::CoreState& state_;
    ms::ui::VirtualListKeyValueOverlay& step_edit_overlay_;
    oc::state::SignalWatcher step_edit_watcher_;
};

}  // namespace core::context::standalone
