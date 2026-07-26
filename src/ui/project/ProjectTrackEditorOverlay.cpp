#include "ui/project/ProjectTrackEditorOverlay.hpp"

#include <cstring>

#include <config/PlatformCompat.hpp>
#include <ms/ui/font/CoreFonts.hpp>

#include "ui/font/StandaloneFonts.hpp"
#include "ui/font/StandaloneIcons.hpp"
#include "ui/theme/StandaloneTheme.hpp"

namespace core::ui::project {
namespace {

namespace theme = ::standalone::theme;
namespace icons = ::standalone::icons;

constexpr lv_coord_t SURFACE_X = 8;
constexpr lv_coord_t SURFACE_Y = 4;
constexpr lv_coord_t SURFACE_WIDTH = 304;
constexpr lv_coord_t SURFACE_HEIGHT = 196;

constexpr lv_opa_t OPACITY_12 = static_cast<lv_opa_t>(31);
constexpr lv_opa_t OPACITY_18 = static_cast<lv_opa_t>(46);
constexpr lv_opa_t OPACITY_35 = static_cast<lv_opa_t>(89);
constexpr lv_opa_t OPACITY_55 = static_cast<lv_opa_t>(140);

template <std::size_t N>
FLASHMEM void copyText(std::array<char, N>& destination, const char* source) {
    const char* text = source ? source : "";
    std::strncpy(destination.data(), text, N - 1U);
    destination[N - 1U] = '\0';
}

FLASHMEM void drawRect(
    lv_layer_t* layer,
    const lv_area_t& area,
    uint32_t color,
    lv_opa_t fillOpacity,
    lv_coord_t borderWidth = 0,
    lv_opa_t borderOpacity = LV_OPA_TRANSP,
    lv_coord_t radius = 0
) {
    lv_draw_rect_dsc_t descriptor;
    lv_draw_rect_dsc_init(&descriptor);
    descriptor.bg_color = lv_color_hex(color);
    descriptor.bg_opa = fillOpacity;
    descriptor.border_color = lv_color_hex(color);
    descriptor.border_width = borderWidth;
    descriptor.border_opa = borderOpacity;
    descriptor.radius = radius;
    lv_draw_rect(layer, &descriptor, &area);
}

FLASHMEM void drawLabel(
    lv_layer_t* layer,
    const lv_area_t& area,
    const char* text,
    const lv_font_t* font,
    uint32_t color,
    lv_opa_t opacity = LV_OPA_COVER,
    lv_text_align_t align = LV_TEXT_ALIGN_LEFT
) {
    lv_draw_label_dsc_t descriptor;
    lv_draw_label_dsc_init(&descriptor);
    descriptor.text = text ? text : "";
    descriptor.font = font ? font : LV_FONT_DEFAULT;
    descriptor.color = lv_color_hex(color);
    descriptor.opa = opacity;
    descriptor.align = align;
    lv_draw_label(layer, &descriptor, &area);
}

FLASHMEM lv_area_t translated(
    const lv_area_t& origin,
    lv_coord_t x,
    lv_coord_t y,
    lv_coord_t width,
    lv_coord_t height
) {
    return {
        .x1 = static_cast<lv_coord_t>(origin.x1 + x),
        .y1 = static_cast<lv_coord_t>(origin.y1 + y),
        .x2 = static_cast<lv_coord_t>(origin.x1 + x + width - 1),
        .y2 = static_cast<lv_coord_t>(origin.y1 + y + height - 1),
    };
}

FLASHMEM void drawStateChip(
    lv_layer_t* layer,
    const lv_area_t& origin,
    lv_coord_t x,
    lv_coord_t width,
    const char* text,
    uint32_t color,
    bool active,
    bool enabled
) {
    const auto area = translated(origin, x, 1, width, 25);
    const uint32_t effectiveColor = enabled ? color : theme::color::INACTIVE;
    drawRect(
        layer,
        area,
        effectiveColor,
        active && enabled ? OPACITY_35 : LV_OPA_TRANSP,
        1,
        active && enabled ? LV_OPA_COVER : OPACITY_35,
        5
    );
    drawLabel(
        layer,
        area,
        text,
        fonts.inter_12_medium,
        active && enabled ? theme::color::TEXT_PRIMARY : effectiveColor,
        enabled ? LV_OPA_COVER : OPACITY_55,
        LV_TEXT_ALIGN_CENTER
    );
}

FLASHMEM void drawPropertyCard(
    lv_layer_t* layer,
    const lv_area_t& origin,
    lv_coord_t y,
    const char* icon,
    const char* key,
    const char* value,
    uint32_t color,
    bool selected,
    bool enabled
) {
    const auto card = translated(origin, 0, y, SURFACE_WIDTH, 54);
    const uint32_t effectiveColor = enabled ? color : theme::color::INACTIVE;
    drawRect(
        layer,
        card,
        effectiveColor,
        selected && enabled ? OPACITY_18 : OPACITY_12,
        selected ? 2 : 1,
        selected && enabled ? LV_OPA_COVER : OPACITY_35,
        7
    );

    const auto iconArea = translated(origin, 12, y + 18, 18, 18);
    drawLabel(
        layer,
        iconArea,
        icon,
        standalone_fonts.icons_16,
        effectiveColor,
        enabled ? LV_OPA_COVER : OPACITY_55,
        LV_TEXT_ALIGN_CENTER
    );
    drawLabel(
        layer,
        translated(origin, 40, y + 7, 170, 15),
        key,
        fonts.inter_12_medium,
        theme::color::TEXT_SECONDARY,
        enabled ? LV_OPA_80 : OPACITY_55
    );
    drawLabel(
        layer,
        translated(origin, 40, y + 23, 228, 22),
        value,
        fonts.inter_14_semibold,
        enabled ? theme::color::TEXT_PRIMARY : theme::color::INACTIVE,
        enabled ? LV_OPA_COVER : OPACITY_55
    );
    if (selected) {
        drawLabel(
            layer,
            translated(origin, 252, y + 7, 38, 15),
            "OPT",
            fonts.inter_12_medium,
            effectiveColor,
            enabled ? LV_OPA_80 : OPACITY_55,
            LV_TEXT_ALIGN_RIGHT
        );
    }
}

}  // namespace

FLASHMEM ProjectTrackEditorOverlay::ProjectTrackEditorOverlay(lv_obj_t* parent) {
    createUi(parent);
}

FLASHMEM ProjectTrackEditorOverlay::~ProjectTrackEditorOverlay() {
    if (root_) {
        lv_obj_delete(root_);
        root_ = nullptr;
        surface_ = nullptr;
    }
}

FLASHMEM void ProjectTrackEditorOverlay::createUi(lv_obj_t* parent) {
    if (!parent) return;

    root_ = lv_obj_create(parent);
    lv_obj_remove_style_all(root_);
    lv_obj_add_flag(root_, LV_OBJ_FLAG_FLOATING);
    lv_obj_add_flag(root_, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_pos(root_, 0, 0);
    lv_obj_set_size(root_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(root_, lv_color_hex(theme::color::BACKGROUND), 0);
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);

    surface_ = lv_obj_create(root_);
    lv_obj_remove_style_all(surface_);
    lv_obj_add_flag(surface_, LV_OBJ_FLAG_FLOATING);
    lv_obj_add_flag(surface_, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_pos(surface_, SURFACE_X, SURFACE_Y);
    lv_obj_set_size(surface_, SURFACE_WIDTH, SURFACE_HEIGHT);
    lv_obj_clear_flag(surface_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(surface_, onDraw, LV_EVENT_DRAW_MAIN, this);
}

FLASHMEM void ProjectTrackEditorOverlay::render(
    const ProjectTrackEditorOverlayProps& props
) {
    if (!root_) return;
    if (!props.visible) {
        if (visible_) {
            lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
            visible_ = false;
        }
        return;
    }

    RenderCache next{};
    copyText(next.title, props.title);
    copyText(next.route, props.route);
    copyText(next.delay, props.delay);
    copyText(next.structureHint, props.structureHint);
    next.trackColor = props.trackColor;
    next.selectedProperty = props.selectedProperty;
    next.muted = props.muted;
    next.soloed = props.soloed;
    next.trackEnabled = props.trackEnabled;

    if (!rendered_ || !(cache_ == next)) {
        cache_ = next;
        if (surface_) lv_obj_invalidate(surface_);
        rendered_ = true;
    }
    if (!visible_) {
        lv_obj_clear_flag(root_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(root_);
        visible_ = true;
    }
}

FLASHMEM void ProjectTrackEditorOverlay::draw(lv_layer_t* layer) const {
    if (!layer || !surface_) return;

    lv_area_t origin{};
    lv_obj_get_coords(surface_, &origin);
    drawRect(layer, origin, theme::color::BACKGROUND, LV_OPA_COVER);

    drawLabel(
        layer,
        translated(origin, 0, 2, 148, 24),
        cache_.title.data(),
        fonts.inter_14_semibold,
        cache_.trackEnabled ? cache_.trackColor : theme::color::INACTIVE
    );
    drawStateChip(
        layer, origin, 154, 69, "MUTE", theme::color::STEP_NUDGE,
        cache_.muted, cache_.trackEnabled
    );
    drawStateChip(
        layer, origin, 231, 73, "SOLO", theme::color::MACRO_4,
        cache_.soloed, cache_.trackEnabled
    );

    drawPropertyCard(
        layer,
        origin,
        38,
        icons::MIDI_CHANNEL,
        "OUTPUT",
        cache_.route.data(),
        theme::color::STEP_STATE,
        cache_.selectedProperty ==
            core::state::project::ProjectTrackEditorProperty::CHANNEL,
        cache_.trackEnabled
    );
    drawPropertyCard(
        layer,
        origin,
        100,
        icons::OFFSET,
        "DELAY",
        cache_.delay.data(),
        theme::color::STEP_NUDGE,
        cache_.selectedProperty ==
            core::state::project::ProjectTrackEditorProperty::DELAY,
        cache_.trackEnabled
    );

    const auto structure = translated(origin, 0, 165, SURFACE_WIDTH, 31);
    drawRect(
        layer,
        structure,
        cache_.trackEnabled ? cache_.trackColor : theme::color::INACTIVE,
        OPACITY_12,
        1,
        OPACITY_35,
        6
    );
    drawLabel(
        layer,
        translated(origin, 12, 173, 142, 16),
        "STRUCTURE",
        fonts.inter_12_medium,
        cache_.trackEnabled ? theme::color::TEXT_PRIMARY : theme::color::INACTIVE,
        cache_.trackEnabled ? LV_OPA_COVER : OPACITY_55
    );
    drawLabel(
        layer,
        translated(origin, 156, 173, 122, 16),
        cache_.structureHint.data(),
        fonts.inter_12_medium,
        theme::color::TEXT_SECONDARY,
        cache_.trackEnabled ? LV_OPA_80 : OPACITY_55,
        LV_TEXT_ALIGN_RIGHT
    );
    drawLabel(
        layer,
        translated(origin, 282, 171, 12, 19),
        ">",
        fonts.inter_14_semibold,
        cache_.trackEnabled ? cache_.trackColor : theme::color::INACTIVE,
        cache_.trackEnabled ? LV_OPA_COVER : OPACITY_55,
        LV_TEXT_ALIGN_RIGHT
    );
}

FLASHMEM void ProjectTrackEditorOverlay::onDraw(lv_event_t* event) {
    auto* self = static_cast<ProjectTrackEditorOverlay*>(
        lv_event_get_user_data(event)
    );
    if (self) self->draw(lv_event_get_layer(event));
}

}  // namespace core::ui::project
