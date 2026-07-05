#pragma once

/**
 * @file MacroKnobWidget.hpp
 * @brief Knob-style macro widget
 */

#include <memory>

#include <oc/ui/lvgl/widget/KnobWidget.hpp>

#include "BaseMacroWidget.hpp"

namespace core::ui {

class MacroKnobWidget : public BaseMacroWidget {
public:
    MacroKnobWidget(lv_obj_t* parent, uint8_t index);
    ~MacroKnobWidget() override;

    void setValue(float value) override;
    void setAutomationActive(bool active) override;
    void setAutomationRecording(bool active) override;
    void setAutomationManualOverride(bool active) override;
    void setSlotState(bool active, bool addSlot) override;
    void setFocused(bool focused) override;

private:
    void createUI(lv_obj_t* parent);
    void updateAutomationTrackColor();
    void updateFocusFrame();
    void updateSlotVisibility();

    std::unique_ptr<oc::ui::lvgl::KnobWidget> knob_;
    lv_obj_t* add_label_ = nullptr;
    bool automation_active_ = false;
    bool automation_recording_ = false;
    bool automation_manual_override_ = false;
    bool slot_active_ = true;
    bool add_slot_ = false;
    bool focused_ = false;
    float current_value_ = 0.0f;
};

}  // namespace core::ui
