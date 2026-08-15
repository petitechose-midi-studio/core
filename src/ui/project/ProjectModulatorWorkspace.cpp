#include "ui/project/ProjectModulatorWorkspace.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include <config/PlatformCompat.hpp>
#include <ms/ui/font/CoreFonts.hpp>
#include <oc/diagnostics/Performance.hpp>
#include <oc/time/Time.hpp>

#include "state/modulation/ModulationDepthParameterMapping.hpp"
#include "state/modulation/ProjectControlMacroOps.hpp"
#include "state/modulation/ProjectControlRuntime.hpp"
#include "ui/font/StandaloneIcons.hpp"
#include "ui/modulation/ModulatorLfoUiModel.hpp"
#include "ui/modulation/ModulatorAdsrUiModel.hpp"
#include "ui/modulation/ModulatorSparklineModel.hpp"
#include "ui/project/ProjectModulatorUiModel.hpp"
#include "ui/theme/StandaloneTheme.hpp"

namespace core::ui::project {
namespace {

namespace theme = standalone::theme;
using namespace core::state::modulation;
using Item = core::state::project::modulators::SourceDetailItem;
namespace adsr_ui = core::ui::modulation::adsr;
namespace depth_parameter = core::state::modulation::depth;

constexpr lv_coord_t HEADER_HEIGHT = 21;
constexpr lv_coord_t CURVE_Y = 22;
constexpr lv_coord_t CURVE_HEIGHT = 66;
constexpr lv_coord_t CARD_TOP_Y = 93;
constexpr lv_coord_t CARD_BOTTOM_Y = 134;
constexpr lv_coord_t CARD_TOP_HEIGHT = 36;
constexpr lv_coord_t CARD_BOTTOM_HEIGHT = 37;
constexpr lv_coord_t CARD_GAP = 3;
constexpr lv_coord_t HORIZONTAL_PAD = 4;

const char SOURCE_KIND_LFO[] PROGMEM = "LFO";
const char SOURCE_KIND_MOTION[] PROGMEM = "MOTION";
const char SOURCE_KIND_ADSR[] PROGMEM = "DAHDSR";
const char SOURCE_STATE_ON[] PROGMEM = "ON";
const char SOURCE_STATE_OFF[] PROGMEM = "OFF";
const char SOURCE_STATE_FORMAT[] PROGMEM = "%s · %s";

FLASHMEM const char* sourceTimingLabel(const ModulatorSourceState& source) {
    if (source.kind == ModulatorKind::LFO) {
        return source.parameters.lfo.timing == ModulatorTimingMode::FREE
            ? "FREE" : "SYNC";
    }
    if (source.kind == ModulatorKind::ADSR) {
        return modulatorAdsrTiming(source.parameters.adsr.traits) ==
                ModulatorTimingMode::FREE
            ? "FREE" : "SYNC";
    }
    return nullptr;
}

FLASHMEM lv_obj_t* createLabel(
    lv_obj_t* parent,
    const lv_font_t* font,
    uint32_t color,
    lv_text_align_t align = LV_TEXT_ALIGN_LEFT
) {
    if (!parent) return nullptr;
    auto* label = lv_label_create(parent);
    if (!label) return nullptr;
    lv_obj_remove_style_all(label);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(label, align, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    return label;
}

template <size_t Capacity>
FLASHMEM bool copyText(
    std::array<char, Capacity>& destination,
    const char* source
) {
    static_assert(Capacity > 0U);
    std::array<char, Capacity> next{};
    if (source) {
        size_t index = 0U;
        while (index + 1U < Capacity && source[index] != '\0') {
            next[index] = source[index];
            ++index;
        }
    }
    if (destination == next) return false;
    destination = next;
    return true;
}

FLASHMEM uint16_t normalizedToQ16(float value) {
    return static_cast<uint16_t>(std::lround(
        std::clamp(value, 0.0f, 1.0f) * 65535.0f
    ));
}

FLASHMEM bool sourceUsesPositiveDomain(
    const ProjectControlState& control,
    const ModulatorSourceState& source
) {
    if (source.kind == ModulatorKind::LFO) return false;
    if (source.kind == ModulatorKind::ADSR) return true;
    const auto* curve = findProjectCurve(
        control.authored.curves,
        source.parameters.recordedCurveId
    );
    return curve != nullptr &&
        curve->valueDomain == ProjectCurveValueDomain::ABSOLUTE_UNIPOLAR;
}

FLASHMEM bool editableItem(Item item, ModulatorKind kind) {
    if (item == Item::DEPTH) return true;
    if (kind == ModulatorKind::LFO) {
        return item == Item::SHAPE || item == Item::TIMING ||
               item == Item::RATE || item == Item::PHASE ||
               item == Item::RETRIGGER;
    }
    if (kind == ModulatorKind::ADSR) {
        return item == Item::DELAY || item == Item::ATTACK ||
               item == Item::HOLD || item == Item::DECAY ||
               item == Item::SUSTAIN || item == Item::RELEASE ||
               item == Item::TIMING || item == Item::SMOOTH ||
               item == Item::RESPONSE ||
               item == Item::RETRIGGER;
    }
    return kind == ModulatorKind::RECORDED_SHAPE && item == Item::LENGTH;
}

FLASHMEM bool actionableItem(Item item) {
    return item == Item::OPTIONS || item == Item::RENAME ||
           item == Item::DESTINATIONS ||
           item == Item::TRIGGER || item == Item::RECORD;
}

FLASHMEM bool captureMatches(
    const ProjectModulatorWorkspaceProps& props
) {
    return props.capture != nullptr && props.capture->active() &&
        props.capture->mode == ProjectRecordedShapeCaptureMode::REPLACE_EXISTING &&
        props.source != nullptr && props.capture->sourceId == props.source->id &&
        props.capture->take != nullptr;
}

FLASHMEM const char* captureStatusLabel(
    ProjectRecordedShapeCaptureStatus status
) {
    switch (status) {
        case ProjectRecordedShapeCaptureStatus::ARMED:
            return "ARMED · TURN OPT";
        case ProjectRecordedShapeCaptureStatus::RECORDING:
            return "RECORDING";
        case ProjectRecordedShapeCaptureStatus::REDUCED:
            return "RECORDING · REDUCED";
        default:
            return "RECORDING";
    }
}

FLASHMEM int itemOrdinal(
    const core::state::project::modulators::SourceDetailLayout& layout,
    Item item
) {
    for (uint8_t index = 0U; index < layout.count; ++index) {
        if (layout.at(index) == item) return static_cast<int>(index);
    }
    return -1;
}

FLASHMEM void populateSourceItemRow(
    const ProjectControlState& control,
    const ModulatorSourceState& source,
    Item item,
    ms::ui::KeyValueRowBuffer& out
) {
    const auto detailLayout =
        core::state::project::modulators::sourceDetailLayout(source.kind);
    const int detailOrdinal = itemOrdinal(detailLayout, item);
    if (detailOrdinal >= 0) {
        core::ui::project::modulators::populateSourceDetailRow(
            control,
            source,
            detailOrdinal,
            out
        );
        return;
    }

    const auto optionsLayout =
        core::state::project::modulators::sourceOptionsLayout(source.kind);
    const int optionsOrdinal = itemOrdinal(optionsLayout, item);
    if (optionsOrdinal >= 0) {
        core::ui::project::modulators::populateSourceOptionsRow(
            control,
            source,
            optionsOrdinal,
            out
        );
    }
}

FLASHMEM void populateAuditionDepthRow(
    const ProjectControlState* control,
    const ModulationBindingState* binding,
    ms::ui::KeyValueRowBuffer& out
) {
    std::snprintf(out.key.data(), out.key.size(), "Depth");
    const int percent = control != nullptr && binding != nullptr
        ? depth_parameter::amountQ15ToPercent(
              binding->amountQ15,
              depth_parameter::scaleFor(
                  control->authored.modulation,
                  control->authored.curves,
                  *binding
              )
          )
        : 0;
    std::snprintf(out.value.data(), out.value.size(), "%+d%%", percent);
    std::snprintf(
        out.icon.data(),
        out.icon.size(),
        "%s",
        standalone::icons::MACRO_MODULATION
    );
    out.iconFont = standalone_fonts.icons_14;
    out.iconColor = theme::color::MACRO_MODULATION;
}

}  // namespace

FLASHMEM ProjectModulatorWorkspace::ProjectModulatorWorkspace(lv_obj_t* parent) {
    createUi(parent);
}

FLASHMEM ProjectModulatorWorkspace::~ProjectModulatorWorkspace() {
    edit_feedback_timer_.reset();
    curve_preview_.reset();
    if (root_) {
        lv_obj_delete(root_);
        root_ = nullptr;
    }
    source_icon_ = nullptr;
    title_ = nullptr;
    state_icon_ = nullptr;
    state_text_ = nullptr;
    cards_ = {};
    edit_feedback_ = nullptr;
    edit_feedback_key_ = nullptr;
    edit_feedback_value_ = nullptr;
}

FLASHMEM bool ProjectModulatorWorkspace::valid() const {
    if (!root_ || !source_icon_ || !title_ || !state_icon_ || !state_text_ ||
        !curve_preview_ || !curve_preview_->getElement() || !edit_feedback_ ||
        !edit_feedback_key_ || !edit_feedback_value_ || !edit_feedback_timer_ ||
        !edit_feedback_timer_->valid()) {
        return false;
    }
    for (const auto& card : cards_) {
        if (!card.root || !card.icon || !card.label || !card.value) return false;
    }
    return true;
}

FLASHMEM void ProjectModulatorWorkspace::createUi(lv_obj_t* parent) {
    if (!parent) return;
    root_ = lv_obj_create(parent);
    if (!root_) return;
    lv_obj_remove_style_all(root_);
    lv_obj_set_width(root_, LV_PCT(100));
    lv_obj_set_height(root_, 0);
    lv_obj_set_flex_grow(root_, 1);
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);

    source_icon_ = createLabel(
        root_,
        standalone_fonts.icons_16,
        theme::color::MACRO_MODULATION,
        LV_TEXT_ALIGN_CENTER
    );
    if (source_icon_) {
        lv_obj_set_pos(source_icon_, 5, 2);
        lv_obj_set_size(source_icon_, 18, 17);
    }
    title_ = createLabel(root_, fonts.context_title(), theme::color::TEXT_PRIMARY);
    if (title_) {
        lv_obj_set_pos(title_, 27, 1);
        lv_obj_set_size(title_, 110, HEADER_HEIGHT);
    }
    state_icon_ = createLabel(
        root_,
        standalone_fonts.icons_12,
        theme::color::MACRO_MODULATION,
        LV_TEXT_ALIGN_RIGHT
    );
    if (state_icon_) {
        lv_obj_set_pos(state_icon_, 140, 3);
        lv_obj_set_size(state_icon_, 14, 15);
    }
    state_text_ = createLabel(
        root_,
        fonts.meta_label(),
        theme::color::TEXT_SECONDARY,
        LV_TEXT_ALIGN_RIGHT
    );
    if (state_text_) {
        lv_obj_set_pos(state_text_, 156, 3);
        lv_obj_set_size(state_text_, 156, 15);
    }

    curve_preview_ = core::app::makeExtmemUnique<ms::ui::CurvePreviewWidget>(root_);
    if (curve_preview_ && curve_preview_->getElement()) {
        auto* curve = curve_preview_->getElement();
        lv_obj_set_pos(curve, HORIZONTAL_PAD, CURVE_Y);
        lv_obj_set_size(curve, LV_PCT(100), CURVE_HEIGHT);
        lv_obj_set_style_bg_color(curve, lv_color_hex(theme::color::TEXT_PRIMARY), 0);
        lv_obj_set_style_bg_opa(curve, LV_OPA_10, 0);
        lv_obj_set_style_border_width(curve, 1, 0);
        lv_obj_set_style_border_color(
            curve,
            lv_color_hex(theme::color::MACRO_MODULATION),
            0
        );
        lv_obj_set_style_border_opa(curve, LV_OPA_30, 0);
        lv_obj_set_style_radius(curve, 3, 0);
    }

    for (uint8_t index = 0U; index < cards_.size(); ++index) {
        createCard(index);
    }

    edit_feedback_ = lv_obj_create(root_);
    if (edit_feedback_) {
        lv_obj_remove_style_all(edit_feedback_);
        lv_obj_set_pos(edit_feedback_, 69, 39);
        lv_obj_set_size(edit_feedback_, 146, 34);
        lv_obj_set_style_bg_color(
            edit_feedback_,
            lv_color_hex(theme::color::BACKGROUND),
            0
        );
        lv_obj_set_style_bg_opa(edit_feedback_, LV_OPA_90, 0);
        lv_obj_set_style_border_width(edit_feedback_, 1, 0);
        lv_obj_set_style_border_color(
            edit_feedback_,
            lv_color_hex(theme::color::FOCUS_EDIT),
            0
        );
        lv_obj_set_style_border_opa(edit_feedback_, LV_OPA_80, 0);
        lv_obj_set_style_radius(edit_feedback_, 3, 0);
        lv_obj_clear_flag(edit_feedback_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(edit_feedback_, LV_OBJ_FLAG_HIDDEN);
    }
    edit_feedback_key_ = createLabel(
        edit_feedback_,
        fonts.meta_label(),
        theme::color::TEXT_SECONDARY
    );
    if (edit_feedback_key_) {
        lv_obj_set_pos(edit_feedback_key_, 7, 2);
        lv_obj_set_size(edit_feedback_key_, 132, 13);
    }
    edit_feedback_value_ = createLabel(
        edit_feedback_,
        fonts.primary_value(),
        theme::color::TEXT_PRIMARY
    );
    if (edit_feedback_value_) {
        lv_obj_set_pos(edit_feedback_value_, 7, 15);
        lv_obj_set_size(edit_feedback_value_, 132, 16);
    }
    edit_feedback_timer_.emplace(
        EDIT_FEEDBACK_POLL_MS,
        &ProjectModulatorWorkspace::onEditFeedbackTimeout,
        this
    );
}

FLASHMEM void ProjectModulatorWorkspace::createCard(uint8_t index) {
    if (!root_ || index >= cards_.size()) return;
    auto& card = cards_[index];
    card.root = lv_obj_create(root_);
    if (!card.root) return;
    lv_obj_remove_style_all(card.root);
    lv_obj_set_style_radius(card.root, 3, 0);
    lv_obj_set_style_border_width(card.root, 1, 0);
    lv_obj_clear_flag(card.root, LV_OBJ_FLAG_SCROLLABLE);

    card.icon = createLabel(
        card.root,
        standalone_fonts.icons_12,
        theme::color::TEXT_SECONDARY,
        LV_TEXT_ALIGN_CENTER
    );
    if (card.icon) {
        lv_obj_set_pos(card.icon, 4, 5);
        lv_obj_set_size(card.icon, 15, 14);
    }
    card.label = createLabel(
        card.root,
        fonts.meta_label(),
        theme::color::TEXT_SECONDARY
    );
    if (card.label) {
        lv_obj_set_pos(card.label, 21, 2);
        lv_obj_set_size(card.label, 110, 14);
    }
    card.value = createLabel(
        card.root,
        fonts.compact_selected(),
        theme::color::TEXT_PRIMARY
    );
    if (card.value) {
        lv_obj_set_pos(card.value, 21, 16);
        lv_obj_set_size(card.value, 110, 16);
    }
}

FLASHMEM void ProjectModulatorWorkspace::renderHeader(
    const ProjectModulatorWorkspaceProps& props
) {
    const auto& source = *props.source;
    const bool audition = props.session.audition();
    const bool existing = props.session.existingAudition();
    const bool enabled =
        (source.flags & PROJECT_MODULATOR_FLAG_ENABLED) != 0U;
    const bool recording = captureMatches(props);
    if (copyText(titleText_, source.name.data())) {
        lv_label_set_text_static(title_, titleText_.data());
    }
    standalone::icons::set(
        source_icon_,
        source.kind == ModulatorKind::LFO
            ? standalone::icons::MACRO_MODULATION
            : (source.kind == ModulatorKind::ADSR
                ? standalone::icons::NOTE_PROP_GATE
                : standalone::icons::MACRO_AUTOMATION),
        standalone::icons::Size::L
    );
    lv_obj_set_style_text_color(
        source_icon_,
        lv_color_hex(
            enabled ? theme::color::MACRO_MODULATION : theme::color::INACTIVE
        ),
        0
    );
    standalone::icons::set(
        state_icon_,
        recording ? standalone::icons::MACRO_AUTOMATION
        : existing ? standalone::icons::LOCK
                       : (audition ? standalone::icons::ACTION_APPLY
                       : (enabled ? standalone::icons::STATUS_RESUME
                                  : standalone::icons::STATUS_PAUSED)),
        standalone::icons::Size::S
    );
    lv_obj_set_style_text_color(
        state_icon_,
        lv_color_hex(
            recording || audition
                ? theme::color::LIVE_TIME
                : (enabled ? theme::color::CONTENT_ACTIVE
                           : theme::color::INACTIVE)
        ),
        0
    );
    if (recording) {
        std::snprintf(
            stateText_.data(),
            stateText_.size(),
            "%s",
            captureStatusLabel(props.capture->status)
        );
    } else if (existing && props.transientFeedback &&
        props.transientFeedback[0] != '\0') {
        std::snprintf(
            stateText_.data(),
            stateText_.size(),
            "%s",
            props.transientFeedback
        );
    } else if (audition && props.auditionBinding) {
        const int depth = depth_parameter::amountQ15ToPercent(
            props.auditionBinding->amountQ15,
            depth_parameter::scaleFor(
                props.control->authored.modulation,
                props.control->authored.curves,
                *props.auditionBinding
            )
        );
        if (existing) {
            std::snprintf(
                stateText_.data(),
                stateText_.size(),
                "%s · %s · SHARED %+d%%",
                source.kind == ModulatorKind::ADSR ? SOURCE_KIND_ADSR
                    : (source.kind == ModulatorKind::LFO
                        ? SOURCE_KIND_LFO : SOURCE_KIND_MOTION),
                sourceTimingLabel(source) != nullptr
                    ? sourceTimingLabel(source) : "MOTION",
                depth
            );
        } else {
            std::snprintf(
                stateText_.data(),
                stateText_.size(),
                "%s · %s · PREVIEW %+d%%",
                source.kind == ModulatorKind::ADSR ? SOURCE_KIND_ADSR
                    : (source.kind == ModulatorKind::LFO
                        ? SOURCE_KIND_LFO : SOURCE_KIND_MOTION),
                sourceTimingLabel(source) != nullptr
                    ? sourceTimingLabel(source) : "MOTION",
                depth
            );
        }
    } else {
        const char* kind = source.kind == ModulatorKind::LFO
            ? SOURCE_KIND_LFO
            : (source.kind == ModulatorKind::ADSR
                ? SOURCE_KIND_ADSR : SOURCE_KIND_MOTION);
        const char* timing = sourceTimingLabel(source);
        if (timing != nullptr) {
            std::snprintf(
                stateText_.data(),
                stateText_.size(),
                "%s · %s · %s",
                kind,
                timing,
                enabled ? SOURCE_STATE_ON : SOURCE_STATE_OFF
            );
        } else {
            std::snprintf(
                stateText_.data(),
                stateText_.size(),
                SOURCE_STATE_FORMAT,
                kind,
                enabled ? SOURCE_STATE_ON : SOURCE_STATE_OFF
            );
        }
    }
    lv_label_set_text_static(state_text_, stateText_.data());
    lv_obj_set_style_text_color(
        state_text_,
        lv_color_hex(
            recording || audition
                ? theme::color::LIVE_TIME
                : (enabled ? theme::color::CONTENT_ACTIVE
                           : theme::color::INACTIVE)
        ),
        0
    );
}

FLASHMEM void ProjectModulatorWorkspace::renderCards(
    const ProjectModulatorWorkspaceProps& props
) {
    const auto layout = core::state::project::modulators::sourceWorkspaceLayout(
        props.source->kind,
        props.options,
        props.session.audition()
    );
    const uint8_t itemCount = props.trigger
        ? core::state::project::modulators::MODULATOR_TRIGGER_DETAIL_COUNT
        : layout.count;
    const bool adsr = props.source->kind == ModulatorKind::ADSR;
    const uint8_t bottomCount = adsr && itemCount >= 5U
        ? 3U
        : static_cast<uint8_t>(std::min<uint8_t>(2U, itemCount));
    const uint8_t topCount = static_cast<uint8_t>(itemCount - bottomCount);
    const lv_coord_t availableWidth = static_cast<lv_coord_t>(
        lv_obj_get_width(root_) - 2 * HORIZONTAL_PAD
    );
    const bool layoutChanged = availableWidth != rendered_layout_width_ ||
        itemCount != rendered_layout_item_count_ ||
        bottomCount != rendered_layout_bottom_count_;

    for (uint8_t index = 0U; index < cards_.size(); ++index) {
        auto& card = cards_[index];
        if (index >= itemCount) {
            if (layoutChanged) {
                lv_obj_add_flag(card.root, LV_OBJ_FLAG_HIDDEN);
            }
            continue;
        }
        if (layoutChanged) {
            lv_obj_clear_flag(card.root, LV_OBJ_FLAG_HIDDEN);
        }
        const bool bottom = index >= topCount;
        const uint8_t ordinal = bottom
            ? static_cast<uint8_t>(index - topCount)
            : index;
        const uint8_t count = bottom ? bottomCount : topCount;
        const lv_coord_t width = static_cast<lv_coord_t>(
            (availableWidth - CARD_GAP * (count - 1U)) / count
        );
        if (layoutChanged) {
            lv_obj_set_pos(
                card.root,
                static_cast<lv_coord_t>(
                    HORIZONTAL_PAD + ordinal * (width + CARD_GAP)
                ),
                bottom ? CARD_BOTTOM_Y : CARD_TOP_Y
            );
            lv_obj_set_size(
                card.root,
                width,
                bottom ? CARD_BOTTOM_HEIGHT : CARD_TOP_HEIGHT
            );
            lv_obj_set_width(card.label, std::max<lv_coord_t>(1, width - 25));
            lv_obj_set_width(card.value, std::max<lv_coord_t>(1, width - 25));
        }

        ms::ui::KeyValueRowBuffer row{};
        const Item item = props.trigger ? Item::INVALID : layout.at(index);
        if (props.trigger) {
            core::ui::project::modulators::populateTriggerRow(
                *props.control,
                props.source->id,
                index,
                row
            );
        } else if (item == Item::DEPTH) {
            populateAuditionDepthRow(
                props.control,
                props.auditionBinding,
                row
            );
        } else {
            populateSourceItemRow(
                *props.control,
                *props.source,
                item,
                row
            );
        }
        if (item == Item::RECORD && captureMatches(props)) {
            std::snprintf(
                row.value.data(),
                row.value.size(),
                "%s",
                captureStatusLabel(props.capture->status)
            );
        }
        if (copyText(card.iconText, row.icon.data())) {
            lv_label_set_text_static(card.icon, card.iconText.data());
        }
        if (copyText(card.labelText, row.key.data())) {
            lv_label_set_text_static(card.label, card.labelText.data());
        }
        if (copyText(card.valueText, row.value.data())) {
            lv_label_set_text_static(card.value, card.valueText.data());
        }

        const bool selected = index == props.selectedIndex;
        const bool sourceValue = props.trigger ||
            editableItem(item, props.source->kind);
        const bool mutableValue = item == Item::DEPTH
            ? props.session.allows(
                  ProjectModulatorSourceSessionCapability::EDIT_DEPTH
              )
            : (sourceValue && props.session.allows(
                  ProjectModulatorSourceSessionCapability::EDIT_SOURCE
              ) && (!props.trigger || props.session.allows(
                  ProjectModulatorSourceSessionCapability::EDIT_TRIGGER
              )));
        const bool action = actionableItem(item) &&
            (item == Item::OPTIONS || item == Item::TRIGGER ||
             item == Item::RECORD ||
             (item == Item::DESTINATIONS && props.session.allows(
                  ProjectModulatorSourceSessionCapability::MANAGE_ROUTING
              )) ||
             (item == Item::RENAME && props.session.allows(
                  ProjectModulatorSourceSessionCapability::MANAGE_SOURCE
              )));
        const uint32_t accent = mutableValue || action
            ? theme::color::MACRO_MODULATION
            : theme::color::TEXT_SECONDARY;
        const uint32_t selectionColor = selected
            ? theme::color::FOCUS_EDIT
            : accent;
        lv_obj_set_style_border_color(
            card.root,
            lv_color_hex(selectionColor),
            0
        );
        lv_obj_set_style_border_opa(
            card.root,
            selected ? LV_OPA_COVER : LV_OPA_20,
            0
        );
        lv_obj_set_style_bg_color(
            card.root,
            lv_color_hex(selectionColor),
            0
        );
        lv_obj_set_style_bg_opa(
            card.root,
            selected ? LV_OPA_10 : LV_OPA_TRANSP,
            0
        );
        lv_obj_set_style_text_color(card.icon, lv_color_hex(accent), 0);
        lv_obj_set_style_text_opa(
            card.icon,
            selected ? LV_OPA_COVER : (mutableValue || action ? LV_OPA_70 : LV_OPA_40),
            0
        );
        lv_obj_set_style_text_opa(
            card.label,
            selected ? LV_OPA_COVER : LV_OPA_60,
            0
        );
        lv_obj_set_style_text_opa(
            card.value,
            mutableValue || action ? LV_OPA_COVER : LV_OPA_50,
            0
        );
    }
    rendered_layout_width_ = availableWidth;
    rendered_layout_item_count_ = itemCount;
    rendered_layout_bottom_count_ = bottomCount;
}

FLASHMEM bool ProjectModulatorWorkspace::sampleCurve(
    void* rawContext,
    uint16_t positionQ16,
    ms::ui::CurvePreviewSample& out
) {
    auto* context = static_cast<CurveSampleContext*>(rawContext);
    if (!context || !context->control || !context->source) return false;
    const auto& source = *context->source;
    float value = 0.0f;
    bool positive = false;
    bool discontinuity = false;
    if (context->recordedTake != nullptr) {
        int16_t captured = 0;
        if (!context->recordedTake->samplePreviewValue(positionQ16, captured)) {
            return false;
        }
        value = static_cast<float>(captured) / 32767.0f;
    } else if (source.kind == ModulatorKind::LFO) {
        const uint16_t phaseQ16 = projectLfoShapePositionQ16(
            positionQ16,
            source.parameters.lfo.phaseQ15
        );
        value = evaluateProjectLfoShape(
            source.parameters.lfo.shape,
            static_cast<float>(phaseQ16) / 65535.0f
        );
        if (context->hasPrevious &&
            source.parameters.lfo.shape == ModulatorLfoShape::SQUARE) {
            const uint16_t previousPhaseQ16 = projectLfoShapePositionQ16(
                context->previousPositionQ16,
                source.parameters.lfo.phaseQ15
            );
            discontinuity = phaseQ16 < previousPhaseQ16 ||
                (previousPhaseQ16 < 32768U && phaseQ16 >= 32768U);
        }
    } else if (source.kind == ModulatorKind::ADSR) {
        positive = true;
        const adsr_ui::PreviewBoundaries boundaries{
            context->delayEndQ16,
            context->attackEndQ16,
            context->holdEndQ16,
            context->decayEndQ16,
            context->sustainEndQ16,
        };
        const float rawValue = adsr_ui::previewValue(
            source.parameters.adsr,
            boundaries,
            positionQ16
        );
        value = rawValue;
        if (context->smoothDuration > 0U && context->hasPrevious) {
            const uint32_t deltaPosition = static_cast<uint32_t>(
                positionQ16 - context->previousPositionQ16
            );
            const uint32_t deltaTime = std::max<uint32_t>(
                1U,
                (deltaPosition * context->previewDuration + 32767U) / 65535U
            );
            const float alpha = static_cast<float>(deltaTime) /
                static_cast<float>(context->smoothDuration + deltaTime);
            const float previous = static_cast<float>(
                context->previousValue
            ) / 65535.0f;
            value = previous + alpha * (rawValue - previous);
        }
        out.base = normalizedToQ16(rawValue);
    } else {
        const auto* curve = findProjectCurve(
            context->control->authored.curves,
            source.parameters.recordedCurveId
        );
        if (!curve || curve->pointCount == 0U) return false;
        positive = curve->valueDomain ==
            ProjectCurveValueDomain::ABSOLUTE_UNIPOLAR;
        const uint32_t tick =
            (static_cast<uint32_t>(positionQ16) * curve->durationTicks) / 65535U;
        value = evaluateProjectControlCurve(
            *context->control,
            source.parameters.recordedCurveId,
            static_cast<float>(tick) /
                static_cast<float>(PROJECT_CONTROL_TICKS_PER_BEAT),
            0.0f
        );
        // Project curves currently author only linear interpolation. Unlike a
        // Square LFO, a large adjacent delta is therefore not a semantic jump
        // and must remain connected rather than inferred from magnitude.
    }
    const uint16_t curveValue = normalizedToQ16(
        positive ? value : value * 0.5f + 0.5f
    );
    out = {
        .curve = curveValue,
        .base = source.kind == ModulatorKind::ADSR ? out.base : curveValue,
        .impact = curveValue,
        .discontinuityBefore = discontinuity,
    };
    context->previousPositionQ16 = positionQ16;
    context->previousValue = curveValue;
    context->hasPrevious = true;
    return true;
}

FLASHMEM bool ProjectModulatorWorkspace::sampleMarker(
    void* rawContext,
    ms::ui::CurvePreviewMarker& out
) {
    auto* context = static_cast<CurveSampleContext*>(rawContext);
    out = {};
    if (!context || !context->control || !context->source) return false;
    const auto& control = *context->control;
    const auto& source = *context->source;
    if (context->recordedTake != nullptr) {
        uint16_t positionQ16 = 0U;
        if (!context->recordedTake->writePositionQ16(positionQ16)) return true;
        out = {
            .visible = true,
            .positionQ16 = positionQ16,
            .valueQ16 = normalizedToQ16(
                static_cast<float>(context->recordedTake->currentValue) /
                    65534.0f + 0.5f
            ),
        };
        return true;
    }
    const auto time = extrapolateProjectControlTime(
        control.timeTelemetry,
        oc::time::millis()
    );
    ProjectModulatorRuntimeProjection projection{};
    if (!projectModulatorRuntimeProjectionAtIndex(
            control.plan,
            control.authored.curves,
            control.runtime,
            time,
            context->runtimeSourceIndex,
            projection
        )) {
        return true;
    }
    uint16_t positionQ16 = projection.positionQ16;
    if (source.kind == ModulatorKind::ADSR) {
        if (!adsr_ui::runtimeMarkerPosition(
                {
                    context->delayEndQ16,
                    context->attackEndQ16,
                    context->holdEndQ16,
                    context->decayEndQ16,
                    context->sustainEndQ16,
                },
                projection.adsrStage,
                projection.stageProgressQ16,
                positionQ16
            )) {
            return true;
        }
    } else if (source.kind == ModulatorKind::LFO) {
        positionQ16 = projectLfoPreviewPositionQ16(
            positionQ16,
            source.parameters.lfo.phaseQ15
        );
    } else if (!projection.positionKnown) {
        return true;
    }
    const bool positive = sourceUsesPositiveDomain(control, source);
    out = {
        .visible =
            (source.flags & PROJECT_MODULATOR_FLAG_ENABLED) != 0U,
        .positionQ16 = positionQ16,
        .valueQ16 = normalizedToQ16(
            positive
                ? projection.value
                : projection.value * 0.5f + 0.5f
        ),
    };
    return true;
}

FLASHMEM void ProjectModulatorWorkspace::renderCurve(
    const ProjectModulatorWorkspaceProps& props
) {
    const lv_coord_t curveWidth = std::max<lv_coord_t>(
        1,
        lv_obj_get_width(root_) - 2 * HORIZONTAL_PAD
    );
    if (lv_obj_get_width(curve_preview_->getElement()) != curveWidth) {
        lv_obj_set_width(curve_preview_->getElement(), curveWidth);
    }
    const bool enabled =
        (props.source->flags & PROJECT_MODULATOR_FLAG_ENABLED) != 0U;
    const bool positive = sourceUsesPositiveDomain(*props.control, *props.source);
    const auto* recordedTake = captureMatches(props)
        ? props.capture->take.get()
        : nullptr;
    const uint32_t captureRevision = recordedTake != nullptr &&
            props.capture != nullptr
        ? props.capture->revision
        : 0U;
    uint32_t geometryRevision =
        core::ui::modulation::sparkline::sourceGeometryRevision(
            *props.control,
            *props.source
        ) ^ (captureRevision * 2246822519U);
    if (geometryRevision == 0U) geometryRevision = 1U;
    const auto adsrBoundaries = props.source->kind == ModulatorKind::ADSR
        ? adsr_ui::previewBoundaries(props.source->parameters.adsr)
        : adsr_ui::PreviewBoundaries{};
    const auto adsrTiming = props.source->kind == ModulatorKind::ADSR
        ? modulatorAdsrTiming(props.source->parameters.adsr.traits)
        : ModulatorTimingMode::FREE;
    const uint32_t adsrSmooth = props.source->kind == ModulatorKind::ADSR
        ? (adsrTiming == ModulatorTimingMode::FREE
            ? static_cast<uint32_t>(props.source->parameters.adsr.smooth)
            : resolveModulatorEnvelopeSyncTicks(
                  props.source->parameters.adsr.smooth,
                  modulatorAdsrFeel(
                      props.source->parameters.adsr.traits,
                      ModulatorEnvelopeTimeParameter::SMOOTH
                  )
              ))
        : 0U;
    uint16_t runtimeSourceIndex = 0U;
    while (runtimeSourceIndex < props.control->plan.sourceCount &&
           props.control->plan.sources[runtimeSourceIndex].id !=
               props.source->id) {
        ++runtimeSourceIndex;
    }
    curve_sample_context_ = {
        .control = props.control,
        .source = props.source,
        .recordedTake = recordedTake,
        .runtimeSourceIndex = runtimeSourceIndex,
        .delayEndQ16 = adsrBoundaries.delayEndQ16,
        .attackEndQ16 = adsrBoundaries.attackEndQ16,
        .holdEndQ16 = adsrBoundaries.holdEndQ16,
        .decayEndQ16 = adsrBoundaries.decayEndQ16,
        .sustainEndQ16 = adsrBoundaries.sustainEndQ16,
        .previewDuration = adsrBoundaries.totalDuration,
        .smoothDuration = adsrSmooth,
        .previousPositionQ16 = 0U,
        .previousValue = 0U,
        .hasPrevious = false,
    };
    curve_preview_->render({
        .visible = true,
        .sampleProvider = &ProjectModulatorWorkspace::sampleCurve,
        .sampleContext = &curve_sample_context_,
        .geometryRevision = geometryRevision,
        .markerProvider = &ProjectModulatorWorkspace::sampleMarker,
        .markerContext = &curve_sample_context_,
        .showImpactBand = false,
        .showCenterGuide = !positive,
        .showRestGuide = positive,
        .showVerticalGuide = props.source->kind == ModulatorKind::ADSR,
        .restValueQ16 = 0U,
        .verticalGuidePositionQ16 = adsrBoundaries.sustainEndQ16,
        .paddingX = 5,
        .paddingY = 5,
        .curveColor = enabled
            ? theme::color::MACRO_MODULATION
            : theme::color::INACTIVE,
        .baseColor = theme::color::TEXT_PRIMARY,
        .impactColor = theme::color::MACRO_MODULATION,
        .guideColor = theme::color::TEXT_SECONDARY,
        .markerColor = theme::color::LIVE_TIME,
        .curveOpacity = static_cast<lv_opa_t>(
            enabled ? LV_OPA_COVER : LV_OPA_40
        ),
        .baseOpacity = static_cast<lv_opa_t>(
            adsrSmooth > 0U ? LV_OPA_40 : LV_OPA_TRANSP
        ),
        .impactOpacity = LV_OPA_TRANSP,
        .bandOpacity = LV_OPA_TRANSP,
        .guideOpacity = LV_OPA_30,
        .curveWidth = 2,
        .baseWidth = 1,
        .impactWidth = 1,
        .markerRadius = 2,
        .marker = {},
    });
}

FLASHMEM void ProjectModulatorWorkspace::hideEditFeedback() {
    if (edit_feedback_) {
        lv_obj_add_flag(edit_feedback_, LV_OBJ_FLAG_HIDDEN);
        // The feedback card is the last retained child.  Redraw the complete
        // workspace when its stacking state changes so partial-refresh
        // displays cannot briefly expose a cleared sibling region.
        lv_obj_invalidate(root_);
    }
    if (edit_feedback_timer_) edit_feedback_timer_->pause();
    edit_feedback_deadline_ms_ = 0U;
}

FLASHMEM void ProjectModulatorWorkspace::presentFeedback(
    const char* key,
    const char* value
) {
    if (key == nullptr || value == nullptr) return;
    copyText(editFeedbackKeyText_, key);
    copyText(editFeedbackValueText_, value);
    lv_label_set_text_static(edit_feedback_key_, editFeedbackKeyText_.data());
    lv_label_set_text_static(edit_feedback_value_, editFeedbackValueText_.data());
    lv_obj_clear_flag(edit_feedback_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_invalidate(root_);
    if (edit_feedback_timer_) {
        edit_feedback_deadline_ms_ = lv_tick_get() + EDIT_FEEDBACK_MS;
        edit_feedback_timer_->resume();
    }
}

FLASHMEM void ProjectModulatorWorkspace::showEditFeedback(
    const ProjectModulatorWorkspaceProps& props,
    bool sourceChanged
) {
    if (!sourceChanged || !has_rendered_source_) return;
    if (props.trigger) {
        if (props.selectedIndex >= core::state::project::modulators::
                MODULATOR_TRIGGER_DETAIL_COUNT) {
            return;
        }
        ms::ui::KeyValueRowBuffer row{};
        core::ui::project::modulators::populateTriggerRow(
            *props.control,
            props.source->id,
            props.selectedIndex,
            row
        );
        presentFeedback(row.key.data(), row.value.data());
        return;
    }
    const auto layout = core::state::project::modulators::sourceWorkspaceLayout(
        props.source->kind,
        props.options,
        props.session.audition()
    );
    if (props.selectedIndex >= layout.count) return;
    const Item item = layout.at(props.selectedIndex);
    if (!editableItem(item, props.source->kind)) return;

    ms::ui::KeyValueRowBuffer row{};
    if (item == Item::DEPTH) {
        populateAuditionDepthRow(
            props.control,
            props.auditionBinding,
            row
        );
    } else if (props.options) {
        core::ui::project::modulators::populateSourceOptionsRow(
            *props.control,
            *props.source,
            props.selectedIndex,
            row
        );
    } else {
        core::ui::project::modulators::populateSourceDetailRow(
            *props.control,
            *props.source,
            props.selectedIndex,
            row
        );
    }
    copyText(editFeedbackKeyText_, row.key.data());
    if (item == Item::ATTACK) copyText(editFeedbackKeyText_, "Attack");
    if (item == Item::DELAY) copyText(editFeedbackKeyText_, "Delay");
    if (item == Item::HOLD) copyText(editFeedbackKeyText_, "Hold");
    if (item == Item::DECAY) copyText(editFeedbackKeyText_, "Decay");
    if (item == Item::SUSTAIN) copyText(editFeedbackKeyText_, "Sustain");
    if (item == Item::RELEASE) copyText(editFeedbackKeyText_, "Release");
    if (item == Item::SMOOTH) copyText(editFeedbackKeyText_, "Smooth");
    if (item == Item::DEPTH) copyText(editFeedbackKeyText_, "Depth");
    copyText(editFeedbackValueText_, row.value.data());
    const bool temporal = item == Item::DELAY || item == Item::ATTACK ||
        item == Item::HOLD || item == Item::DECAY ||
        item == Item::RELEASE || item == Item::SMOOTH;
    if (temporal && props.source->kind == ModulatorKind::ADSR &&
        modulatorAdsrTiming(props.source->parameters.adsr.traits) ==
            ModulatorTimingMode::SYNC) {
        const auto parameter = item == Item::DELAY
            ? ModulatorEnvelopeTimeParameter::DELAY
            : (item == Item::ATTACK
                ? ModulatorEnvelopeTimeParameter::ATTACK
                : (item == Item::HOLD
                    ? ModulatorEnvelopeTimeParameter::HOLD
                    : (item == Item::DECAY
                        ? ModulatorEnvelopeTimeParameter::DECAY
                        : (item == Item::RELEASE
                            ? ModulatorEnvelopeTimeParameter::RELEASE
                            : ModulatorEnvelopeTimeParameter::SMOOTH))));
        std::array<char, 32> exact{};
        std::snprintf(
            exact.data(),
            exact.size(),
            "%.18s · %s",
            row.value.data(),
            adsr_ui::feelLabel(modulatorAdsrFeel(
                props.source->parameters.adsr.traits,
                parameter
            ))
        );
        copyText(editFeedbackValueText_, exact.data());
    }
    if (item == Item::TIMING && props.source->kind == ModulatorKind::ADSR) {
        copyText(
            editFeedbackValueText_,
            modulatorAdsrTiming(props.source->parameters.adsr.traits) ==
                    ModulatorTimingMode::FREE
                ? "Free" : "Tempo Sync"
        );
    }
    if (item == Item::RESPONSE && props.source->kind == ModulatorKind::ADSR) {
        const char* curve = "Exponential";
        const auto response = modulatorAdsrCurve(
            props.source->parameters.adsr.traits
        );
        if (response == ModulatorAdsrCurve::LINEAR) {
            curve = "Linear";
        } else if (response == ModulatorAdsrCurve::SMOOTH) {
            curve = "Ease";
        }
        copyText(editFeedbackValueText_, curve);
    }
    presentFeedback(
        editFeedbackKeyText_.data(),
        editFeedbackValueText_.data()
    );
}

FLASHMEM void ProjectModulatorWorkspace::showCaptureFeedback(
    const ProjectModulatorWorkspaceProps& props,
    bool captureActive
) {
    if (!rendered_capture_active_ || captureActive || props.capture == nullptr) {
        return;
    }
    const char* value = nullptr;
    switch (props.capture->status) {
        case ProjectRecordedShapeCaptureStatus::COMMITTED:
            value = "Recorded · Undo";
            break;
        case ProjectRecordedShapeCaptureStatus::NO_CHANGE:
            value = "No movement · Kept";
            break;
        case ProjectRecordedShapeCaptureStatus::CANCELLED:
            value = "Cancelled · Unchanged";
            break;
        case ProjectRecordedShapeCaptureStatus::INVALIDATED:
        case ProjectRecordedShapeCaptureStatus::SCRATCH_UNAVAILABLE:
        case ProjectRecordedShapeCaptureStatus::COMMIT_FAILED:
            value = "Failed · Unchanged";
            break;
        default:
            break;
    }
    if (value != nullptr) presentFeedback("Record", value);
}

FLASHMEM void ProjectModulatorWorkspace::onEditFeedbackTimeout(lv_timer_t* timer) {
    auto* self = static_cast<ProjectModulatorWorkspace*>(
        lv_timer_get_user_data(timer)
    );
    if (!self) return;
    if (self->edit_feedback_deadline_ms_ == 0U ||
        static_cast<int32_t>(
            lv_tick_get() - self->edit_feedback_deadline_ms_
        ) < 0) {
        return;
    }
    self->hideEditFeedback();
}

FLASHMEM void ProjectModulatorWorkspace::render(
    const ProjectModulatorWorkspaceProps& props
) {
    if (!valid()) return;
    OC_PERF_SCOPE(perfMutation, "ui.project.modulator-workspace.mutation");
    OC_PERF_UNITS(
        perfMutation,
        props.selectedIndex,
        props.source ? static_cast<uint32_t>(props.source->kind) : 0U
    );
    if (!props.visible || !props.control || !props.source) {
        if (visible_) {
            hideEditFeedback();
            curve_preview_->render({.visible = false});
            lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
            visible_ = false;
        }
        has_rendered_source_ = false;
        rendered_capture_active_ = false;
        return;
    }
    if (!visible_) {
        lv_obj_clear_flag(root_, LV_OBJ_FLAG_HIDDEN);
        visible_ = true;
        lv_obj_update_layout(root_);
    } else if (!has_rendered_source_) {
        lv_obj_update_layout(root_);
    }

    const bool sameContext = has_rendered_source_ &&
        rendered_source_id_ == props.source->id &&
        rendered_options_ == props.options &&
        rendered_trigger_ == props.trigger &&
        rendered_session_mode_ == props.session.mode;
    const bool selectionChanged = !sameContext ||
        rendered_selected_index_ != props.selectedIndex;
    const bool sourceChanged = sameContext &&
        std::memcmp(
            &rendered_source_,
            props.source,
            sizeof(rendered_source_)
        ) != 0;
    const bool authoredChanged = sameContext &&
        rendered_authored_revision_ != props.control->authoredRevision;
    if (selectionChanged) hideEditFeedback();

    const bool captureActive = captureMatches(props);
    renderHeader(props);
    renderCards(props);
    renderCurve(props);
    showEditFeedback(
        props,
        (sourceChanged || (props.trigger && authoredChanged)) &&
            !selectionChanged
    );
    showCaptureFeedback(props, captureActive);

    rendered_source_ = *props.source;
    rendered_source_id_ = props.source->id;
    rendered_authored_revision_ = props.control->authoredRevision;
    rendered_selected_index_ = props.selectedIndex;
    rendered_options_ = props.options;
    rendered_trigger_ = props.trigger;
    rendered_session_mode_ = props.session.mode;
    has_rendered_source_ = true;
    rendered_capture_active_ = captureActive;
}

}  // namespace core::ui::project
