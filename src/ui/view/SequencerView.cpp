#include "SequencerView.hpp"

#include <cstdio>

#include <oc/ui/lvgl/style/StyleBuilder.hpp>

#include <ms/ui/font/CoreFonts.hpp>

namespace theme = oc::ui::lvgl::base_theme;
namespace style = oc::ui::lvgl::style;

namespace core::ui {

namespace {

constexpr uint32_t COLOR_STEP_ON_HEX = theme::color::MACRO_1_RED;
constexpr uint32_t COLOR_STEP_OFF_HEX = theme::color::INACTIVE_LIGHTER;
constexpr uint32_t COLOR_STEP_PLAY_HEX = 0x5CA8EE;

constexpr uint32_t COLOR_PAGE_DISABLED_HEX = theme::color::KNOB_BACKGROUND;
constexpr uint32_t COLOR_PAGE_ACTIVE_HEX = theme::color::INACTIVE;
constexpr uint32_t COLOR_PAGE_PLAY_HEX = 0x5CA8EE;

void formatNoteName(char* buf, size_t bufSize, uint8_t midiNote) {
    static const char* NAMES[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    const int idx = static_cast<int>(midiNote) % 12;
    const int octave = static_cast<int>(midiNote) / 12 - 1;
    snprintf(buf, bufSize, "%s%d", NAMES[idx], octave);
}

}  // namespace

SequencerView::SequencerView(lv_obj_t* parent, core::state::CoreState& coreState)
    : core_state_(coreState) {
    createLayout(parent);
    createTopBar();
    createPageBar();
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
}

void SequencerView::createTopBar() {
    top_bar_ = std::make_unique<TopBar>(top_bar_container_, core_state_.statusBar);
}

void SequencerView::createPageBar() {
    page_bar_container_ = lv_obj_create(header_container_);
    lv_obj_set_size(page_bar_container_, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    style::apply(page_bar_container_).transparent().noScroll().flexRow(LV_FLEX_ALIGN_END, theme::layout::MARGIN_XS);

    constexpr lv_coord_t RECT_W = 14;
    constexpr lv_coord_t RECT_H = 6;
    constexpr lv_coord_t DOT_S = 3;

    for (uint8_t i = 0; i < page_rects_.size(); ++i) {
        lv_obj_t* r = lv_obj_create(page_bar_container_);
        page_rects_[i] = r;
        lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_size(r, RECT_W, RECT_H);
        lv_obj_set_style_radius(r, 2, 0);
        lv_obj_set_style_border_width(r, 0, 0);
        lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(r, lv_color_hex(COLOR_PAGE_DISABLED_HEX), 0);

        lv_obj_t* dot = lv_obj_create(r);
        page_focus_dots_[i] = dot;
        lv_obj_remove_style_all(dot);
        lv_obj_set_size(dot, DOT_S, DOT_S);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(dot, lv_color_hex(theme::color::TEXT_PRIMARY), 0);
        lv_obj_set_style_border_width(dot, 0, 0);
        lv_obj_align(dot, LV_ALIGN_CENTER, 0, 0);
        lv_obj_add_flag(dot, LV_OBJ_FLAG_HIDDEN);
    }
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

    constexpr lv_coord_t BUTTON_SIZE = 56;
    constexpr lv_coord_t INDICATOR_SIZE = 14;

    for (uint8_t i = 0; i < tiles_.size(); ++i) {
        uint8_t col = i % 4;
        uint8_t row = i / 4;

        lv_obj_t* tile = lv_obj_create(grid_);
        tiles_[i] = tile;
        style::apply(tile).transparent().noBorder().pad(0).noScroll();
        lv_obj_set_grid_cell(tile,
            LV_GRID_ALIGN_STRETCH, col, 1,
            LV_GRID_ALIGN_STRETCH, row, 1);

        lv_obj_set_layout(tile, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(tile, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_set_style_pad_row(tile, theme::layout::MARGIN_XS, 0);

        // Note label (top-left)
        lv_obj_t* note = lv_label_create(tile);
        note_labels_[i] = note;
        lv_label_set_text(note, "");
        lv_obj_set_width(note, LV_PCT(100));
        lv_obj_set_style_text_align(note, LV_TEXT_ALIGN_LEFT, 0);
        lv_obj_set_style_text_font(note, fonts.inter_14_semibold, 0);
        lv_obj_set_style_text_color(note, lv_color_hex(theme::color::TEXT_SECONDARY), 0);

        // Button wrapper to center the step button
        lv_obj_t* btnWrap = lv_obj_create(tile);
        lv_obj_remove_style_all(btnWrap);
        lv_obj_clear_flag(btnWrap, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_width(btnWrap, LV_PCT(100));
        lv_obj_set_flex_grow(btnWrap, 1);
        lv_obj_set_layout(btnWrap, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(btnWrap, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(btnWrap, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        // Step button (gray) + small state indicator
        lv_obj_t* btn = lv_obj_create(btnWrap);
        step_buttons_[i] = btn;
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_size(btn, BUTTON_SIZE, BUTTON_SIZE);
        lv_obj_set_style_radius(btn, 10, 0);
        lv_obj_set_style_border_width(btn, 0, 0);
        lv_obj_set_style_border_color(btn, lv_color_hex(theme::color::TEXT_PRIMARY), 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(theme::color::INACTIVE), 0);
        lv_obj_set_style_pad_all(btn, 0, 0);

        lv_obj_t* ind = lv_obj_create(btn);
        step_indicators_[i] = ind;
        lv_obj_clear_flag(ind, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_size(ind, INDICATOR_SIZE, INDICATOR_SIZE);
        lv_obj_set_style_radius(ind, 3, 0);
        lv_obj_set_style_border_width(ind, 0, 0);
        lv_obj_set_style_bg_opa(ind, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(ind, lv_color_hex(COLOR_STEP_OFF_HEX), 0);
        lv_obj_align(ind, LV_ALIGN_BOTTOM_MID, 0, -4);
    }
}

void SequencerView::bindToState() {
    subscriptions_.reserve(5);

    subscriptions_.push_back(core_state_.sequencer.length.subscribe([this](uint8_t) { render(); }));
    subscriptions_.push_back(core_state_.sequencer.page.subscribe([this](uint8_t) { render(); }));
    subscriptions_.push_back(core_state_.sequencer.enabledMask.subscribe([this](uint64_t) { render(); }));
    subscriptions_.push_back(core_state_.sequencer.focusedStep.subscribe([this](uint8_t) { render(); }));
    subscriptions_.push_back(core_state_.sequencer.playheadStep.subscribe([this](int16_t) { render(); }));

    render();
}

void SequencerView::render() {
    if (!container_) return;

    const uint8_t len = core_state_.sequencer.length.get();
    constexpr uint8_t stepsPerPage = core::state::sequencer::SequencerState::STEPS_PER_PAGE;

    const uint8_t activePages = (len == 0)
        ? 0
        : static_cast<uint8_t>((len + stepsPerPage - 1) / stepsPerPage);
    const uint8_t page = (activePages == 0)
        ? 0
        : static_cast<uint8_t>(core_state_.sequencer.page.get() % activePages);

    const uint8_t startStep = static_cast<uint8_t>(page * stepsPerPage);
    const uint64_t mask = core_state_.sequencer.enabledMask.get();
    const uint8_t focused = core_state_.sequencer.focusedStep.get();
    const int16_t playhead = core_state_.sequencer.playheadStep.get();
    const int16_t playingPage = (playhead >= 0 && playhead < len)
        ? static_cast<int16_t>(playhead / stepsPerPage)
        : -1;

    // Page bar (8 rectangles)
    for (uint8_t p = 0; p < page_rects_.size(); ++p) {
        const uint8_t pageStart = static_cast<uint8_t>(p * stepsPerPage);
        const bool hasSteps = (pageStart < len);

        bool anyEnabled = false;
        if (hasSteps) {
            const uint8_t end = (len < static_cast<uint8_t>(pageStart + stepsPerPage))
                ? len
                : static_cast<uint8_t>(pageStart + stepsPerPage);
            for (uint8_t s = pageStart; s < end; ++s) {
                if ((mask & (1ULL << s)) != 0) {
                    anyEnabled = true;
                    break;
                }
            }
        }

        const bool isPlaying = (playingPage >= 0) && (p == static_cast<uint8_t>(playingPage));
        const uint32_t fill = isPlaying
            ? COLOR_PAGE_PLAY_HEX
            : (anyEnabled ? COLOR_PAGE_ACTIVE_HEX : COLOR_PAGE_DISABLED_HEX);

        if (page_rects_[p]) {
            lv_obj_set_style_bg_color(page_rects_[p], lv_color_hex(fill), 0);
            lv_obj_set_style_bg_opa(page_rects_[p], LV_OPA_COVER, 0);
        }

        if (page_focus_dots_[p]) {
            const bool isFocusedPage = (activePages > 0) && (p == page);
            if (isFocusedPage) {
                lv_obj_clear_flag(page_focus_dots_[p], LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(page_focus_dots_[p], LV_OBJ_FLAG_HIDDEN);
            }
        }
    }

    // Step tiles (8 visible)
    for (uint8_t i = 0; i < tiles_.size(); ++i) {
        const uint8_t abs = static_cast<uint8_t>(startStep + i);
        const bool inPattern = (abs < len);
        const bool enabled = inPattern ? ((mask & (1ULL << abs)) != 0) : false;
        const bool isFocused = inPattern && (abs == focused);
        const bool isPlaying = inPattern && (playhead >= 0) && (abs == static_cast<uint8_t>(playhead));

        // Note label
        if (note_labels_[i]) {
            if (inPattern) {
                char buf[8];
                formatNoteName(buf, sizeof(buf), core_state_.sequencer.note[abs]);
                lv_label_set_text(note_labels_[i], buf);
            } else {
                lv_label_set_text(note_labels_[i], " ");
            }
        }

        // Step button (gray)
        if (step_buttons_[i]) {
            lv_obj_set_style_border_width(step_buttons_[i], isFocused ? 2 : 0, 0);

            const uint32_t bg = inPattern ? theme::color::INACTIVE : theme::color::KNOB_BACKGROUND;
            lv_obj_set_style_bg_color(step_buttons_[i], lv_color_hex(bg), 0);
            lv_obj_set_style_bg_opa(step_buttons_[i], inPattern ? LV_OPA_COVER : theme::opacity::OPA_50, 0);
        }

        // State indicator
        if (step_indicators_[i]) {
            if (!inPattern) {
                lv_obj_add_flag(step_indicators_[i], LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_clear_flag(step_indicators_[i], LV_OBJ_FLAG_HIDDEN);
            }

            if (!inPattern) {
                // Hidden
            } else if (isPlaying) {
                lv_obj_set_style_bg_color(step_indicators_[i], lv_color_hex(COLOR_STEP_PLAY_HEX), 0);
                lv_obj_set_style_bg_opa(step_indicators_[i], LV_OPA_COVER, 0);
            } else if (enabled) {
                lv_obj_set_style_bg_color(step_indicators_[i], lv_color_hex(COLOR_STEP_ON_HEX), 0);
                lv_obj_set_style_bg_opa(step_indicators_[i], LV_OPA_COVER, 0);
            } else {
                lv_obj_set_style_bg_color(step_indicators_[i], lv_color_hex(COLOR_STEP_OFF_HEX), 0);
                lv_obj_set_style_bg_opa(step_indicators_[i], LV_OPA_COVER, 0);
            }
        }
    }
}

}  // namespace core::ui
