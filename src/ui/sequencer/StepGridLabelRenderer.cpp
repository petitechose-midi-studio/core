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
constexpr lv_opa_t STEP_INLINE_VALUE_OPA = LV_OPA_70;
constexpr lv_opa_t STEP_PROBABILITY_MASKED_OPA = LV_OPA_30;
constexpr uint32_t STEP_SCALE_DEGREE_COLOR =
    core::ui::sequencer::semantic::color(core::ui::sequencer::semantic::Tone::PITCH);
constexpr uint32_t STEP_CHILD_OFFSET_COLOR =
    core::ui::sequencer::semantic::color(core::ui::sequencer::semantic::Tone::PITCH);
constexpr const char* STEP_SCALE_SEPARATOR = ":";

bool showsSecondaryLabel(const TileRenderState& state) {
    return state.childContentContext || state.childPitchSummaryVisible;
}

lv_color_t secondaryLabelColor(const TileRenderState& state) {
    if (state.childContentContext) {
        return lv_color_hex(STEP_CHILD_OFFSET_COLOR);
    }
    if (state.childPitchSummaryVisible && hasRuntimePitchFeedback(state)) {
        return noteLabelColor(runtimePitchDisplayNote(state));
    }
    if (state.childPitchSummaryVisible) {
        return noteLabelColor(state.childPitchSummaryNote);
    }
    return lv_color_hex(STEP_TEXT_DISABLED_COLOR);
}

lv_opa_t secondaryLabelOpa(const TileRenderState& state,
                           bool probabilityMasked) {
    if (!state.enabled) return STEP_TEXT_DISABLED_OPA;
    if (probabilityMasked) return STEP_PROBABILITY_MASKED_OPA;
    if (state.childContentContext || state.childPitchSummaryVisible) {
        return LV_OPA_COVER;
    }
    return STEP_TEXT_DISABLED_OPA;
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

void renderTileNoteLabel(TileRenderCache& cache,
                         const TileLabelWidgets& widgets,
                         const TileRenderState& state,
                         const TileRenderDiff& diff,
                         bool propertyVisualChanged,
                         bool tileFeedbackChanged,
                         bool geometryChanged,
                         const StepGridFrameState& frameState,
    const TileLabelGeometry& geometry) {
    auto* noteLabel = widgets.primary;
    auto* secondaryLabel = widgets.secondary;
    auto* inlineIcon = widgets.inlineIcon;
    if (!noteLabel || !secondaryLabel || !inlineIcon) return;

    const auto labelPresentation = buildNoteLabelPresentation(state, frameState);

    if (!labelPresentation.showLabel) {
        if (cache.noteLabelVisible) {
            lv_obj_add_flag(noteLabel, LV_OBJ_FLAG_HIDDEN);
            cache.noteLabelVisible = false;
        }
        if (cache.secondaryLabelVisible) {
            lv_obj_add_flag(secondaryLabel, LV_OBJ_FLAG_HIDDEN);
            cache.secondaryLabelVisible = false;
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

    const bool showSecondaryLabel =
        frameState.presentation != StepGridPresentation::DRUM_LANE &&
        showsSecondaryLabel(state);
    if (showSecondaryLabel) {
        if (!cache.secondaryLabelVisible) {
            lv_obj_clear_flag(secondaryLabel, LV_OBJ_FLAG_HIDDEN);
            cache.secondaryLabelVisible = true;
        }
    } else if (cache.secondaryLabelVisible) {
        lv_obj_add_flag(secondaryLabel, LV_OBJ_FLAG_HIDDEN);
        cache.secondaryLabelVisible = false;
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
        char buf[16];
        if (state.childPitchSummaryVisible && hasRuntimePitchFeedback(state)) {
            formatRuntimePitchPrimaryLabel(buf, sizeof(buf), state);
        } else if (state.childPitchSummaryVisible) {
            formatChildSummaryPrimaryLabel(buf, sizeof(buf), state);
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
        cache.noteLabelHeight = geometry.labelHeight;

        char secondaryBuf[12];
        if (state.childPitchSummaryVisible && hasRuntimePitchFeedback(state)) {
            if (runtimePitchShowsScaleDegree(state)) {
                formatNoteLabel(
                    secondaryBuf,
                    sizeof(secondaryBuf),
                    runtimePitchDisplayNote(state),
                    state
                );
            } else {
                secondaryBuf[0] = '\0';
            }
        } else if (state.childPitchSummaryVisible) {
            if (childSummaryShowsScaleDegree(state)) {
                formatNoteLabel(secondaryBuf, sizeof(secondaryBuf), state.childPitchSummaryNote, state);
            } else {
                secondaryBuf[0] = '\0';
            }
        } else if (state.childContentContext) {
            formatOffsetLabel(
                secondaryBuf,
                sizeof(secondaryBuf),
                frameState.activeProperty,
                state.childContentOffset,
                state.childContentNoteOffsetUsesScaleDegrees
            );
        } else {
            secondaryBuf[0] = '\0';
        }
        if (std::strcmp(cache.secondaryLabelText, secondaryBuf) != 0) {
            lv_label_set_text(secondaryLabel, secondaryBuf);
            std::strncpy(cache.secondaryLabelText, secondaryBuf, sizeof(cache.secondaryLabelText) - 1);
            cache.secondaryLabelText[sizeof(cache.secondaryLabelText) - 1] = '\0';
        }
    }

    if (propertyVisualChanged || diff.noteChanged || diff.enabledChanged || diff.velocityChanged ||
        diff.gateChanged || diff.nudgeChanged || diff.probabilityChanged || diff.variationChanged ||
        diff.probabilityCycleActiveChanged || diff.childContentChanged || diff.childPitchSummaryChanged) {
        const lv_color_t nextNoteLabelColor =
            !state.enabled
                ? lv_color_hex(STEP_TEXT_DISABLED_COLOR)
                : frameState.presentation == StepGridPresentation::DRUM_LANE
                    ? lv_color_hex(frameState.accentColor)
                    : (state.childPitchSummaryVisible
                       ? (hasRuntimePitchFeedback(state)
                              ? (runtimePitchShowsScaleDegree(state)
                                     ? lv_color_hex(STEP_SCALE_DEGREE_COLOR)
                                     : noteLabelColor(runtimePitchDisplayNote(state)))
                              : childSummaryShowsScaleDegree(state)
                              ? lv_color_hex(STEP_SCALE_DEGREE_COLOR)
                              : noteLabelColor(state.childPitchSummaryNote))
                       : noteLabelColor(runtimePitchDisplayNote(state)));
        const lv_opa_t nextNoteLabelOpa =
            state.enabled
                ? (labelPresentation.probabilityMasked
                       ? STEP_PROBABILITY_MASKED_OPA
                       : STEP_INLINE_VALUE_OPA)
                : STEP_TEXT_DISABLED_OPA;
        const lv_color_t nextInlineIconColor =
            state.enabled
                ? frameState.presentation == StepGridPresentation::DRUM_LANE
                    ? drumLaneAccentColor(
                          frameState.accentColor,
                          static_cast<uint8_t>(
                              (static_cast<uint16_t>(state.probability) *
                               VELOCITY_MAX) /
                              100U
                          ),
                          true
                      )
                    : probabilityInlineIconColor(state.note, state.probability)
                : lv_color_hex(STEP_TEXT_DISABLED_COLOR);
        const lv_opa_t nextInlineIconOpa =
            state.enabled
                ? (labelPresentation.probabilityMasked ? STEP_PROBABILITY_MASKED_OPA
                                                       : STEP_INLINE_VALUE_OPA)
                : STEP_TEXT_DISABLED_OPA;

        const uint32_t nextNoteLabelColorInt = lv_color_to_int(nextNoteLabelColor);
        const lv_color_t nextSecondaryLabelColor = secondaryLabelColor(state);
        const lv_opa_t nextSecondaryLabelOpa =
            secondaryLabelOpa(state, labelPresentation.probabilityMasked);
        const uint32_t nextSecondaryLabelColorInt = lv_color_to_int(nextSecondaryLabelColor);
        const uint32_t nextInlineIconColorInt = lv_color_to_int(nextInlineIconColor);

        if (cache.noteLabelColorFull != nextNoteLabelColorInt) {
            lv_obj_set_style_text_color(noteLabel, nextNoteLabelColor, 0);
            cache.noteLabelColorFull = nextNoteLabelColorInt;
        }
        if (cache.noteLabelOpa != nextNoteLabelOpa) {
            lv_obj_set_style_text_opa(noteLabel, nextNoteLabelOpa, 0);
            cache.noteLabelOpa = nextNoteLabelOpa;
        }
        if (cache.secondaryLabelColorFull != nextSecondaryLabelColorInt) {
            lv_obj_set_style_text_color(secondaryLabel, nextSecondaryLabelColor, 0);
            cache.secondaryLabelColorFull = nextSecondaryLabelColorInt;
        }
        if (cache.secondaryLabelOpa != nextSecondaryLabelOpa) {
            lv_obj_set_style_text_opa(secondaryLabel, nextSecondaryLabelOpa, 0);
            cache.secondaryLabelOpa = nextSecondaryLabelOpa;
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
        cache.noteLabelHeight = geometry.labelHeight;
    }

    if (geometryChanged || propertyVisualChanged || tileFeedbackChanged || diff.inPatternChanged ||
        diff.noteChanged || diff.variationChanged || diff.childContentChanged ||
        diff.childPitchSummaryChanged ||
        cache.noteLabelHeight <= 0) {
        const lv_coord_t iconHeight = labelPresentation.showInlineIcon ? geometry.iconHeight : 0;
        const lv_coord_t iconWidth = labelPresentation.showInlineIcon ? geometry.iconWidth : 0;

        const auto layout = buildInlineLabelLayout(
            geometry.baseX,
            geometry.baselineY,
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

        const bool inlineScaleStatus = childPitchSummaryShowsScalePrefix(state);
        lv_coord_t primaryLabelWidth = 0;
        if (inlineScaleStatus) {
            lv_point_t textSize{};
            const lv_font_t* font = fonts.compact_selected()
                ? fonts.compact_selected()
                : LV_FONT_DEFAULT;
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
        const lv_coord_t secondaryLabelX =
            inlineScaleStatus
                ? static_cast<lv_coord_t>(layout.labelX + primaryLabelWidth + 1)
                : layout.labelX;
        const lv_coord_t secondaryLabelY =
            inlineScaleStatus
                ? layout.labelY
                : static_cast<lv_coord_t>(layout.labelY - cache.noteLabelHeight + 1);
        if (cache.secondaryLabelX != secondaryLabelX ||
            cache.secondaryLabelY != secondaryLabelY) {
            lv_obj_set_pos(secondaryLabel, secondaryLabelX, secondaryLabelY);
            cache.secondaryLabelX = secondaryLabelX;
            cache.secondaryLabelY = secondaryLabelY;
        }
    }
}

}  // namespace core::ui::sequencer::grid::label_renderer
