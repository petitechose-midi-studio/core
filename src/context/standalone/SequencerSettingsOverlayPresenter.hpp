#pragma once

#include <oc/state/SignalWatcher.hpp>

#include "state/SequencerSettingsState.hpp"

namespace ms::ui {
class VirtualListKeyValueOverlay;
}

namespace core::context::standalone {

class SequencerSettingsOverlayPresenter {
public:
    struct StateRefs {
        core::state::SequencerSettingsState& sequencerSettings;
    };

    SequencerSettingsOverlayPresenter(StateRefs stateRefs,
                                      ms::ui::VirtualListKeyValueOverlay& overlay);

    void bind();
    void renderOverlay();

private:
    StateRefs state_refs_;
    ms::ui::VirtualListKeyValueOverlay& overlay_;
    oc::state::SignalWatcher overlay_watcher_;
};

}  // namespace core::context::standalone
