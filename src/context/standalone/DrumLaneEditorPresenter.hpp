#pragma once


#include <array>
#include <cstdint>
#include <oc/ui/lvgl/PausableTimer.hpp>

#include "state/sequencer/SequencerState.hpp"
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

private:
    static constexpr size_t FIELD_COUNT = static_cast<size_t>(
        core::state::sequencer::DrumLaneEditorField::COUNT
    );
    static constexpr size_t VALUE_CAPACITY = 20U;

    static void onFrame(lv_timer_t* timer);
    void render();

    core::state::sequencer::SequencerState& sequencer_;
    core::ui::SequencerStepEditOverlay& overlay_;
    core::ui::interaction::TextKeyboardView& keyboard_;
    core::ui::ContextActionStrip& action_strip_;
    oc::ui::lvgl::PausableTimer frame_timer_;
    std::array<std::array<char, VALUE_CAPACITY>, FIELD_COUNT> values_{};
    std::array<char, 8> badge_{};
    std::array<char, 24> title_{};
    uint32_t observed_revision_ = UINT32_MAX;
    bool observed_visible_ = false;
};

}  // namespace core::context::standalone
