#include "context/standalone/ProjectTrackEditorPresenter.hpp"

#include <cstdio>

#include <config/PlatformCompat.hpp>

#include "ui/project/ProjectTrackEditorViewModel.hpp"
#include "ui/theme/StandaloneTheme.hpp"

namespace core::context::standalone {

namespace theme = ::standalone::theme;

FLASHMEM ProjectTrackEditorPresenter::ProjectTrackEditorPresenter(
    StateRefs state,
    core::ui::project::ProjectTrackEditorOverlay& overlay,
    core::ui::ContextActionStrip& actionStrip
)
    : state_(state)
    , overlay_(overlay)
    , action_strip_(actionStrip)
    , render_scheduler_(
          core::ui::renderSchedulerDebugLabel("TrackEditor"),
          &ProjectTrackEditorPresenter::drainRender,
          this
      ) {}

FLASHMEM bool ProjectTrackEditorPresenter::bind() {
    if (!render_scheduler_.valid()) return false;
    requestRender();
    return true;
}

void ProjectTrackEditorPresenter::requestRender() {
    render_scheduler_.request(RENDER);
}

void ProjectTrackEditorPresenter::update() {
    const uint32_t tracksRevision = state_.tracks.revision.get();
    const uint16_t enabledMask = state_.enabledMask.get();
    const uint8_t activeTrack = state_.activeTrack.get();
    if (observed_editor_revision_ == state_.editor.revision &&
        observed_tracks_revision_ == tracksRevision &&
        observed_enabled_mask_ == enabledMask &&
        observed_active_track_ == activeTrack) {
        return;
    }
    observed_editor_revision_ = state_.editor.revision;
    observed_tracks_revision_ = tracksRevision;
    observed_enabled_mask_ = enabledMask;
    observed_active_track_ = activeTrack;
    requestRender();
}

void ProjectTrackEditorPresenter::drainRender(
    void* context,
    uint32_t flags
) {
    auto* self = static_cast<ProjectTrackEditorPresenter*>(context);
    if (self && (flags & RENDER) != 0U) self->render();
}

FLASHMEM void ProjectTrackEditorPresenter::render() {
    observed_editor_revision_ = state_.editor.revision;
    observed_tracks_revision_ = state_.tracks.revision.get();
    observed_enabled_mask_ = state_.enabledMask.get();
    observed_active_track_ = state_.activeTrack.get();
    const auto viewModel = core::ui::project::buildProjectTrackEditorViewModel(
        state_.editor,
        state_.tracks,
        state_.enabledMask.get()
    );
    if (!viewModel.visible) {
        overlay_.render({.visible = false});
        action_strip_.render({.visible = false});
        return;
    }

    std::snprintf(
        route_.data(),
        route_.size(),
        "%s \xC2\xB7 CH %u",
        viewModel.port.data(),
        static_cast<unsigned>(viewModel.midiChannel)
    );
    if (viewModel.delayMs == 0) {
        std::snprintf(delay_.data(), delay_.size(), "0 ms");
    } else {
        std::snprintf(
            delay_.data(),
            delay_.size(),
            "%+d ms",
            static_cast<int>(viewModel.delayMs)
        );
    }

    overlay_.render({
        .visible = true,
        .title = viewModel.title.data(),
        .route = route_.data(),
        .delay = delay_.data(),
        .structureHint = "OPEN",
        .trackColor = theme::color::trackColor(viewModel.trackIndex),
        .selectedProperty = viewModel.selectedProperty,
        .muted = viewModel.muted,
        .soloed = viewModel.soloed,
        .trackEnabled = viewModel.trackEnabled,
    });

    core::ui::ContextActionStripProps actions{.visible = true};
    actions.slots[0] = {
        .visualState = viewModel.trackEnabled
            ? (viewModel.muted
                ? core::ui::ContextActionStripVisualState::ARMED
                : core::ui::ContextActionStripVisualState::AVAILABLE)
            : core::ui::ContextActionStripVisualState::DISABLED,
        .tone = core::ui::ContextActionStripTone::WARNING,
        .showLabel = true,
        .label = "MUTE",
    };
    actions.slots[1] = {
        .visualState = viewModel.trackEnabled
            ? (viewModel.soloed
                ? core::ui::ContextActionStripVisualState::ARMED
                : core::ui::ContextActionStripVisualState::AVAILABLE)
            : core::ui::ContextActionStripVisualState::DISABLED,
        .tone = core::ui::ContextActionStripTone::POSITIVE,
        .showLabel = true,
        .label = "SOLO",
    };
    actions.slots[2] = {
        .visualState = viewModel.trackEnabled
            ? core::ui::ContextActionStripVisualState::AVAILABLE
            : core::ui::ContextActionStripVisualState::DISABLED,
        .tone = core::ui::ContextActionStripTone::NEUTRAL,
        .showLabel = true,
        .label = "STRUCT",
    };
    action_strip_.render(actions);
}

}  // namespace core::context::standalone
