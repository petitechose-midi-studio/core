#pragma once

#include <array>
#include <cstdint>

#include <lvgl.h>

#include <oc/ui/lvgl/IWidget.hpp>

#include "state/StatusBarState.hpp"
#include "ui/common/AddSlotIcon.hpp"

namespace core::ui {

/**
 * Shared compact renderer for the 16-track navigation strip.
 *
 * Props carry active/preview/add/selection/activity state; the widget owns LVGL
 * objects and geometry caches for cursors and per-track cells.
 */
struct TrackNavigationStripProps {
    static constexpr uint8_t TRACK_COUNT = core::state::StatusBarState::TRACK_COUNT;

    uint8_t activeTrack = 0;
    uint8_t previewTrack = 0;
    uint8_t addTrackIndex = TRACK_COUNT;
    uint16_t enabledMask = 0x0001;
    uint16_t selectedMask = 0;
    bool focusingTrack = false;
    bool selectingTrack = false;
    std::array<uint8_t, TRACK_COUNT> activity{};
};

class TrackNavigationStrip : public oc::ui::lvgl::IWidget {
public:
    explicit TrackNavigationStrip(lv_obj_t* parent);
    ~TrackNavigationStrip() override;

    TrackNavigationStrip(const TrackNavigationStrip&) = delete;
    TrackNavigationStrip& operator=(const TrackNavigationStrip&) = delete;

    void render(const TrackNavigationStripProps& props);
    lv_obj_t* getElement() const override { return container_; }

private:
    void createUI(lv_obj_t* parent);
    void refreshItemGeometryCache_();

    struct ItemRenderCache {
        bool initialized = false;
        lv_coord_t width = -1;
        uint32_t bgColor = 0;
        lv_opa_t bgOpa = LV_OPA_TRANSP;
        bool addVisible = false;
        lv_coord_t outlineWidth = -1;
        lv_opa_t outlineOpa = LV_OPA_TRANSP;
    };

    lv_obj_t* container_ = nullptr;
    lv_obj_t* items_row_ = nullptr;
    lv_obj_t* active_cursor_ = nullptr;
    lv_obj_t* current_cursor_ = nullptr;
    std::array<lv_obj_t*, TrackNavigationStripProps::TRACK_COUNT> items_{};
    std::array<add_slot_icon::ObjectPair, TrackNavigationStripProps::TRACK_COUNT> item_add_icons_{};
    std::array<ItemRenderCache, TrackNavigationStripProps::TRACK_COUNT> item_cache_{};
    bool item_geometry_cache_initialized_ = false;
    lv_coord_t cached_row_width_ = -1;
    std::array<lv_coord_t, TrackNavigationStripProps::TRACK_COUNT> item_x_cache_{};
    std::array<lv_coord_t, TrackNavigationStripProps::TRACK_COUNT> item_y_cache_{};
    std::array<lv_coord_t, TrackNavigationStripProps::TRACK_COUNT> item_width_cache_{};
    std::array<lv_coord_t, TrackNavigationStripProps::TRACK_COUNT> item_height_cache_{};
    bool active_cursor_visible_cache_ = false;
    lv_coord_t active_cursor_x_cache_ = -1;
    lv_coord_t active_cursor_y_cache_ = -1;
    lv_coord_t active_cursor_width_cache_ = -1;
    bool current_cursor_visible_cache_ = false;
    lv_coord_t current_cursor_x_cache_ = -1;
    lv_coord_t current_cursor_y_cache_ = -1;
    lv_coord_t current_cursor_width_cache_ = -1;
    lv_coord_t current_cursor_height_cache_ = -1;
    lv_opa_t current_cursor_opa_cache_ = LV_OPA_TRANSP;
};

}  // namespace core::ui
