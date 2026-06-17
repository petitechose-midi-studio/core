#pragma once

#include <array>
#include <cstdint>

#include <lvgl.h>

#include <oc/ui/lvgl/IWidget.hpp>

#include "state/sequencer/StepProperty.hpp"

namespace core::ui {

struct StepPropertySelectionOverlayProps {
    bool visible = false;
    core::state::sequencer::StepProperty property =
        core::state::sequencer::StepProperty::NOTE;
    bool customContent = false;
    const char* icon = nullptr;
    const char* label = nullptr;
    const char* value = nullptr;
    bool useValueText = false;
    std::array<char, 16> valueText{};
    uint32_t color = 0;
};

class StepPropertySelectionOverlay : public oc::ui::lvgl::IWidget {
public:
    explicit StepPropertySelectionOverlay(lv_obj_t* parent);
    ~StepPropertySelectionOverlay() override;

    StepPropertySelectionOverlay(const StepPropertySelectionOverlay&) = delete;
    StepPropertySelectionOverlay& operator=(const StepPropertySelectionOverlay&) = delete;

    void render(const StepPropertySelectionOverlayProps& props);

    lv_obj_t* getElement() const override { return container_; }

private:
    void createUI(lv_obj_t* parent);

    lv_obj_t* container_ = nullptr;
    lv_obj_t* content_row_ = nullptr;
    lv_obj_t* icon_ = nullptr;
    lv_obj_t* text_column_ = nullptr;
    lv_obj_t* label_ = nullptr;
    lv_obj_t* value_ = nullptr;
    bool visible_cache_ = false;
    bool has_rendered_ = false;
    bool rendered_custom_content_ = false;
    core::state::sequencer::StepProperty rendered_property_ =
        core::state::sequencer::StepProperty::NOTE;
    uint32_t rendered_color_ = 0;
    const char* rendered_icon_ = nullptr;
    std::array<char, 24> rendered_label_{};
    std::array<char, 16> rendered_value_{};
};

}  // namespace core::ui
