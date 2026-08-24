#pragma once

#include <array>
#include <cstdint>

#include <lvgl.h>
#include <oc/ui/lvgl/IWidget.hpp>

#include "state/sequencer/SequencerClipGridState.hpp"
#include "state/sequencer/SequencerClipLaunchQueue.hpp"
#include "state/sequencer/SequencerTrackBankState.hpp"
#include "state/sequencer/SequencerState.hpp"
#include "state/sequencer/SequencerUiState.hpp"
#include "state/TrackNavigationState.hpp"

namespace core::ui::sequencer {

struct SequencerClipLauncherSurfaceProps {
    bool visible = false;
    const core::state::sequencer::ClipWorkspaceUiState* ui = nullptr;
    const core::state::sequencer::SequencerClipGridState* clips = nullptr;
    const core::state::sequencer::SequencerClipLaunchQueue* launches = nullptr;
    const core::state::sequencer::SequencerTrackBankState* tracks = nullptr;
    const core::state::sequencer::SequencerState* sequencer = nullptr;
    const core::state::TrackNavigationState* trackNavigation = nullptr;
    uint16_t enabledTrackMask = 0U;
};

/** One retained draw surface for the spatial 4-Track x 4-Scene launcher. */
class SequencerClipLauncherSurface : public oc::ui::lvgl::IWidget {
public:
    explicit SequencerClipLauncherSurface(lv_obj_t* parent);
    ~SequencerClipLauncherSurface() override;

    void render(const SequencerClipLauncherSurfaceProps& props);
    void invalidatePlaybackProgress();
    lv_obj_t* getElement() const override { return root_; }

private:
    static constexpr uint8_t PREVIEW_BINS = 8U;
    struct ClipPreview {
        std::array<uint8_t, PREVIEW_BINS> density{};
        uint8_t peak = 0U;
    };

    static void onDraw(lv_event_t* event);
    void rebuildPreviews();
    void draw(lv_layer_t* layer) const;

    lv_obj_t* root_ = nullptr;
    SequencerClipLauncherSurfaceProps props_{};
    std::array<
        ClipPreview,
        core::state::sequencer::ClipWorkspaceUiState::VISIBLE_TRACKS *
            core::state::sequencer::ClipWorkspaceUiState::VISIBLE_ROWS>
        previews_{};
};

}  // namespace core::ui::sequencer
