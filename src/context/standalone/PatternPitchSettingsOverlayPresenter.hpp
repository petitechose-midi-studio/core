#pragma once

#include <oc/state/StaticSignalWatcher.hpp>

#include "state/PatternPitchSettingsState.hpp"
#include "state/sequencer/SequencerState.hpp"
#include "state/sequencer/SequencerTrackBankState.hpp"
#include "ui/common/CoalescedLvglRenderScheduler.hpp"

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
    oc::state::StaticWatchGroup<4> overlay_watcher_;
    oc::state::StaticWatchGroup<4> selector_watcher_;
};

}  // namespace core::context::standalone
