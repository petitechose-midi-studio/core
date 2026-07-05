#include "MacroKnobWidget.hpp"

#include <config/PlatformCompat.hpp>
#include <oc/ui/lvgl/theme/BaseTheme.hpp>
#include <ms/ui/font/CoreFonts.hpp>

#include "ui/theme/StandaloneTheme.hpp"

namespace core::ui {

namespace theme = oc::ui::lvgl::base_theme;
namespace stheme = standalone::theme;

namespace {

uint32_t automationTrackColor(bool slotActive,
                              bool automationActive,
                              bool automationRecording,
                              bool manualOverride) {
    if (!slotActive) return theme::color::KNOB_VALUE;
    if (automationRecording) return stheme::color::MACRO_AUTOMATION_RECORDING;
    if (manualOverride) return stheme::color::MACRO_AUTOMATION_MANUAL;
    return automationActive ? stheme::color::MACRO_AUTOMATION : theme::color::KNOB_VALUE;
}

}  // namespace

MacroKnobWidget::MacroKnobWidget(lv_obj_t* parent, uint8_t index)
    : BaseMacroWidget(index) {
    createUI(parent);
}

MacroKnobWidget::~MacroKnobWidget() {
    knob_.reset();
}

FLASHMEM void MacroKnobWidget::createUI(lv_obj_t* parent) {
    createContainerWithGrid(parent);

    knob_ = std::make_unique<oc::ui::lvgl::KnobWidget>(container_);
    knob_
        ->sizeMode(oc::ui::lvgl::SizeMode::FitContent)
        .renderProfile(oc::ui::lvgl::KnobRenderProfile::ArcOnly)
        .centered(false)
        .bgColor(theme::color::KNOB_BACKGROUND)
        .trackColor(theme::color::KNOB_VALUE)
        .flashEnabled(false);
    knob_->setRibbonEnabled(false);

    lv_obj_set_grid_cell(knob_->getElement(),
        LV_GRID_ALIGN_STRETCH, 0, 1,
        LV_GRID_ALIGN_CENTER, 0, 1);

    createConfigLabels(container_);

    add_label_ = lv_label_create(container_);
    lv_label_set_text(add_label_, "+");
    if (fonts.inter_13_bold) {
        lv_obj_set_style_text_font(add_label_, fonts.inter_13_bold, 0);
    }
    lv_obj_set_style_text_color(add_label_, lv_color_hex(stheme::color::TEXT_SECONDARY), 0);
    lv_obj_set_style_text_opa(add_label_, LV_OPA_60, 0);
    lv_obj_add_flag(add_label_, LV_OBJ_FLAG_FLOATING);
    lv_obj_center(add_label_);
    lv_obj_add_flag(add_label_, LV_OBJ_FLAG_HIDDEN);
}

void MacroKnobWidget::setValue(float value) {
    if (!knob_) return;
    current_value_ = value;
    knob_->setValue(value);
}

void MacroKnobWidget::setAutomationActive(bool active) {
    if (!knob_ || automation_active_ == active) return;
    automation_active_ = active;
    updateAutomationTrackColor();
}

void MacroKnobWidget::setAutomationRecording(bool active) {
    if (!knob_ || automation_recording_ == active) return;
    automation_recording_ = active;
    updateAutomationTrackColor();
}

void MacroKnobWidget::setAutomationManualOverride(bool active) {
    if (!knob_ || automation_manual_override_ == active) return;
    automation_manual_override_ = active;
    updateAutomationTrackColor();
}

void MacroKnobWidget::setSlotState(bool active, bool addSlot) {
    if (slot_active_ == active && add_slot_ == addSlot) return;
    slot_active_ = active;
    add_slot_ = addSlot;
    updateSlotVisibility();
}

void MacroKnobWidget::setFocused(bool focused) {
    if (focused_ == focused) return;
    focused_ = focused;
    updateFocusFrame();
}

void MacroKnobWidget::updateAutomationTrackColor() {
    if (!knob_) return;
    knob_->trackColor(
        automationTrackColor(
            slot_active_,
            automation_active_,
            automation_recording_,
            automation_manual_override_
        )
    );
    knob_->setValue(current_value_);
}

void MacroKnobWidget::updateFocusFrame() {
    if (!container_) return;
    lv_obj_set_style_border_width(container_, focused_ ? 1 : 0, 0);
    lv_obj_set_style_border_color(container_, lv_color_hex(stheme::color::TEXT_PRIMARY), 0);
    lv_obj_set_style_border_opa(container_, focused_ ? LV_OPA_70 : LV_OPA_TRANSP, 0);
    if (add_label_) {
        lv_obj_set_style_text_opa(add_label_, focused_ ? LV_OPA_COVER : LV_OPA_60, 0);
    }
}

void MacroKnobWidget::updateSlotVisibility() {
    if (knob_) {
        lv_obj_t* knobElement = knob_->getElement();
        if (slot_active_) {
            lv_obj_clear_flag(knobElement, LV_OBJ_FLAG_HIDDEN);
            updateAutomationTrackColor();
        } else {
            lv_obj_add_flag(knobElement, LV_OBJ_FLAG_HIDDEN);
        }
    }

    setConfigLabelsVisible(slot_active_);

    if (!add_label_) return;
    if (!slot_active_ && add_slot_) {
        lv_obj_clear_flag(add_label_, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(add_label_, LV_OBJ_FLAG_HIDDEN);
    }
    updateFocusFrame();
}

}  // namespace core::ui
