#pragma once

#include <array>
#include <cstdint>

#include <oc/state/StaticSignalWatcher.hpp>

#include "app/ExtmemAllocator.hpp"
#include "state/sequencer/SequencerState.hpp"
#include "state/sequencer/SequencerPatternRandomizeSession.hpp"
#include "state/sequencer/SequencerTrackBankState.hpp"
#include "ui/common/CoalescedLvglRenderScheduler.hpp"
#include "ui/sequencer/SequencerPatternEditorOverlay.hpp"
#include "ui/strip/ContextActionStrip.hpp"

namespace core::context::standalone {

/** Projects Pattern domain facts into one retained timeline surface. */
class SequencerPatternEditorPresenter final {
public:
    struct StateRefs {
        core::state::sequencer::SequencerState& sequencer;
        core::state::sequencer::SequencerTrackBankState& tracks;
        core::state::sequencer::SequencerPatternRandomizeSession& randomize;
    };

    SequencerPatternEditorPresenter(
        StateRefs state,
        core::ui::SequencerPatternEditorOverlay& overlay,
        core::ui::ContextActionStrip& actionStrip
    );
    ~SequencerPatternEditorPresenter();

    [[nodiscard]] bool bind();

private:
    static constexpr uint32_t RENDER_STATIC = 1U << 0U;
    static constexpr uint32_t RENDER_PLAYHEAD = 1U << 1U;

    static void drainRender(void* context, uint32_t flags);
    void requestStaticRender();
    void requestPlayheadRender();
    void renderPending(uint32_t flags);
    void renderStatic();
    void renderPlayhead();
    bool ensureGeometry();
    core::ui::sequencer::SequencerPatternTimelinePlayhead projectPlayhead() const;

    StateRefs state_;
    core::ui::SequencerPatternEditorOverlay& overlay_;
    core::ui::ContextActionStrip& action_strip_;
    core::app::ExtmemUniquePtr<
        core::ui::sequencer::SequencerPatternTimelineGeometry> geometry_;
    core::ui::CoalescedLvglRenderScheduler render_scheduler_;
    oc::state::StaticWatchGroup<6> static_watcher_;
    oc::state::StaticWatchGroup<2> playhead_watcher_;

    std::array<char, 40> title_{};
    std::array<char, 32> meta_{};
    std::array<char, 24> layer_{};
    std::array<char, 64> hint_{};
    std::array<
        std::array<char, 12>,
        core::ui::SequencerPatternEditorOverlayProps::FIELD_COUNT>
        field_values_{};
    uint32_t geometry_revision_ = 0U;
};

}  // namespace core::context::standalone
