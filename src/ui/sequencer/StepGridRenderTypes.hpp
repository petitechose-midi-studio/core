#pragma once

#include <array>
#include <cstdint>
#include <cstring>

#include <config/PlatformCompat.hpp>
#include <lvgl.h>
#include <oc/note/sequencer/StepBitMask128.hpp>
#include <oc/note/sequencer/StepSequencerVariation.hpp>

#include "state/sequencer/SequencerUiState.hpp"
#include "ui/sequencer/StepContentBadgeProjection.hpp"

namespace core::ui::sequencer::grid {

enum class StepGridPresentation : uint8_t {
    MELODIC = 0,
    DRUM_LANE,
};

struct TileVariationRenderState {
    bool visible = false;
    bool rangeVisible = false;
    bool deltaVisible = false;
    core::state::sequencer::StepProperty rangeProperty =
        core::state::sequencer::StepProperty::NOTE;
    oc::note::sequencer::StepSequencerResolvedVariation resolved{};
};

using TileContentBadgeState = StepContentBadgeProjection;

struct StepPitchViewport {
    static constexpr uint8_t DEFAULT_LOW_NOTE = 48U;
    static constexpr uint8_t DEFAULT_SEMITONE_SPAN = 12U;

    uint8_t lowNote = DEFAULT_LOW_NOTE;
    uint8_t semitoneSpan = DEFAULT_SEMITONE_SPAN;
};

struct TileNoteEventRenderState {
    // Q8 positions are relative to the owning root step: 256 == one step.
    int16_t startQ8 = 0;
    uint16_t spanQ8 = 256U;
    uint8_t note = 0U;
    uint8_t velocity = 0U;
    uint8_t active = 0U;
};

struct TileNoteEventProjection {
    static constexpr uint8_t MAX_DETAILED_EVENTS = 8U;
    static constexpr uint8_t VISIBLE_TILE_COUNT = 8U;
    static constexpr int32_t Q8_PER_TILE = 256;

    std::array<TileNoteEventRenderState, MAX_DETAILED_EVENTS> events{};
    uint8_t count = 0U;
    bool dense = false;

    FLASHMEM uint8_t coveredTileMask(uint8_t ownerTile) const {
        if (ownerTile >= VISIBLE_TILE_COUNT) return 0U;

        constexpr int32_t PAGE_END_Q8 =
            VISIBLE_TILE_COUNT * Q8_PER_TILE;
        uint8_t mask = 0U;
        for (uint8_t index = 0U; index < count; ++index) {
            const auto& event = events[index];
            const int32_t startQ8 =
                static_cast<int32_t>(ownerTile) * Q8_PER_TILE +
                event.startQ8;
            const int32_t endQ8 = startQ8 +
                (event.spanQ8 == 0U ? 1 : event.spanQ8);
            if (endQ8 <= 0 || startQ8 >= PAGE_END_Q8) continue;

            const int32_t clippedStartQ8 = startQ8 < 0 ? 0 : startQ8;
            const int32_t clippedEndQ8 =
                endQ8 > PAGE_END_Q8 ? PAGE_END_Q8 : endQ8;
            const uint8_t first = static_cast<uint8_t>(
                clippedStartQ8 / Q8_PER_TILE
            );
            const uint8_t last = static_cast<uint8_t>(
                (clippedEndQ8 - 1) / Q8_PER_TILE
            );
            for (uint8_t tile = first; tile <= last; ++tile) {
                mask = static_cast<uint8_t>(mask | (1U << tile));
            }
        }
        return mask;
    }
};

/**
 * Data exchanged between step-grid projection, planning, and rendering.
 *
 * Frame state is the current desired visual model, cache stores what LVGL last
 * rendered, and diffs tell renderers which tile surfaces must be updated.
 */
struct TileRenderState {
    uint8_t absoluteStep = 0;
    bool inPattern = false;
    bool enabled = false;
    bool stepSelectionActive = false;
    bool stepSelectionCursor = false;
    bool stepSelectionSelected = false;
    bool stepPastePreviewActive = false;
    core::state::sequencer::SequencerStepPastePreview stepPastePreview =
        core::state::sequencer::SequencerStepPastePreview::NONE;
    bool playheadVisible = false;
    bool playing = false;
    bool probabilityCycleActive = false;
    uint8_t note = 0;
    uint8_t velocity = 0;
    uint8_t probability = 0;
    uint16_t gate = 0;
    int8_t nudge = 0;
    bool childContentContext = false;
    int16_t childContentOffset = 0;
    bool childContentNoteOffsetUsesScaleDegrees = false;
    bool childPitchSummaryVisible = false;
    uint8_t childPitchSummaryNote = 0;
    uint8_t playheadProgress = 0U;
    TileVariationRenderState variation{};
    TileContentBadgeState contentBadges{};
    TileNoteEventProjection noteEvents{};
};

struct TileRenderDiff {
    bool absoluteStepChanged = false;
    bool inPatternChanged = false;
    bool enabledChanged = false;
    bool stepSelectionChanged = false;
    bool playheadVisibleChanged = false;
    bool playheadProgressChanged = false;
    bool noteChanged = false;
    bool velocityChanged = false;
    bool probabilityChanged = false;
    bool probabilityCycleActiveChanged = false;
    bool gateChanged = false;
    bool nudgeChanged = false;
    bool childContentChanged = false;
    bool childPitchSummaryChanged = false;
    bool variationChanged = false;
    bool contentBadgesChanged = false;
    bool noteEventsChanged = false;
    bool probabilityMaskChanged = false;
    bool dataChanged = false;
    bool playheadChanged = false;
};

struct TileRenderCache : TileRenderState {
    bool initialized = false;
    lv_coord_t noteLabelHeight = 0;
    bool noteLabelVisible = false;
    bool secondaryLabelVisible = false;
    bool inlineIconVisible = false;
    uint32_t noteLabelColorFull = 0;
    lv_opa_t noteLabelOpa = LV_OPA_TRANSP;
    uint32_t secondaryLabelColorFull = 0;
    lv_opa_t secondaryLabelOpa = LV_OPA_TRANSP;
    uint32_t inlineIconColorFull = 0;
    lv_opa_t inlineIconOpa = LV_OPA_TRANSP;
    lv_coord_t noteLabelX = 0;
    lv_coord_t noteLabelY = 0;
    lv_coord_t secondaryLabelX = 0;
    lv_coord_t secondaryLabelY = 0;
    lv_coord_t inlineIconX = 0;
    lv_coord_t inlineIconY = 0;
    char noteLabelText[16] = {0};
    char secondaryLabelText[12] = {0};
    bool playheadLineVisible = false;
    uint32_t playheadLineColorFull = 0;
    lv_opa_t playheadLineOpa = LV_OPA_TRANSP;
    char stepIndexText[4] = {0};

    void commitRenderedState(const TileRenderState& state) {
        static_cast<TileRenderState&>(*this) = state;
        initialized = true;
    }
};

struct StepGridFrameState {
    StepGridPresentation presentation = StepGridPresentation::MELODIC;
    uint32_t accentColor = 0;
    StepPitchViewport pitchViewport{};
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings{};
    bool chromaticPitchEditing = false;
    core::state::sequencer::StepProperty activeProperty =
        core::state::sequencer::StepProperty::NOTE;
    bool feedbackVisible = false;
    oc::note::sequencer::StepBitMask128 feedbackTouchedMask{};
    core::state::sequencer::StepProperty feedbackProperty =
        core::state::sequencer::StepProperty::NOTE;
    std::array<TileRenderState, 8> tiles{};
};

}  // namespace core::ui::sequencer::grid
