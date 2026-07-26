#pragma once

#include <oc/state/StaticSignalWatcher.hpp>

#include "state/SequencerSettingsState.hpp"
#include "state/sequencer/SequencerTrackBankState.hpp"
#include "ui/common/CoalescedLvglRenderScheduler.hpp"

namespace ms::ui {
class VirtualListKeyValueOverlay;
class VirtualListSelectorOverlay;
}

namespace core::context::standalone {

class SequencerSettingsOverlayPresenter {
public:
    struct StateRefs {
        core::state::SequencerSettingsState& sequencerSettings;
        core::state::sequencer::SequencerTrackBankState& trackBank;
    };

    SequencerSettingsOverlayPresenter(StateRefs stateRefs,
                                      ms::ui::VirtualListKeyValueOverlay& overlay,
                                      ms::ui::VirtualListSelectorOverlay& selectorOverlay);
    ~SequencerSettingsOverlayPresenter();

    [[nodiscard]] bool bind();

private:
    static constexpr uint32_t RENDER_OVERLAY = 1U << 0;
    static constexpr uint32_t RENDER_SELECTOR = 1U << 1;

    static void drainRenderQueue(void* context, uint32_t flags);
    void requestOverlayRender();
    void requestSelectorRender();
    void renderPending(uint32_t flags);
    void renderOverlay();
    void renderSelector();

    StateRefs state_refs_;
    ms::ui::VirtualListKeyValueOverlay& overlay_;
    ms::ui::VirtualListSelectorOverlay& selector_overlay_;
    core::ui::CoalescedLvglRenderScheduler render_scheduler_;
    oc::state::StaticWatchGroup<3> overlay_watcher_;
    oc::state::StaticWatchGroup<4> selector_watcher_;
};

}  // namespace core::context::standalone
