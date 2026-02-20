#include "MacroView.hpp"

#include <config/App.hpp>

namespace core::ui {

MacroView::MacroView(lv_obj_t* parent, core::state::CoreState& coreState)
    : core_state_(coreState) {
    createLayout(parent);
    createTopBar();
    createMacros();

    // Coalesce value redraws to a practical UI cadence.
    // 120 Hz is enough for smooth knob motion while avoiding unnecessary
    // per-frame work at very high internal refresh rates.
    constexpr uint32_t MAX_MACRO_UI_HZ = 120;
    constexpr uint32_t targetHz =
        (Config::Timing::LVGL_HZ > MAX_MACRO_UI_HZ) ? MAX_MACRO_UI_HZ : Config::Timing::LVGL_HZ;
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

void MacroView::bindToState() {
    subscriptions_.reserve(MACRO_COUNT + 1);

    // Refresh CH/CC labels when config or page changes
    subscriptions_.push_back(
        core_state_.configRevision.subscribe([this](uint32_t) {
            for (uint8_t i = 0; i < MACRO_COUNT; ++i) {
                updateConfigLabel(i);
            }
        })
    );

    for (uint8_t i = 0; i < MACRO_COUNT; ++i) {
        auto& slot = core_state_.macros.slots[i];

        // Subscribe to value changes (debounced via dirty flags)
        subscriptions_.push_back(
            slot.value.subscribe([this, i](float) {
                markDirty(i);
            })
        );

        // Initialize UI with current state values
        if (macros_[i]) {
            macros_[i]->setValue(slot.value.get());
            updateConfigLabel(i);
        }
    }
}

void MacroView::updateConfigLabel(uint8_t index) {
    const auto& config = core_state_.getMacroConfig(index);
    if (index < MACRO_COUNT && macros_[index]) {
        macros_[index]->setConfig(config.channel, config.cc);
    }
}

void MacroView::createLayout(lv_obj_t* parent) {
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

void MacroView::createTopBar() {
    top_bar_ = std::make_unique<TopBar>(top_bar_container_, core_state_.statusBar);
}

void MacroView::createMacros() {
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

// =============================================================================
// Debounced Update System
// =============================================================================

void MacroView::markDirty(uint8_t index) {
    if (index < MACRO_COUNT) {
        dirty_flags_[index] = true;
        has_dirty_ = true;

        if (update_timer_) {
            lv_timer_resume(update_timer_);
            lv_timer_ready(update_timer_);
        }
    }
}

void MacroView::processDirtyFlags() {
    if (!container_ || lv_obj_has_flag(container_, LV_OBJ_FLAG_HIDDEN)) {
        if (update_timer_) {
            lv_timer_pause(update_timer_);
        }
        return;
    }

    if (!has_dirty_) {
        if (update_timer_) {
            lv_timer_pause(update_timer_);
        }
        return;
    }

    for (uint8_t i = 0; i < MACRO_COUNT; ++i) {
        if (dirty_flags_[i]) {
            dirty_flags_[i] = false;
            if (macros_[i]) {
                macros_[i]->setValue(core_state_.macros.slots[i].value.get());
            }
        }
    }

    has_dirty_ = false;

    if (update_timer_) {
        lv_timer_pause(update_timer_);
    }
}

void MacroView::onUpdateTimer(lv_timer_t* timer) {
    auto* self = static_cast<MacroView*>(lv_timer_get_user_data(timer));
    if (self) {
        self->processDirtyFlags();
    }
}

}  // namespace core::ui
