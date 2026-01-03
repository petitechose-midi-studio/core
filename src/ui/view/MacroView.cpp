#include "MacroView.hpp"

namespace Theme = oc::ui::lvgl::BaseTheme;

MacroView::MacroView(lv_obj_t* parent, state::CoreState& coreState)
    : coreState_(coreState) {
    createLayout(parent);
    createMacros();
    bindToState();
}

MacroView::~MacroView() {
    subscriptions_.clear();
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
        auto& slot = coreState_.macros.slots[i];

        // Subscribe to value changes
        subscriptions_.push_back(
            slot.value.subscribe([this, i](float value) {
                macros_[i]->setValue(value);
            })
        );

        // Initialize UI with current state values
        macros_[i]->setValue(slot.value.get());
        updateConfigLabel(i);
    }
}

void MacroView::updateConfigLabel(uint8_t index) {
    const auto& config = coreState_.getMacroConfig(index);
    macros_[index]->setConfig(config.channel, config.cc);
}

void MacroView::createLayout(lv_obj_t* parent) {
    // Main container (full screen, no padding for maximum widget space)
    container_ = lv_obj_create(parent);
    lv_obj_set_size(container_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(container_, lv_color_hex(Theme::Color::BACKGROUND), 0);
    lv_obj_set_style_bg_opa(container_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(container_, 0, 0);
    lv_obj_set_style_pad_all(container_, 0, 0);

    // Grid container for 4x2 layout
    grid_ = lv_obj_create(container_);
    lv_obj_set_size(grid_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(grid_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(grid_, 0, 0);
    lv_obj_set_style_pad_all(grid_, 0, 0);

    // Configure grid layout: 4 columns, 2 rows (no gaps for maximum widget size)
    static lv_coord_t col_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static lv_coord_t row_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};

    lv_obj_set_grid_dsc_array(grid_, col_dsc, row_dsc);
    lv_obj_set_layout(grid_, LV_LAYOUT_GRID);
    lv_obj_set_style_pad_column(grid_, 0, 0);  // No column gap
    lv_obj_set_style_pad_row(grid_, 0, 0);     // No row gap
}

void MacroView::createMacros() {
    for (uint8_t i = 0; i < MACRO_COUNT; ++i) {
        uint8_t col = i % COLS;
        uint8_t row = i / COLS;

        // Create macro widget directly in grid
        macros_[i] = std::make_unique<ui::MacroKnobWidget>(grid_, i);

        // Position in grid (widget container handles internal layout)
        lv_obj_set_grid_cell(macros_[i]->getElement(),
            LV_GRID_ALIGN_STRETCH, col, 1,
            LV_GRID_ALIGN_STRETCH, row, 1);
    }
}
