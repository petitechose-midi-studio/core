#pragma once

#include <array>
#include <cstdint>

#include <oc/state/StaticSignalWatcher.hpp>

#include "state/StatusBarState.hpp"
#include "state/project/ProjectNavigationState.hpp"
#include "state/project/ProjectTrackState.hpp"
#include "state/sequencer/SequencerState.hpp"
#include "state/sequencer/SequencerTrackBankState.hpp"
#include "ui/common/CoalescedLvglRenderScheduler.hpp"
#include "ui/sequencer/SequencerCcLaneGrid.hpp"

namespace ms::ui {
class VirtualListKeyValueOverlay;
}

namespace core::ui {
class ContextActionStrip;
}

namespace core::handler {
class MidiCcGlobalFrameCoordinator;
}

namespace core::context::standalone {

/**
 * Projects the route-aware CC-lane workflow onto one retained overlay.
 *
 * The presenter owns all temporary text buffers. No LVGL callback reconstructs
 * domain meaning and no render path allocates.
 */
class SequencerCcLaneOverlayPresenter {
public:
    struct StateRefs {
        core::state::sequencer::SequencerState& sequencer;
        core::state::sequencer::SequencerTrackBankState& tracks;
        const core::state::project::ProjectNavigationState& projectNavigation;
        core::state::project::ProjectTrackState& projectTracks;
        core::state::StatusBarState& statusBar;
        const core::handler::MidiCcGlobalFrameCoordinator* midiCcCoordinator = nullptr;
    };

    SequencerCcLaneOverlayPresenter(
        StateRefs state,
        ms::ui::VirtualListKeyValueOverlay& overlay,
        core::ui::ContextActionStrip& actionStrip
    );
    ~SequencerCcLaneOverlayPresenter();

    [[nodiscard]] bool bind();

private:
    static constexpr size_t ROW_CAPACITY = 16;
    static constexpr size_t TEXT_CAPACITY = 48;

    static void drainRenderQueue(void* context, uint32_t flags);
    void requestRender();
    void render();
    void renderOverlay();
    void renderActionStrip();

    using Text = std::array<char, TEXT_CAPACITY>;

    StateRefs state_;
    ms::ui::VirtualListKeyValueOverlay& overlay_;
    core::ui::ContextActionStrip& action_strip_;
    core::ui::SequencerCcLaneGrid grid_;
    core::ui::CoalescedLvglRenderScheduler render_scheduler_;
    oc::state::StaticWatchGroup<6> watcher_;
    std::array<Text, ROW_CAPACITY> keys_{};
    std::array<Text, ROW_CAPACITY> values_{};
    std::array<char, TEXT_CAPACITY> title_{};
    std::array<char, TEXT_CAPACITY> meta_{};
    std::array<char, 64> hint_{};
};

}  // namespace core::context::standalone
