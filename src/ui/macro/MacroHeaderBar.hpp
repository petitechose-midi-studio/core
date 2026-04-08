#pragma once

#include <array>
#include <cstdint>

#include <lvgl.h>

#include <oc/ui/lvgl/IWidget.hpp>

namespace core::ui {

struct MacroHeaderBarProps {
    static constexpr uint8_t ACTIVITY_COUNT = 8;

    uint8_t activePage = 0;
    uint8_t previewPage = 0;
    uint8_t enabledMask = 0xFF;
    bool clutchActive = false;
    bool selectingPage = false;
    std::array<uint8_t, ACTIVITY_COUNT> pageOutputActivity{};
    const char* leftText = "";
};

class MacroHeaderBar : public oc::ui::lvgl::IWidget {
public:
    explicit MacroHeaderBar(lv_obj_t* parent);
    ~MacroHeaderBar() override;

    MacroHeaderBar(const MacroHeaderBar&) = delete;
    MacroHeaderBar& operator=(const MacroHeaderBar&) = delete;

    void render(const MacroHeaderBarProps& props);
    lv_obj_t* getElement() const override { return container_; }

private:
    void createUI(lv_obj_t* parent);

    lv_obj_t* container_ = nullptr;
    lv_obj_t* top_row_ = nullptr;
    lv_obj_t* page_accent_ = nullptr;
    lv_obj_t* left_label_ = nullptr;
    lv_obj_t* top_row_spacer_ = nullptr;
    lv_obj_t* channel_activity_row_ = nullptr;
    std::array<lv_obj_t*, MacroHeaderBarProps::ACTIVITY_COUNT> channel_activity_items_{};

    bool cache_initialized_ = false;
    uint8_t cached_page_ = 0;
    uint8_t cached_preview_page_ = 0;
    bool cached_clutch_active_ = false;
    bool cached_selecting_page_ = false;
    uint8_t cached_enabled_mask_ = 0xFF;
    std::array<uint8_t, MacroHeaderBarProps::ACTIVITY_COUNT> cached_activity_{};
    std::array<char, 32> left_text_cache_{};
};

}  // namespace core::ui
