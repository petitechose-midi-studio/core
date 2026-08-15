#include "ui/interaction/TextKeyboardView.hpp"

#include <cstring>

#include <config/PlatformCompat.hpp>
#include <ms/ui/font/CoreFonts.hpp>
#include <oc/ui/lvgl/style/StyleBuilder.hpp>

#include "ui/font/StandaloneIcons.hpp"
#include "ui/interaction/InteractiveSurfaceVisual.hpp"
#include "ui/theme/StandaloneTheme.hpp"

namespace core::ui::interaction {

namespace {

namespace style = oc::ui::lvgl::style;
namespace theme = standalone::theme;

constexpr lv_coord_t KEYBOARD_KEY_W = 25;
constexpr lv_coord_t KEYBOARD_KEY_H = 24;
constexpr lv_coord_t KEYBOARD_KEY_GAP = 3;
constexpr lv_coord_t KEYBOARD_GRID_X = 5;
constexpr lv_coord_t KEYBOARD_GRID_Y = 38;
constexpr lv_coord_t KEYBOARD_ROW_CENTER_OFFSET =
    (KEYBOARD_KEY_W + KEYBOARD_KEY_GAP) / 2;
constexpr lv_coord_t KEYBOARD_LABEL_Y_OFFSET = 0;

FLASHMEM void setLabelTextIfChanged(lv_obj_t* label, const char* text) {
    if (!label) return;
    const char* next = text ? text : "";
    const char* current = lv_label_get_text(label);
    if (current && std::strcmp(current, next) == 0) return;
    lv_label_set_text(label, next);
}

FLASHMEM void configureKeyLabel(lv_obj_t* label, const char* text) {
    if (!label) return;

    lv_obj_set_style_text_font(label, fonts.compact_label(), 0);
    lv_label_set_text(label, text ? text : "");
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, KEYBOARD_LABEL_Y_OFFSET);
}

FLASHMEM bool isLetter(char character) {
    return character >= 'a' && character <= 'z';
}

FLASHMEM char shiftedCharacter(char character) {
    return isLetter(character)
        ? static_cast<char>(character - 'a' + 'A')
        : character;
}

FLASHMEM void setKeyTextStyle(lv_obj_t* label, bool selected) {
    if (!label) return;
    lv_obj_set_style_text_font(
        label,
        selected ? fonts.compact_selected() : fonts.compact_label(),
        0
    );
    lv_obj_set_style_text_color(
        label,
        lv_color_hex(
            selected ? theme::color::TEXT_PRIMARY
                     : theme::color::TEXT_SECONDARY
        ),
        0
    );
    lv_obj_set_style_text_opa(
        label,
        selected ? LV_OPA_COVER : LV_OPA_70,
        0
    );
}

}  // namespace

FLASHMEM TextKeyboardView::TextKeyboardView(lv_obj_t* parent) {
    createLayout(parent);
    if (!container_ || !title_ || !meta_ || !name_box_ || !name_label_) {
        return;
    }
    for (uint8_t i = 0; i < keys_.size(); ++i) {
        const auto& widgets = keys_[i];
        const auto& cell =
            core::state::interaction::textKeyboardCellAt(i);
        if (!widgets.label ||
            (isLetter(cell.character) && !widgets.shiftLabel)) {
            return;
        }
    }
    initialized_ = true;
}

FLASHMEM void TextKeyboardView::createLayout(lv_obj_t* parent) {
    if (!parent) return;

    container_ = lv_obj_create(parent);
    if (!container_) return;
    style::apply(container_)
        .size(LV_PCT(100), LV_PCT(100))
        .transparent()
        .noBorder()
        .pad(0)
        .noScroll();
    lv_obj_add_flag(container_, LV_OBJ_FLAG_FLOATING);
    lv_obj_add_flag(container_, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_add_flag(container_, LV_OBJ_FLAG_HIDDEN);

    title_ = lv_label_create(container_);
    if (!title_) return;
    lv_label_set_text(title_, "");
    lv_obj_set_pos(title_, 10, 4);
    lv_obj_set_size(title_, 78, 24);
    lv_obj_set_style_text_font(title_, fonts.context_title(), 0);
    lv_obj_set_style_text_color(
        title_,
        lv_color_hex(theme::color::TEXT_PRIMARY),
        0
    );
    lv_label_set_long_mode(title_, LV_LABEL_LONG_CLIP);

    meta_ = lv_label_create(container_);
    if (!meta_) return;
    lv_label_set_text(meta_, "");
    lv_obj_set_pos(meta_, 224, 5);
    lv_obj_set_size(meta_, 88, 20);
    lv_obj_set_style_text_font(meta_, fonts.meta_label(), 0);
    lv_obj_set_style_text_color(
        meta_,
        lv_color_hex(theme::color::MACRO_2),
        0
    );
    lv_obj_set_style_text_opa(meta_, LV_OPA_90, 0);
    lv_obj_set_style_text_align(meta_, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_long_mode(meta_, LV_LABEL_LONG_DOT);

    name_box_ = lv_obj_create(container_);
    if (!name_box_) return;
    style::apply(name_box_).transparent().noBorder().pad(0).noScroll();
    lv_obj_set_pos(name_box_, 90, 2);
    lv_obj_set_size(name_box_, 130, 26);
    lv_obj_set_style_radius(
        name_box_, theme::layout::INTERACTIVE_SURFACE_RADIUS, 0
    );
    lv_obj_set_style_bg_color(
        name_box_,
        lv_color_hex(theme::color::SURFACE_RAISED),
        0
    );
    lv_obj_set_style_bg_opa(name_box_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(
        name_box_, theme::layout::INTERACTIVE_SURFACE_BORDER_WIDTH, 0
    );
    lv_obj_set_style_border_color(
        name_box_,
        lv_color_hex(theme::color::BORDER_SUBTLE),
        0
    );
    lv_obj_set_style_border_opa(name_box_, LV_OPA_COVER, 0);

    name_label_ = lv_label_create(name_box_);
    if (!name_label_) return;
    lv_label_set_text(name_label_, "");
    lv_obj_set_pos(name_label_, 6, 2);
    lv_obj_set_size(name_label_, 118, 18);
    lv_obj_set_style_text_font(name_label_, fonts.primary_value(), 0);
    lv_obj_set_style_text_color(
        name_label_,
        lv_color_hex(theme::color::TEXT_PRIMARY),
        0
    );
    lv_label_set_long_mode(name_label_, LV_LABEL_LONG_DOT);

    for (uint8_t i = 0; i < keys_.size(); ++i) {
        const auto& cell =
            core::state::interaction::textKeyboardCellAt(i);
        const lv_coord_t centeredOffset =
            (cell.row == 2 || cell.row == 3)
                ? KEYBOARD_ROW_CENTER_OFFSET
                : 0;
        const lv_coord_t x = static_cast<lv_coord_t>(
            KEYBOARD_GRID_X + centeredOffset +
            cell.column * (KEYBOARD_KEY_W + KEYBOARD_KEY_GAP)
        );
        const lv_coord_t y = static_cast<lv_coord_t>(
            KEYBOARD_GRID_Y +
            cell.row * (KEYBOARD_KEY_H + KEYBOARD_KEY_GAP)
        );
        const lv_coord_t width = static_cast<lv_coord_t>(
            cell.columnSpan * KEYBOARD_KEY_W +
            (cell.columnSpan - 1U) * KEYBOARD_KEY_GAP
        );

        auto& widgets = keys_[i];
        lv_obj_t* keyContainer = lv_obj_create(container_);
        if (!keyContainer) return;
        style::apply(keyContainer)
            .transparent()
            .noBorder()
            .pad(0)
            .noScroll();
        lv_obj_set_pos(keyContainer, x, y);
        lv_obj_set_size(keyContainer, width, KEYBOARD_KEY_H);
        lv_obj_set_style_radius(
            keyContainer, theme::layout::INTERACTIVE_SURFACE_RADIUS, 0
        );
        lv_obj_set_style_border_width(
            keyContainer,
            theme::layout::INTERACTIVE_SURFACE_BORDER_WIDTH,
            0
        );

        widgets.label = lv_label_create(keyContainer);
        if (!widgets.label) return;
        configureKeyLabel(widgets.label, cell.label);
        if (isLetter(cell.character)) {
            char shiftedText[2] = {
                shiftedCharacter(cell.character),
                '\0'
            };
            widgets.shiftLabel = lv_label_create(keyContainer);
            if (!widgets.shiftLabel) return;
            configureKeyLabel(widgets.shiftLabel, shiftedText);
            lv_obj_add_flag(widgets.shiftLabel, LV_OBJ_FLAG_HIDDEN);
        }
        renderKey(i, false);
    }
}

void TextKeyboardView::render(
    const TextKeyboardViewProps& props
) {
    if (!initialized_) return;
    setVisible(props.visible);
    if (!props.visible) return;

    setLabelTextIfChanged(title_, props.title);
    setLabelTextIfChanged(meta_, props.meta);
    setLabelTextIfChanged(name_label_, props.name);

    if (rendered_shift_ != props.shiftActive) {
        applyShiftVisibility(props.shiftActive);
        rendered_shift_ = props.shiftActive;
    }

    if (rendered_selected_ != props.selectedKey) {
        if (rendered_selected_ < keys_.size()) {
            renderKey(rendered_selected_, false);
        }
        renderKey(props.selectedKey, true);
        rendered_selected_ = props.selectedKey;
    }
}

void TextKeyboardView::renderKey(
    uint8_t index,
    bool selected
) {
    if (index >= keys_.size()) return;

    auto& widgets = keys_[index];
    if (!widgets.label) return;
    lv_obj_t* keyContainer = lv_obj_get_parent(widgets.label);
    if (!keyContainer) return;

    const auto state = selected
        ? InteractiveSurfaceState::FOCUSED
        : InteractiveSurfaceState::IDLE;
    applyInteractiveSurfaceChrome(keyContainer, state);
    setKeyTextStyle(widgets.label, selected);
    setKeyTextStyle(widgets.shiftLabel, selected);
}

FLASHMEM void TextKeyboardView::applyShiftVisibility(bool shiftActive) {
    for (auto& widgets : keys_) {
        if (!widgets.label || !widgets.shiftLabel) continue;
        if (shiftActive) {
            lv_obj_add_flag(widgets.label, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(widgets.shiftLabel, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_clear_flag(widgets.label, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(widgets.shiftLabel, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

FLASHMEM void TextKeyboardView::setVisible(bool visible) {
    if (!container_ || visible_ == visible) return;
    visible_ = visible;
    if (visible) {
        lv_obj_clear_flag(container_, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    if (rendered_shift_) applyShiftVisibility(false);
    lv_obj_add_flag(container_, LV_OBJ_FLAG_HIDDEN);
    rendered_selected_ =
        core::state::interaction::TEXT_KEYBOARD_CELL_COUNT;
    rendered_shift_ = false;
}

FLASHMEM ContextActionStripProps
TextKeyboardView::leftActionStripProps(
    bool visible,
    bool shiftActive
) {
    ContextActionStripProps props;
    props.visible = visible;
    if (!visible) return props;
    props.slots[1] = core::ui::makeStandaloneIconStripSlot(
        standalone::icons::MODIFIER_SHIFT,
        shiftActive
            ? ContextActionStripVisualState::ACTIVE
            : ContextActionStripVisualState::DIM,
        ContextActionStripTone::NEUTRAL,
        standalone::icons::Size::M
    );
    props.slots[2] = core::ui::makeStandaloneIconStripSlot(
        standalone::icons::ACTION_CLEAR,
        ContextActionStripVisualState::ACTIVE,
        ContextActionStripTone::DESTRUCTIVE,
        standalone::icons::Size::S
    );
    return props;
}

FLASHMEM ContextActionStripProps
TextKeyboardView::bottomActionStripProps(bool visible, bool playing) {
    ContextActionStripProps props;
    props.visible = visible;
    if (!visible) return props;
    props.slots[0] = core::ui::makeStandaloneIconStripSlot(
        standalone::icons::ACTION_BACKWARD,
        ContextActionStripVisualState::ACTIVE,
        ContextActionStripTone::WARNING
    );
    props.slots[1] = core::ui::makeStandaloneIconStripSlot(
        standalone::icons::TRANSPORT_PLAY,
        ContextActionStripVisualState::ACTIVE,
        playing
            ? ContextActionStripTone::WARNING
            : ContextActionStripTone::POSITIVE,
        standalone::icons::Size::M
    );
    props.slots[2] = core::ui::makeStandaloneIconStripSlot(
        standalone::icons::ACTION_VALIDATE,
        ContextActionStripVisualState::ACTIVE,
        ContextActionStripTone::POSITIVE
    );
    return props;
}

}  // namespace core::ui::interaction
