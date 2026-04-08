#pragma once

#include <array>

#include <lvgl.h>

#include <oc/ui/lvgl/IWidget.hpp>

#include "state/macro/MacroUiState.hpp"

namespace core::ui {

struct MacroBottomControlsProps {
    bool selectingQuickControls = false;
    core::state::macro::MacroQuickControlItem focusedQuickControl =
        core::state::macro::MacroQuickControlItem::GLOBAL_CHANNEL;
    uint8_t globalChannel = 0;
    int8_t ccOffset = 0;
};

class MacroBottomControls : public oc::ui::lvgl::IWidget {
public:
    explicit MacroBottomControls(lv_obj_t* parent);
    ~MacroBottomControls() override;

    MacroBottomControls(const MacroBottomControls&) = delete;
    MacroBottomControls& operator=(const MacroBottomControls&) = delete;

    void render(const MacroBottomControlsProps& props);
    lv_obj_t* getElement() const override { return container_; }

private:
    using QuickItem = core::state::macro::MacroQuickControlItem;

    struct QuickControlWidgets {
        QuickItem item = QuickItem::GLOBAL_CHANNEL;
        lv_obj_t* slot = nullptr;
        lv_obj_t* content = nullptr;
        lv_obj_t* label = nullptr;
        lv_obj_t* value = nullptr;
        std::array<char, 16> renderedValue{};
        bool valueInitialized = false;
        bool highlightedInitialized = false;
        bool highlighted = false;
    };

    void createUI(lv_obj_t* parent);
    void createQuickControl(
        QuickControlWidgets& widgets,
        lv_obj_t* parent,
        QuickItem item,
        const char* labelText,
        lv_coord_t slotWidth,
        lv_flex_align_t align,
        const lv_font_t* valueFont
    );
    void renderQuickControl(QuickControlWidgets& widgets, const MacroBottomControlsProps& props);
    void positionQuickControlCursor(const MacroBottomControlsProps& props);
    void ensureCursorGeometry();
    lv_obj_t* quickControlAnchor(QuickItem item) const;

    lv_obj_t* container_ = nullptr;
    lv_obj_t* top_row_ = nullptr;
    QuickControlWidgets channel_;
    QuickControlWidgets offset_;
    lv_obj_t* quick_cursor_ = nullptr;
    bool has_rendered_ = false;
    MacroBottomControlsProps rendered_props_{};
    bool cursor_visible_cache_ = false;
    lv_coord_t cursor_x_cache_ = 0;
    lv_coord_t cursor_y_cache_ = 0;
    bool geometry_cache_initialized_ = false;
    lv_coord_t geometry_cache_width_ = 0;
    lv_coord_t geometry_cache_height_ = 0;
    std::array<lv_point_t, 2> cursor_positions_{};
};

}  // namespace core::ui
