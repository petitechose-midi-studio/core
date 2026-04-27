#include "ui/sequencer/StepGridLabelRenderer.hpp"

#include <cstring>

#include <oc/ui/lvgl/theme/BaseTheme.hpp>

#include "state/sequencer/StepPropertyDisplay.hpp"
#include "ui/sequencer/StepGridGeometryLogic.hpp"
#include "ui/sequencer/StepGridRenderLogic.hpp"

namespace theme = oc::ui::lvgl::base_theme;

namespace core::ui::sequencer::grid::label_renderer {

namespace {

constexpr uint32_t STEP_TEXT_DISABLED_COLOR = theme::color::INACTIVE_LIGHTER;
constexpr lv_opa_t STEP_TEXT_DISABLED_OPA = static_cast<lv_opa_t>(theme::opacity::OPA_50);
constexpr lv_opa_t STEP_INLINE_NOTE_OPA = LV_OPA_COVER;
constexpr lv_opa_t STEP_INLINE_VALUE_OPA = LV_OPA_70;
constexpr lv_opa_t STEP_PROBABILITY_MASKED_OPA = LV_OPA_30;

}  // namespace

void renderTileNoteLabel(uint8_t tileIndex,
                         TileRenderCache& cache,
                         lv_obj_t* noteLabel,
                         lv_obj_t* inlineIcon,
                         const TileRenderState& state,
                         const TileRenderDiff& diff,
                         bool propertyVisualChanged,
                         bool tileFeedbackChanged,
                         core::state::sequencer::StepProperty activeProperty,
                         const InlineFeedbackSnapshot& feedback,
                         const visual::StepPropertyVisualSpec& propertyVisual,
                         lv_coord_t noteBaseX,
                         lv_coord_t noteLabelY,
                         lv_coord_t noteLabelHeight,
                         lv_coord_t inlineIconWidth,
                         lv_coord_t inlineIconHeight) {
    (void)tileIndex;
    if (!noteLabel || !inlineIcon) return;

    const auto labelPresentation = buildNoteLabelPresentation(
        state,
        propertyVisual,
        activeProperty,
        feedback
    );

    if (!labelPresentation.showLabel) {
        if (cache.noteLabelVisible) {
            lv_obj_add_flag(noteLabel, LV_OBJ_FLAG_HIDDEN);
            cache.noteLabelVisible = false;
        }
        if (cache.inlineIconVisible) {
            lv_obj_add_flag(inlineIcon, LV_OBJ_FLAG_HIDDEN);
            cache.inlineIconVisible = false;
        }
        return;
    }

    if (!cache.noteLabelVisible) {
        lv_obj_clear_flag(noteLabel, LV_OBJ_FLAG_HIDDEN);
        cache.noteLabelVisible = true;
    }

    if (labelPresentation.showInlineIcon) {
        if (!cache.inlineIconVisible) {
            lv_obj_clear_flag(inlineIcon, LV_OBJ_FLAG_HIDDEN);
            cache.inlineIconVisible = true;
        }
    } else if (cache.inlineIconVisible) {
        lv_obj_add_flag(inlineIcon, LV_OBJ_FLAG_HIDDEN);
        cache.inlineIconVisible = false;
    }

    if (propertyVisualChanged || diff.noteChanged || diff.velocityChanged || diff.gateChanged ||
        diff.nudgeChanged || diff.probabilityChanged) {
        char buf[16];
        core::state::sequencer::formatStepPropertyValue(
            buf,
            sizeof(buf),
            labelPresentation.displayProperty,
            state.note,
            state.velocity,
            state.gate,
            state.nudge,
            state.probability
        );
        if (std::strcmp(cache.noteLabelText, buf) != 0) {
            lv_label_set_text(noteLabel, buf);
            std::strncpy(cache.noteLabelText, buf, sizeof(cache.noteLabelText) - 1);
            cache.noteLabelText[sizeof(cache.noteLabelText) - 1] = '\0';
        }
        cache.noteLabelHeight = noteLabelHeight;
    }

    if (propertyVisualChanged || diff.noteChanged || diff.enabledChanged || diff.velocityChanged ||
        diff.gateChanged || diff.nudgeChanged || diff.probabilityChanged ||
        diff.probabilityCycleActiveChanged) {
        const lv_color_t nextNoteLabelColor =
            state.enabled ? noteLabelColor(state.note) : lv_color_hex(STEP_TEXT_DISABLED_COLOR);
        const lv_opa_t nextNoteLabelOpa =
            state.enabled
                ? (labelPresentation.probabilityMasked
                       ? STEP_PROBABILITY_MASKED_OPA
                       : (labelPresentation.showNoteStyle ? STEP_INLINE_NOTE_OPA
                                                          : STEP_INLINE_VALUE_OPA))
                : STEP_TEXT_DISABLED_OPA;
        const lv_color_t nextInlineIconColor =
            state.enabled ? probabilityInlineIconColor(state.note, state.probability)
                          : lv_color_hex(STEP_TEXT_DISABLED_COLOR);
        const lv_opa_t nextInlineIconOpa =
            state.enabled
                ? (labelPresentation.probabilityMasked ? STEP_PROBABILITY_MASKED_OPA
                                                       : STEP_INLINE_VALUE_OPA)
                : STEP_TEXT_DISABLED_OPA;

        const uint32_t nextNoteLabelColorInt = lv_color_to_int(nextNoteLabelColor);
        const uint32_t nextInlineIconColorInt = lv_color_to_int(nextInlineIconColor);

        if (cache.noteLabelColorFull != nextNoteLabelColorInt) {
            lv_obj_set_style_text_color(noteLabel, nextNoteLabelColor, 0);
            cache.noteLabelColorFull = nextNoteLabelColorInt;
        }
        if (cache.noteLabelOpa != nextNoteLabelOpa) {
            lv_obj_set_style_text_opa(noteLabel, nextNoteLabelOpa, 0);
            cache.noteLabelOpa = nextNoteLabelOpa;
        }

        if (cache.inlineIconColorFull != nextInlineIconColorInt) {
            lv_obj_set_style_text_color(inlineIcon, nextInlineIconColor, 0);
            cache.inlineIconColorFull = nextInlineIconColorInt;
        }
        if (cache.inlineIconOpa != nextInlineIconOpa) {
            lv_obj_set_style_text_opa(inlineIcon, nextInlineIconOpa, 0);
            cache.inlineIconOpa = nextInlineIconOpa;
        }
    }

    if ((propertyVisualChanged || diff.inPatternChanged || diff.noteChanged || diff.velocityChanged ||
         diff.gateChanged || diff.nudgeChanged || diff.probabilityChanged) &&
        cache.noteLabelHeight <= 0) {
        cache.noteLabelHeight = noteLabelHeight;
    }

    if (propertyVisualChanged || tileFeedbackChanged || diff.inPatternChanged ||
        cache.noteLabelHeight <= 0) {
        const lv_coord_t iconHeight = labelPresentation.showInlineIcon ? inlineIconHeight : 0;
        const lv_coord_t iconWidth = labelPresentation.showInlineIcon ? inlineIconWidth : 0;

        const auto layout = buildInlineLabelLayout(
            noteBaseX,
            noteLabelY,
            cache.noteLabelHeight,
            labelPresentation.showInlineIcon,
            iconWidth,
            iconHeight
        );

        if (labelPresentation.showInlineIcon) {
            if (cache.inlineIconX != layout.iconX || cache.inlineIconY != layout.iconY) {
                lv_obj_set_pos(inlineIcon, layout.iconX, layout.iconY);
                cache.inlineIconX = layout.iconX;
                cache.inlineIconY = layout.iconY;
            }
        }

        if (cache.noteLabelX != layout.labelX || cache.noteLabelY != layout.labelY) {
            lv_obj_set_pos(noteLabel, layout.labelX, layout.labelY);
            cache.noteLabelX = layout.labelX;
            cache.noteLabelY = layout.labelY;
        }
    }
}

}  // namespace core::ui::sequencer::grid::label_renderer
