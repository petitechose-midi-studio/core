#pragma once

#include <cstdint>

#include <lvgl.h>
#include <oc/ui/lvgl/IWidget.hpp>

#include "state/sequencer/SequencerClipGridState.hpp"
#include "state/sequencer/SequencerClipLaunchQueue.hpp"
#include "state/sequencer/SequencerTrackBankState.hpp"
#include "state/sequencer/SequencerUiState.hpp"
#include "state/TrackNavigationState.hpp"

namespace core::ui::sequencer {

struct SequencerClipLauncherSurfaceProps {
    bool visible = false;
    const core::state::sequencer::ClipWorkspaceUiState* ui = nullptr;
    const core::state::sequencer::SequencerClipGridState* clips = nullptr;
    const core::state::sequencer::SequencerClipLaunchQueue* launches = nullptr;
    const core::state::sequencer::SequencerTrackBankState* tracks = nullptr;
    const core::state::TrackNavigationState* trackNavigation = nullptr;
    uint16_t enabledTrackMask = 0U;
};

/** One retained draw surface for the 4-Track x 2-row launcher viewport. */
class SequencerClipLauncherSurface : public oc::ui::lvgl::IWidget {
public:
    explicit SequencerClipLauncherSurface(lv_obj_t* parent);
    ~SequencerClipLauncherSurface() override;

    void render(const SequencerClipLauncherSurfaceProps& props);
    lv_obj_t* getElement() const override { return root_; }

private:
    static void onDraw(lv_event_t* event);
    void draw(lv_layer_t* layer) const;

    lv_obj_t* root_ = nullptr;
    SequencerClipLauncherSurfaceProps props_{};
};

}  // namespace core::ui::sequencer
