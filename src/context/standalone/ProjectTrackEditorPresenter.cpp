#include "context/standalone/ProjectTrackEditorPresenter.hpp"

#include <cstdio>

#include <config/PlatformCompat.hpp>
#include <config/Timing.hpp>

#include "ui/font/StandaloneIcons.hpp"
#include "ui/project/ProjectTrackEditorViewModel.hpp"
#include "ui/theme/StandaloneTheme.hpp"

namespace core::context::standalone {

namespace theme = ::standalone::theme;

FLASHMEM ProjectTrackEditorPresenter::ProjectTrackEditorPresenter(
    StateRefs state,
    core::ui::project::ProjectTrackEditorOverlay& overlay,
    core::ui::interaction::TextKeyboardView& keyboard,
    core::ui::ContextActionStrip& actionStrip
)
    : state_(state)
    , overlay_(overlay)
    , keyboard_(keyboard)
    , action_strip_(actionStrip)
    , frame_timer_(
          Config::Timing::UI_FRAME_PERIOD_MS,
          &ProjectTrackEditorPresenter::onFrame,
          this
      ) {}

FLASHMEM bool ProjectTrackEditorPresenter::bind() {
    if (!frame_timer_.valid()) return false;
    frame_timer_.resume(true);
    return true;
}

void ProjectTrackEditorPresenter::onFrame(lv_timer_t* timer) {
    auto& self = *static_cast<ProjectTrackEditorPresenter*>(lv_timer_get_user_data(timer));
    if (self.observed_editor_revision_ == self.state_.editor.revision &&
        self.observed_tracks_revision_ == self.state_.tracks.revision.get() &&
        self.observed_enabled_mask_ == self.state_.enabledMask.get()) {
        return;
    }
    self.render();
}

FLASHMEM void ProjectTrackEditorPresenter::render() {
    observed_editor_revision_ = state_.editor.revision;
    observed_tracks_revision_ = state_.tracks.revision.get();
    observed_enabled_mask_ = state_.enabledMask.get();
    const auto viewModel = core::ui::project::buildProjectTrackEditorViewModel(
        state_.editor,
        state_.tracks,
        state_.enabledMask.get()
    );
    if (!viewModel.visible) {
        keyboard_.setVisible(false);
        overlay_.setContentVisible(true);
        overlay_.render({.visible = false});
        action_strip_.render({.visible = false});
        return;
    }

    std::snprintf(
        route_.data(),
        route_.size(),
        "%s \xC2\xB7 Ch %u",
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
        .structureHint = viewModel.draftDrum ? "Drum" : "Instrument",
        .status = viewModel.typeChangeBlocked
            ? "Remove other clips"
            : viewModel.typeChangePending
                ? "Type \xC2\xB7 Edited"
            : (viewModel.selectedProperty ==
                    core::state::project::ProjectTrackEditorProperty::TYPE
                ? "Type \xC2\xB7 Ready"
                : "Direct"),
        .trackColor = theme::color::trackColor(viewModel.trackIndex),
        .statusColor = viewModel.typeChangeBlocked
            ? theme::color::DESTRUCTIVE
            : viewModel.typeChangePending
            ? theme::color::WARNING
            : theme::color::TEXT_SECONDARY,
        .selectedProperty = viewModel.selectedProperty,
        .trackEnabled = viewModel.trackEnabled,
        .drum = viewModel.draftDrum,
    });

    if (state_.editor.textEditing) {
        overlay_.setContentVisible(false);
        keyboard_.render({
            .visible = true,
            .title = "Track name",
            .meta = "",
            .name = state_.editor.nameDraft.data(),
            .selectedKey = state_.editor.textKeyIndex,
            .shiftActive = state_.editor.textShiftActive,
        });
        action_strip_.render(
            core::ui::interaction::TextKeyboardView::
                bottomActionStripProps(true, false)
        );
        return;
    }
    keyboard_.setVisible(false);
    overlay_.setContentVisible(true);

    core::ui::ContextActionStripProps actions{.visible = true};
    if (viewModel.typeChangePending) {
        actions.slots[0] = {
            .visualState = core::ui::ContextActionStripVisualState::AVAILABLE,
            .tone = core::ui::ContextActionStripTone::NEUTRAL,
            .showLabel = true,
            .label = "Cancel",
        };
    } else {
        actions.slots[0] = core::ui::makeStandaloneIconStripSlot(
            ::standalone::icons::TRACK_MUTE,
            viewModel.trackEnabled
                ? (viewModel.muted
                    ? core::ui::ContextActionStripVisualState::ARMED
                    : core::ui::ContextActionStripVisualState::AVAILABLE)
                : core::ui::ContextActionStripVisualState::DISABLED,
            viewModel.muted
                ? core::ui::ContextActionStripTone::WARNING
                : core::ui::ContextActionStripTone::NEUTRAL,
            ::standalone::icons::Size::L
        );
    }
    // BOTTOM_CENTER remains the global Transport control.
    actions.slots[1].visualState =
        core::ui::ContextActionStripVisualState::HIDDEN;
    if (viewModel.typeChangePending) {
        actions.slots[2] = {
            .visualState = viewModel.trackEnabled &&
                    !viewModel.typeChangeBlocked
                ? core::ui::ContextActionStripVisualState::AVAILABLE
                : core::ui::ContextActionStripVisualState::DISABLED,
            .tone = core::ui::ContextActionStripTone::POSITIVE,
            .showLabel = true,
            .label = "Apply",
        };
    } else {
        actions.slots[2] = core::ui::makeStandaloneIconStripSlot(
            ::standalone::icons::TRACK_SOLO,
            viewModel.trackEnabled
                ? (viewModel.soloed
                    ? core::ui::ContextActionStripVisualState::ARMED
                    : core::ui::ContextActionStripVisualState::AVAILABLE)
                : core::ui::ContextActionStripVisualState::DISABLED,
            viewModel.soloed
                ? core::ui::ContextActionStripTone::POSITIVE
                : core::ui::ContextActionStripTone::NEUTRAL,
            ::standalone::icons::Size::L
        );
    }
    action_strip_.render(actions);
}

}  // namespace core::context::standalone
