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

lv_color_t velocityMarkerColor(uint8_t note, uint8_t velocity) {
    return lv_color_lighten(velocityAccentColor(note, velocity), STEP_NOTE_MARKER_LIGHTEN);
}

lv_color_t probabilityInlineIconColor(uint8_t note, uint8_t probability) {
    using namespace core::ui::sequencer::visual;

    const lv_color_t fullColor = noteLabelColor(note);
    const lv_color_t minColor = grayscaleColor(PROBABILITY_ICON_MIN_BRIGHTNESS);
    const uint8_t mix = mapToRangeU8(probability, 100, 0, LV_OPA_COVER);
    return lv_color_mix(fullColor, minColor, mix);
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
    diff.inPatternChanged = !diff.initialized || cache.inPattern != state.inPattern;
    diff.enabledChanged = !diff.initialized || cache.enabled != state.enabled;
    diff.noteChanged = !diff.initialized || cache.note != state.note;
    diff.velocityChanged = !diff.initialized || cache.velocity != state.velocity;
    diff.probabilityChanged = !diff.initialized || cache.probability != state.probability;
    diff.probabilityCycleActiveChanged =
        !diff.initialized || cache.probabilityCycleActive != state.probabilityCycleActive;
    diff.gateChanged = !diff.initialized || cache.gate != state.gate;
    diff.nudgeChanged = !diff.initialized || cache.nudge != state.nudge;
    diff.velocityZeroChanged =
        !diff.initialized || ((cache.velocity == 0) != (state.velocity == 0));

    const bool baseChanged = diff.inPatternChanged || diff.enabledChanged;
    diff.dataChanged =
        baseChanged ||
        (state.inPattern &&
         (diff.noteChanged || diff.velocityChanged || diff.probabilityChanged ||
          diff.probabilityCycleActiveChanged || diff.gateChanged || diff.nudgeChanged));
    diff.barChanged = !diff.initialized || diff.inPatternChanged || cache.playing != state.playing;
    return diff;
}

}  // namespace core::ui::sequencer::grid
