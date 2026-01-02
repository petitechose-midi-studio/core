#include "MacroView.hpp"

namespace Theme = oc::ui::lvgl::BaseTheme;
using oc::ui::lvgl::ParameterKnob;

MacroView::MacroView(lv_obj_t* parent, state::MacroState& state)
    : state_(state) {
    createLayout(parent);
    createMacros();
    bindToState();
}

MacroView::~MacroView() {
    // Clear subscriptions first (RAII unsubscribes)
    subscriptions_.clear();

    // Destroy macros before container
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
    subscriptions_.reserve(MACRO_COUNT * 2);

    for (uint8_t i = 0; i < MACRO_COUNT; ++i) {
        auto& slot = state_.slots[i];

        // Subscribe to value changes
        subscriptions_.push_back(
            slot.value.subscribe([this, i](float value) {
                macros_[i]->knob().setValue(value);
            })
        );

        // Subscribe to label changes
        subscriptions_.push_back(
            slot.label.subscribe([this, i](const char* text) {
                macros_[i]->label().setText(text);
            })
        );

        // Initialize UI with current state values
        macros_[i]->knob().setValue(slot.value.get());
        macros_[i]->label().setText(slot.label.get());
    }
}

void MacroView::createLayout(lv_obj_t* parent) {
    // Main container (full screen)
    container_ = lv_obj_create(parent);
    lv_obj_set_size(container_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(container_, lv_color_hex(Theme::Color::BACKGROUND), 0);
    lv_obj_set_style_bg_opa(container_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(container_, 0, 0);
    lv_obj_set_style_pad_all(container_, Theme::Layout::MARGIN_SM, 0);

    // Grid container for 4x2 layout
    grid_ = lv_obj_create(container_);
    lv_obj_set_size(grid_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(grid_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(grid_, 0, 0);
    lv_obj_set_style_pad_all(grid_, 0, 0);

    // Configure grid layout: 4 columns, 2 rows
    static lv_coord_t col_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static lv_coord_t row_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};

    lv_obj_set_grid_dsc_array(grid_, col_dsc, row_dsc);
    lv_obj_set_layout(grid_, LV_LAYOUT_GRID);
    lv_obj_set_style_pad_column(grid_, Theme::Layout::MARGIN_SM, 0);
    lv_obj_set_style_pad_row(grid_, Theme::Layout::MARGIN_SM, 0);
}

void MacroView::createMacros() {
    for (uint8_t i = 0; i < MACRO_COUNT; ++i) {
        // Calculate grid position
        uint8_t col = i % COLS;
        uint8_t row = i / COLS;

        // Create cell container for grid placement
        lv_obj_t* cell = lv_obj_create(grid_);
        lv_obj_set_style_bg_opa(cell, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(cell, 0, 0);
        lv_obj_set_style_pad_all(cell, 0, 0);
        lv_obj_set_grid_cell(cell, LV_GRID_ALIGN_STRETCH, col, 1, LV_GRID_ALIGN_STRETCH, row, 1);

        // Create ParameterKnob in cell
        macros_[i] = std::make_unique<ParameterKnob>(cell);
        configureMacro(i);
    }
}

void MacroView::configureMacro(uint8_t index) {
    auto& pk = *macros_[index];
    uint32_t color = Theme::Color::getMacroColor(index);

    // Configure knob appearance (centered = bipolar)
    pk.knob()
        .bgColor(Theme::Color::KNOB_BACKGROUND)
        .trackColor(color)
        .valueColor(Theme::Color::KNOB_VALUE)
        .flashColor(color)
        .centered(true);

    // Configure label style (value set via state binding)
    pk.label()
        .color(Theme::Color::TEXT_SECONDARY)
        .alignment(LV_TEXT_ALIGN_CENTER);

    if (fonts.parameter_label) {
        pk.label().font(fonts.parameter_label);
    }
}
