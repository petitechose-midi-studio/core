#include "TransportBar.hpp"

#include <config/PlatformCompat.hpp>
#include <ms/ui/font/CoreFonts.hpp>
#include <oc/state/Bind.hpp>
#include <oc/type/TextFormat.hpp>

#include "ui/common/ContextSurfaceDraw.hpp"
#include "ui/font/StandaloneIcons.hpp"
#include "ui/theme/StandaloneTheme.hpp"

namespace core::ui {
namespace theme = standalone::theme;
namespace icons = standalone::icons;

FLASHMEM TransportBar::TransportBar(lv_obj_t* parent, const core::state::StatusBarState& state)
    : state_(state) {
    container_ = lv_obj_create(parent);
    if (!container_) return;
    lv_obj_remove_style_all(container_);
    lv_obj_set_style_bg_color(container_, lv_color_hex(theme::color::SURFACE_IDLE), 0);
    lv_obj_set_style_bg_opa(container_, LV_OPA_COVER, 0);
    lv_obj_set_size(container_, theme::layout::TRANSPORT_CENTER_WIDTH,
                    theme::layout::TRANSPORT_BAR_HEIGHT);
    lv_obj_align(container_, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(container_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(container_, &TransportBar::onDraw, LV_EVENT_DRAW_MAIN, this);
    setupBindings();
    refresh();
}

FLASHMEM TransportBar::~TransportBar() {
    subs_.clear();
    if (container_) lv_obj_delete(container_);
}

FLASHMEM void TransportBar::setupBindings() {
    oc::state::bind(subs_)
        .on(state_.ccInActive, [this](bool) { refresh(); })
        .on(state_.ccOutActive, [this](bool) { refresh(); })
        .on(state_.playing, [this](bool) { refresh(); })
        .on(state_.tempoDisplay, [this](float) { refresh(); })
        .on(state_.tempoLocked, [this](bool) { refresh(); })
        .on(state_.transportLocked, [this](bool) { refresh(); })
        .on(state_.beatPulse, [this](bool) { refresh(); });
}

void TransportBar::refresh() {
    const auto tempo = state_.tempoDisplay.get();
    const uint8_t flags =
        (state_.ccInActive.get() ? 1U : 0U) |
        (state_.ccOutActive.get() ? 2U : 0U) |
        (state_.playing.get() ? 4U : 0U) |
        (state_.tempoLocked.get() ? 8U : 0U) |
        (state_.transportLocked.get() ? 16U : 0U) |
        (state_.beatPulse.get() ? 32U : 0U);
    if (initialized_ && tempo_ == tempo && flags_ == flags) return;
    if (!initialized_ || tempo_ != tempo) {
        tempo_ = tempo;
        oc::type::text::formatFixed1(tempo_text_.data(), tempo_text_.size(), tempo);
    }
    flags_ = flags;
    initialized_ = true;
    if (container_) lv_obj_invalidate(container_);
}

void TransportBar::draw(lv_layer_t* layer) const {
    if (!layer || !container_) return;
    lv_area_t bounds{};
    lv_obj_get_coords(container_, &bounds);
    const auto glyph = [&](int x, const char* value, uint32_t color, bool small = false) {
        surface::text(layer, surface::area(bounds, x, small ? 4 : 3, 16, 17), value,
            small ? standalone_fonts.icons_12 : standalone_fonts.icons_14, color,
            LV_OPA_COVER, LV_TEXT_ALIGN_CENTER);
    };
    if (flags_ & 8U) {
        glyph(0, icons::LOCK, theme::color::MIDI_IN_ACTIVE, true);
    } else {
        surface::fill(layer, surface::area(bounds, 5, 8, 5, 5),
            flags_ & 32U ? theme::color::BEAT_PULSE : theme::color::TEXT_DISABLED,
            LV_OPA_COVER, LV_RADIUS_CIRCLE);
    }
    surface::text(layer, surface::area(bounds, 16, 3, 40, 17),
        tempo_text_.data(), fonts.compact_label(), theme::color::TEXT_SECONDARY);
    glyph(54, icons::TRANSPORT_PLAY,
        flags_ & 4U ? theme::color::PLAY_ACTIVE : theme::color::TEXT_DISABLED);
    const auto activity = flags_ & 3U;
    glyph(77, icons::KNOB,
        activity == 3U ? theme::color::MACRO_4 :
        activity == 1U ? theme::color::MIDI_IN_ACTIVE :
        activity == 2U ? theme::color::BEAT_PULSE : theme::color::TEXT_DISABLED, true);
    if (flags_ & 16U) glyph(98, icons::LOCK, theme::color::MIDI_IN_ACTIVE, true);
}

void TransportBar::onDraw(lv_event_t* event) {
    auto* self = static_cast<TransportBar*>(lv_event_get_user_data(event));
    if (self) self->draw(lv_event_get_layer(event));
}

FLASHMEM void TransportBar::show() {
    if (container_) lv_obj_clear_flag(container_, LV_OBJ_FLAG_HIDDEN);
}

FLASHMEM void TransportBar::hide() {
    if (container_) lv_obj_add_flag(container_, LV_OBJ_FLAG_HIDDEN);
}

FLASHMEM bool TransportBar::isVisible() const {
    return container_ && !lv_obj_has_flag(container_, LV_OBJ_FLAG_HIDDEN);
}
}  // namespace core::ui
