#pragma once

#include <oc/state/SignalWatcher.hpp>

#include "state/DeviceSettingsState.hpp"
#include "state/MidiSyncState.hpp"

namespace ms::ui {
class VirtualListSelectorOverlay;
}

namespace core::context::standalone {

class DeviceSettingsSelectorPresenter {
public:
    struct StateRefs {
        core::state::DeviceSettingsState& settings;
        core::state::MidiSyncState& midiSync;
    };

    DeviceSettingsSelectorPresenter(StateRefs stateRefs,
                                    ms::ui::VirtualListSelectorOverlay& selectorOverlay);

    void bind();

private:
    void renderSelector();

    StateRefs state_refs_;
    ms::ui::VirtualListSelectorOverlay& selector_overlay_;
    oc::state::SignalWatcher selector_watcher_;
};

}  // namespace core::context::standalone
