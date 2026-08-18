#include "ContextActionStrip.hpp"

#include <array>
#include <cstdio>
#include <cstring>

#include <oc/ui/lvgl/style/StyleBuilder.hpp>

#include <config/PlatformCompat.hpp>
#include <config/TimeCompat.hpp>
#include <ms/ui/font/CoreFonts.hpp>

#include "ui/theme/StandaloneTheme.hpp"

namespace style = oc::ui::lvgl::style;
namespace theme = standalone::theme;

namespace core::ui {

namespace {

constexpr lv_coord_t HORIZONTAL_STRIP_HEIGHT = theme::layout::CONTEXT_ACTION_STRIP_HEIGHT;
constexpr lv_coord_t VERTICAL_STRIP_WIDTH = 16;
constexpr lv_coord_t SLOT_RADIUS = 0;
constexpr lv_coord_t SLOT_PAD = 0;
constexpr lv_coord_t INDICATOR_LONG = 8;
constexpr lv_coord_t INDICATOR_THICKNESS = 1;
constexpr lv_coord_t CONTENT_GAP = 0;
constexpr lv_coord_t HORIZONTAL_CONTENT_GAP = 4;
constexpr lv_coord_t HORIZONTAL_OUTER_PAD = 2;
constexpr lv_coord_t VERTICAL_OUTER_PAD = 6;
constexpr lv_coord_t VERTICAL_SLOT_GAP = 2;
constexpr lv_coord_t VERTICAL_SPREAD_OUTER_PAD = 4;
constexpr lv_coord_t HOLD_FEEDBACK_WIDTH = 84;
constexpr lv_coord_t HOLD_FEEDBACK_HEIGHT = 34;
constexpr uint32_t HOLD_TIMER_PERIOD_MS = 33;

FLASHMEM uint32_t toneColor(ContextActionStripTone tone) {
    switch (tone) {
        case ContextActionStripTone::CONSTRUCTIVE:
            return theme::color::CONTENT_ACTIVE;
        case ContextActionStripTone::DESTRUCTIVE:
            return theme::color::DESTRUCTIVE;
        case ContextActionStripTone::WARNING:
            return theme::color::WARNING;
        case ContextActionStripTone::POSITIVE:
            return theme::color::POSITIVE;
        case ContextActionStripTone::NEUTRAL:
        default:
            return theme::color::CONTENT_ACTIVE;
    }
}

FLASHMEM lv_opa_t contentOpacity(ContextActionStripVisualState state) {
    switch (state) {
        case ContextActionStripVisualState::DISABLED:
            return LV_OPA_30;
        case ContextActionStripVisualState::DIM:
            return LV_OPA_60;
        case ContextActionStripVisualState::ACTIVE:
        case ContextActionStripVisualState::PRESSED:
        case ContextActionStripVisualState::ARMED:
        case ContextActionStripVisualState::CANCELLED:
        case ContextActionStripVisualState::APPLIED:
            return LV_OPA_COVER;
        case ContextActionStripVisualState::HIDDEN:
        default:
            return LV_OPA_TRANSP;
    }
}

FLASHMEM lv_opa_t backgroundOpacity(ContextActionStripVisualState /*state*/) {
    return LV_OPA_TRANSP;
}

FLASHMEM lv_opa_t indicatorOpacity(ContextActionStripVisualState state) {
    switch (state) {
        case ContextActionStripVisualState::PRESSED:
        case ContextActionStripVisualState::ARMED:
        case ContextActionStripVisualState::APPLIED:
            return LV_OPA_COVER;
        case ContextActionStripVisualState::CANCELLED:
            return LV_OPA_30;
        default:
            return LV_OPA_TRANSP;
    }
}

FLASHMEM const char* slotLabel(const ContextActionStripSlotProps& props) {
    if (props.label) return props.label;
    return props.labelText[0] != '\0' ? props.labelText.data() : nullptr;
}

FLASHMEM bool contentVisible(const ContextActionStripSlotProps& props) {
    return props.visualState != ContextActionStripVisualState::HIDDEN &&
           ((props.showIcon && props.icon) || (props.showLabel && slotLabel(props)));
}

FLASHMEM bool sameText(const char* lhs, const char* rhs) {
    if (lhs == rhs) return true;
    if (!lhs || !rhs) return false;
    return std::strcmp(lhs, rhs) == 0;
}

template <size_t N>
FLASHMEM bool setCachedText(lv_obj_t* label, std::array<char, N>& cache, const char* text) {
    if (!label) return false;
    const char* next = text ? text : "";
    if (std::strncmp(cache.data(), next, N) == 0) {
        return false;
    }
    std::strncpy(cache.data(), next, N - 1);
    cache[N - 1] = '\0';
    lv_label_set_text(label, cache.data());
    return true;
}

FLASHMEM bool sameSlotProps(const ContextActionStripSlotProps& lhs, const ContextActionStripSlotProps& rhs) {
    return lhs.visualState == rhs.visualState &&
           lhs.tone == rhs.tone &&
           lhs.showIcon == rhs.showIcon &&
           sameText(lhs.icon, rhs.icon) &&
           lhs.iconUsesStandaloneFont == rhs.iconUsesStandaloneFont &&
           lhs.iconSize == rhs.iconSize &&
           lhs.iconRotated180 == rhs.iconRotated180 &&
           lhs.showLabel == rhs.showLabel &&
           sameText(lhs.label, rhs.label) &&
           lhs.labelText == rhs.labelText &&
           lhs.holdActive == rhs.holdActive &&
           lhs.holdStartedAtMs == rhs.holdStartedAtMs &&
           lhs.holdDurationMs == rhs.holdDurationMs;
}

}  // namespace

FLASHMEM ContextActionStrip::ContextActionStrip(
    lv_obj_t* parent,
    ContextActionStripOrientation orientation,
    ContextActionStripVerticalLayout verticalLayout
)
    : orientation_(orientation), vertical_layout_(verticalLayout) {
    createUI(parent);
}

FLASHMEM ContextActionStrip::~ContextActionStrip() {
    hold_timer_.reset();
    if (hold_feedback_) {
        lv_obj_delete(hold_feedback_);
        hold_feedback_ = nullptr;
    }
    if (container_) {
        lv_obj_delete(container_);
        container_ = nullptr;
    }
}

FLASHMEM void ContextActionStrip::createUI(lv_obj_t* parent) {
    if (!parent) return;

    container_ = lv_obj_create(parent);
    style::apply(container_).transparent().noBorder().pad(0).noScroll();
    lv_obj_set_style_bg_opa(container_, LV_OPA_TRANSP, 0);

    if (orientation_ == ContextActionStripOrientation::HORIZONTAL) {
        lv_obj_set_size(container_, LV_PCT(100), HORIZONTAL_STRIP_HEIGHT);
        lv_obj_set_style_bg_color(
            container_, lv_color_hex(theme::color::BACKGROUND), 0
        );
        lv_obj_set_style_bg_opa(container_, LV_OPA_30, 0);
        lv_obj_set_layout(container_, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(container_, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(container_, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_left(container_, HORIZONTAL_OUTER_PAD, 0);
        lv_obj_set_style_pad_right(container_, HORIZONTAL_OUTER_PAD, 0);
        lv_obj_set_style_pad_top(container_, 0, 0);
        lv_obj_set_style_pad_bottom(container_, 0, 0);
        lv_obj_set_style_pad_column(container_, 0, 0);
    } else {
        lv_obj_set_size(container_, VERTICAL_STRIP_WIDTH, LV_PCT(100));
        lv_obj_set_layout(container_, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(container_, LV_FLEX_FLOW_COLUMN);
        const bool spread = vertical_layout_ == ContextActionStripVerticalLayout::SPREAD;
        lv_obj_set_flex_align(
            container_,
            spread ? LV_FLEX_ALIGN_SPACE_BETWEEN : LV_FLEX_ALIGN_CENTER,
            LV_FLEX_ALIGN_CENTER,
            LV_FLEX_ALIGN_CENTER
        );
        lv_obj_set_style_pad_left(container_, 0, 0);
        lv_obj_set_style_pad_right(container_, 1, 0);
        lv_obj_set_style_pad_top(
            container_,
            spread ? VERTICAL_SPREAD_OUTER_PAD : VERTICAL_OUTER_PAD,
            0
        );
        lv_obj_set_style_pad_bottom(
            container_,
            spread ? VERTICAL_SPREAD_OUTER_PAD : VERTICAL_OUTER_PAD,
            0
        );
        lv_obj_set_style_pad_row(container_, spread ? 0 : VERTICAL_SLOT_GAP, 0);
    }

    for (auto& slot : slots_) {
        slot.container = lv_obj_create(container_);
        lv_obj_remove_style_all(slot.container);
        lv_obj_clear_flag(slot.container, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_radius(slot.container, SLOT_RADIUS, 0);
        lv_obj_set_style_border_width(slot.container, 0, 0);
        lv_obj_set_style_pad_all(slot.container, SLOT_PAD, 0);
        lv_obj_set_style_bg_opa(slot.container, LV_OPA_TRANSP, 0);
        lv_obj_set_layout(slot.container, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(slot.container, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(slot.container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        if (orientation_ == ContextActionStripOrientation::HORIZONTAL) {
            lv_obj_set_width(slot.container, 0);
            lv_obj_set_height(slot.container, LV_PCT(100));
            lv_obj_set_flex_grow(slot.container, 1);
        } else {
            lv_obj_set_width(slot.container, LV_PCT(100));
            lv_obj_set_height(slot.container, 18);
        }

        slot.indicator = lv_bar_create(slot.container);
        lv_obj_remove_style_all(slot.indicator);
        lv_obj_add_flag(slot.indicator, LV_OBJ_FLAG_IGNORE_LAYOUT);
        lv_obj_clear_flag(slot.indicator, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_radius(slot.indicator, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_radius(slot.indicator, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
        lv_obj_set_style_border_width(slot.indicator, 0, 0);
        lv_obj_set_style_bg_opa(
            slot.indicator,
            LV_OPA_TRANSP,
            LV_PART_INDICATOR
        );
        lv_bar_set_range(slot.indicator, 0, 1000);
        lv_bar_set_value(slot.indicator, 0, LV_ANIM_OFF);

        if (orientation_ == ContextActionStripOrientation::HORIZONTAL) {
            lv_obj_set_size(slot.indicator, INDICATOR_LONG, INDICATOR_THICKNESS);
            lv_obj_align(slot.indicator, LV_ALIGN_TOP_MID, 0, 0);
        } else {
            lv_obj_set_size(slot.indicator, INDICATOR_THICKNESS, INDICATOR_LONG);
            lv_obj_align(slot.indicator, LV_ALIGN_LEFT_MID, 0, 0);
        }

        slot.content = lv_obj_create(slot.container);
        lv_obj_remove_style_all(slot.content);
        lv_obj_clear_flag(slot.content, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_pad_all(slot.content, 0, 0);
        lv_obj_set_style_pad_row(slot.content, CONTENT_GAP, 0);
        lv_obj_set_style_pad_column(slot.content, HORIZONTAL_CONTENT_GAP, 0);
        lv_obj_set_style_bg_opa(slot.content, LV_OPA_TRANSP, 0);
        lv_obj_set_layout(slot.content, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(
            slot.content,
            orientation_ == ContextActionStripOrientation::HORIZONTAL
                ? LV_FLEX_FLOW_ROW
                : LV_FLEX_FLOW_COLUMN
        );
        lv_obj_set_flex_align(slot.content, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        slot.icon = lv_label_create(slot.content);
        lv_obj_set_style_text_color(slot.icon, lv_color_hex(theme::color::TEXT_PRIMARY), 0);
        lv_obj_add_flag(slot.icon, LV_OBJ_FLAG_HIDDEN);

        slot.label = lv_label_create(slot.content);
        lv_obj_set_style_text_font(slot.label, fonts.compact_label(), 0);
        lv_obj_set_style_text_color(slot.label, lv_color_hex(theme::color::TEXT_PRIMARY), 0);
        lv_label_set_long_mode(slot.label, LV_LABEL_LONG_CLIP);
        lv_obj_set_width(slot.label, LV_SIZE_CONTENT);
        lv_obj_add_flag(slot.label, LV_OBJ_FLAG_HIDDEN);
        slot.label_font = fonts.compact_label();
        slot.label_color = theme::color::TEXT_PRIMARY;
        slot.label_opa = LV_OPA_COVER;
    }

    lv_obj_add_flag(container_, LV_OBJ_FLAG_HIDDEN);
    createHoldFeedback(parent);
    hold_timer_.emplace(HOLD_TIMER_PERIOD_MS, onHoldTimer, this);
}

FLASHMEM void ContextActionStrip::createHoldFeedback(lv_obj_t* parent) {
    if (!parent) return;

    hold_feedback_ = lv_obj_create(parent);
    if (!hold_feedback_) return;
    lv_obj_add_flag(hold_feedback_, LV_OBJ_FLAG_HIDDEN);
    style::apply(hold_feedback_)
        .size(HOLD_FEEDBACK_WIDTH, HOLD_FEEDBACK_HEIGHT)
        .noScroll()
        .pad(6);
    lv_obj_add_flag(hold_feedback_, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_style_bg_color(
        hold_feedback_, lv_color_hex(theme::color::SURFACE_RAISED), 0
    );
    lv_obj_set_style_bg_opa(hold_feedback_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(hold_feedback_, 1, 0);
    lv_obj_set_style_border_color(
        hold_feedback_, lv_color_hex(theme::color::BORDER_SUBTLE), 0
    );
    lv_obj_set_style_border_opa(hold_feedback_, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(
        hold_feedback_, theme::layout::INTERACTIVE_SURFACE_RADIUS, 0
    );
    lv_obj_set_layout(hold_feedback_, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(hold_feedback_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(
        hold_feedback_,
        LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_CENTER
    );
    lv_obj_set_style_pad_column(hold_feedback_, 6, 0);
    lv_obj_align(hold_feedback_, LV_ALIGN_CENTER, 0, 0);

    hold_feedback_icon_ = lv_label_create(hold_feedback_);
    hold_feedback_label_ = lv_label_create(hold_feedback_);
    if (!hold_feedback_icon_ || !hold_feedback_label_) return;
    lv_obj_set_style_text_font(
        hold_feedback_label_, fonts.compact_selected(), 0
    );
    lv_obj_set_style_text_color(
        hold_feedback_label_, lv_color_hex(theme::color::TEXT_PRIMARY), 0
    );
    lv_obj_add_flag(hold_feedback_icon_, LV_OBJ_FLAG_HIDDEN);
}

FLASHMEM void ContextActionStrip::render(const ContextActionStripProps& props) {
    if (!container_) return;

    if (!props.visible) {
        if (!has_rendered_ || rendered_props_.visible) {
            lv_obj_add_flag(container_, LV_OBJ_FLAG_HIDDEN);
        }
        rendered_props_ = props;
        has_rendered_ = true;
        if (hold_feedback_) {
            lv_obj_add_flag(hold_feedback_, LV_OBJ_FLAG_HIDDEN);
        }
        updateHoldTimer();
        return;
    }

    if (!has_rendered_ || !rendered_props_.visible) {
        lv_obj_add_flag(container_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(container_, LV_OBJ_FLAG_HIDDEN);
    }

    for (size_t i = 0; i < slots_.size(); ++i) {
        if (!has_rendered_ || !sameSlotProps(rendered_props_.slots[i], props.slots[i])) {
            renderSlot(i, props.slots[i]);
        }
    }

    rendered_props_ = props;
    has_rendered_ = true;
    refreshHoldIndicators();
    updateHoldTimer();
}

FLASHMEM void ContextActionStrip::renderSlot(size_t index, const ContextActionStripSlotProps& props) {
    if (index >= slots_.size()) return;

    auto& slot = slots_[index];
    if (!slot.container || !slot.indicator || !slot.icon || !slot.label || !slot.content) return;

    const uint32_t colorHex = toneColor(props.tone);
    const lv_color_t color = lv_color_hex(colorHex);
    const lv_opa_t textOpa = contentOpacity(props.visualState);
    const lv_opa_t bgOpa = backgroundOpacity(props.visualState);
    const lv_opa_t accentOpa = indicatorOpacity(props.visualState);
    const bool showContent = contentVisible(props);

    lv_obj_set_style_bg_color(slot.container, color, 0);
    lv_obj_set_style_bg_opa(slot.container, bgOpa, 0);
    lv_obj_set_style_bg_color(slot.indicator, color, 0);
    lv_obj_set_style_bg_color(slot.indicator, color, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(
        slot.indicator,
        props.holdActive ? LV_OPA_TRANSP : accentOpa,
        0
    );
    lv_obj_set_style_bg_opa(
        slot.indicator,
        props.holdActive ? LV_OPA_COVER : LV_OPA_TRANSP,
        LV_PART_INDICATOR
    );

    if (!showContent) {
        lv_obj_add_flag(slot.icon, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(slot.label, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    if (props.showIcon && props.icon) {
        if (props.iconUsesStandaloneFont) {
            standalone::icons::set(slot.icon, props.icon, props.iconSize);
        } else {
            lv_label_set_text(slot.icon, props.icon);
        }
        lv_obj_set_style_text_color(slot.icon, color, 0);
        lv_obj_set_style_text_opa(slot.icon, textOpa, 0);
        lv_obj_set_style_transform_rotation(
            slot.icon,
            props.iconRotated180 ? 1800 : 0,
            0
        );
        lv_obj_clear_flag(slot.icon, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(slot.icon, LV_OBJ_FLAG_HIDDEN);
    }

    if (props.showLabel && slotLabel(props)) {
        setCachedText(slot.label, slot.label_text, slotLabel(props));
        const lv_font_t* labelFont =
            (props.visualState == ContextActionStripVisualState::ACTIVE ||
             props.visualState == ContextActionStripVisualState::PRESSED ||
             props.visualState == ContextActionStripVisualState::ARMED)
                ? fonts.compact_selected()
                : fonts.compact_label();
        if (slot.label_font != labelFont) {
            lv_obj_set_style_text_font(slot.label, labelFont, 0);
            slot.label_font = labelFont;
        }
        if (slot.label_color != colorHex) {
            lv_obj_set_style_text_color(slot.label, color, 0);
            slot.label_color = colorHex;
        }
        if (slot.label_opa != textOpa) {
            lv_obj_set_style_text_opa(slot.label, textOpa, 0);
            slot.label_opa = textOpa;
        }
        lv_obj_clear_flag(slot.label, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(slot.label, LV_OBJ_FLAG_HIDDEN);
    }
}

FLASHMEM void ContextActionStrip::refreshHoldIndicators() {
    if (!container_ || !has_rendered_) return;

    const uint32_t nowMs = core::time_compat::millis();
    for (size_t index = 0; index < slots_.size(); ++index) {
        const auto& props = rendered_props_.slots[index];
        auto& slot = slots_[index];
        if (!slot.container || !slot.indicator || !slot.label) continue;

        if (!props.holdActive || props.holdDurationMs == 0) {
            if (!props.showLabel || !slotLabel(props)) {
                lv_obj_add_flag(slot.label, LV_OBJ_FLAG_HIDDEN);
            }
            if (!slot.hold_geometry_initialized || slot.indicator_fill_mode) {
                if (orientation_ == ContextActionStripOrientation::HORIZONTAL) {
                    lv_obj_set_size(slot.indicator, INDICATOR_LONG, INDICATOR_THICKNESS);
                    lv_obj_align(slot.indicator, LV_ALIGN_TOP_MID, 0, 0);
                } else {
                    lv_obj_set_size(slot.indicator, INDICATOR_THICKNESS, INDICATOR_LONG);
                    lv_obj_align(slot.indicator, LV_ALIGN_LEFT_MID, 0, 0);
                }
                slot.indicator_long = INDICATOR_LONG;
                slot.indicator_fill_mode = false;
                slot.hold_geometry_initialized = true;
                lv_bar_set_value(slot.indicator, 0, LV_ANIM_OFF);
            }
            const lv_opa_t baseIndicatorOpa = indicatorOpacity(props.visualState);
            if (slot.indicator_opa != baseIndicatorOpa) {
                lv_obj_set_style_bg_opa(slot.indicator, baseIndicatorOpa, 0);
                lv_obj_set_style_bg_opa(
                    slot.indicator,
                    LV_OPA_TRANSP,
                    LV_PART_INDICATOR
                );
                slot.indicator_opa = baseIndicatorOpa;
            }
            continue;
        }

        const uint32_t elapsedMs =
            (nowMs > props.holdStartedAtMs) ? (nowMs - props.holdStartedAtMs) : 0;
        const uint32_t clampedElapsed = std::min(elapsedMs, props.holdDurationMs);
        if (!slot.hold_geometry_initialized || !slot.indicator_fill_mode) {
            if (orientation_ == ContextActionStripOrientation::HORIZONTAL) {
                lv_obj_set_size(
                    slot.indicator,
                    LV_PCT(100),
                    INDICATOR_THICKNESS
                );
                lv_obj_align(slot.indicator, LV_ALIGN_TOP_LEFT, 0, 0);
            } else {
                lv_obj_set_size(
                    slot.indicator,
                    INDICATOR_THICKNESS,
                    LV_PCT(100)
                );
                lv_obj_align(slot.indicator, LV_ALIGN_BOTTOM_LEFT, 0, 0);
            }
            slot.indicator_long = -1;
            slot.indicator_fill_mode = true;
            slot.hold_geometry_initialized = true;
        }
        const int32_t progress = static_cast<int32_t>(
            (static_cast<uint64_t>(clampedElapsed) * 1000U) /
            props.holdDurationMs
        );
        if (slot.indicator_long != progress) {
            lv_bar_set_value(slot.indicator, progress, LV_ANIM_OFF);
            slot.indicator_long = static_cast<lv_coord_t>(progress);
        }
        if (slot.indicator_opa != LV_OPA_COVER) {
            lv_obj_set_style_bg_opa(slot.indicator, LV_OPA_TRANSP, 0);
            lv_obj_set_style_bg_opa(
                slot.indicator,
                LV_OPA_COVER,
                LV_PART_INDICATOR
            );
            slot.indicator_opa = LV_OPA_COVER;
        }
    }
    refreshHoldFeedback(nowMs);
}

FLASHMEM void ContextActionStrip::refreshHoldFeedback(uint32_t nowMs) {
    if (!hold_feedback_ || !hold_feedback_icon_ || !hold_feedback_label_) {
        return;
    }

    const ContextActionStripSlotProps* active = nullptr;
    for (const auto& slot : rendered_props_.slots) {
        if (slot.holdActive && slot.holdDurationMs > 0U) {
            active = &slot;
            break;
        }
    }
    if (!active) {
        lv_obj_add_flag(hold_feedback_, LV_OBJ_FLAG_HIDDEN);
        hold_feedback_icon_value_ = nullptr;
        hold_feedback_tenths_ = std::numeric_limits<uint16_t>::max();
        return;
    }

    const uint32_t elapsedMs = nowMs > active->holdStartedAtMs
        ? nowMs - active->holdStartedAtMs
        : 0U;
    const uint32_t remainingMs = active->holdDurationMs - std::min(
        elapsedMs,
        active->holdDurationMs
    );
    const uint16_t remainingTenths = static_cast<uint16_t>(
        (remainingMs + 99U) / 100U
    );
    if (hold_feedback_tenths_ != remainingTenths) {
        char timerText[8]{};
        std::snprintf(
            timerText,
            sizeof(timerText),
            "%u.%us",
            static_cast<unsigned>(remainingTenths / 10U),
            static_cast<unsigned>(remainingTenths % 10U)
        );
        setCachedText(
            hold_feedback_label_, hold_feedback_text_, timerText
        );
        hold_feedback_tenths_ = remainingTenths;
    }

    const bool showIcon = active->showIcon && active->icon;
    if (showIcon) {
        if (!sameText(hold_feedback_icon_value_, active->icon)) {
            if (active->iconUsesStandaloneFont) {
                standalone::icons::set(
                    hold_feedback_icon_,
                    active->icon,
                    standalone::icons::Size::L
                );
            } else {
                lv_label_set_text(hold_feedback_icon_, active->icon);
            }
            hold_feedback_icon_value_ = active->icon;
        }
        lv_obj_clear_flag(hold_feedback_icon_, LV_OBJ_FLAG_HIDDEN);
    } else {
        hold_feedback_icon_value_ = nullptr;
        lv_obj_add_flag(hold_feedback_icon_, LV_OBJ_FLAG_HIDDEN);
    }

    const uint32_t color = toneColor(active->tone);
    if (hold_feedback_color_ != color) {
        lv_obj_set_style_text_color(
            hold_feedback_icon_, lv_color_hex(color), 0
        );
        lv_obj_set_style_border_color(
            hold_feedback_, lv_color_hex(color), 0
        );
        hold_feedback_color_ = color;
    }
    lv_obj_move_foreground(hold_feedback_);
    lv_obj_clear_flag(hold_feedback_, LV_OBJ_FLAG_HIDDEN);
}

FLASHMEM void ContextActionStrip::updateHoldTimer() {
    if (!hold_timer_) return;

    bool active = false;
    for (const auto& slot : rendered_props_.slots) {
        if (slot.holdActive && slot.holdDurationMs > 0) {
            active = true;
            break;
        }
    }

    if (active) {
        hold_timer_->resume();
    } else {
        hold_timer_->pause();
    }
}

FLASHMEM void ContextActionStrip::onHoldTimer(lv_timer_t* timer) {
    auto* self = static_cast<ContextActionStrip*>(lv_timer_get_user_data(timer));
    if (!self || !self->has_rendered_) return;
    self->refreshHoldIndicators();
    self->updateHoldTimer();
}

}  // namespace core::ui
