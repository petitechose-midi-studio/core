#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

#include <lvgl.h>

#include <oc/ui/lvgl/IWidget.hpp>
#include <oc/ui/lvgl/widget/Label.hpp>

#include "state/sequencer/DrumPatternState.hpp"
#include "state/StructureNavigationState.hpp"


namespace core::state::sequencer {
struct DrumSequencerState;
}  // namespace core::state::sequencer

namespace core::ui::sequencer {

struct DrumOverviewSurfaceProps {
    bool visible = false;
    const core::state::sequencer::DrumSequencerState* projection =
        nullptr;
    core::state::StructureNavigationFocus navigationFocus =
        core::state::StructureNavigationFocus::PAGE;
    uint8_t midiChannel = 1U;
    uint32_t authoredRevision = 0U;
    uint32_t uiRevision = 0U;
};

/**
 * Retained 8 x 8 Drum performance overview.
 *
 * Authored and navigation state remain owned by Core. This widget owns only
 * LVGL presentation plus the small playback snapshot required to invalidate
 * moving playheads and chance decisions without redrawing the complete view.
 */
class DrumOverviewSurface : public oc::ui::lvgl::IWidget {
public:
    explicit DrumOverviewSurface(lv_obj_t* parent);
    ~DrumOverviewSurface() override;

    DrumOverviewSurface(const DrumOverviewSurface&) = delete;
    DrumOverviewSurface& operator=(const DrumOverviewSurface&) = delete;

    void render(const DrumOverviewSurfaceProps& props);

    lv_obj_t* getElement() const override { return root_; }

private:
    static constexpr size_t RUNTIME_LANE_CAPACITY = 16U;
    static constexpr size_t STATIC_ROW_COUNT = 8U;
    static constexpr size_t STATIC_STEP_COUNT = 8U;
    using StaticRows = std::array<uint32_t, STATIC_ROW_COUNT>;

    struct PlaybackSnapshot {
        std::array<uint8_t, RUNTIME_LANE_CAPACITY> playheadSteps{};
        std::array<uint8_t, RUNTIME_LANE_CAPACITY> playheadPhasesQ8{};
        std::array<uint8_t, RUNTIME_LANE_CAPACITY> chanceDecisionSteps{};
        uint16_t playheadValidMask = 0U;
        uint16_t chanceDecisionValidMask = 0U;
        uint16_t chanceDecisionPlayedMask = 0U;
        core::state::sequencer::DrumResolvedPageProjection resolvedPage{};
        bool playbackActive = false;
    };

    void createUi(lv_obj_t* parent);
    void drawSurface(lv_layer_t* layer);
    static void onDrawEvent(lv_event_t* event);
    void syncFocusedLaneName(const DrumOverviewSurfaceProps& props);
    void hideFocusedLaneName();

    [[nodiscard]] bool fullSurfaceChanged(
        const DrumOverviewSurfaceProps& props
    ) const;
    [[nodiscard]] StaticRows captureStaticRows(
        const DrumOverviewSurfaceProps& props
    ) const;
    void invalidateStaticDelta(
        const StaticRows& previous,
        const StaticRows& next
    );
    [[nodiscard]] PlaybackSnapshot capturePlayback(
        const core::state::sequencer::DrumSequencerState& projection
    ) const;
    void invalidatePlaybackDelta(
        const PlaybackSnapshot& previous,
        const PlaybackSnapshot& next
    );
    void includePlayheadDamage(
        const PlaybackSnapshot& previous,
        const PlaybackSnapshot& next,
        uint8_t lane,
        lv_coord_t rowY,
        const lv_area_t& surface,
        lv_area_t& damage,
        bool& hasDamage
    );
    void includeChanceCellDamage(
        const PlaybackSnapshot& snapshot,
        uint8_t lane,
        lv_coord_t rowY,
        const lv_area_t& surface,
        lv_area_t& damage,
        bool& hasDamage
    );
    void includeResolvedCellDamage(
        uint8_t row,
        uint8_t column,
        const lv_area_t& surface,
        lv_area_t& damage,
        bool& hasDamage
    );

    lv_obj_t* root_ = nullptr;
    std::unique_ptr<oc::ui::lvgl::Label> focused_lane_name_;
    std::array<char, 16> focused_lane_name_cache_{};
    uint8_t focused_lane_name_lane_ = 0xFFU;
    uint8_t focused_lane_name_row_ = 0xFFU;
    lv_coord_t focused_lane_name_height_ = 0;
    DrumOverviewSurfaceProps renderedProps_{};
    StaticRows static_rows_{};
    PlaybackSnapshot playback_{};
    bool rendered_ = false;
    bool visible_ = false;
    bool focused_lane_name_visible_ = false;
};

}  // namespace core::ui::sequencer
