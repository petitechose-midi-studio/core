#include "ui/sequencer/StepGridRenderLogic.hpp"

#include <algorithm>

#include <config/PlatformCompat.hpp>

#include "ui/sequencer/StepGridDataPalette.hpp"
#include "ui/sequencer/StepVisualUtils.hpp"

namespace core::ui::sequencer::grid {

namespace {

constexpr uint8_t CHROMATIC_NOTE_COUNT =
    static_cast<uint8_t>(palette::CHROMATIC_NOTES.size());
constexpr uint8_t PITCH_VIEW_OCTAVE = 12U;
constexpr uint8_t PITCH_VIEW_MAX_SPAN = 24U;
constexpr lv_coord_t PITCH_VIEW_TOP_PAD = 14;
// The two compact phase ribbons (Cycle then Micro) own the bottom of a tile.
// Keep pitch rails above that band so musical pitch and temporal phase never
// become visually ambiguous.
constexpr lv_coord_t PITCH_VIEW_BOTTOM_PAD = 14;
constexpr uint8_t EVENT_COLOR_FLOOR_MIX = 96U;

FLASHMEM uint8_t chromaIndexForNote(uint8_t note) {
    return static_cast<uint8_t>(note % CHROMATIC_NOTE_COUNT);
}

FLASHMEM lv_color_t noteBaseColor(uint8_t note) {
    return lv_color_hex(palette::CHROMATIC_NOTES[chromaIndexForNote(note)]);
}

bool sameNoteEvents(const TileNoteEventProjection& lhs,
                    const TileNoteEventProjection& rhs) {
    if (lhs.count != rhs.count || lhs.dense != rhs.dense) {
        return false;
    }
    for (uint8_t index = 0U; index < lhs.count; ++index) {
        const auto& a = lhs.events[index];
        const auto& b = rhs.events[index];
        if (a.startQ8 != b.startQ8 ||
            a.spanQ8 != b.spanQ8 ||
            a.note != b.note ||
            a.velocity != b.velocity ||
            a.active != b.active) {
            return false;
        }
    }
    return true;
}

}  // namespace

FLASHMEM lv_color_t noteLabelColor(uint8_t note) {
    return lv_color_lighten(noteBaseColor(note), STEP_NOTE_LABEL_LIGHTEN);
}

FLASHMEM lv_color_t probabilityInlineIconColor(uint8_t note, uint8_t probability) {
    using namespace core::ui::sequencer::visual;

    const lv_color_t fullColor = noteLabelColor(note);
    const lv_color_t minColor = grayscaleColor(PROBABILITY_ICON_MIN_BRIGHTNESS);
    const uint8_t mix = mapToRangeU8(probability, 100, 0, LV_OPA_COVER);
    return lv_color_mix(fullColor, minColor, mix);
}

FLASHMEM lv_color_t drumLaneAccentColor(
    uint32_t accentColor,
    uint8_t velocity,
    bool enabled
) {
    using namespace core::ui::sequencer::visual;

    if (!enabled) {
        return grayscaleColor(SHAPE_DISABLED_FILL_BRIGHTNESS);
    }
    const lv_color_t fullColor = lv_color_hex(accentColor);
    const lv_color_t minColor = grayscaleColor(VELOCITY_ZERO_FILL_BRIGHTNESS);
    const uint8_t mix = mapToRangeU8(velocity, VELOCITY_MAX, 0, LV_OPA_COVER);
    return lv_color_mix(fullColor, minColor, mix);
}

FLASHMEM lv_color_t stepEventAccentColor(uint32_t accentColor, uint8_t velocity) {
    using namespace core::ui::sequencer::visual;

    const lv_color_t fullColor = lv_color_hex(accentColor);
    const lv_color_t floorColor = grayscaleColor(VELOCITY_ZERO_FILL_BRIGHTNESS);
    const uint8_t mix = static_cast<uint8_t>(
        EVENT_COLOR_FLOOR_MIX +
        (static_cast<uint16_t>(velocity) *
         static_cast<uint16_t>(LV_OPA_COVER - EVENT_COLOR_FLOOR_MIX)) /
            VELOCITY_MAX
    );
    return lv_color_mix(fullColor, floorColor, mix);
}

FLASHMEM StepPitchViewport buildStepPitchViewport(
    const std::array<TileRenderState, 8>& tiles
) {
    bool found = false;
    uint8_t minNote = 127U;
    uint8_t maxNote = 0U;
    // Anchor the page to authored root/child steps only. Representative child
    // summaries and noteEvents can change with the active cycle and must never
    // recenter the visual scale during playback.
    for (const auto& tile : tiles) {
        if (!tile.inPattern || !tile.enabled) continue;
        minNote = std::min<uint8_t>(minNote, tile.note);
        maxNote = std::max<uint8_t>(maxNote, tile.note);
        found = true;
    }
    if (!found) return {};

    uint8_t lowNote = static_cast<uint8_t>(
        (minNote / PITCH_VIEW_OCTAVE) * PITCH_VIEW_OCTAVE
    );
    uint16_t span = StepPitchViewport::DEFAULT_SEMITONE_SPAN;
    const uint16_t required = static_cast<uint16_t>(maxNote) - lowNote;
    while (span < required && span < PITCH_VIEW_MAX_SPAN) {
        span = static_cast<uint16_t>(span + PITCH_VIEW_OCTAVE);
    }
    span = std::min<uint16_t>(span, PITCH_VIEW_MAX_SPAN);
    if (static_cast<uint16_t>(lowNote) + span > 127U) {
        lowNote = static_cast<uint8_t>(127U - span);
    }
    return {
        .lowNote = lowNote,
        .semitoneSpan = static_cast<uint8_t>(span),
    };
}

FLASHMEM StepPitchOverflow stepPitchOverflow(
    uint8_t note,
    const StepPitchViewport& viewport
) {
    if (note < viewport.lowNote) return StepPitchOverflow::BELOW;
    const uint16_t high = static_cast<uint16_t>(viewport.lowNote) +
        std::max<uint8_t>(1U, viewport.semitoneSpan);
    return static_cast<uint16_t>(note) > high
        ? StepPitchOverflow::ABOVE
        : StepPitchOverflow::NONE;
}

FLASHMEM lv_coord_t stepPitchY(
    uint8_t note,
    const StepPitchViewport& viewport,
    lv_coord_t buttonHeight
) {
    const lv_coord_t top = std::min<lv_coord_t>(
        PITCH_VIEW_TOP_PAD,
        std::max<lv_coord_t>(0, buttonHeight - 2)
    );
    const lv_coord_t bottom = std::max<lv_coord_t>(
        top,
        static_cast<lv_coord_t>(buttonHeight - PITCH_VIEW_BOTTOM_PAD - 1)
    );
    const lv_coord_t height = std::max<lv_coord_t>(1, bottom - top);
    const uint8_t span = std::max<uint8_t>(1U, viewport.semitoneSpan);
    const int relative = std::clamp<int>(
        static_cast<int>(note) - static_cast<int>(viewport.lowNote),
        0,
        span
    );
    const lv_coord_t offset = static_cast<lv_coord_t>(
        (static_cast<int32_t>(relative) * height + span / 2U) / span
    );
    return static_cast<lv_coord_t>(bottom - offset);
}

FLASHMEM uint8_t stepEventHeadHeight(uint8_t velocity) {
    if (velocity < 43U) return 3U;
    if (velocity < 86U) return 4U;
    return 5U;
}

FLASHMEM int scaleDegreeIndexForNote(oc::note::sequencer::StepSequencerScaleSettings settings,
                                     uint8_t note) {
    settings.clamp();
    const uint16_t mask = oc::note::sequencer::scaleMask(settings.type);
    const uint8_t outputPc =
        static_cast<uint8_t>(note % CHROMATIC_NOTE_COUNT);
    const uint8_t relative =
        static_cast<uint8_t>((outputPc + CHROMATIC_NOTE_COUNT - settings.root) % CHROMATIC_NOTE_COUNT);
    int degree = 0;
    for (uint8_t pc = 0; pc < CHROMATIC_NOTE_COUNT; ++pc) {
        if ((mask & static_cast<uint16_t>(1U << pc)) == 0) continue;
        if (pc == relative) return degree;
        ++degree;
    }
    return -1;
}

FLASHMEM const char* scaleDegreeLabel(int degree) {
    static constexpr const char* LABELS[] = {
        "I",
        "II",
        "III",
        "IV",
        "V",
        "VI",
        "VII",
        "VIII",
        "IX",
        "X",
        "XI",
        "XII",
    };
    return (degree >= 0 && degree < static_cast<int>(sizeof(LABELS) / sizeof(LABELS[0])))
        ? LABELS[degree]
        : "";
}

FLASHMEM bool hasRuntimePitchFeedback(const TileRenderState& state) {
    return state.variation.visible &&
           state.variation.deltaVisible &&
           (state.variation.resolved.ranges.pitchSemitones > 0 ||
            state.variation.resolved.resolved.note != state.note ||
            state.variation.resolved.resolved.note != state.variation.resolved.base.note ||
            !state.variation.resolved.scale.inputInScale);
}

FLASHMEM bool hasRuntimeVelocityFeedback(const TileRenderState& state) {
    return state.variation.visible &&
           state.variation.deltaVisible &&
           (state.variation.resolved.ranges.velocity > 0 ||
            state.variation.resolved.resolved.velocity != state.velocity ||
            state.variation.resolved.resolved.velocity != state.variation.resolved.base.velocity);
}

FLASHMEM bool hasRuntimeGateFeedback(const TileRenderState& state) {
    return state.variation.visible &&
           state.variation.deltaVisible &&
           (state.variation.resolved.ranges.gatePercent > 0 ||
            state.variation.resolved.resolved.gate != state.gate ||
            state.variation.resolved.resolved.gate != state.variation.resolved.base.gate);
}

FLASHMEM bool hasRuntimeNudgeFeedback(const TileRenderState& state) {
    return state.variation.visible &&
           state.variation.deltaVisible &&
           (state.variation.resolved.ranges.nudge > 0 ||
            state.variation.resolved.resolved.nudge != state.nudge ||
            state.variation.resolved.resolved.nudge != state.variation.resolved.base.nudge);
}

FLASHMEM bool hasRuntimePropertyFeedback(
    const TileRenderState& state,
    core::state::sequencer::StepProperty property
) {
    using core::state::sequencer::StepProperty;

    switch (property) {
        case StepProperty::NOTE:
            return hasRuntimePitchFeedback(state);
        case StepProperty::VELOCITY:
            return hasRuntimeVelocityFeedback(state);
        case StepProperty::GATE:
            return hasRuntimeGateFeedback(state);
        case StepProperty::NUDGE:
            return hasRuntimeNudgeFeedback(state);
        case StepProperty::PROBABILITY:
            return false;
    }
    return false;
}

FLASHMEM uint8_t runtimePitchDisplayNote(const TileRenderState& state) {
    return hasRuntimePitchFeedback(state)
        ? state.variation.resolved.resolved.note
        : state.note;
}

FLASHMEM uint8_t runtimeVelocityDisplayValue(const TileRenderState& state) {
    return hasRuntimeVelocityFeedback(state)
        ? state.variation.resolved.resolved.velocity
        : state.velocity;
}

FLASHMEM uint16_t runtimeGateDisplayValue(const TileRenderState& state) {
    return hasRuntimeGateFeedback(state)
        ? state.variation.resolved.resolved.gate
        : state.gate;
}

FLASHMEM int8_t runtimeNudgeDisplayValue(const TileRenderState& state) {
    return hasRuntimeNudgeFeedback(state)
        ? state.variation.resolved.resolved.nudge
        : state.nudge;
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
           a.scaleSettings.root == b.scaleSettings.root &&
           a.scaleSettings.type == b.scaleSettings.type &&
           a.scaleSettings.mode == b.scaleSettings.mode &&
           a.scale.inputInScale == b.scale.inputInScale &&
           a.scale.constrained == b.scale.constrained &&
           a.scale.outputNote == b.scale.outputNote &&
           a.pitchDelta == b.pitchDelta &&
           a.velocityDelta == b.velocityDelta &&
           a.gateDelta == b.gateDelta &&
           a.nudgeDelta == b.nudgeDelta;
}

bool sameContentBadges(const TileContentBadgeState& lhs,
                       const TileContentBadgeState& rhs) {
    return lhs.microSequence == rhs.microSequence &&
           lhs.cycleStates == rhs.cycleStates &&
           lhs.chord == rhs.chord &&
           lhs.expansionLimitReached == rhs.expansionLimitReached &&
           lhs.microCursorVisible == rhs.microCursorVisible &&
           lhs.cycleCursorVisible == rhs.cycleCursorVisible &&
           lhs.chordVoiceCount == rhs.chordVoiceCount &&
           lhs.chordSource == rhs.chordSource &&
           lhs.microLength == rhs.microLength &&
           lhs.microCursor == rhs.microCursor &&
           lhs.cycleLength == rhs.cycleLength &&
           lhs.cycleCursor == rhs.cycleCursor &&
           lhs.microActiveMask == rhs.microActiveMask &&
           lhs.cycleActiveMask == rhs.cycleActiveMask;
}

TileRenderDiff diffTileRenderState(const TileRenderCache& cache, const TileRenderState& state) {
    TileRenderDiff diff;
    const bool initialized = cache.initialized;
    diff.absoluteStepChanged = !initialized || cache.absoluteStep != state.absoluteStep;
    diff.inPatternChanged = !initialized || cache.inPattern != state.inPattern;
    diff.enabledChanged = !initialized || cache.enabled != state.enabled;
    diff.stepSelectionChanged =
        !initialized ||
        cache.stepSelectionActive != state.stepSelectionActive ||
        cache.stepSelectionCursor != state.stepSelectionCursor ||
        cache.stepSelectionSelected != state.stepSelectionSelected ||
        cache.stepPastePreviewActive != state.stepPastePreviewActive ||
        cache.stepPastePreview != state.stepPastePreview;
    diff.playheadVisibleChanged =
        !initialized || cache.playheadVisible != state.playheadVisible;
    diff.playheadProgressChanged =
        !initialized || cache.playheadProgress != state.playheadProgress;
    diff.noteChanged = !initialized || cache.note != state.note;
    diff.velocityChanged = !initialized || cache.velocity != state.velocity;
    diff.probabilityChanged = !initialized || cache.probability != state.probability;
    diff.probabilityCycleActiveChanged =
        !initialized || cache.probabilityCycleActive != state.probabilityCycleActive;
    diff.gateChanged = !initialized || cache.gate != state.gate;
    diff.nudgeChanged = !initialized || cache.nudge != state.nudge;
    diff.childContentChanged =
        !initialized ||
        cache.childContentContext != state.childContentContext ||
        cache.childContentOffset != state.childContentOffset ||
        cache.childContentNoteOffsetUsesScaleDegrees != state.childContentNoteOffsetUsesScaleDegrees;
    diff.childPitchSummaryChanged =
        !initialized ||
        cache.childPitchSummaryVisible != state.childPitchSummaryVisible ||
        cache.childPitchSummaryNote != state.childPitchSummaryNote;
    diff.variationChanged = !initialized || !sameVariationState(cache.variation, state.variation);
    diff.contentBadgesChanged =
        !initialized || !sameContentBadges(cache.contentBadges, state.contentBadges);
    diff.noteEventsChanged =
        !initialized || !sameNoteEvents(cache.noteEvents, state.noteEvents);
    diff.probabilityMaskChanged =
        !initialized || (state.inPattern && diff.probabilityCycleActiveChanged);

    const bool baseChanged = diff.absoluteStepChanged || diff.inPatternChanged || diff.enabledChanged;
    diff.dataChanged =
        baseChanged ||
        diff.stepSelectionChanged ||
        (state.inPattern &&
         (diff.noteChanged || diff.velocityChanged || diff.probabilityChanged ||
          diff.gateChanged || diff.nudgeChanged || diff.variationChanged ||
          diff.childContentChanged || diff.childPitchSummaryChanged ||
          diff.noteEventsChanged));
    diff.playheadChanged =
        !initialized || diff.inPatternChanged || diff.playheadVisibleChanged ||
        diff.playheadProgressChanged || cache.playing != state.playing ||
        diff.variationChanged;
    return diff;
}

}  // namespace core::ui::sequencer::grid
