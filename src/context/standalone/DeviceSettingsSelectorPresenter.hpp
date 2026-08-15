#pragma once

#include <oc/state/StaticSignalWatcher.hpp>

#include "state/DeviceSettingsState.hpp"
#include "state/MidiNoteDisplayState.hpp"
#include "state/MidiSyncState.hpp"
#include "ui/common/CoalescedLvglRenderScheduler.hpp"

namespace ms::ui {
class VirtualListSelectorOverlay;
}

namespace core::context::standalone {

class DeviceSettingsSelectorPresenter {
public:
    struct StateRefs {
        core::state::DeviceSettingsState& settings;
        core::state::MidiSyncState& midiSync;
        core::state::MidiNoteDisplayState& midiNoteDisplay;
    };

    DeviceSettingsSelectorPresenter(StateRefs stateRefs,
                                    ms::ui::VirtualListSelectorOverlay& selectorOverlay);
    ~DeviceSettingsSelectorPresenter();

    [[nodiscard]] bool bind();

private:
    static constexpr uint32_t RENDER_SELECTOR = 1U;

    static void drainRenderQueue(void* context, uint32_t flags);
    void requestSelectorRender();
    void renderSelector();

    StateRefs state_refs_;
    ms::ui::VirtualListSelectorOverlay& selector_overlay_;
    core::ui::CoalescedLvglRenderScheduler render_scheduler_;
    oc::state::StaticWatchGroup<8> selector_watcher_;
};

}  // namespace core::context::standalone
