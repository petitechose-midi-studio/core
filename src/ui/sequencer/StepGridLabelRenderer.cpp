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
constexpr lv_opa_t STEP_ORIGINAL_NOTE_OPA = LV_OPA_50;

bool showsOriginalPitchReminder(const TileRenderState& state) {
    return hasRuntimePitchFeedback(state) &&
           runtimePitchDisplayNote(state) != state.note;
}

void formatNoteLabel(char* buffer, size_t size, uint8_t note, const TileRenderState& state) {
    if (!buffer || size == 0) return;

    core::state::sequencer::formatStepPropertyValue(
        buffer,
        size,
        core::state::sequencer::StepProperty::NOTE,
        note,
        state.velocity,
        state.gate,
        state.nudge,
        state.probability
    );
}

}  // namespace

void renderTileNoteLabel(uint8_t tileIndex,
                         TileRenderCache& cache,
                         lv_obj_t* noteLabel,
                         lv_obj_t* originalNoteLabel,
                         lv_obj_t* inlineIcon,
                         const TileRenderState& state,
                         const TileRenderDiff& diff,
                         bool propertyVisualChanged,
                         bool tileFeedbackChanged,
                         bool geometryChanged,
                         core::state::sequencer::StepProperty activeProperty,
                         const InlineFeedbackSnapshot& feedback,
                         const visual::StepPropertyVisualSpec& propertyVisual,
                         lv_coord_t noteBaseX,
                         lv_coord_t noteLabelY,
                         lv_coord_t noteLabelHeight,
                         lv_coord_t inlineIconWidth,
                         lv_coord_t inlineIconHeight) {
    (void)tileIndex;
    if (!noteLabel || !originalNoteLabel || !inlineIcon) return;

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
        if (cache.originalNoteLabelVisible) {
            lv_obj_add_flag(originalNoteLabel, LV_OBJ_FLAG_HIDDEN);
            cache.originalNoteLabelVisible = false;
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

    const bool showOriginalNoteLabel = showsOriginalPitchReminder(state);
    if (showOriginalNoteLabel) {
        if (!cache.originalNoteLabelVisible) {
            lv_obj_clear_flag(originalNoteLabel, LV_OBJ_FLAG_HIDDEN);
            cache.originalNoteLabelVisible = true;
        }
    } else if (cache.originalNoteLabelVisible) {
        lv_obj_add_flag(originalNoteLabel, LV_OBJ_FLAG_HIDDEN);
        cache.originalNoteLabelVisible = false;
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
        diff.nudgeChanged || diff.probabilityChanged || diff.variationChanged) {
        char buf[16];
        if (hasRuntimePitchFeedback(state)) {
            formatNoteLabel(buf, sizeof(buf), runtimePitchDisplayNote(state), state);
        } else {
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
        }
        if (std::strcmp(cache.noteLabelText, buf) != 0) {
            lv_label_set_text(noteLabel, buf);
            std::strncpy(cache.noteLabelText, buf, sizeof(cache.noteLabelText) - 1);
            cache.noteLabelText[sizeof(cache.noteLabelText) - 1] = '\0';
        }
        cache.noteLabelHeight = noteLabelHeight;

        char originalBuf[8];
        if (showOriginalNoteLabel) {
            formatNoteLabel(originalBuf, sizeof(originalBuf), state.note, state);
        } else {
            originalBuf[0] = '\0';
        }
        if (std::strcmp(cache.originalNoteLabelText, originalBuf) != 0) {
            lv_label_set_text(originalNoteLabel, originalBuf);
            std::strncpy(cache.originalNoteLabelText, originalBuf, sizeof(cache.originalNoteLabelText) - 1);
            cache.originalNoteLabelText[sizeof(cache.originalNoteLabelText) - 1] = '\0';
        }
    }

    if (propertyVisualChanged || diff.noteChanged || diff.enabledChanged || diff.velocityChanged ||
        diff.gateChanged || diff.nudgeChanged || diff.probabilityChanged || diff.variationChanged ||
        diff.probabilityCycleActiveChanged) {
        const lv_color_t nextNoteLabelColor =
            state.enabled ? noteLabelColor(runtimePitchDisplayNote(state))
                          : lv_color_hex(STEP_TEXT_DISABLED_COLOR);
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
        const lv_color_t nextOriginalNoteLabelColor = lv_color_hex(STEP_TEXT_DISABLED_COLOR);
        const lv_opa_t nextOriginalNoteLabelOpa =
            state.enabled
                ? (labelPresentation.probabilityMasked ? STEP_PROBABILITY_MASKED_OPA
                                                       : STEP_ORIGINAL_NOTE_OPA)
                : STEP_TEXT_DISABLED_OPA;
        const uint32_t nextOriginalNoteLabelColorInt = lv_color_to_int(nextOriginalNoteLabelColor);
        const uint32_t nextInlineIconColorInt = lv_color_to_int(nextInlineIconColor);

        if (cache.noteLabelColorFull != nextNoteLabelColorInt) {
            lv_obj_set_style_text_color(noteLabel, nextNoteLabelColor, 0);
            cache.noteLabelColorFull = nextNoteLabelColorInt;
        }
        if (cache.noteLabelOpa != nextNoteLabelOpa) {
            lv_obj_set_style_text_opa(noteLabel, nextNoteLabelOpa, 0);
            cache.noteLabelOpa = nextNoteLabelOpa;
        }
        if (cache.originalNoteLabelColorFull != nextOriginalNoteLabelColorInt) {
            lv_obj_set_style_text_color(originalNoteLabel, nextOriginalNoteLabelColor, 0);
            cache.originalNoteLabelColorFull = nextOriginalNoteLabelColorInt;
        }
        if (cache.originalNoteLabelOpa != nextOriginalNoteLabelOpa) {
            lv_obj_set_style_text_opa(originalNoteLabel, nextOriginalNoteLabelOpa, 0);
            cache.originalNoteLabelOpa = nextOriginalNoteLabelOpa;
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

    if (geometryChanged || propertyVisualChanged || tileFeedbackChanged || diff.inPatternChanged ||
        diff.noteChanged || diff.variationChanged ||
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

        const lv_coord_t originalLabelY =
            static_cast<lv_coord_t>(layout.labelY - cache.noteLabelHeight + 1);
        if (cache.originalNoteLabelX != layout.labelX ||
            cache.originalNoteLabelY != originalLabelY) {
            lv_obj_set_pos(originalNoteLabel, layout.labelX, originalLabelY);
            cache.originalNoteLabelX = layout.labelX;
            cache.originalNoteLabelY = originalLabelY;
        }
    }
}

}  // namespace core::ui::sequencer::grid::label_renderer
