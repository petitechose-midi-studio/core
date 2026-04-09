#pragma once

#include <array>
#include <cstdint>

#include <lvgl.h>

#include <oc/ui/lvgl/IWidget.hpp>

namespace core::ui {

struct TrackHeaderRowProps {
    static constexpr uint8_t ITEM_COUNT = 8;

    const char* leftText = "";
    uint32_t accentColor = 0;
    lv_opa_t accentOpa = LV_OPA_COVER;
    uint32_t backgroundColor = 0;
    lv_opa_t backgroundOpa = LV_OPA_TRANSP;
    std::array<uint32_t, ITEM_COUNT> itemColors{};
    std::array<lv_opa_t, ITEM_COUNT> itemOpacities{};
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
    std::array<lv_obj_t*, TrackHeaderRowProps::ITEM_COUNT> items_{};

    std::array<char, 32> left_text_cache_{};
    bool surface_cache_initialized_ = false;
    uint32_t accent_cache_color_ = 0;
    lv_opa_t accent_cache_opa_ = LV_OPA_TRANSP;
    uint32_t background_cache_color_ = 0;
    lv_opa_t background_cache_opa_ = LV_OPA_TRANSP;
    std::array<uint32_t, TrackHeaderRowProps::ITEM_COUNT> item_color_cache_{};
    std::array<lv_opa_t, TrackHeaderRowProps::ITEM_COUNT> item_opa_cache_{};
};

}  // namespace core::ui
