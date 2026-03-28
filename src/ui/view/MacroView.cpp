#include "MacroView.hpp"

#include <Arduino.h>

#include <oc/log/Log.hpp>
#include <config/App.hpp>
#include <config/PlatformCompat.hpp>

#include "ui/view/MacroViewModelBuilder.hpp"
#include "ui/widget/MacroKnobWidget.hpp"

namespace core::ui {

namespace {

struct MacroRenderProfiling {
    uint32_t window_start_ms = 0;
    uint32_t pass_count = 0;
    uint32_t total_us = 0;
    uint32_t max_us = 0;
    uint32_t total_value_updates = 0;
    uint32_t total_config_updates = 0;

    void record(uint32_t elapsed_us, uint32_t value_updates, uint32_t config_updates) {
        const uint32_t now = millis();
        if (window_start_ms == 0) {
            window_start_ms = now;
        }

        pass_count += 1;
        total_us += elapsed_us;
        max_us = std::max(max_us, elapsed_us);
        total_value_updates += value_updates;
        total_config_updates += config_updates;

        if ((now - window_start_ms) < 500) return;

        const uint32_t avg_us = pass_count > 0 ? (total_us / pass_count) : 0;
        if (max_us >= 2000 || avg_us >= 1000) {
            OC_LOG_INFO("[Perf][MacroView] passes={} avg={}us max={}us valueUpdates={} configUpdates={}",
                        pass_count,
                        avg_us,
                        max_us,
                        total_value_updates,
                        total_config_updates);
        }

        window_start_ms = now;
        pass_count = 0;
        total_us = 0;
        max_us = 0;
        total_value_updates = 0;
        total_config_updates = 0;
    }
};

MacroRenderProfiling g_macro_render_profiling;

}  // namespace

MacroView::MacroView(lv_obj_t* parent, core::state::CoreState& coreState)
    : core_state_(coreState) {
    createLayout(parent);
    createTopBar();
    createMacros();

    // Macro rendering is sampled at the global LVGL cadence.
    constexpr uint32_t targetHz = Config::Timing::LVGL_HZ;
    constexpr uint32_t periodMs = (targetHz > 1000)
        ? 1
        : ((1000 + targetHz - 1) / targetHz);
    update_timer_ = lv_timer_create(onUpdateTimer, periodMs, this);
    if (update_timer_) {
        lv_timer_pause(update_timer_);
    }

    bindToState();
}

MacroView::~MacroView() {
    if (update_timer_) {
        lv_timer_delete(update_timer_);
        update_timer_ = nullptr;
    }
    subscriptions_.clear();
    top_bar_.reset();
    for (auto& macro : macros_) {
        macro.reset();
    }

    // Destroy the LVGL tree last (child widgets must be gone first)
    layout_.reset();
    container_ = nullptr;
    top_bar_container_ = nullptr;
    body_container_ = nullptr;
}

void MacroView::onActivate() {
    if (container_) {
        lv_obj_clear_flag(container_, LV_OBJ_FLAG_HIDDEN);
        requestTopBarRender();
        processDirtyFlags();
    }
}

void MacroView::onDeactivate() {
    if (container_) {
        lv_obj_add_flag(container_, LV_OBJ_FLAG_HIDDEN);
    }

    if (update_timer_) {
        lv_timer_pause(update_timer_);
    }
}

FLASHMEM void MacroView::bindToState() {
    subscriptions_.reserve(MACRO_COUNT + 2);

    subscriptions_.push_back(
        core_state_.configRevision.subscribe([this](uint32_t) {
            markAllConfigDirty();
        })
    );

    subscriptions_.push_back(
        core_state_.statusBar.pageName.subscribe([this](const char*) {
            requestTopBarRender();
        })
    );

    for (uint8_t i = 0; i < MACRO_COUNT; ++i) {
        auto& slot = core_state_.macros.slots[i];

        subscriptions_.push_back(
            slot.value.subscribe([this, i](float) {
                markDirty(i);
            })
        );
    }

    top_bar_dirty_ = true;
    markAllDirty();
    processDirtyFlags();
}

FLASHMEM void MacroView::createLayout(lv_obj_t* parent) {
    layout_ = std::make_unique<ms::ui::LayoutView>(parent);
    container_ = layout_->getElement();
    top_bar_container_ = layout_->header();
    body_container_ = layout_->content();

    // Configure grid layout: 4 columns, 2 rows (no gaps for maximum widget size)
    static lv_coord_t col_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static lv_coord_t row_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};

    lv_obj_set_grid_dsc_array(body_container_, col_dsc, row_dsc);
    lv_obj_set_layout(body_container_, LV_LAYOUT_GRID);
    lv_obj_set_style_pad_column(body_container_, 0, 0);
    lv_obj_set_style_pad_row(body_container_, 0, 0);
}

FLASHMEM void MacroView::createTopBar() {
    top_bar_ = std::make_unique<TopBar>(top_bar_container_);
}

FLASHMEM void MacroView::createMacros() {
    for (uint8_t i = 0; i < MACRO_COUNT; ++i) {
        uint8_t col = i % COLS;
        uint8_t row = i / COLS;

        // Create macro widget directly in body grid
        macros_[i] = std::make_unique<MacroKnobWidget>(body_container_, i);

        // Position in grid (widget container handles internal layout)
        lv_obj_set_grid_cell(macros_[i]->getElement(),
            LV_GRID_ALIGN_STRETCH, col, 1,
            LV_GRID_ALIGN_STRETCH, row, 1);
    }
}

void MacroView::scheduleUpdate() {
    if (update_timer_) {
        lv_timer_resume(update_timer_);
    }
}

void MacroView::pauseUpdateIfIdle() {
    if (!update_timer_) return;
    if (has_dirty_ || top_bar_dirty_) return;
    lv_timer_pause(update_timer_);
}

void MacroView::requestTopBarRender() {
    top_bar_dirty_ = true;
    scheduleUpdate();
}

// =============================================================================
// Debounced Update System
// =============================================================================

void MacroView::markAllDirty() {
    dirty_flags_.fill(true);
    config_dirty_flags_.fill(true);
    has_dirty_ = true;
    scheduleUpdate();
}

void MacroView::markAllConfigDirty() {
    config_dirty_flags_.fill(true);
    has_dirty_ = true;
    scheduleUpdate();
}

void MacroView::markDirty(uint8_t index) {
    if (index < MACRO_COUNT) {
        dirty_flags_[index] = true;
        has_dirty_ = true;
        scheduleUpdate();
    }
}

void MacroView::processDirtyFlags() {
    const uint32_t start_us = micros();
    if (!container_ || lv_obj_has_flag(container_, LV_OBJ_FLAG_HIDDEN)) {
        pauseUpdateIfIdle();
        return;
    }

    if (top_bar_dirty_ && top_bar_) {
        top_bar_->render(buildMacroTopBarProps(core_state_));
        top_bar_dirty_ = false;
    }

    if (!has_dirty_) {
        pauseUpdateIfIdle();
        return;
    }

    uint32_t value_updates = 0;
    uint32_t config_updates = 0;
    for (uint8_t i = 0; i < MACRO_COUNT; ++i) {
        if (dirty_flags_[i] || config_dirty_flags_[i]) {
            if (macros_[i]) {
                if (dirty_flags_[i]) {
                    macros_[i]->setValue(core::state::macro::MacroWorkflow::runtimeValue(core_state_, i));
                    dirty_flags_[i] = false;
                    value_updates += 1;
                }
                if (config_dirty_flags_[i]) {
                    const auto& config = core::state::macro::MacroWorkflow::activeConfig(core_state_, i);
                    macros_[i]->setConfig(config.channel, config.cc);
                    config_dirty_flags_[i] = false;
                    config_updates += 1;
                }
            }
        }
    }

    has_dirty_ = false;
    pauseUpdateIfIdle();
    g_macro_render_profiling.record(micros() - start_us, value_updates, config_updates);
}

void MacroView::onUpdateTimer(lv_timer_t* timer) {
    auto* self = static_cast<MacroView*>(lv_timer_get_user_data(timer));
    if (self) {
        self->processDirtyFlags();
    }
}

}  // namespace core::ui
