#include "ui/sequencer/SequencerTrackPastePreflightCard.hpp"

#include <cstdio>

#include <config/PlatformCompat.hpp>
#include <ms/ui/font/CoreFonts.hpp>

#include "ui/theme/StandaloneTheme.hpp"

namespace core::ui::sequencer {
namespace {

namespace theme = standalone::theme;

FLASHMEM lv_obj_t* createLabel(
    lv_obj_t* parent,
    const lv_font_t* font,
    uint32_t color,
    lv_text_align_t align = LV_TEXT_ALIGN_LEFT
) {
    lv_obj_t* label = lv_label_create(parent);
    if (!label) return nullptr;
    lv_obj_set_width(label, 258);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(label, align, 0);
    return label;
}

FLASHMEM uint32_t toneColor(SequencerTrackPastePreflightTone tone) {
    switch (tone) {
        case SequencerTrackPastePreflightTone::CONSTRUCTIVE:
        case SequencerTrackPastePreflightTone::SUCCESS:
            return theme::color::POSITIVE;
        case SequencerTrackPastePreflightTone::WARNING:
            return theme::color::WARNING;
        case SequencerTrackPastePreflightTone::ERROR:
            return theme::color::DESTRUCTIVE;
        case SequencerTrackPastePreflightTone::NEUTRAL:
        default:
            return theme::color::SECONDARY;
    }
}

}  // namespace

FLASHMEM SequencerTrackPastePreflightCard::SequencerTrackPastePreflightCard(
    lv_obj_t* parent
) {
    if (!parent) return;
    panel_ = lv_obj_create(parent);
    if (!panel_) return;
    lv_obj_remove_style_all(panel_);
    lv_obj_add_flag(panel_, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_add_flag(panel_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(panel_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(panel_, 276, LV_SIZE_CONTENT);
    lv_obj_set_style_max_height(panel_, 166, 0);
    lv_obj_set_style_bg_color(
        panel_, lv_color_hex(theme::color::SURFACE_RAISED), 0
    );
    lv_obj_set_style_bg_opa(panel_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(panel_, 1, 0);
    lv_obj_set_style_radius(panel_, 3, 0);
    lv_obj_set_style_pad_left(panel_, 8, 0);
    lv_obj_set_style_pad_right(panel_, 8, 0);
    lv_obj_set_style_pad_top(panel_, 6, 0);
    lv_obj_set_style_pad_bottom(panel_, 6, 0);
    lv_obj_set_style_pad_row(panel_, 2, 0);
    lv_obj_set_layout(panel_, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(panel_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        panel_,
        LV_FLEX_ALIGN_START,
        LV_FLEX_ALIGN_START,
        LV_FLEX_ALIGN_START
    );
    lv_obj_align(panel_, LV_ALIGN_TOP_MID, 0, 34);

    header_ = createLabel(
        panel_,
        fonts.compact_selected(),
        theme::color::TEXT_PRIMARY
    );
    mapping_ = createLabel(
        panel_,
        fonts.compact_selected(),
        theme::color::TEXT_PRIMARY
    );
    footprint_ = createLabel(
        panel_,
        fonts.compact_label(),
        theme::color::TEXT_PRIMARY
    );
    route_ = createLabel(
        panel_,
        fonts.meta_label(),
        theme::color::TEXT_SECONDARY
    );
    lane_bindings_ = createLabel(
        panel_,
        fonts.meta_label(),
        theme::color::TEXT_SECONDARY
    );
    detail_ = createLabel(
        panel_,
        fonts.compact_label(),
        theme::color::TEXT_SECONDARY
    );

    if (!panel_ || !header_ || !mapping_ || !footprint_ || !route_ ||
        !lane_bindings_ || !detail_) return;
    lv_obj_set_style_border_side(detail_, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_width(detail_, 1, 0);
    lv_obj_set_style_border_color(
        detail_, lv_color_hex(theme::color::BORDER_SUBTLE), 0
    );
    lv_obj_set_style_border_opa(detail_, LV_OPA_50, 0);
    lv_obj_set_style_pad_top(detail_, 3, 0);
    applied_timer_.emplace(
        APPLIED_CONFIRMATION_MS,
        &SequencerTrackPastePreflightCard::onAppliedTimeout,
        this
    );
}

FLASHMEM SequencerTrackPastePreflightCard::~SequencerTrackPastePreflightCard() {
    applied_timer_.reset();
    if (panel_) {
        lv_obj_delete(panel_);
        panel_ = nullptr;
    }
    header_ = nullptr;
    mapping_ = nullptr;
    footprint_ = nullptr;
    route_ = nullptr;
    lane_bindings_ = nullptr;
    detail_ = nullptr;
}

FLASHMEM bool SequencerTrackPastePreflightCard::valid() const {
    return panel_ && header_ && mapping_ && footprint_ && route_ &&
           lane_bindings_ && detail_ && applied_timer_ && applied_timer_->valid();
}

FLASHMEM void SequencerTrackPastePreflightCard::copyText(
    char* destination,
    size_t capacity,
    const char* source
) {
    if (!destination || capacity == 0) return;
    std::snprintf(destination, capacity, "%s", source ? source : "");
}

FLASHMEM void SequencerTrackPastePreflightCard::onAppliedTimeout(lv_timer_t* timer) {
    auto* self = static_cast<SequencerTrackPastePreflightCard*>(
        lv_timer_get_user_data(timer)
    );
    if (!self) return;
    self->dismissed_applied_generation_ = self->shown_applied_generation_;
    self->hide();
    if (self->applied_timer_) self->applied_timer_->pause();
}

FLASHMEM void SequencerTrackPastePreflightCard::hide() {
    if (panel_) lv_obj_add_flag(panel_, LV_OBJ_FLAG_HIDDEN);
}

FLASHMEM void SequencerTrackPastePreflightCard::show() {
    if (!panel_) return;
    lv_obj_clear_flag(panel_, LV_OBJ_FLAG_HIDDEN);
}

FLASHMEM void SequencerTrackPastePreflightCard::applyTone(
    SequencerTrackPastePreflightTone tone
) {
    if (!panel_ || !header_) return;
    const lv_color_t color = lv_color_hex(toneColor(tone));
    lv_obj_set_style_border_color(panel_, color, 0);
    lv_obj_set_style_border_opa(panel_, LV_OPA_80, 0);
    lv_obj_set_style_text_color(header_, color, 0);
}

FLASHMEM void SequencerTrackPastePreflightCard::renderText(
    const SequencerTrackPastePreflightViewModel& model
) {
    copyText(header_text_.data(), header_text_.size(), model.header.data());
    copyText(mapping_text_.data(), mapping_text_.size(), model.mapping.data());
    copyText(
        footprint_text_.data(),
        footprint_text_.size(),
        model.footprint.data()
    );
    copyText(route_text_.data(), route_text_.size(), model.route.data());
    copyText(
        lane_bindings_text_.data(),
        lane_bindings_text_.size(),
        model.laneBindings.data()
    );
    copyText(detail_text_.data(), detail_text_.size(), model.detail.data());
    const auto renderLabel = [](lv_obj_t* label, const char* text) {
        lv_label_set_text_static(label, text);
        if (text && text[0] != '\0') {
            lv_obj_clear_flag(label, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
        }
    };
    renderLabel(header_, header_text_.data());
    renderLabel(mapping_, mapping_text_.data());
    renderLabel(footprint_, footprint_text_.data());
    renderLabel(route_, route_text_.data());
    renderLabel(lane_bindings_, lane_bindings_text_.data());
    renderLabel(detail_, detail_text_.data());
}

FLASHMEM void SequencerTrackPastePreflightCard::render(
    const SequencerTrackPastePreflightViewModel& model
) {
    if (!valid()) return;
    if (!model.visible || model.phase == SequencerTrackPastePreflightPhase::HIDDEN) {
        if (shown_applied_generation_ != 0) {
            dismissed_applied_generation_ = shown_applied_generation_;
        }
        if (applied_timer_) applied_timer_->pause();
        hide();
        return;
    }

    if (model.phase == SequencerTrackPastePreflightPhase::APPLIED) {
        if (!shouldShowSequencerTrackPasteAppliedConfirmation(
                model,
                dismissed_applied_generation_
            )) {
            hide();
            return;
        }
        if (model.operationGeneration != shown_applied_generation_) {
            shown_applied_generation_ = model.operationGeneration;
            // Recreate the RAII timer so every distinct applied generation gets
            // the full confirmation interval. PausableTimer intentionally does
            // not expose the underlying LVGL timer or a mutable period/reset API.
            applied_timer_.reset();
            applied_timer_.emplace(
                APPLIED_CONFIRMATION_MS,
                &SequencerTrackPastePreflightCard::onAppliedTimeout,
                this
            );
            if (applied_timer_->valid()) applied_timer_->resume();
        }
    } else {
        if (shown_applied_generation_ != 0) {
            dismissed_applied_generation_ = shown_applied_generation_;
        }
        if (applied_timer_) applied_timer_->pause();
    }

    applyTone(model.tone);
    renderText(model);
    show();
}

}  // namespace core::ui::sequencer
