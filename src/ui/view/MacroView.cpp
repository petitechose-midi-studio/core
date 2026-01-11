#include "MacroView.hpp"

#include <oc/ui/lvgl/style/StyleBuilder.hpp>

#include <config/App.hpp>

namespace theme = oc::ui::lvgl::base_theme;
namespace style = oc::ui::lvgl::style;

namespace core::ui {

MacroView::MacroView(lv_obj_t* parent, core::state::CoreState& coreState)
    : core_state_(coreState) {
    createLayout(parent);
    createTopBar();
    createMacros();

    // Create debounce timer (synced with display refresh)
    constexpr uint32_t periodMs = 1000 / Config::Timing::LVGL_HZ;
    update_timer_ = lv_timer_create(onUpdateTimer, periodMs, this);

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
    if (container_) {
        lv_obj_delete(container_);
    }
}

void MacroView::onActivate() {
    lv_obj_clear_flag(container_, LV_OBJ_FLAG_HIDDEN);
}

void MacroView::onDeactivate() {
    lv_obj_add_flag(container_, LV_OBJ_FLAG_HIDDEN);
}

void MacroView::bindToState() {
    subscriptions_.reserve(MACRO_COUNT);

    for (uint8_t i = 0; i < MACRO_COUNT; ++i) {
        auto& slot = core_state_.macros.slots[i];

        // Subscribe to value changes (debounced via dirty flags)
        subscriptions_.push_back(
            slot.value.subscribe([this, i](float) {
                markDirty(i);
            })
        );

        // Initialize UI with current state values
        macros_[i]->setValue(slot.value.get());
        updateConfigLabel(i);
    }
}

void MacroView::updateConfigLabel(uint8_t index) {
    const auto& config = core_state_.getMacroConfig(index);
    macros_[index]->setConfig(config.channel, config.cc);
}

void MacroView::createLayout(lv_obj_t* parent) {
    // Main container (full size, flex column layout)
    container_ = lv_obj_create(parent);
    style::apply(container_).fullSize().pad(0).bgColor(theme::color::BACKGROUND);
    lv_obj_set_layout(container_, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(container_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(container_, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(container_, 0, LV_STATE_DEFAULT);

    // TopBar container (content height, for TopBar component)
    top_bar_container_ = lv_obj_create(container_);
    lv_obj_set_size(top_bar_container_, LV_PCT(100), LV_SIZE_CONTENT);
    style::apply(top_bar_container_).transparent();

    // Body container (takes remaining space, grid layout)
    body_container_ = lv_obj_create(container_);
    lv_obj_set_size(body_container_, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(body_container_, 1);
    style::apply(body_container_).transparent();

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
    }
}

void MacroView::processDirtyFlags() {
    for (uint8_t i = 0; i < MACRO_COUNT; ++i) {
        if (dirty_flags_[i]) {
            dirty_flags_[i] = false;
            macros_[i]->setValue(core_state_.macros.slots[i].value.get());
        }
    }
}

void MacroView::onUpdateTimer(lv_timer_t* timer) {
    auto* self = static_cast<MacroView*>(lv_timer_get_user_data(timer));
    if (self) {
        self->processDirtyFlags();
    }
}

}  // namespace core::ui
