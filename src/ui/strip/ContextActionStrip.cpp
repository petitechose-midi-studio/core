#include "ContextActionStrip.hpp"

#include <algorithm>
#include <cstring>

#include <config/PlatformCompat.hpp>
#include <config/TimeCompat.hpp>
#include <ms/ui/font/CoreFonts.hpp>
#include <ms/ui/widget/TextOverflow.hpp>

#include "ui/common/ContextSurfaceDraw.hpp"
#include "ui/theme/StandaloneTheme.hpp"

namespace core::ui {
namespace {
namespace theme = standalone::theme;
using Visual = ContextActionStripVisualState;
using Tone = ContextActionStripTone;

constexpr uint32_t toneColor(Tone tone) {
    switch (tone) {
        case Tone::CONSTRUCTIVE: return theme::color::ROUTING;
        case Tone::DESTRUCTIVE: return theme::color::DESTRUCTIVE;
        case Tone::WARNING: return theme::color::WARNING;
        case Tone::POSITIVE: return theme::color::POSITIVE;
        case Tone::NEUTRAL: return theme::color::TEXT_PRIMARY;
    }
    return theme::color::TEXT_PRIMARY;
}

constexpr lv_opa_t contentOpacity(Visual state) {
    if (state == Visual::HIDDEN) return LV_OPA_TRANSP;
    if (state == Visual::DISABLED) return LV_OPA_30;
    if (state == Visual::DIM) return LV_OPA_60;
    return LV_OPA_COVER;
}

bool sameText(const char* left, const char* right) {
    return left == right || std::strcmp(left ? left : "", right ? right : "") == 0;
}

using surface::copyText;

bool sameSlot(const ContextActionStripSlotProps& a, const ContextActionStripSlotProps& b) {
    return a.visualState == b.visualState && a.tone == b.tone &&
        a.showIcon == b.showIcon && sameText(a.icon, b.icon) &&
        a.iconUsesStandaloneFont == b.iconUsesStandaloneFont && a.iconSize == b.iconSize &&
        a.showLabel == b.showLabel && a.holdOnly == b.holdOnly && a.holdActive == b.holdActive &&
        a.holdStartedAtMs == b.holdStartedAtMs && a.holdDurationMs == b.holdDurationMs;
}

const lv_font_t* iconFont(const ContextActionStripSlotProps& slot) {
    if (!slot.iconUsesStandaloneFont) return fonts.compact_label();
    switch (slot.iconSize) {
        case standalone::icons::Size::S: return standalone_fonts.icons_12;
        case standalone::icons::Size::M: return standalone_fonts.icons_14;
        case standalone::icons::Size::L: return standalone_fonts.icons_16;
    }
    return standalone_fonts.icons_14;
}
}  // namespace

FLASHMEM ContextActionStrip::ContextActionStrip(
    lv_obj_t* parent, ContextActionStripOrientation orientation,
    ContextActionStripVerticalLayout verticalLayout
) : orientation_(orientation), vertical_layout_(verticalLayout) {
    createUI(parent);
}

FLASHMEM ContextActionStrip::~ContextActionStrip() {
    hold_timer_.reset();
    if (container_) lv_obj_delete(container_);
}

FLASHMEM void ContextActionStrip::alignToFooter() {
    if (!container_) return;
    lv_obj_add_flag(container_, LV_OBJ_FLAG_FLOATING);
    lv_obj_add_flag(container_, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_align(container_, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_move_foreground(container_);
}

FLASHMEM void ContextActionStrip::createUI(lv_obj_t* parent) {
    if (!parent) return;
    container_ = lv_obj_create(parent);
    if (!container_) return;
    lv_obj_remove_style_all(container_);
    lv_obj_clear_flag(container_, LV_OBJ_FLAG_SCROLLABLE);
    const bool horizontal = orientation_ == ContextActionStripOrientation::HORIZONTAL;
    if (horizontal) {
        lv_obj_set_style_bg_color(container_, lv_color_hex(theme::color::BACKGROUND), 0);
        lv_obj_set_style_bg_opa(container_, LV_OPA_COVER, 0);
    }
    lv_obj_set_size(container_, horizontal ? LV_PCT(100) : 22,
                    horizontal ? theme::layout::CONTEXT_ACTION_STRIP_HEIGHT : LV_PCT(100));
    lv_obj_add_flag(container_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(container_, &ContextActionStrip::onDraw, LV_EVENT_DRAW_MAIN, this);
    hold_timer_.emplace(33U, &ContextActionStrip::onHoldTimer, this);
}

FLASHMEM void ContextActionStrip::render(const ContextActionStripProps& props) {
    if (!container_) return;
    if (!props.visible) {
        rendered_props_.visible = false;
        lv_obj_add_flag(container_, LV_OBJ_FLAG_HIDDEN);
        if (hold_timer_) hold_timer_->pause();
        return;
    }
    bool changed = !has_rendered_ || !rendered_props_.visible;
    for (size_t index = 0; index < props.slots.size(); ++index) {
        const auto& next = props.slots[index];
        const bool slot_changed = !sameSlot(next, rendered_props_.slots[index]);
        const bool text_changed = copyText(label_text_[index],
            next.label ? next.label : next.labelText.data());
        if (slot_changed || text_changed) {
            if (next.holdOnly && !next.holdActive && label_text_[index][0]) {
                std::snprintf(command_text_[index].data(), command_text_[index].size(),
                              "Hold %s", label_text_[index].data());
            } else {
                copyText(command_text_[index], label_text_[index].data());
            }
        }
        changed = slot_changed || text_changed || changed;
    }
    changed = copyText(hint_left_, props.hintLeft) || changed;
    changed = copyText(hint_right_, props.hintRight) || changed;
    rendered_props_ = props;
    // Caller-owned buffers can be transient. Only bounded owned text survives;
    // glyph pointers refer to the static icon vocabulary.
    rendered_props_.hintLeft = nullptr;
    rendered_props_.hintRight = nullptr;
    for (auto& slot : rendered_props_.slots) slot.label = nullptr;
    has_rendered_ = true;
    lv_obj_clear_flag(container_, LV_OBJ_FLAG_HIDDEN);
    refreshHoldProgress();
    if (changed) lv_obj_invalidate(container_);
    updateHoldTimer();
}

void ContextActionStrip::drawSlot(lv_layer_t* layer, size_t index,
                                 const lv_area_t& bounds) const {
    const auto& slot = rendered_props_.slots[index];
    if (slot.visualState == Visual::HIDDEN) return;
    const auto color = toneColor(slot.tone);
    const auto opacity = contentOpacity(slot.visualState);
    const bool horizontal = orientation_ == ContextActionStripOrientation::HORIZONTAL;
    const int width = lv_area_get_width(&bounds);
    const int height = lv_area_get_height(&bounds);
    const bool label = slot.showLabel && label_text_[index][0];
    const bool icon = slot.showIcon && slot.icon && slot.icon[0];
    const auto align = horizontal && index != 1U
        ? (index == 0U ? LV_TEXT_ALIGN_LEFT : LV_TEXT_ALIGN_RIGHT)
        : LV_TEXT_ALIGN_CENTER;
    // Words describe horizontal actions; glyphs retain the compact vertical
    // controls and remain the fallback when the owner supplies no label.
    if (label && (horizontal || !icon)) {
        const auto* font = fonts.compact_label();
        const int line_height = font ? lv_font_get_line_height(font) : 14;
        surface::text(layer, surface::area(bounds, 0, (height - line_height) / 2,
                      width, line_height), command_text_[index].data(), font, color, opacity, align);
    } else if (icon) {
        const auto* font = iconFont(slot);
        const int line_height = font ? lv_font_get_line_height(font) : 14;
        surface::text(layer, surface::area(bounds, 0, (height - line_height) / 2,
                      width, line_height), slot.icon, font, color, opacity, align);
    }
    const bool accent = slot.visualState == Visual::PRESSED ||
        slot.visualState == Visual::ARMED || slot.visualState == Visual::APPLIED;
    if (slot.holdActive && slot.holdDurationMs > 0U) {
        const int extent = horizontal ? width : height;
        const int filled = extent * hold_progress_[index] / 1000;
        if (filled > 0) surface::fill(layer,
            horizontal ? surface::area(bounds, 0, 0, filled, 2)
                       : surface::area(bounds, 0, height - filled, 2, filled), color);
    } else if (accent) {
        surface::fill(layer, horizontal ? surface::area(bounds, width / 2 - 6, 0, 12, 2)
                                       : surface::area(bounds, 0, height / 2 - 6, 2, 12), color);
    }
}

void ContextActionStrip::draw(lv_layer_t* layer) const {
    if (!layer || !has_rendered_ || !rendered_props_.visible) return;
    lv_area_t bounds{};
    lv_obj_get_coords(container_, &bounds);
    const int width = lv_area_get_width(&bounds);
    const int height = lv_area_get_height(&bounds);
    if (orientation_ == ContextActionStripOrientation::VERTICAL) {
        const int top = vertical_layout_ == ContextActionStripVerticalLayout::SPREAD
            ? 2 : std::max(0, (height - 66) / 2);
        const int gap = vertical_layout_ == ContextActionStripVerticalLayout::SPREAD
            ? std::max(0, (height - 24) / 2) : 24;
        for (size_t index = 0; index < 3; ++index) {
            drawSlot(layer, index, surface::area(bounds, 0, top + int(index) * gap, width, 20));
        }
        return;
    }
    const int action_y = height - theme::layout::TRANSPORT_BAR_HEIGHT;
    surface::fill(layer, surface::area(bounds, 0, action_y, width, 20), theme::color::SURFACE_IDLE);
    const int side = std::max(0, (width - theme::layout::TRANSPORT_CENTER_WIDTH) / 2);
    drawSlot(layer, 0, surface::area(bounds, 6, action_y, side - 10, 20));
    drawSlot(layer, 2, surface::area(bounds, width - side + 4, action_y, side - 10, 20));
    if (hold_text_[0]) {
        surface::text(layer, surface::area(bounds, 6, 2, width - 12, 17),
            hold_text_.data(), fonts.compact_label(), theme::color::WARNING, LV_OPA_COVER, LV_TEXT_ALIGN_CENTER);
    } else if (rendered_props_.slots[1].visualState != Visual::HIDDEN) {
        drawSlot(layer, 1, surface::area(bounds, 6, 0, width - 12, 20));
    } else {
        const int left_width = hint_right_[0] ? width / 2 - 8 : width - 12;
        surface::text(layer, surface::area(bounds, 6, 2, left_width, 17),
            hint_left_.data(), fonts.compact_label(), theme::color::TEXT_SECONDARY);
        surface::text(layer, surface::area(bounds, width / 2, 2, width / 2 - 6, 17),
            hint_right_.data(), fonts.compact_label(), theme::color::TEXT_SECONDARY,
            LV_OPA_COVER, LV_TEXT_ALIGN_RIGHT);
    }
}

void ContextActionStrip::refreshHoldProgress() {
    const auto now = core::time_compat::millis();
    std::array<char, 48> message{};
    bool changed = false;
    for (size_t index = 0; index < 3; ++index) {
        const auto& slot = rendered_props_.slots[index];
        uint16_t progress = 0;
        if (slot.holdActive && slot.holdDurationMs && slot.visualState != Visual::HIDDEN) {
            const int32_t elapsed = static_cast<int32_t>(now - slot.holdStartedAtMs);
            const uint32_t clamped = std::min(slot.holdDurationMs,
                elapsed > 0 ? static_cast<uint32_t>(elapsed) : 0U);
            progress = static_cast<uint16_t>(uint64_t(clamped) * 1000U / slot.holdDurationMs);
            if (!message[0]) {
                const auto tenths = (slot.holdDurationMs - clamped + 99U) / 100U;
                std::snprintf(message.data(), message.size(), "Hold %s %u.%us",
                    label_text_[index][0] ? label_text_[index].data() : "action",
                    unsigned(tenths / 10U), unsigned(tenths % 10U));
            }
        }
        changed = progress != hold_progress_[index] || changed;
        hold_progress_[index] = progress;
    }
    changed = message != hold_text_ || changed;
    hold_text_ = message;
    if (changed && container_) lv_obj_invalidate(container_);
}

void ContextActionStrip::updateHoldTimer() {
    if (!hold_timer_) return;
    bool active = false;
    for (const auto& slot : rendered_props_.slots) {
        active = active || (slot.holdActive && slot.holdDurationMs &&
                            slot.visualState != Visual::HIDDEN);
    }
    if (active && rendered_props_.visible && lv_obj_is_visible(container_)) hold_timer_->resume();
    else hold_timer_->pause();
}

void ContextActionStrip::onHoldTimer(lv_timer_t* timer) {
    auto* self = static_cast<ContextActionStrip*>(lv_timer_get_user_data(timer));
    if (!self) return;
    self->updateHoldTimer();
    if (self->rendered_props_.visible && lv_obj_is_visible(self->container_)) {
        self->refreshHoldProgress();
    }
}

void ContextActionStrip::onDraw(lv_event_t* event) {
    auto* self = static_cast<ContextActionStrip*>(lv_event_get_user_data(event));
    if (self) self->draw(lv_event_get_layer(event));
}
}  // namespace core::ui
