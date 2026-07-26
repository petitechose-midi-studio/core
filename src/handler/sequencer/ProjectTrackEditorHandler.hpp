#pragma once

#include <cstdint>

#include <oc/api/ButtonAPI.hpp>
#include <oc/api/EncoderAPI.hpp>
#include <oc/context/OverlayManager.hpp>

#include "app/OverlayTypes.hpp"
#include "handler/common/SharedTrackDomainServices.hpp"
#include "state/project/ProjectTrackDomainServices.hpp"
#include "state/project/ProjectTrackEditorState.hpp"
#include "state/project/ProjectTrackState.hpp"

namespace core::handler {

/**
 * Allocation-free input owner for the retained Track Editor.
 *
 * LEFT_CENTER + NAV switches enabled Tracks, NAV selects the scalar property,
 * OPT edits it, and the bottom actions expose Mute/Solo. All authored writes
 * cross ProjectTrackDomainServices.
 */
class ProjectTrackEditorHandler {
public:
    static constexpr uint32_t GESTURE_IDLE_COMMIT_MS = 250U;

    struct StateRefs {
        core::state::project::ProjectTrackEditorState& editor;
        core::state::project::ProjectTrackState& tracks;
        SharedTrackDomainServices sharedTracks;
        core::state::project::ProjectTrackDomainServices trackDomain;
    };

    ProjectTrackEditorHandler(
        StateRefs state,
        oc::context::OverlayManager<core::ui::OverlayType>& overlays,
        oc::api::EncoderAPI& encoders,
        oc::api::ButtonAPI& buttons,
        oc::type::ScopeID sequencerViewScope,
        oc::type::ScopeID overlayScope
    );

    ProjectTrackEditorHandler(const ProjectTrackEditorHandler&) = delete;
    ProjectTrackEditorHandler& operator=(const ProjectTrackEditorHandler&) = delete;

    bool openActiveTrack();
    void close();
    void update(uint32_t nowMs);

private:
    void setupBindings();
    void moveTrack(float delta);
    void moveProperty(float delta);
    void setFocusedValue(float normalized);
    void toggleMute();
    void toggleSolo();
    void configureOpt();
    void commitPendingGesture();
    void cancelPendingGesture();
    [[nodiscard]] bool ownsActiveTrack() const;

    core::state::project::ProjectTrackEditorState& editor_;
    core::state::project::ProjectTrackState& tracks_;
    SharedTrackDomainServices shared_tracks_;
    core::state::project::ProjectTrackDomainServices track_domain_;
    oc::context::OverlayManager<core::ui::OverlayType>& overlays_;
    oc::api::EncoderAPI& encoders_;
    oc::api::ButtonAPI& buttons_;
    oc::type::ScopeID sequencer_view_scope_ = 0;
    oc::type::ScopeID overlay_scope_ = 0;
    uint32_t gesture_commit_deadline_ms_ = 0U;
};

}  // namespace core::handler
