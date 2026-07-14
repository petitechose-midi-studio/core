#pragma once

/**
 * @file MacroKnobWidget.hpp
 * @brief Knob-style macro widget
 */

#include <cstdint>

#include <lvgl.h>

#include <oc/ui/lvgl/IWidget.hpp>
#include <oc/ui/lvgl/widget/Label.hpp>

#include "app/ExtmemAllocator.hpp"

namespace core::ui {

/**
 * Concrete macro-slot widget used by the standalone macro view.
 *
 * Macro slots have a single visual representation. Keeping the CC label and
 * automation arc in one concrete widget avoids a dormant polymorphic widget
 * hierarchy and makes the hot value-update path explicit.
 */
class MacroKnobWidget : public oc::ui::lvgl::IWidget {
public:
    explicit MacroKnobWidget(lv_obj_t* parent);
    ~MacroKnobWidget() override;

    MacroKnobWidget(const MacroKnobWidget&) = delete;
    MacroKnobWidget& operator=(const MacroKnobWidget&) = delete;

    lv_obj_t* getElement() const override { return container_; }
    [[nodiscard]] bool valid() const {
        return container_ && knob_ && config_label_container_ && add_label_ &&
               automation_source_label_ && modulation_source_label_ &&
               cc_prefix_ && cc_prefix_->getElement() &&
               cc_value_ && cc_value_->getElement();
    }

    void setValue(float value);
    void setConfig(uint8_t cc);
    void setAutomationActive(bool active);
    void setAutomationRecording(bool active);
    void setAutomationManualOverride(bool active);
    void setSourceIndicators(bool automationStored,
                             bool automationActive,
                             bool modulationStored,
                             bool modulationActive,
                             bool modulationPaused,
                             bool modulationSuspended);
    void setSlotState(bool active, bool addSlot);
    void setFocused(bool focused);

private:
    struct ArcGeometry {
        lv_point_t center{};
        uint16_t radius = 0;
        lv_coord_t width = 0;
    };

    void createUI(lv_obj_t* parent);
    void createContainer(lv_obj_t* parent);
    void createConfigLabels();
    void createSourceIndicators();
    void setConfigLabelsVisible(bool visible);
    void updateAutomationTrackColor();
    void updateSourceIndicators();
    void updateFocusFrame();
    void updateSlotVisibility();
    bool buildArcGeometry(ArcGeometry& geometry) const;
    void invalidateValueArc();
    void invalidateArcRange(lv_value_precise_t startAngle, lv_value_precise_t endAngle);
    void invalidateArcDelta(uint16_t previousAngle, uint16_t nextAngle);
    void drawArc(lv_layer_t* layer, lv_obj_t* target) const;
    static void onArcDrawEvent(lv_event_t* event);

    lv_obj_t* container_ = nullptr;
    lv_obj_t* knob_ = nullptr;
    lv_obj_t* config_label_container_ = nullptr;
    lv_obj_t* add_label_ = nullptr;
    lv_obj_t* automation_source_label_ = nullptr;
    lv_obj_t* modulation_source_label_ = nullptr;
    core::app::ExtmemUniquePtr<oc::ui::lvgl::Label> cc_prefix_;
    core::app::ExtmemUniquePtr<oc::ui::lvgl::Label> cc_value_;
    uint32_t track_color_ = 0;
    uint16_t rendered_value_angle_ = 0xFFFF;
    uint8_t current_cc_ = 0xFF;
    bool automation_active_ = false;
    bool automation_stored_ = false;
    bool modulation_stored_ = false;
    bool modulation_active_ = false;
    bool modulation_paused_ = false;
    bool modulation_suspended_ = false;
    bool automation_recording_ = false;
    bool automation_manual_override_ = false;
    bool slot_active_ = true;
    bool add_slot_ = false;
    bool focused_ = false;
    bool config_labels_visible_ = true;
    float current_value_ = 0.0f;
};

}  // namespace core::ui
