#pragma once


#include <array>
#include <cstdint>

#include "state/sequencer/SequencerState.hpp"
#include "ui/common/CoalescedLvglRenderScheduler.hpp"
#include "ui/sequencer/SequencerStepEditOverlay.hpp"
#include "ui/interaction/TextKeyboardView.hpp"
#include "ui/strip/ContextActionStrip.hpp"

namespace core::context::standalone {

/** Projects Drum lane state into the shared sequencer editor surface. */
class DrumLaneEditorPresenter {
public:
    DrumLaneEditorPresenter(
        core::state::sequencer::SequencerState& sequencer,
        core::ui::SequencerStepEditOverlay& overlay,
        core::ui::interaction::TextKeyboardView& keyboard,
        core::ui::ContextActionStrip& actionStrip
    );

    [[nodiscard]] bool bind();
    void update();

private:
    static constexpr uint32_t RENDER = 1U;
    static constexpr size_t FIELD_COUNT = static_cast<size_t>(
        core::state::sequencer::DrumLaneEditorField::COUNT
    );
    static constexpr size_t VALUE_CAPACITY = 20U;

    static void drainRender(void* context, uint32_t flags);
    void requestRender();
    void render();

    core::state::sequencer::SequencerState& sequencer_;
    core::ui::SequencerStepEditOverlay& overlay_;
    core::ui::interaction::TextKeyboardView& keyboard_;
    core::ui::ContextActionStrip& action_strip_;
    core::ui::CoalescedLvglRenderScheduler render_scheduler_;
    std::array<std::array<char, VALUE_CAPACITY>, FIELD_COUNT> values_{};
    std::array<char, 8> badge_{};
    std::array<char, 24> title_{};
    uint32_t observed_revision_ = UINT32_MAX;
    bool observed_visible_ = false;
};

}  // namespace core::context::standalone
