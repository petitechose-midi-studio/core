#pragma once

#include <array>
#include <cstdint>

#include <oc/state/Signal.hpp>
#include <oc/ui/lvgl/PausableTimer.hpp>

#include "state/project/ProjectTrackEditorState.hpp"
#include "state/project/ProjectTrackState.hpp"
#include "ui/interaction/TextKeyboardView.hpp"
#include "ui/project/ProjectTrackEditorOverlay.hpp"
#include "ui/strip/ContextActionStrip.hpp"

namespace core::context::standalone {

/** Projects canonical Track state into the retained Track Editor surface. */
class ProjectTrackEditorPresenter final {
public:
    struct StateRefs {
        core::state::project::ProjectTrackEditorState& editor;
        core::state::project::ProjectTrackState& tracks;
        oc::state::Signal<uint16_t, 16>& enabledMask;
    };

    ProjectTrackEditorPresenter(
        StateRefs state,
        core::ui::project::ProjectTrackEditorOverlay& overlay,
        core::ui::interaction::TextKeyboardView& keyboard,
        core::ui::ContextActionStrip& actionStrip
    );

    /** Starts change detection at the UI frame cadence. */
    [[nodiscard]] bool bind();

private:
    static void onFrame(lv_timer_t* timer);
    void render();

    StateRefs state_;
    core::ui::project::ProjectTrackEditorOverlay& overlay_;
    core::ui::interaction::TextKeyboardView& keyboard_;
    core::ui::ContextActionStrip& action_strip_;
    oc::ui::lvgl::PausableTimer frame_timer_;
    std::array<char, 24> route_{};
    std::array<char, 16> delay_{};
    uint32_t observed_editor_revision_ = UINT32_MAX;
    uint32_t observed_tracks_revision_ = UINT32_MAX;
    uint16_t observed_enabled_mask_ = UINT16_MAX;
};

}  // namespace core::context::standalone
