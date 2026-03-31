#pragma once

#include <array>
#include <cstdint>

#include <lvgl.h>

#include <oc/ui/lvgl/IWidget.hpp>

#include "state/sequencer/SequencerUiState.hpp"

namespace core::ui {

struct StepPropertyStripProps {
    core::state::sequencer::StepProperty activeProperty =
        core::state::sequencer::StepProperty::NOTE;
    bool selecting = false;
    int selectedIndex = 0;
};

class StepPropertyStrip : public oc::ui::lvgl::IWidget {
public:
    explicit StepPropertyStrip(lv_obj_t* parent);
    ~StepPropertyStrip() override;

    StepPropertyStrip(const StepPropertyStrip&) = delete;
    StepPropertyStrip& operator=(const StepPropertyStrip&) = delete;

    void render(const StepPropertyStripProps& props);

    lv_obj_t* getElement() const override { return container_; }

private:
    static constexpr size_t PROPERTY_COUNT = 5;

    void createUI(lv_obj_t* parent);
    void ensureCursorGeometry();

    struct RenderCache {
        bool initialized = false;
        lv_opa_t textOpa = LV_OPA_TRANSP;
    };

    lv_obj_t* container_ = nullptr;
    lv_obj_t* selection_cursor_ = nullptr;
    std::array<lv_obj_t*, PROPERTY_COUNT> items_{};
    std::array<lv_obj_t*, PROPERTY_COUNT> icons_{};
    std::array<lv_point_t, PROPERTY_COUNT> cursor_positions_{};
    std::array<RenderCache, PROPERTY_COUNT> icon_render_cache_{};
    bool has_rendered_ = false;
    StepPropertyStripProps rendered_props_{};
    bool geometry_cache_initialized_ = false;
    lv_coord_t geometry_cache_width_ = -1;
    lv_coord_t geometry_cache_height_ = -1;
    bool cursor_visible_cache_ = false;
    lv_coord_t cursor_x_cache_ = -1;
    lv_coord_t cursor_y_cache_ = -1;
};

}  // namespace core::ui
