#include "MacroKnobWidget.hpp"

#include <algorithm>
#include <cmath>

#include <config/PlatformCompat.hpp>
#include <oc/ui/lvgl/theme/BaseTheme.hpp>
#include <oc/ui/lvgl/style/StyleBuilder.hpp>
#include <ms/ui/font/CoreFonts.hpp>

#include <oc/ui/lvgl/StaticSurfaceInvalidation.hpp>
#include "ui/font/StandaloneFonts.hpp"
#include "ui/font/StandaloneIcons.hpp"
#include "ui/theme/StandaloneTheme.hpp"

namespace core::ui {

namespace theme = oc::ui::lvgl::base_theme;
namespace style = oc::ui::lvgl::style;
namespace stheme = standalone::theme;
namespace icons = standalone::icons;

namespace {

constexpr float KNOB_ARC_SWEEP_DEGREES = 270.0f;
constexpr int16_t KNOB_START_ANGLE = 135;
constexpr int16_t KNOB_END_ANGLE = 45;
constexpr float KNOB_ARC_WIDTH_RATIO = 0.11f;
constexpr lv_coord_t KNOB_MIN_ARC_WIDTH = 3;
constexpr lv_coord_t KNOB_EDGE_PAD = 2;
constexpr lv_opa_t KNOB_BACKGROUND_OPA = LV_OPA_60;
constexpr uint16_t INVALID_VALUE_ANGLE = 0xFFFF;

uint32_t automationTrackColor(bool slotActive,
                              bool automationActive,
                              bool automationRecording,
                              bool manualOverride) {
    if (!slotActive) return theme::color::KNOB_VALUE;
    if (automationRecording) return stheme::color::MACRO_AUTOMATION_RECORDING;
    if (manualOverride) return stheme::color::MACRO_AUTOMATION_MANUAL;
    return automationActive ? stheme::color::MACRO_AUTOMATION : theme::color::KNOB_VALUE;
}

float clampNormalized(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

uint16_t valueAngle(float value) {
    return static_cast<uint16_t>(
        std::round(KNOB_START_ANGLE + clampNormalized(value) * KNOB_ARC_SWEEP_DEGREES)
    );
}

}  // namespace

FLASHMEM MacroKnobWidget::MacroKnobWidget(lv_obj_t* parent) {
    createUI(parent);
}

MacroKnobWidget::~MacroKnobWidget() {
    cc_value_.reset();
    cc_prefix_.reset();
    if (container_) {
        lv_obj_delete(container_);
        container_ = nullptr;
    }
    knob_ = nullptr;
    config_label_container_ = nullptr;
    add_label_ = nullptr;
}

FLASHMEM void MacroKnobWidget::createUI(lv_obj_t* parent) {
    createContainer(parent);
    if (!container_) return;

    knob_ = lv_obj_create(container_);
    if (!knob_) return;
    style::apply(knob_).transparent().noScroll().noBorder().pad(0);
    lv_obj_remove_flag(knob_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(knob_, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_event_cb(knob_, onArcDrawEvent, LV_EVENT_DRAW_MAIN, this);

    lv_obj_set_grid_cell(knob_,
        LV_GRID_ALIGN_STRETCH, 0, 1,
        LV_GRID_ALIGN_CENTER, 0, 1);
    track_color_ = theme::color::KNOB_VALUE;

    createConfigLabels();

    add_label_ = lv_label_create(container_);
    if (!add_label_) return;
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

FLASHMEM void MacroKnobWidget::createContainer(lv_obj_t* parent) {
    if (!parent) return;

    container_ = lv_obj_create(parent);
    if (!container_) return;
    lv_obj_set_size(container_, LV_PCT(100), LV_PCT(100));
    style::apply(container_).transparent().noScroll().noBorder().pad(0);

    static const int32_t colDsc[] = {LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static const int32_t rowDsc[] = {LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    lv_obj_set_grid_dsc_array(container_, colDsc, rowDsc);
    lv_obj_set_layout(container_, LV_LAYOUT_GRID);
}

FLASHMEM void MacroKnobWidget::createConfigLabels() {
    if (!container_) return;

    config_label_container_ = lv_obj_create(container_);
    if (!config_label_container_) return;
    style::apply(config_label_container_).transparent().noScroll();
    lv_obj_set_size(config_label_container_, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_add_flag(config_label_container_, LV_OBJ_FLAG_FLOATING);
    lv_obj_align(config_label_container_, LV_ALIGN_CENTER, 0, 6);
    lv_obj_set_layout(config_label_container_, LV_LAYOUT_GRID);

    static constexpr lv_coord_t COL_WIDTH = 18;
    static const int32_t colDsc[] = {COL_WIDTH, COL_WIDTH, LV_GRID_TEMPLATE_LAST};
    static const int32_t rowDsc[] = {LV_GRID_CONTENT, LV_GRID_TEMPLATE_LAST};
    lv_obj_set_grid_dsc_array(config_label_container_, colDsc, rowDsc);
    lv_obj_set_style_pad_column(config_label_container_, 2, 0);

    cc_prefix_ = core::app::makeExtmemUnique<oc::ui::lvgl::Label>(config_label_container_);
    if (!cc_prefix_ || !cc_prefix_->getElement()) return;
    cc_prefix_->alignment(LV_TEXT_ALIGN_RIGHT)
        .color(stheme::color::MACRO_CC_COLOR)
        .autoScroll(false)
        .ownsLvglObjects(false);
    style::apply(cc_prefix_->getElement()).textOpa(stheme::color::MACRO_PREFIX_OPA);
    if (standalone_fonts.icons_12) {
        cc_prefix_->font(standalone_fonts.icons_12);
    }
    cc_prefix_->setText(icons::MIDI_CC);
    lv_obj_set_grid_cell(
        cc_prefix_->getElement(),
        LV_GRID_ALIGN_STRETCH, 0, 1,
        LV_GRID_ALIGN_CENTER, 0, 1
    );

    cc_value_ = core::app::makeExtmemUnique<oc::ui::lvgl::Label>(config_label_container_);
    if (!cc_value_ || !cc_value_->getElement()) return;
    cc_value_->alignment(LV_TEXT_ALIGN_LEFT)
        .color(stheme::color::MACRO_CC_COLOR)
        .autoScroll(false)
        .ownsLvglObjects(false);
    if (fonts.inter_13_bold) {
        cc_value_->font(fonts.inter_13_bold);
    }
    cc_value_->setText("-");
    lv_obj_set_grid_cell(
        cc_value_->getElement(),
        LV_GRID_ALIGN_STRETCH, 1, 1,
        LV_GRID_ALIGN_CENTER, 0, 1
    );
}

void MacroKnobWidget::setValue(float value) {
    current_value_ = clampNormalized(value);
    const uint16_t nextAngle = valueAngle(current_value_);
    if (rendered_value_angle_ == nextAngle) return;
    const uint16_t previousAngle = rendered_value_angle_;
    rendered_value_angle_ = nextAngle;
    invalidateArcDelta(previousAngle, nextAngle);
}

void MacroKnobWidget::setConfig(uint8_t cc) {
    if (!cc_value_ || current_cc_ == cc) return;
    cc_value_->setText(static_cast<int>(cc));
    current_cc_ = cc;
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
    const uint32_t nextColor = automationTrackColor(
        slot_active_,
        automation_active_,
        automation_recording_,
        automation_manual_override_
    );
    if (track_color_ == nextColor) return;
    track_color_ = nextColor;
    invalidateValueArc();
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
        if (slot_active_) {
            lv_obj_clear_flag(knob_, LV_OBJ_FLAG_HIDDEN);
            updateAutomationTrackColor();
        } else {
            lv_obj_add_flag(knob_, LV_OBJ_FLAG_HIDDEN);
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

void MacroKnobWidget::setConfigLabelsVisible(bool visible) {
    if (!config_label_container_ || config_labels_visible_ == visible) return;
    config_labels_visible_ = visible;
    if (visible) {
        lv_obj_clear_flag(config_label_container_, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(config_label_container_, LV_OBJ_FLAG_HIDDEN);
    }
}

bool MacroKnobWidget::buildArcGeometry(ArcGeometry& geometry) const {
    if (!knob_) return false;

    lv_area_t area{};
    lv_obj_get_coords(knob_, &area);
    const lv_coord_t width = static_cast<lv_coord_t>(area.x2 - area.x1 + 1);
    const lv_coord_t height = static_cast<lv_coord_t>(area.y2 - area.y1 + 1);
    const lv_coord_t size = std::min(width, height);
    if (size <= KNOB_EDGE_PAD * 2) return false;

    const lv_coord_t arcWidth = std::max<lv_coord_t>(
        KNOB_MIN_ARC_WIDTH,
        static_cast<lv_coord_t>(std::round(static_cast<float>(size) * KNOB_ARC_WIDTH_RATIO))
    );
    const lv_coord_t radius = static_cast<lv_coord_t>(
        std::max<lv_coord_t>(1, static_cast<lv_coord_t>(size / 2 - arcWidth / 2 - KNOB_EDGE_PAD))
    );

    geometry.center = lv_point_t{
        .x = static_cast<lv_coord_t>(area.x1 + width / 2),
        .y = static_cast<lv_coord_t>(area.y1 + height / 2),
    };
    geometry.radius = static_cast<uint16_t>(radius);
    geometry.width = arcWidth;
    return true;
}

void MacroKnobWidget::invalidateValueArc() {
    if (!knob_ || lv_obj_has_flag(knob_, LV_OBJ_FLAG_HIDDEN)) return;

    const uint16_t angle = rendered_value_angle_ == INVALID_VALUE_ANGLE
        ? valueAngle(current_value_)
        : rendered_value_angle_;
    invalidateArcRange(KNOB_START_ANGLE, angle);
}

void MacroKnobWidget::invalidateArcRange(lv_value_precise_t startAngle, lv_value_precise_t endAngle) {
    if (!knob_ || lv_obj_has_flag(knob_, LV_OBJ_FLAG_HIDDEN)) return;
    if (startAngle == endAngle) return;

    ArcGeometry geometry;
    if (!buildArcGeometry(geometry)) {
        lv_obj_invalidate(knob_);
        return;
    }

    if (startAngle > endAngle) {
        std::swap(startAngle, endAngle);
    }

    lv_area_t area{};
    lv_draw_arc_get_area(
        geometry.center.x,
        geometry.center.y,
        geometry.radius,
        startAngle,
        endAngle,
        geometry.width,
        true,
        &area
    );
    oc::ui::lvgl::invalidateStaticSurfaceArea(knob_, area);
}

void MacroKnobWidget::invalidateArcDelta(uint16_t previousAngle, uint16_t nextAngle) {
    if (previousAngle == INVALID_VALUE_ANGLE) {
        invalidateValueArc();
        return;
    }
    invalidateArcRange(previousAngle, nextAngle);
}

void MacroKnobWidget::drawArc(lv_layer_t* layer, lv_obj_t* target) const {
    if (!layer || !target) return;

    ArcGeometry geometry;
    if (!buildArcGeometry(geometry)) return;

    lv_draw_arc_dsc_t arcDsc;
    lv_draw_arc_dsc_init(&arcDsc);
    arcDsc.base.layer = layer;
    arcDsc.center = geometry.center;
    arcDsc.radius = geometry.radius;
    arcDsc.width = geometry.width;
    arcDsc.rounded = 1;

    arcDsc.color = lv_color_hex(theme::color::KNOB_BACKGROUND);
    arcDsc.opa = KNOB_BACKGROUND_OPA;
    arcDsc.start_angle = KNOB_START_ANGLE;
    arcDsc.end_angle = KNOB_END_ANGLE;
    lv_draw_arc(layer, &arcDsc);

    arcDsc.color = lv_color_hex(track_color_);
    arcDsc.opa = LV_OPA_COVER;
    arcDsc.start_angle = KNOB_START_ANGLE;
    arcDsc.end_angle = static_cast<lv_value_precise_t>(
        rendered_value_angle_ == INVALID_VALUE_ANGLE ? valueAngle(current_value_)
                                                     : rendered_value_angle_
    );
    lv_draw_arc(layer, &arcDsc);
}

void MacroKnobWidget::onArcDrawEvent(lv_event_t* event) {
    auto* self = static_cast<MacroKnobWidget*>(lv_event_get_user_data(event));
    if (!self) return;
    self->drawArc(lv_event_get_layer(event), lv_event_get_target_obj(event));
}

}  // namespace core::ui
