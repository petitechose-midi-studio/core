#include "SequencerView.hpp"

#include <cstdio>

#include <oc/ui/lvgl/style/StyleBuilder.hpp>

#include <ms/ui/font/CoreFonts.hpp>

namespace theme = oc::ui::lvgl::base_theme;
namespace style = oc::ui::lvgl::style;

namespace core::ui {

SequencerView::SequencerView(lv_obj_t* parent, core::state::CoreState& coreState)
    : core_state_(coreState) {
    createLayout(parent);
    createTopBar();
    createSteps();
    bindToState();
}

SequencerView::~SequencerView() {
    subscriptions_.clear();
    top_bar_.reset();
    if (container_) {
        lv_obj_delete(container_);
        container_ = nullptr;
    }
}

void SequencerView::onActivate() {
    if (container_) {
        lv_obj_clear_flag(container_, LV_OBJ_FLAG_HIDDEN);
    }
}

void SequencerView::onDeactivate() {
    if (container_) {
        lv_obj_add_flag(container_, LV_OBJ_FLAG_HIDDEN);
    }
}

void SequencerView::createLayout(lv_obj_t* parent) {
    container_ = lv_obj_create(parent);
    style::apply(container_).fullSize().pad(0).bgColor(theme::color::BACKGROUND);
    lv_obj_set_layout(container_, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(container_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(container_, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(container_, 0, LV_STATE_DEFAULT);

    top_bar_container_ = lv_obj_create(container_);
    lv_obj_set_size(top_bar_container_, LV_PCT(100), LV_SIZE_CONTENT);
    style::apply(top_bar_container_).transparent();

    body_container_ = lv_obj_create(container_);
    lv_obj_set_size(body_container_, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(body_container_, 1);
    style::apply(body_container_).transparent().pad(theme::layout::MARGIN_MD);

    lv_obj_set_layout(body_container_, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(body_container_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(body_container_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(body_container_, theme::layout::ROW_GAP_MD, 0);

    header_container_ = lv_obj_create(body_container_);
    lv_obj_set_size(header_container_, LV_PCT(100), LV_SIZE_CONTENT);
    style::apply(header_container_).transparent().flexRow(LV_FLEX_ALIGN_END, theme::layout::MARGIN_SM);

    page_label_ = lv_label_create(header_container_);
    style::apply(page_label_)
        .textFont(fonts.inter_14_medium)
        .textColor(theme::color::TEXT_SECONDARY);
}

void SequencerView::createTopBar() {
    top_bar_ = std::make_unique<TopBar>(top_bar_container_, core_state_.statusBar);
}

void SequencerView::createSteps() {
    grid_ = lv_obj_create(body_container_);
    style::apply(grid_).size(LV_PCT(100), LV_PCT(100)).transparent().noBorder();
    lv_obj_set_flex_grow(grid_, 1);

    static lv_coord_t col_dsc[] = {
        LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static lv_coord_t row_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    lv_obj_set_grid_dsc_array(grid_, col_dsc, row_dsc);
    lv_obj_set_layout(grid_, LV_LAYOUT_GRID);
    lv_obj_set_style_pad_column(grid_, theme::layout::MARGIN_SM, 0);
    lv_obj_set_style_pad_row(grid_, theme::layout::MARGIN_SM, 0);

    for (uint8_t i = 0; i < steps_.size(); ++i) {
        uint8_t col = i % 4;
        uint8_t row = i / 4;

        lv_obj_t* step = lv_obj_create(grid_);
        steps_[i] = step;

        style::apply(step)
            .bgColor(theme::color::INACTIVE)
            .radius(8)
            .noBorder()
            .pad(0);

        lv_obj_set_grid_cell(step,
            LV_GRID_ALIGN_STRETCH, col, 1,
            LV_GRID_ALIGN_STRETCH, row, 1);

        lv_obj_t* label = lv_label_create(step);
        step_labels_[i] = label;
        char buf[4];
        snprintf(buf, sizeof(buf), "%d", static_cast<int>(i) + 1);
        lv_label_set_text(label, buf);
        lv_obj_center(label);

        style::apply(label)
            .textFont(fonts.inter_14_semibold)
            .textColor(theme::color::TEXT_PRIMARY);
    }
}

void SequencerView::bindToState() {
    subscriptions_.reserve(3);

    subscriptions_.push_back(core_state_.sequencer.page.subscribe([this](uint8_t) {
        render();
    }));
    subscriptions_.push_back(core_state_.sequencer.enabledMask.subscribe([this](uint64_t) {
        render();
    }));
    subscriptions_.push_back(core_state_.sequencer.focusedStep.subscribe([this](uint8_t) {
        render();
    }));

    render();
}

void SequencerView::render() {
    if (!container_) return;

    const uint8_t pageCount = core::state::sequencer::SequencerState::PAGE_COUNT;
    const uint8_t page = (pageCount == 0)
        ? 0
        : static_cast<uint8_t>(core_state_.sequencer.page.get() % pageCount);
    const uint8_t startStep = page * core::state::sequencer::SequencerState::STEPS_PER_PAGE;
    const uint64_t mask = core_state_.sequencer.enabledMask.get();
    const uint8_t focused = core_state_.sequencer.focusedStep.get();

    if (page_label_) {
        char buf[16];
        snprintf(buf, sizeof(buf), "Page %d/%d", static_cast<int>(page) + 1, static_cast<int>(pageCount));
        lv_label_set_text(page_label_, buf);
    }

    for (uint8_t i = 0; i < steps_.size(); ++i) {
        const uint8_t abs = startStep + i;
        const bool enabled = (abs < core::state::sequencer::SequencerState::MAX_STEPS)
            ? ((mask & (1ULL << abs)) != 0)
            : false;
        const bool isFocused = (abs == focused);

        if (steps_[i]) {
            style::apply(steps_[i])
                .bgColor(enabled ? theme::color::ACTIVE : theme::color::INACTIVE)
                .border(isFocused ? 2 : 0, theme::color::TEXT_PRIMARY)
                .radius(8)
                .pad(0);
        }

        if (step_labels_[i]) {
            char buf[4];
            snprintf(buf, sizeof(buf), "%d", static_cast<int>(abs) + 1);
            lv_label_set_text(step_labels_[i], buf);

            lv_obj_set_style_text_color(step_labels_[i],
                lv_color_hex(enabled ? theme::color::BACKGROUND : theme::color::TEXT_PRIMARY), 0);
        }
    }
}

}  // namespace core::ui
