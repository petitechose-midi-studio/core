#include "ui/project/ProjectTrackEditorOverlay.hpp"

#include <cstring>

#include <config/PlatformCompat.hpp>
#include <ms/ui/font/CoreFonts.hpp>

#include "ui/common/ContextSurfaceDraw.hpp"
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

FLASHMEM void drawPropertyCard(
    lv_layer_t* layer,
    const lv_area_t& origin,
    lv_coord_t y,
    const char* icon,
    const char* key,
    const char* value,
    uint32_t color,
    bool selected,
    bool enabled,
    bool activate = false
) {
    const auto card = surface::area(origin, 0, y, SURFACE_WIDTH, 36);
    const uint32_t effectiveColor = enabled ? color : theme::color::INACTIVE;
    drawInteractiveSurface(layer, card, selected, enabled);

    const auto iconArea = surface::area(origin, 12, y + 9, 18, 18);
    surface::text(
        layer,
        iconArea,
        icon,
        standalone_fonts.icons_16,
        effectiveColor,
        enabled ? LV_OPA_COVER : OPACITY_55,
        LV_TEXT_ALIGN_CENTER
    );
    surface::text(
        layer,
        surface::area(origin, 40, y + 5, 78, 24),
        key,
        fonts.meta_label(),
        theme::color::TEXT_SECONDARY,
        enabled ? LV_OPA_80 : OPACITY_55
    );
    surface::text(
        layer,
        surface::area(origin, 122, y + 5, 126, 24),
        value,
        fonts.compact_selected(),
        enabled ? theme::color::TEXT_PRIMARY : theme::color::INACTIVE,
        enabled ? LV_OPA_COVER : OPACITY_55,
        LV_TEXT_ALIGN_RIGHT
    );
    if (selected) {
        surface::text(
            layer,
            surface::area(origin, 258, y + 5, 32, 24),
            activate ? ">" : "OPT",
            activate ? fonts.primary_value() : fonts.meta_label(),
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
    next.drum = props.drum;

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

FLASHMEM void ProjectTrackEditorOverlay::setContentVisible(bool visible) {
    if (!surface_ || content_visible_ == visible) return;
    content_visible_ = visible;
    if (visible) {
        lv_obj_clear_flag(surface_, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(surface_, LV_OBJ_FLAG_HIDDEN);
    }
}

FLASHMEM void ProjectTrackEditorOverlay::draw(lv_layer_t* layer) const {
    if (!layer || !surface_) return;

    lv_area_t origin{};
    lv_obj_get_coords(surface_, &origin);
    surface::fill(layer, origin, theme::color::BACKGROUND, LV_OPA_COVER);

    surface::fill(
        layer,
        surface::area(origin, 0, 3, 3, 20),
        cache_.trackEnabled ? cache_.trackColor : theme::color::INACTIVE,
        cache_.trackEnabled ? LV_OPA_COVER : OPACITY_55,
        2
    );

    surface::text(
        layer,
        surface::area(origin, 10, 2, 174, 24),
        cache_.title.data(),
        fonts.context_title(),
        cache_.trackEnabled ? theme::color::TEXT_PRIMARY : theme::color::INACTIVE
    );
    surface::text(
        layer,
        surface::area(origin, 184, 4, 120, 18),
        cache_.status.data(),
        fonts.meta_label(),
        cache_.trackEnabled ? cache_.statusColor : theme::color::INACTIVE,
        cache_.trackEnabled ? LV_OPA_80 : OPACITY_55,
        LV_TEXT_ALIGN_RIGHT
    );

    drawPropertyCard(
        layer,
        origin,
        34,
        icons::ACTION_RENAME,
        "Name",
        cache_.title.data(),
        theme::color::TEXT_PRIMARY,
        cache_.selectedProperty ==
            core::state::project::ProjectTrackEditorProperty::NAME,
        cache_.trackEnabled,
        true
    );
    drawPropertyCard(
        layer,
        origin,
        74,
        icons::MIDI_CHANNEL,
        "MIDI output",
        cache_.route.data(),
        theme::color::ROUTING,
        cache_.selectedProperty ==
            core::state::project::ProjectTrackEditorProperty::CHANNEL,
        cache_.trackEnabled
    );
    drawPropertyCard(
        layer,
        origin,
        114,
        icons::OFFSET,
        "Delay",
        cache_.delay.data(),
        theme::color::STEP_NUDGE,
        cache_.selectedProperty ==
            core::state::project::ProjectTrackEditorProperty::DELAY,
        cache_.trackEnabled
    );

    drawPropertyCard(
        layer,
        origin,
        154,
        cache_.drum ? icons::DRUM_GENERIC : icons::NOTE,
        "Type",
        cache_.structureHint.data(),
        theme::color::ROUTING,
        cache_.selectedProperty ==
            core::state::project::ProjectTrackEditorProperty::TYPE,
        cache_.trackEnabled
    );
}

FLASHMEM void ProjectTrackEditorOverlay::onDraw(lv_event_t* event) {
    auto* self = static_cast<ProjectTrackEditorOverlay*>(
        lv_event_get_user_data(event)
    );
    if (self) self->draw(lv_event_get_layer(event));
}

}  // namespace core::ui::project
