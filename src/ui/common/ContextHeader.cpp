#include "ui/common/ContextHeader.hpp"

#include <config/PlatformCompat.hpp>
#include <ms/ui/font/CoreFonts.hpp>

#include "ui/common/ContextSurfaceDraw.hpp"
#include "ui/font/StandaloneFonts.hpp"

namespace core::ui {

void drawContextHeader(lv_layer_t* layer, const lv_area_t& bounds,
                       const ContextHeaderProps& props) {
    if (!layer) return;
    const int width = lv_area_get_width(&bounds);
    const int split = width / 2;
    const int title_x = props.icon && props.icon[0] ? 23 : 0;
    const int status_x = split + (props.statusIcon && props.statusIcon[0] ? 16 : 0);
    surface::text(layer, surface::area(bounds, 0, 2, 18, 18), props.icon,
        standalone_fonts.icons_16, props.iconColor, LV_OPA_COVER, LV_TEXT_ALIGN_CENTER);
    surface::text(layer, surface::area(bounds, title_x, 1, split - title_x - 4, 21),
        props.title, fonts.context_title(), props.titleColor);
    surface::text(layer, surface::area(bounds, split, 3, 14, 15), props.statusIcon,
        standalone_fonts.icons_12, props.statusColor, props.statusOpacity);
    surface::text(layer, surface::area(bounds, status_x, 3, width - status_x, 17),
        props.status, fonts.meta_label(), props.statusColor, props.statusOpacity,
        LV_TEXT_ALIGN_RIGHT);
}

FLASHMEM ContextHeader::ContextHeader(lv_obj_t* parent) {
    if (!parent) return;
    root_ = lv_obj_create(parent);
    if (!root_) return;
    lv_obj_remove_style_all(root_);
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(root_, onDraw, LV_EVENT_DRAW_MAIN, this);
}

FLASHMEM ContextHeader::~ContextHeader() {
    if (root_) lv_obj_delete(root_);
}

FLASHMEM void ContextHeader::render(const ContextHeaderProps& props) {
    if (!root_) return;
    bool changed = !rendered_ || title_color_ != props.titleColor ||
        status_color_ != props.statusColor || icon_color_ != props.iconColor ||
        status_opacity_ != props.statusOpacity;
    changed = surface::copyText(title_, props.title) || changed;
    changed = surface::copyText(status_, props.status) || changed;
    changed = surface::copyText(icon_, props.icon) || changed;
    changed = surface::copyText(status_icon_, props.statusIcon) || changed;
    title_color_ = props.titleColor;
    status_color_ = props.statusColor;
    icon_color_ = props.iconColor;
    status_opacity_ = props.statusOpacity;
    rendered_ = true;
    if (changed) lv_obj_invalidate(root_);
}

void ContextHeader::onDraw(lv_event_t* event) {
    const auto* self = static_cast<ContextHeader*>(lv_event_get_user_data(event));
    if (!self || !self->rendered_) return;
    lv_area_t bounds{};
    lv_obj_get_coords(self->root_, &bounds);
    drawContextHeader(lv_event_get_layer(event), bounds, {
        .title = self->title_.data(), .status = self->status_.data(),
        .icon = self->icon_.data(), .statusIcon = self->status_icon_.data(),
        .titleColor = self->title_color_, .statusColor = self->status_color_,
        .iconColor = self->icon_color_, .statusOpacity = self->status_opacity_,
    });
}
}  // namespace core::ui
