#pragma once

#include <array>
#include <cstdint>

#include <oc/state/Signal.hpp>

#include "state/project/ProjectTrackEditorState.hpp"
#include "state/project/ProjectTrackState.hpp"
#include "ui/common/CoalescedLvglRenderScheduler.hpp"
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
        oc::state::Signal<uint8_t, 8>& activeTrack;
    };

    ProjectTrackEditorPresenter(
        StateRefs state,
        core::ui::project::ProjectTrackEditorOverlay& overlay,
        core::ui::ContextActionStrip& actionStrip
    );

    /** Validates the render scheduler and schedules the first projection. */
    [[nodiscard]] bool bind();

    /** Explicit invalidation hook used after plain EditorState mutations. */
    void requestRender();

    /** Cheap poll for lifecycle code that cannot call requestRender directly. */
    void update();

private:
    static constexpr uint32_t RENDER = 1U;

    static void drainRender(void* context, uint32_t flags);
    void render();

    StateRefs state_;
    core::ui::project::ProjectTrackEditorOverlay& overlay_;
    core::ui::ContextActionStrip& action_strip_;
    core::ui::CoalescedLvglRenderScheduler render_scheduler_;
    std::array<char, 24> route_{};
    std::array<char, 16> delay_{};
    uint32_t observed_editor_revision_ = UINT32_MAX;
    uint32_t observed_tracks_revision_ = UINT32_MAX;
    uint16_t observed_enabled_mask_ = UINT16_MAX;
    uint8_t observed_active_track_ = UINT8_MAX;
};

}  // namespace core::context::standalone
