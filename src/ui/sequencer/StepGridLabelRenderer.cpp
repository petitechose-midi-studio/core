#include "ui/sequencer/StepGridLabelRenderer.hpp"

#include <cstdio>
#include <cstring>

#include <oc/ui/lvgl/theme/BaseTheme.hpp>
#include <ms/ui/font/CoreFonts.hpp>

#include "state/sequencer/StepPropertyDisplay.hpp"
#include "ui/sequencer/StepGridGeometryLogic.hpp"
#include "ui/sequencer/StepGridRenderLogic.hpp"
#include "ui/sequencer/StepSemanticVisuals.hpp"

namespace theme = oc::ui::lvgl::base_theme;

namespace core::ui::sequencer::grid::label_renderer {

namespace {

constexpr uint32_t STEP_TEXT_DISABLED_COLOR = theme::color::INACTIVE_LIGHTER;
constexpr lv_opa_t STEP_TEXT_DISABLED_OPA = static_cast<lv_opa_t>(theme::opacity::OPA_50);
constexpr lv_opa_t STEP_INLINE_NOTE_OPA = LV_OPA_COVER;
constexpr lv_opa_t STEP_INLINE_VALUE_OPA = LV_OPA_70;
constexpr lv_opa_t STEP_PROBABILITY_MASKED_OPA = LV_OPA_30;
constexpr lv_opa_t STEP_SOURCE_NOTE_REMINDER_OPA = LV_OPA_50;
constexpr uint32_t STEP_SCALE_DEGREE_COLOR =
    core::ui::sequencer::semantic::color(core::ui::sequencer::semantic::Tone::PITCH);
constexpr uint32_t STEP_OUT_OF_SCALE_COLOR = 0xFF6B6B;
constexpr uint32_t STEP_CHILD_OFFSET_COLOR =
    core::ui::sequencer::semantic::color(core::ui::sequencer::semantic::Tone::PITCH);
constexpr const char* STEP_SCALE_SEPARATOR = ":";
constexpr const char* STEP_OUT_OF_SCALE_MARKER = "!";

bool showsOriginalPitchReminder(const TileRenderState& state) {
    return hasRuntimePitchFeedback(state) &&
           runtimePitchDisplayNote(state) != state.variation.resolved.base.note;
}

bool showsSecondaryPitchLabel(const TileRenderState& state) {
    return state.childContentContext ||
           state.childPitchSummaryVisible ||
           hasScaleDegreeFeedback(state) ||
           hasOutOfScaleFeedback(state) ||
           showsOriginalPitchReminder(state);
}

bool primaryLabelShowsScaleDegree(const TileRenderState& state) {
    return !state.childContentContext &&
           !state.childPitchSummaryVisible &&
           hasScaleDegreeFeedback(state);
}

bool primaryLabelShowsOutOfScaleMarker(const TileRenderState& state) {
    return !state.childContentContext &&
           !state.childPitchSummaryVisible &&
           !primaryLabelShowsScaleDegree(state) &&
           hasOutOfScaleFeedback(state);
}

bool primaryLabelShowsScalePrefix(const TileRenderState& state) {
    return primaryLabelShowsScaleDegree(state) || primaryLabelShowsOutOfScaleMarker(state);
}

bool secondaryLabelShowsCurrentNote(const TileRenderState& state) {
    return !state.childContentContext &&
           !state.childPitchSummaryVisible &&
           primaryLabelShowsScalePrefix(state);
}

lv_color_t secondaryPitchLabelColor(const TileRenderState& state) {
    if (state.childContentContext) {
        return lv_color_hex(STEP_CHILD_OFFSET_COLOR);
    }
    if (state.childPitchSummaryVisible && hasRuntimePitchFeedback(state)) {
        return noteLabelColor(runtimePitchDisplayNote(state));
    }
    if (state.childPitchSummaryVisible) {
        return noteLabelColor(state.childPitchSummaryNote);
    }
    if (secondaryLabelShowsCurrentNote(state)) {
        return noteLabelColor(runtimePitchDisplayNote(state));
    }
    return lv_color_hex(STEP_TEXT_DISABLED_COLOR);
}

lv_opa_t secondaryPitchLabelOpa(const TileRenderState& state,
                                bool probabilityMasked) {
    if (!state.enabled) return STEP_TEXT_DISABLED_OPA;
    if (probabilityMasked) return STEP_PROBABILITY_MASKED_OPA;
    if (state.childContentContext) {
        return LV_OPA_COVER;
    }
    if (state.childPitchSummaryVisible) {
        return LV_OPA_COVER;
    }
    if (secondaryLabelShowsCurrentNote(state)) {
        return LV_OPA_COVER;
    }
    return STEP_SOURCE_NOTE_REMINDER_OPA;
}

void appendScaleSeparator(char* buffer, size_t size) {
    const size_t len = std::strlen(buffer);
    if (len + 1 >= size) return;
    std::strncat(buffer, STEP_SCALE_SEPARATOR, size - len - 1);
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

void formatResolvedPropertyLabel(char* buffer,
                                 size_t size,
                                 core::state::sequencer::StepProperty property,
                                 const TileRenderState& state) {
    if (!buffer || size == 0) return;

    core::state::sequencer::formatStepPropertyValue(
        buffer,
        size,
        property,
        runtimePitchDisplayNote(state),
        runtimeVelocityDisplayValue(state),
        runtimeGateDisplayValue(state),
        runtimeNudgeDisplayValue(state),
        state.probability
    );
}

int childSummaryScaleDegreeIndex(const TileRenderState& state) {
    if (!state.childPitchSummaryVisible) return -1;

    auto settings = state.variation.resolved.scaleSettings;
    settings.clamp();
    if (!settings.isConstrained()) return -1;
    if (!oc::note::sequencer::scaleContainsNote(settings, state.childPitchSummaryNote)) {
        return -1;
    }
    return scaleDegreeIndexForNote(settings, state.childPitchSummaryNote);
}

bool childSummaryShowsScaleDegree(const TileRenderState& state) {
    return childSummaryScaleDegreeIndex(state) >= 0;
}

int runtimePitchScaleDegreeIndex(const TileRenderState& state) {
    if (!hasRuntimePitchFeedback(state)) return -1;

    auto settings = state.variation.resolved.scaleSettings;
    settings.clamp();
    if (settings.type == oc::note::sequencer::StepSequencerScaleType::Chromatic) return -1;

    const uint8_t note = runtimePitchDisplayNote(state);
    if (!oc::note::sequencer::scaleContainsNote(settings, note)) {
        return -1;
    }
    return scaleDegreeIndexForNote(settings, note);
}

bool runtimePitchShowsScaleDegree(const TileRenderState& state) {
    return runtimePitchScaleDegreeIndex(state) >= 0;
}

void formatChildSummaryPrimaryLabel(char* buffer,
                                    size_t size,
                                    const TileRenderState& state) {
    if (!buffer || size == 0) return;

    const int degree = childSummaryScaleDegreeIndex(state);
    if (degree < 0) {
        formatNoteLabel(buffer, size, state.childPitchSummaryNote, state);
        return;
    }

    std::strncpy(buffer, scaleDegreeLabel(degree), size - 1);
    buffer[size - 1] = '\0';
    appendScaleSeparator(buffer, size);
}

void formatRuntimePitchPrimaryLabel(char* buffer,
                                    size_t size,
                                    const TileRenderState& state) {
    if (!buffer || size == 0) return;

    const uint8_t note = runtimePitchDisplayNote(state);
    const int degree = runtimePitchScaleDegreeIndex(state);
    if (degree < 0) {
        formatNoteLabel(buffer, size, note, state);
        return;
    }

    std::strncpy(buffer, scaleDegreeLabel(degree), size - 1);
    buffer[size - 1] = '\0';
    appendScaleSeparator(buffer, size);
}

bool childPitchSummaryShowsScalePrefix(const TileRenderState& state) {
    if (!state.childPitchSummaryVisible) return false;
    return hasRuntimePitchFeedback(state)
        ? runtimePitchShowsScaleDegree(state)
        : childSummaryShowsScaleDegree(state);
}

void formatOffsetLabel(char* buffer,
                       size_t size,
                       core::state::sequencer::StepProperty property,
                       int16_t offset,
                       bool noteOffsetUsesScaleDegrees) {
    if (!buffer || size == 0) return;

    const char sign = offset >= 0 ? '+' : '-';
    const int magnitude = offset >= 0 ? offset : -offset;
    const char* unit = (property == core::state::sequencer::StepProperty::NOTE &&
                        noteOffsetUsesScaleDegrees)
                           ? "d"
                           : "";
    std::snprintf(buffer, size, "%c%d%s", sign, magnitude, unit);
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

    const bool showOriginalNoteLabel = showsSecondaryPitchLabel(state);
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
        diff.nudgeChanged || diff.probabilityChanged || diff.variationChanged ||
        diff.childContentChanged || diff.childPitchSummaryChanged) {
        const bool labelDisplaysPitch =
            labelPresentation.displayProperty == core::state::sequencer::StepProperty::NOTE;
        char buf[16];
        if (state.childPitchSummaryVisible && hasRuntimePitchFeedback(state)) {
            formatRuntimePitchPrimaryLabel(buf, sizeof(buf), state);
        } else if (state.childPitchSummaryVisible) {
            formatChildSummaryPrimaryLabel(buf, sizeof(buf), state);
        } else if (state.childContentContext) {
            formatResolvedPropertyLabel(
                buf,
                sizeof(buf),
                labelPresentation.displayProperty,
                state
            );
        } else if (labelDisplaysPitch && primaryLabelShowsScaleDegree(state)) {
            std::strncpy(buf, runtimeScaleDegreeLabel(state), sizeof(buf) - 1);
            buf[sizeof(buf) - 1] = '\0';
            appendScaleSeparator(buf, sizeof(buf));
        } else if (labelDisplaysPitch && primaryLabelShowsOutOfScaleMarker(state)) {
            std::strncpy(buf, STEP_OUT_OF_SCALE_MARKER, sizeof(buf) - 1);
            buf[sizeof(buf) - 1] = '\0';
            appendScaleSeparator(buf, sizeof(buf));
        } else if (labelDisplaysPitch && hasRuntimePitchFeedback(state)) {
            formatNoteLabel(buf, sizeof(buf), runtimePitchDisplayNote(state), state);
        } else {
            formatResolvedPropertyLabel(
                buf,
                sizeof(buf),
                labelPresentation.displayProperty,
                state
            );
        }
        if (std::strcmp(cache.noteLabelText, buf) != 0) {
            lv_label_set_text(noteLabel, buf);
            std::strncpy(cache.noteLabelText, buf, sizeof(cache.noteLabelText) - 1);
            cache.noteLabelText[sizeof(cache.noteLabelText) - 1] = '\0';
        }
        cache.noteLabelHeight = noteLabelHeight;

        char originalBuf[12];
        if (state.childPitchSummaryVisible && hasRuntimePitchFeedback(state)) {
            if (runtimePitchShowsScaleDegree(state)) {
                formatNoteLabel(
                    originalBuf,
                    sizeof(originalBuf),
                    runtimePitchDisplayNote(state),
                    state
                );
            } else {
                originalBuf[0] = '\0';
            }
        } else if (state.childPitchSummaryVisible) {
            if (childSummaryShowsScaleDegree(state)) {
                formatNoteLabel(originalBuf, sizeof(originalBuf), state.childPitchSummaryNote, state);
            } else {
                originalBuf[0] = '\0';
            }
        } else if (state.childContentContext) {
            formatOffsetLabel(
                originalBuf,
                sizeof(originalBuf),
                activeProperty,
                state.childContentOffset,
                state.childContentNoteOffsetUsesScaleDegrees
            );
        } else if (labelDisplaysPitch && secondaryLabelShowsCurrentNote(state)) {
            formatNoteLabel(originalBuf, sizeof(originalBuf), runtimePitchDisplayNote(state), state);
        } else if (labelDisplaysPitch && showsOriginalPitchReminder(state)) {
            formatNoteLabel(originalBuf, sizeof(originalBuf), state.variation.resolved.base.note, state);
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
        diff.probabilityCycleActiveChanged || diff.childContentChanged || diff.childPitchSummaryChanged) {
        const lv_color_t nextNoteLabelColor =
            state.enabled
                ? (state.childPitchSummaryVisible
                       ? (hasRuntimePitchFeedback(state)
                              ? (runtimePitchShowsScaleDegree(state)
                                     ? lv_color_hex(STEP_SCALE_DEGREE_COLOR)
                                     : noteLabelColor(runtimePitchDisplayNote(state)))
                              : childSummaryShowsScaleDegree(state)
                              ? lv_color_hex(STEP_SCALE_DEGREE_COLOR)
                              : noteLabelColor(state.childPitchSummaryNote))
                       : (labelPresentation.displayProperty == core::state::sequencer::StepProperty::NOTE &&
                          primaryLabelShowsScaleDegree(state))
                       ? lv_color_hex(STEP_SCALE_DEGREE_COLOR)
                       : ((labelPresentation.displayProperty == core::state::sequencer::StepProperty::NOTE &&
                           primaryLabelShowsOutOfScaleMarker(state))
                              ? lv_color_hex(STEP_OUT_OF_SCALE_COLOR)
                              : noteLabelColor(runtimePitchDisplayNote(state))))
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
        const lv_color_t nextOriginalNoteLabelColor = secondaryPitchLabelColor(state);
        const lv_opa_t nextOriginalNoteLabelOpa =
            secondaryPitchLabelOpa(state, labelPresentation.probabilityMasked);
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
        diff.noteChanged || diff.variationChanged || diff.childContentChanged ||
        diff.childPitchSummaryChanged ||
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

        const bool inlineScaleStatus =
            primaryLabelShowsScalePrefix(state) ||
            childPitchSummaryShowsScalePrefix(state);
        lv_coord_t primaryLabelWidth = 0;
        if (inlineScaleStatus) {
            lv_point_t textSize{};
            const lv_font_t* font = fonts.inter_13_bold ? fonts.inter_13_bold : LV_FONT_DEFAULT;
            lv_text_get_size(
                &textSize,
                cache.noteLabelText,
                font,
                0,
                0,
                LV_COORD_MAX,
                LV_TEXT_FLAG_NONE
            );
            primaryLabelWidth = static_cast<lv_coord_t>(textSize.x);
        }
        const lv_coord_t originalLabelX =
            inlineScaleStatus
                ? static_cast<lv_coord_t>(layout.labelX + primaryLabelWidth + 1)
                : layout.labelX;
        const lv_coord_t originalLabelY =
            inlineScaleStatus
                ? layout.labelY
                : static_cast<lv_coord_t>(layout.labelY - cache.noteLabelHeight + 1);
        if (cache.originalNoteLabelX != originalLabelX ||
            cache.originalNoteLabelY != originalLabelY) {
            lv_obj_set_pos(originalNoteLabel, originalLabelX, originalLabelY);
            cache.originalNoteLabelX = originalLabelX;
            cache.originalNoteLabelY = originalLabelY;
        }
    }
}

}  // namespace core::ui::sequencer::grid::label_renderer
