#pragma once

#include <array>
#include <cstdint>

#include <lvgl.h>
#include <oc/ui/lvgl/IWidget.hpp>

#include "state/StatusBarState.hpp"
#include "state/project/ProjectTrackState.hpp"
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
    const core::state::project::ProjectTrackState* projectTracks = nullptr;
    const core::state::sequencer::SequencerState* sequencer = nullptr;
    const core::state::TrackNavigationState* trackNavigation = nullptr;
    const core::state::StatusBarState* statusBar = nullptr;
    uint16_t enabledTrackMask = 0U;
    uint8_t contentRevision = 0U;
};

/** One retained surface for the Scene rail plus 3-Track x 4-Scene launcher. */
class SequencerClipLauncherSurface : public oc::ui::lvgl::IWidget {
public:
    explicit SequencerClipLauncherSurface(lv_obj_t* parent);
    ~SequencerClipLauncherSurface() override;

    void render(const SequencerClipLauncherSurfaceProps& props);
    void invalidatePlaybackProgress();
    void invalidateTrackActivity();
    lv_obj_t* getElement() const override { return root_; }

private:
    struct ClipPreview {
        static constexpr uint8_t COLUMNS = 32U;
        static constexpr uint8_t ROWS = 8U;
        static constexpr uint8_t INVALID_COLUMN = 0xFFU;

        std::array<uint8_t, COLUMNS * ROWS> velocity{};
        std::array<uint32_t, ROWS> onsetMask{};
        uint16_t playStartTick = 0U;
        uint16_t loopStartTick = 0U;
        uint16_t loopEndTick = 0U;
        uint8_t loopColumn = INVALID_COLUMN;
        bool content = false;
    };

    struct PlaybackHeadCache {
        lv_area_t area{};
        bool valid = false;
    };

    struct PreviewCacheKey {
        uint32_t clipGridRevision = 0U;
        uint8_t contentRevision = 0U;
        uint8_t firstVisibleTrack = 0U;
        uint8_t firstVisibleSlot = 0U;

        [[nodiscard]] bool matches(const PreviewCacheKey& other) const {
            return clipGridRevision == other.clipGridRevision &&
                contentRevision == other.contentRevision &&
                firstVisibleTrack == other.firstVisibleTrack &&
                firstVisibleSlot == other.firstVisibleSlot;
        }
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
    std::array<
        PlaybackHeadCache,
        core::state::sequencer::ClipWorkspaceUiState::VISIBLE_TRACKS>
        playback_heads_{};
    PreviewCacheKey preview_cache_key_{};
    bool preview_cache_valid_ = false;
};

}  // namespace core::ui::sequencer
