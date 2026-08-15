#include "ui/project/ProjectTrackEditorOverlay.hpp"

#include <cstring>

#include <config/PlatformCompat.hpp>
#include <ms/ui/font/CoreFonts.hpp>

#include "ui/font/StandaloneFonts.hpp"
#include "ui/font/StandaloneIcons.hpp"
#include "ui/interaction/InteractiveSurfaceVisual.hpp"
#include "ui/theme/StandaloneTheme.hpp"

namespace core::ui::project {
namespace {

namespace theme = ::standalone::theme;
namespace icons = ::standalone::icons;

constexpr lv_coord_t SURFACE_X = 8;
constexpr lv_coord_t SURFACE_Y = 4;
constexpr lv_coord_t SURFACE_WIDTH = 304;
constexpr lv_coord_t SURFACE_HEIGHT = 196;

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

FLASHMEM void drawInteractiveSurface(
    lv_layer_t* layer,
    const lv_area_t& area,
    bool selected,
    bool enabled
) {
    using core::ui::interaction::InteractiveSurfaceState;
    const auto state = !enabled
        ? InteractiveSurfaceState::DISABLED
        : (selected
               ? InteractiveSurfaceState::FOCUSED
               : InteractiveSurfaceState::IDLE);
    const auto visual = core::ui::interaction::interactiveSurfaceVisual(state);
    lv_draw_rect_dsc_t descriptor;
    lv_draw_rect_dsc_init(&descriptor);
    descriptor.bg_color = lv_color_hex(visual.backgroundColor);
    descriptor.bg_opa = visual.backgroundOpacity;
    descriptor.border_color = lv_color_hex(visual.borderColor);
    descriptor.border_width =
        theme::layout::INTERACTIVE_SURFACE_BORDER_WIDTH;
    descriptor.border_opa = visual.borderOpacity;
    descriptor.radius = theme::layout::INTERACTIVE_SURFACE_RADIUS;
    lv_draw_rect(layer, &descriptor, &area);
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
    drawInteractiveSurface(layer, card, selected, enabled);

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
        fonts.meta_label(),
        theme::color::TEXT_SECONDARY,
        enabled ? LV_OPA_80 : OPACITY_55
    );
    drawLabel(
        layer,
        translated(origin, 40, y + 23, 228, 22),
        value,
        fonts.primary_value(),
        enabled ? theme::color::TEXT_PRIMARY : theme::color::INACTIVE,
        enabled ? LV_OPA_COVER : OPACITY_55
    );
    if (selected) {
        drawLabel(
            layer,
            translated(origin, 252, y + 7, 38, 15),
            "OPT",
            fonts.meta_label(),
            theme::color::FOCUS_EDIT,
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
    copyText(next.status, props.status);
    next.trackColor = props.trackColor;
    next.statusColor = props.statusColor;
    next.selectedProperty = props.selectedProperty;
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

    drawRect(
        layer,
        translated(origin, 0, 3, 3, 20),
        cache_.trackEnabled ? cache_.trackColor : theme::color::INACTIVE,
        cache_.trackEnabled ? LV_OPA_COVER : OPACITY_55,
        0,
        LV_OPA_TRANSP,
        2
    );

    drawLabel(
        layer,
        translated(origin, 10, 2, 174, 24),
        cache_.title.data(),
        fonts.context_title(),
        cache_.trackEnabled ? theme::color::TEXT_PRIMARY : theme::color::INACTIVE
    );
    drawLabel(
        layer,
        translated(origin, 184, 4, 120, 18),
        cache_.status.data(),
        fonts.meta_label(),
        cache_.trackEnabled ? cache_.statusColor : theme::color::INACTIVE,
        cache_.trackEnabled ? LV_OPA_80 : OPACITY_55,
        LV_TEXT_ALIGN_RIGHT
    );

    drawPropertyCard(
        layer,
        origin,
        38,
        icons::MIDI_CHANNEL,
        "MIDI OUTPUT",
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
    drawInteractiveSurface(
        layer,
        structure,
        cache_.selectedProperty ==
            core::state::project::ProjectTrackEditorProperty::TYPE,
        cache_.trackEnabled
    );
    drawLabel(
        layer,
        translated(origin, 12, 173, 142, 16),
        "TYPE",
        fonts.meta_label(),
        cache_.trackEnabled ? theme::color::TEXT_PRIMARY : theme::color::INACTIVE,
        cache_.trackEnabled ? LV_OPA_COVER : OPACITY_55
    );
    drawLabel(
        layer,
        translated(origin, 156, 173, 88, 16),
        cache_.structureHint.data(),
        fonts.meta_label(),
        theme::color::TEXT_SECONDARY,
        cache_.trackEnabled ? LV_OPA_80 : OPACITY_55,
        LV_TEXT_ALIGN_RIGHT
    );
    drawLabel(
        layer,
        translated(origin, 254, 171, 40, 19),
        cache_.selectedProperty ==
                core::state::project::ProjectTrackEditorProperty::TYPE
            ? "OPT"
            : ">",
        cache_.selectedProperty ==
                core::state::project::ProjectTrackEditorProperty::TYPE
            ? fonts.meta_label()
            : fonts.primary_value(),
        cache_.selectedProperty ==
                core::state::project::ProjectTrackEditorProperty::TYPE
            ? theme::color::FOCUS_EDIT
            : (cache_.trackEnabled
                   ? theme::color::TEXT_SECONDARY
                   : theme::color::INACTIVE),
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
