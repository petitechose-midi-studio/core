#pragma once

#include <oc/state/SignalWatcher.hpp>

#include "state/PatternPitchSettingsState.hpp"
#include "state/sequencer/SequencerState.hpp"
#include "state/sequencer/SequencerTrackBankState.hpp"

namespace ms::ui {
class VirtualListKeyValueOverlay;
class VirtualListSelectorOverlay;
}

namespace core::context::standalone {

class PatternPitchSettingsOverlayPresenter {
public:
    struct StateRefs {
        core::state::PatternPitchSettingsState& settings;
        core::state::sequencer::SequencerState& sequencer;
        core::state::sequencer::SequencerTrackBankState& trackBank;
    };

    PatternPitchSettingsOverlayPresenter(StateRefs stateRefs,
                                         ms::ui::VirtualListKeyValueOverlay& overlay,
                                         ms::ui::VirtualListSelectorOverlay& selectorOverlay);

    void bind();
    void renderOverlay();
    void renderSelector();

private:
    StateRefs state_refs_;
    ms::ui::VirtualListKeyValueOverlay& overlay_;
    ms::ui::VirtualListSelectorOverlay& selector_overlay_;
    oc::state::SignalWatcher overlay_watcher_;
    oc::state::SignalWatcher selector_watcher_;
};

}  // namespace core::context::standalone
