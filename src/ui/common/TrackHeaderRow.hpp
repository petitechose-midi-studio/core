#pragma once

#include <array>
#include <cstdint>

#include <lvgl.h>

#include <oc/ui/lvgl/IWidget.hpp>

namespace core::ui {

struct TrackHeaderRowProps {
    static constexpr uint8_t MAX_ITEM_COUNT = 16;

    const char* leftText = "";
    uint8_t itemCount = 8;
    uint32_t accentColor = 0;
    lv_opa_t accentOpa = LV_OPA_COVER;
    uint32_t backgroundColor = 0;
    lv_opa_t backgroundOpa = LV_OPA_TRANSP;
    std::array<uint32_t, MAX_ITEM_COUNT> itemColors{};
    std::array<lv_opa_t, MAX_ITEM_COUNT> itemOpacities{};
    std::array<bool, MAX_ITEM_COUNT> itemActive{};
    std::array<bool, MAX_ITEM_COUNT> itemAddSlot{};
    bool showCursor = false;
    uint8_t cursorIndex = 0;
    uint16_t selectedMask = 0;
    uint32_t cursorColor = 0;
    lv_opa_t cursorOpa = LV_OPA_80;
};

class TrackHeaderRow : public oc::ui::lvgl::IWidget {
public:
    explicit TrackHeaderRow(lv_obj_t* parent);
    ~TrackHeaderRow() override;

    TrackHeaderRow(const TrackHeaderRow&) = delete;
    TrackHeaderRow& operator=(const TrackHeaderRow&) = delete;

    void render(const TrackHeaderRowProps& props);
    lv_obj_t* getElement() const override { return container_; }

private:
    void createUI(lv_obj_t* parent);

    lv_obj_t* container_ = nullptr;
    lv_obj_t* accent_ = nullptr;
    lv_obj_t* label_ = nullptr;
    lv_obj_t* spacer_ = nullptr;
    lv_obj_t* items_row_ = nullptr;
    lv_obj_t* selection_cursor_ = nullptr;
    std::array<lv_obj_t*, TrackHeaderRowProps::MAX_ITEM_COUNT> items_{};
    std::array<lv_obj_t*, TrackHeaderRowProps::MAX_ITEM_COUNT> item_add_labels_{};

    std::array<char, 32> left_text_cache_{};
    bool surface_cache_initialized_ = false;
    uint32_t accent_cache_color_ = 0;
    lv_opa_t accent_cache_opa_ = LV_OPA_TRANSP;
    uint32_t background_cache_color_ = 0;
    lv_opa_t background_cache_opa_ = LV_OPA_TRANSP;
    std::array<uint32_t, TrackHeaderRowProps::MAX_ITEM_COUNT> item_color_cache_{};
    std::array<lv_opa_t, TrackHeaderRowProps::MAX_ITEM_COUNT> item_opa_cache_{};
    std::array<bool, TrackHeaderRowProps::MAX_ITEM_COUNT> item_selected_cache_{};
    bool cursor_visible_cache_ = false;
    lv_coord_t cursor_x_cache_ = -1;
    lv_coord_t cursor_y_cache_ = -1;
    lv_coord_t cursor_width_cache_ = -1;
    uint32_t cursor_color_cache_ = 0;
    lv_opa_t cursor_opa_cache_ = LV_OPA_TRANSP;
};

}  // namespace core::ui
