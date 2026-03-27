#include "MacroKnobWidget.hpp"

#include <Arduino.h>

#include <oc/log/Log.hpp>
#include <oc/ui/lvgl/style/StyleBuilder.hpp>
#include <oc/ui/lvgl/theme/BaseTheme.hpp>

#include "ui/theme/StandaloneTheme.hpp"

namespace core::ui {

namespace theme = oc::ui::lvgl::base_theme;
namespace style = oc::ui::lvgl::style;

namespace {

struct MacroKnobProfiling {
    uint32_t window_start_ms = 0;
    uint32_t call_count = 0;
    uint32_t total_us = 0;
    uint32_t max_us = 0;

    void record(uint32_t elapsed_us) {
        const uint32_t now = millis();
        if (window_start_ms == 0) {
            window_start_ms = now;
        }

        call_count += 1;
        total_us += elapsed_us;
        max_us = std::max(max_us, elapsed_us);

        if ((now - window_start_ms) < 500) return;

        const uint32_t avg_us = call_count > 0 ? (total_us / call_count) : 0;
        if (max_us >= 1000 || avg_us >= 500) {
            OC_LOG_INFO("[Perf][MacroKnob] calls={} avg={}us max={}us",
                        call_count,
                        avg_us,
                        max_us);
        }

        window_start_ms = now;
        call_count = 0;
        total_us = 0;
        max_us = 0;
    }
};

MacroKnobProfiling g_macro_knob_profiling;

}  // namespace

MacroKnobWidget::MacroKnobWidget(lv_obj_t* parent, uint8_t index)
    : BaseMacroWidget(index) {
    createUI(parent);
}

MacroKnobWidget::~MacroKnobWidget() {
    knob_.reset();
    // BaseMacroWidget destructor handles labels and container
}

void MacroKnobWidget::createUI(lv_obj_t* parent) {
    createContainerWithGrid(parent);

    // KnobWidget - stretch horizontally, CONTENT row sizes to knob height
    knob_ = std::make_unique<oc::ui::lvgl::KnobWidget>(container_);
    knob_
        ->sizeMode(oc::ui::lvgl::SizeMode::SquareFromWidth)  // Explicit: height = width
        .centered(false)
        .bgColor(theme::color::KNOB_BACKGROUND)
        .trackColor(theme::color::getMacroColor(index_))
        .valueColor(theme::color::KNOB_VALUE)
        .flashColor(theme::color::getMacroColor(index_))
        .flashEnabled(false);
    lv_obj_set_grid_cell(knob_->getElement(),
        LV_GRID_ALIGN_STRETCH, 0, 1,  // Horizontal: stretch to get width
        LV_GRID_ALIGN_START, 0, 1);   // Vertical: start in CONTENT row

    // Floating labels (can now be sibling since KnobWidget no longer subtracts sibling height)
    createConfigLabels(container_);
}

void MacroKnobWidget::setValue(float value) {
    if (knob_) {
        const uint32_t start_us = micros();
        knob_->setValue(value);
        g_macro_knob_profiling.record(micros() - start_us);
    }
}

}  // namespace core::ui
