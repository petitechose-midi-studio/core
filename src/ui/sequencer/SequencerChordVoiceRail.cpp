#include "ui/sequencer/SequencerChordVoiceRail.hpp"

#include <algorithm>
#include <cstring>

#include <config/PlatformCompat.hpp>
#include <ms/ui/font/CoreFonts.hpp>

#include "ui/font/StandaloneFonts.hpp"
#include "ui/theme/StandaloneTheme.hpp"

namespace core::ui {
namespace {

namespace theme = standalone::theme;

constexpr lv_coord_t RAIL_HEIGHT = 42;
constexpr lv_coord_t ITEM_GAP = 2;
constexpr lv_coord_t ITEM_RADIUS = 3;
constexpr lv_opa_t OPACITY_55 = static_cast<lv_opa_t>(140);

template <std::size_t N>
bool copyText(std::array<char, N>& destination, const char* source) {
    const char* text = source ? source : "";
    if (std::strncmp(destination.data(), text, N) == 0) return false;
    std::strncpy(destination.data(), text, N - 1U);
    destination[N - 1U] = '\0';
    return true;
}

void drawRect(
    lv_layer_t* layer,
    const lv_area_t& area,
    uint32_t color,
    lv_opa_t fillOpacity,
    lv_opa_t borderOpacity
) {
    lv_draw_rect_dsc_t descriptor;
    lv_draw_rect_dsc_init(&descriptor);
    descriptor.bg_color = lv_color_hex(color);
    descriptor.bg_opa = fillOpacity;
    descriptor.border_color = lv_color_hex(color);
    descriptor.border_width = 1;
    descriptor.border_opa = borderOpacity;
    descriptor.radius = ITEM_RADIUS;
    lv_draw_rect(layer, &descriptor, &area);
}

void drawLabel(
    lv_layer_t* layer,
    const lv_area_t& area,
    const char* text,
    const lv_font_t* font,
    uint32_t color,
    lv_opa_t opacity
) {
    lv_draw_label_dsc_t descriptor;
    lv_draw_label_dsc_init(&descriptor);
    descriptor.text = text ? text : "";
    descriptor.font = font ? font : LV_FONT_DEFAULT;
    descriptor.color = lv_color_hex(color);
    descriptor.opa = opacity;
    descriptor.align = LV_TEXT_ALIGN_CENTER;
    lv_draw_label(layer, &descriptor, &area);
}

}  // namespace

FLASHMEM void SequencerChordVoiceRail::create(lv_obj_t* parent) {
    if (!parent || surface_) return;

    surface_ = lv_obj_create(parent);
    lv_obj_remove_style_all(surface_);
    lv_obj_set_width(surface_, LV_PCT(100));
    lv_obj_set_height(surface_, RAIL_HEIGHT);
    lv_obj_clear_flag(surface_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(surface_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(surface_, onDraw, LV_EVENT_DRAW_MAIN, this);
}

FLASHMEM void SequencerChordVoiceRail::render(
    const SequencerChordVoiceRailProps& props
) {
    if (!surface_) return;

    if (!props.visible) {
        if (visible_) {
            lv_obj_add_flag(surface_, LV_OBJ_FLAG_HIDDEN);
            visible_ = false;
        }
        return;
    }

    const uint8_t itemCount = std::clamp<uint8_t>(
        props.itemCount,
        1U,
        static_cast<uint8_t>(items_.size())
    );
    bool changed = !rendered_ ||
        item_count_ != itemCount ||
        focused_item_ != props.focusedItem ||
        color_ != props.color;

    for (std::size_t index = 0; index < items_.size(); ++index) {
        auto& cached = items_[index];
        const auto& incoming = props.items[index];
        changed = copyText(cached.label, incoming.label) || changed;
        changed = copyText(cached.value, incoming.value) || changed;
        if (cached.add != incoming.add ||
            cached.enabled != incoming.enabled) {
            cached.add = incoming.add;
            cached.enabled = incoming.enabled;
            changed = true;
        }
    }

    item_count_ = itemCount;
    focused_item_ = std::min<uint8_t>(
        props.focusedItem,
        static_cast<uint8_t>(itemCount - 1U)
    );
    color_ = props.color;

    if (!visible_) {
        lv_obj_clear_flag(surface_, LV_OBJ_FLAG_HIDDEN);
        visible_ = true;
        changed = true;
    }
    if (changed) lv_obj_invalidate(surface_);
    rendered_ = true;
}

FLASHMEM void SequencerChordVoiceRail::draw(lv_layer_t* layer) {
    if (!layer || !surface_ || item_count_ == 0) return;

    lv_area_t area{};
    lv_obj_get_coords(surface_, &area);
    const lv_coord_t width = lv_area_get_width(&area);
    const lv_coord_t gaps = static_cast<lv_coord_t>(
        (item_count_ - 1U) * ITEM_GAP
    );
    const lv_coord_t cellWidth = std::max<lv_coord_t>(
        1,
        static_cast<lv_coord_t>((width - gaps) / item_count_)
    );

    for (uint8_t index = 0; index < item_count_; ++index) {
        const auto& item = items_[index];
        const bool selected = index > 0U && index == focused_item_;
        const lv_coord_t x = static_cast<lv_coord_t>(
            area.x1 + index * (cellWidth + ITEM_GAP)
        );
        const lv_coord_t right = index + 1U == item_count_
            ? area.x2
            : static_cast<lv_coord_t>(x + cellWidth - 1);
        const lv_area_t cell{
            .x1 = x,
            .y1 = area.y1,
            .x2 = right,
            .y2 = area.y2,
        };
        drawRect(
            layer,
            cell,
            color_,
            selected ? LV_OPA_20 : LV_OPA_TRANSP,
            selected
                ? (item.enabled ? LV_OPA_COVER : LV_OPA_50)
                : LV_OPA_20
        );

        const lv_opa_t opacity = !item.enabled
            ? LV_OPA_30
            : (selected ? LV_OPA_COVER : (index == 0U ? LV_OPA_50 : OPACITY_55));
        if (item.add) {
            drawLabel(
                layer,
                cell,
                item.value.data(),
                fonts.primary_value(),
                color_,
                opacity
            );
            continue;
        }

        const lv_area_t labelArea{
            .x1 = cell.x1,
            .y1 = static_cast<lv_coord_t>(cell.y1 + 3),
            .x2 = cell.x2,
            .y2 = static_cast<lv_coord_t>(cell.y1 + 17),
        };
        const lv_area_t valueArea{
            .x1 = cell.x1,
            .y1 = static_cast<lv_coord_t>(cell.y1 + 20),
            .x2 = cell.x2,
            .y2 = static_cast<lv_coord_t>(cell.y2 - 2),
        };
        drawLabel(
            layer,
            labelArea,
            item.label.data(),
            fonts.meta_label(),
            theme::color::TEXT_SECONDARY,
            opacity
        );
        drawLabel(
            layer,
            valueArea,
            item.value.data(),
            fonts.compact_selected(),
            color_,
            opacity
        );
    }
}

FLASHMEM void SequencerChordVoiceRail::onDraw(lv_event_t* event) {
    auto* self = static_cast<SequencerChordVoiceRail*>(
        lv_event_get_user_data(event)
    );
    if (self) self->draw(lv_event_get_layer(event));
}

}  // namespace core::ui
