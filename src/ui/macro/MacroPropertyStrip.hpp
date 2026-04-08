#pragma once

#include <array>
#include <cstdint>

#include <lvgl.h>

#include <oc/ui/lvgl/IWidget.hpp>

#include "state/macro/MacroUiState.hpp"

namespace core::ui {

struct MacroPropertyStripProps {
    core::state::macro::MacroPerformanceProperty activeProperty =
        core::state::macro::MacroPerformanceProperty::VALUE;
    bool clutchActive = false;
};

class MacroPropertyStrip : public oc::ui::lvgl::IWidget {
public:
    explicit MacroPropertyStrip(lv_obj_t* parent);
    ~MacroPropertyStrip() override;

    MacroPropertyStrip(const MacroPropertyStrip&) = delete;
    MacroPropertyStrip& operator=(const MacroPropertyStrip&) = delete;

    void render(const MacroPropertyStripProps& props);
    lv_obj_t* getElement() const override { return container_; }

private:
    static constexpr size_t PROPERTY_COUNT = 3;

    struct RenderCache {
        bool initialized = false;
        lv_opa_t textOpa = LV_OPA_TRANSP;
    };

    void createUI(lv_obj_t* parent);
    void ensureCursorGeometry();

    lv_obj_t* container_ = nullptr;
    lv_obj_t* selection_cursor_ = nullptr;
    std::array<lv_obj_t*, PROPERTY_COUNT> items_{};
    std::array<lv_obj_t*, PROPERTY_COUNT> icons_{};
    std::array<lv_point_t, PROPERTY_COUNT> cursor_positions_{};
    std::array<RenderCache, PROPERTY_COUNT> icon_render_cache_{};
    bool has_rendered_ = false;
    MacroPropertyStripProps rendered_props_{};
    bool geometry_cache_initialized_ = false;
    lv_coord_t geometry_cache_width_ = -1;
    lv_coord_t geometry_cache_height_ = -1;
    bool cursor_visible_cache_ = false;
    lv_coord_t cursor_x_cache_ = -1;
    lv_coord_t cursor_y_cache_ = -1;
};

}  // namespace core::ui
