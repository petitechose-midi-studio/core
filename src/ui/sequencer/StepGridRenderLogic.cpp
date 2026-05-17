#include "ui/sequencer/StepGridRenderLogic.hpp"

#include <algorithm>

#include "ui/sequencer/StepVisualUtils.hpp"

namespace core::ui::sequencer::grid {

namespace {

constexpr uint8_t CHROMATIC_NOTE_COUNT = 12;
constexpr uint32_t CHROMATIC_NOTE_BASE_PALETTE_HEX[] = {
    0xF4F1DE,
    0xEAB69E,
    0xE07A5F,
    0xAA675E,
    0x73535C,
    0x3D405B,
    0x5F797A,
    0x81B29A,
    0xBABF94,
    0xF2CC8F,
    0xF3D8A9,
    0xF3E5C4,
};

uint8_t chromaIndexForNote(uint8_t note) {
    return static_cast<uint8_t>(note % CHROMATIC_NOTE_COUNT);
}

lv_color_t noteBaseColor(uint8_t note) {
    return lv_color_hex(CHROMATIC_NOTE_BASE_PALETTE_HEX[chromaIndexForNote(note)]);
}

lv_color_t velocityAccentColor(uint8_t note, uint8_t velocity) {
    using namespace core::ui::sequencer::visual;

    const lv_color_t fullColor = noteLabelColor(note);
    const lv_color_t minColor = grayscaleColor(VELOCITY_ZERO_FILL_BRIGHTNESS);
    const uint8_t mix = mapToRangeU8(velocity, VELOCITY_MAX, 0, LV_OPA_COVER);
    return lv_color_mix(fullColor, minColor, mix);
}

}  // namespace

lv_color_t noteLabelColor(uint8_t note) {
    return lv_color_lighten(noteBaseColor(note), STEP_NOTE_LABEL_LIGHTEN);
}

lv_color_t probabilityInlineIconColor(uint8_t note, uint8_t probability) {
    using namespace core::ui::sequencer::visual;

    const lv_color_t fullColor = noteLabelColor(note);
    const lv_color_t minColor = grayscaleColor(PROBABILITY_ICON_MIN_BRIGHTNESS);
    const uint8_t mix = mapToRangeU8(probability, 100, 0, LV_OPA_COVER);
    return lv_color_mix(fullColor, minColor, mix);
}

bool hasRuntimePitchFeedback(const TileRenderState& state) {
    return state.variation.visible &&
           state.variation.deltaVisible &&
           state.variation.resolved.triggered &&
           state.variation.resolved.ranges.pitchSemitones > 0;
}

uint8_t runtimePitchDisplayNote(const TileRenderState& state) {
    return hasRuntimePitchFeedback(state)
        ? state.variation.resolved.resolved.note
        : state.note;
}

bool sameVariationState(const TileVariationRenderState& lhs,
                        const TileVariationRenderState& rhs) {
    if (lhs.visible != rhs.visible) return false;
    if (!lhs.visible) return true;
    if (lhs.rangeVisible != rhs.rangeVisible) return false;
    if (lhs.deltaVisible != rhs.deltaVisible) return false;
    if (lhs.rangeProperty != rhs.rangeProperty) return false;

    const auto& a = lhs.resolved;
    const auto& b = rhs.resolved;
    return a.stepIndex == b.stepIndex &&
           a.cycleIndex == b.cycleIndex &&
           a.triggered == b.triggered &&
           a.base.note == b.base.note &&
           a.base.velocity == b.base.velocity &&
           a.base.gate == b.base.gate &&
           a.base.nudge == b.base.nudge &&
           a.resolved.note == b.resolved.note &&
           a.resolved.velocity == b.resolved.velocity &&
           a.resolved.gate == b.resolved.gate &&
           a.resolved.nudge == b.resolved.nudge &&
           a.ranges.pitchSemitones == b.ranges.pitchSemitones &&
           a.ranges.velocity == b.ranges.velocity &&
           a.ranges.gatePercent == b.ranges.gatePercent &&
           a.ranges.nudge == b.ranges.nudge &&
           a.pitchDelta == b.pitchDelta &&
           a.velocityDelta == b.velocityDelta &&
           a.gateDelta == b.gateDelta &&
           a.nudgeDelta == b.nudgeDelta;
}

StepVisualStyle buildStepVisualStyle(uint8_t note,
                                     uint8_t velocity,
                                     uint16_t gate,
                                     int8_t nudge,
                                     bool enabled,
                                     lv_coord_t railWidth,
                                     lv_coord_t buttonHeight) {
    using namespace core::ui::sequencer::visual;

    StepVisualStyle style;
    const lv_coord_t shapeMaxWidth = std::max<lv_coord_t>(STEP_SHAPE_MIN_WIDTH, railWidth);
    const lv_coord_t shapeMaxHeight = std::max<lv_coord_t>(
        STEP_SHAPE_MIN_HEIGHT,
        buttonHeight - STEP_BAR_HEIGHT - STEP_SHAPE_PAD_Y
    );
    const uint32_t scaledWidth =
        (static_cast<uint32_t>(gate) * static_cast<uint32_t>(shapeMaxWidth) + 50U) / 100U;
    style.width = static_cast<lv_coord_t>(std::max<uint32_t>(STEP_SHAPE_MIN_WIDTH, scaledWidth));

    const float velocityNorm =
        std::clamp(static_cast<float>(velocity) / static_cast<float>(VELOCITY_MAX), 0.0f, 1.0f);
    style.height = static_cast<lv_coord_t>(
        STEP_SHAPE_MIN_HEIGHT +
        static_cast<lv_coord_t>(
            velocityNorm * static_cast<float>(shapeMaxHeight - STEP_SHAPE_MIN_HEIGHT)
        )
    );

    const int clampedNudge = std::clamp<int>(nudge, -NUDGE_VISUAL_MAX, NUDGE_VISUAL_MAX);
    const int availableOffset = shapeMaxWidth / 2;
    style.x = static_cast<lv_coord_t>(
        STEP_SHAPE_PAD_X + ((clampedNudge * availableOffset) / NUDGE_VISUAL_MAX)
    );
    style.y = static_cast<lv_coord_t>(buttonHeight - STEP_BAR_HEIGHT - STEP_SHAPE_PAD_Y - style.height);

    if (enabled) {
        style.strokeColor = velocityAccentColor(note, velocity);
        style.strokeOpa = (velocity == 0) ? STEP_SHAPE_OPA_VELOCITY_ZERO : STEP_SHAPE_OPA_ENABLED;
    } else {
        style.strokeColor = grayscaleColor(SHAPE_DISABLED_FILL_BRIGHTNESS);
        style.strokeOpa = STEP_SHAPE_OPA_DISABLED;
    }

    return style;
}

TileRenderDiff diffTileRenderState(const TileRenderCache& cache, const TileRenderState& state) {
    TileRenderDiff diff;
    diff.initialized = cache.initialized;
    diff.absoluteStepChanged = !diff.initialized || cache.absoluteStep != state.absoluteStep;
    diff.inPatternChanged = !diff.initialized || cache.inPattern != state.inPattern;
    diff.enabledChanged = !diff.initialized || cache.enabled != state.enabled;
    diff.noteChanged = !diff.initialized || cache.note != state.note;
    diff.velocityChanged = !diff.initialized || cache.velocity != state.velocity;
    diff.probabilityChanged = !diff.initialized || cache.probability != state.probability;
    diff.probabilityCycleActiveChanged =
        !diff.initialized || cache.probabilityCycleActive != state.probabilityCycleActive;
    diff.gateChanged = !diff.initialized || cache.gate != state.gate;
    diff.nudgeChanged = !diff.initialized || cache.nudge != state.nudge;
    diff.variationChanged = !diff.initialized || !sameVariationState(cache.variation, state.variation);
    diff.velocityZeroChanged =
        !diff.initialized || ((cache.velocity == 0) != (state.velocity == 0));
    diff.probabilityMaskChanged =
        !diff.initialized || (state.inPattern && diff.probabilityCycleActiveChanged);

    const bool baseChanged = diff.absoluteStepChanged || diff.inPatternChanged || diff.enabledChanged;
    diff.dataChanged =
        baseChanged ||
        (state.inPattern &&
         (diff.noteChanged || diff.velocityChanged || diff.probabilityChanged ||
          diff.gateChanged || diff.nudgeChanged || diff.variationChanged));
    diff.barChanged =
        !diff.initialized || diff.inPatternChanged || cache.playing != state.playing ||
        diff.variationChanged;
    return diff;
}

}  // namespace core::ui::sequencer::grid
