#include "ui/common/ParameterCard.hpp"

#include <config/PlatformCompat.hpp>
#include <ms/ui/font/CoreFonts.hpp>

#include "ui/common/ContextSurfaceDraw.hpp"
#include "ui/font/StandaloneFonts.hpp"

namespace core::ui {
namespace theme = standalone::theme;

FLASHMEM ParameterCard::ParameterCard(lv_obj_t* parent) {
    if (!parent) return;
    root_ = lv_obj_create(parent);
    if (!root_) return;
    lv_obj_remove_style_all(root_);
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(root_, theme::layout::INTERACTIVE_SURFACE_RADIUS, 0);
    lv_obj_set_style_border_width(root_, theme::layout::INTERACTIVE_SURFACE_BORDER_WIDTH, 0);
    lv_obj_add_event_cb(root_, onDraw, LV_EVENT_DRAW_MAIN, this);
}

FLASHMEM ParameterCard::~ParameterCard() {
    if (root_) lv_obj_delete(root_);
}

FLASHMEM void ParameterCard::render(const ParameterCardProps& props) {
    if (!root_) return;
    const uint8_t encoderNumber = props.encoderNumber <= 8U ? props.encoderNumber : 0U;
    bool changed = !rendered_ || !(visual_ == props.visual) || encoder_number_ != encoderNumber;
    if (!rendered_ || visual_.state != props.visual.state) {
        interaction::applyInteractiveSurfaceChrome(root_, props.visual.state);
    }
    changed = surface::copyText(icon_, props.icon) || changed;
    changed = surface::copyText(label_, props.label) || changed;
    changed = surface::copyText(value_, props.value) || changed;
    visual_ = props.visual;
    encoder_number_ = encoderNumber;
    rendered_ = true;
    if (changed) lv_obj_invalidate(root_);
}

void ParameterCard::draw(lv_layer_t* layer) const {
    if (!layer || !rendered_) return;
    lv_area_t bounds{};
    lv_obj_get_coords(root_, &bounds);
    const int width = lv_area_get_width(&bounds);
    const auto chrome = interaction::interactiveSurfaceVisual(visual_.state);
    const char encoderLabel[]{'E', static_cast<char>('0' + encoder_number_), '\0'};
    surface::text(layer, surface::area(bounds, encoder_number_ ? 3 : 4, 5,
        encoder_number_ ? 17 : 15, 14),
        encoder_number_ ? encoderLabel : icon_.data(),
        encoder_number_ ? fonts.meta_label() : standalone_fonts.icons_12,
        visual_.accent, visual_.iconOpacity, LV_TEXT_ALIGN_CENTER);
    surface::text(layer, surface::area(bounds, 21, 2, width - 25, 14), label_.data(),
        fonts.meta_label(), chrome.textColor, visual_.labelOpacity);
    surface::text(layer, surface::area(bounds, 21, 16, width - 25, 16), value_.data(),
        visual_.emphasizeValue ? fonts.compact_selected() : fonts.meta_label(),
        visual_.emphasizeValue ? theme::color::TEXT_PRIMARY : theme::color::TEXT_SECONDARY,
        visual_.valueOpacity);
    surface::fill(layer, surface::area(bounds, 5, 24, 10, 3), visual_.accent,
        visual_.activityOpacity, 2);
}

void ParameterCard::onDraw(lv_event_t* event) {
    auto* self = static_cast<ParameterCard*>(lv_event_get_user_data(event));
    if (self) self->draw(lv_event_get_layer(event));
}
}  // namespace core::ui
