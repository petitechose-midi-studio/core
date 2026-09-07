#pragma once

#include <cstdint>

#include <lvgl.h>

#include <oc/ui/lvgl/IWidget.hpp>

#include "ui/common/AddSlotIcon.hpp"
#include "ui/common/TrackNavigationStripProps.hpp"

namespace core::ui {

/**
 * Shared compact renderer for the 16-track navigation strip.
 *
 * Props carry active/preview/add/selection/activity state; the widget owns LVGL
 * objects; cursor geometry follows the row's completed LVGL layout.
 */
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
    void updateCursors();

    struct ItemRenderCache {
        bool initialized = false;
        uint32_t bgColor = 0;
        lv_opa_t bgOpa = LV_OPA_TRANSP;
        bool addVisible = false;
        lv_coord_t borderWidth = -1;
        lv_opa_t borderOpa = LV_OPA_TRANSP;
        lv_coord_t outlineWidth = -1;
        lv_opa_t outlineOpa = LV_OPA_TRANSP;
        bool destinationVisible = false;
        uint32_t destinationColor = 0;
    };

    lv_obj_t* container_ = nullptr;
    lv_obj_t* items_row_ = nullptr;
    lv_obj_t* active_cursor_ = nullptr;
    lv_obj_t* current_cursor_ = nullptr;
    std::array<lv_obj_t*, TrackNavigationStripProps::TRACK_COUNT> items_{};
    std::array<
        lv_obj_t*,
        TrackNavigationStripProps::TRACK_COUNT
    > destination_markers_{};
    std::array<add_slot_icon::ObjectPair, TrackNavigationStripProps::TRACK_COUNT> item_add_icons_{};
    std::array<ItemRenderCache, TrackNavigationStripProps::TRACK_COUNT> item_cache_{};
    uint8_t active_track_ = TrackNavigationStripProps::TRACK_COUNT;
    uint8_t focused_track_ = TrackNavigationStripProps::TRACK_COUNT;
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
