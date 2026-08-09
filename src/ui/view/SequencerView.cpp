#include "SequencerView.hpp"

#include <array>
#include <cstddef>
#include <cstdio>

#include <config/PlatformCompat.hpp>
#include <ms/ui/font/CoreFonts.hpp>
#include <oc/diagnostics/Performance.hpp>

#include "ui/sequencer/SequencerViewModelBuilder.hpp"
#include "ui/sequencer/SequencerCcLaneGridViewModelBuilder.hpp"
#include "ui/sequencer/SequencerTrackPasteProjection.hpp"
#include "ui/theme/StandaloneTheme.hpp"
#include "ui/view/RetainedViewRenderPolicy.hpp"
#if defined(MS_DRUM_TRACK_UX_PROTOTYPE)
#include "state/sequencer/DrumPatternState.hpp"
#endif

namespace core::ui {

namespace theme = standalone::theme;

#if defined(MS_DRUM_TRACK_UX_PROTOTYPE)
namespace {

constexpr uint32_t DRUM_ACCENT = standalone::theme::color::STEP_VELOCITY;
constexpr uint32_t DRUM_TIMING = standalone::theme::color::STEP_DIVISION;
constexpr lv_coord_t DRUM_HEADER_HEIGHT = 18;
constexpr lv_coord_t DRUM_LANE_HEIGHT = 29;
constexpr lv_coord_t DRUM_LABEL_WIDTH = 62;

FLASHMEM void drawDrumPrototypeRect(
    lv_layer_t* layer,
    const lv_area_t& area,
    uint32_t color,
    lv_opa_t backgroundOpacity,
    lv_coord_t borderWidth = 0,
    lv_opa_t borderOpacity = LV_OPA_TRANSP,
    lv_coord_t radius = 0
) {
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color = lv_color_hex(color);
    dsc.bg_opa = backgroundOpacity;
    dsc.border_color = lv_color_hex(color);
    dsc.border_width = borderWidth;
    dsc.border_opa = borderOpacity;
    dsc.radius = radius;
    lv_draw_rect(layer, &dsc, &area);
}

FLASHMEM void drawDrumPrototypeLabel(
    lv_layer_t* layer,
    const lv_area_t& area,
    const char* text,
    uint32_t color,
    lv_opa_t opacity,
    lv_text_align_t alignment = LV_TEXT_ALIGN_CENTER
) {
    lv_draw_label_dsc_t dsc;
    lv_draw_label_dsc_init(&dsc);
    dsc.text = text;
    dsc.font = fonts.inter_12_medium ? fonts.inter_12_medium : LV_FONT_DEFAULT;
    dsc.color = lv_color_hex(color);
    dsc.opa = opacity;
    dsc.align = alignment;
    lv_draw_label(layer, &dsc, &area);
}

}  // namespace
#endif

FLASHMEM SequencerView::SequencerView(lv_obj_t* parent, StateRefs stateRefs)
    : state_refs_(stateRefs) {
    createLayout(parent);
    if (!frame_ || !frame_->valid() || !container_ || !body_container_) return;
    createHistoryToast();
    if (!history_toast_ || !history_toast_line1_ ||
        !history_toast_line2_ || !history_toast_line3_) return;
    createHeaderBar();
    if (!header_bar_ || !header_bar_->getElement()) return;
    createActionStrips();
    if (!left_action_strip_ || !left_action_strip_->getElement() ||
        !bottom_action_strip_ || !bottom_action_strip_->getElement() ||
        !center_column_) return;
    createPropertySelectionOverlay();
    if (!property_selection_overlay_ || !property_selection_overlay_->getElement()) return;
    createGrid();
    if (!step_grid_ || !step_grid_->getElement() ||
        !cc_lane_grid_ || !cc_lane_grid_->getElement()
#if defined(MS_DRUM_TRACK_UX_PROTOTYPE)
        || !drum_track_ux_prototype_grid_
#endif
    ) return;
    createTrackPastePreflightCard();
    if (!track_paste_preflight_card_ || !track_paste_preflight_card_->valid()) return;
    ensureRenderScheduler();
    if (!render_scheduler_ || !render_scheduler_->valid() || !bindToState()) return;
    initialized_ = true;
}

FLASHMEM SequencerView::~SequencerView() {
    render_scheduler_.reset();

    track_paste_preflight_card_.reset();
#if defined(MS_DRUM_TRACK_UX_PROTOTYPE)
    if (drum_track_ux_prototype_grid_) {
        lv_obj_delete(drum_track_ux_prototype_grid_);
        drum_track_ux_prototype_grid_ = nullptr;
    }
#endif
    cc_lane_grid_.reset();
    step_grid_.reset();
    bottom_action_strip_.reset();
    property_selection_overlay_.reset();
    left_action_strip_.reset();
    history_toast_ = nullptr;
    history_toast_line1_ = nullptr;
    history_toast_line2_ = nullptr;
    history_toast_line3_ = nullptr;
    header_bar_.reset();
    frame_.reset();
    container_ = nullptr;
    body_container_ = nullptr;
    interaction_container_ = nullptr;
    center_column_ = nullptr;
}

FLASHMEM void SequencerView::onActivate() {
    if (!container_) return;

    RetainedViewRenderPolicy::show(container_);
    if (render_scheduler_) render_scheduler_->resumePending(true);
}

FLASHMEM void SequencerView::onDeactivate() {
    if (render_scheduler_) render_scheduler_->pause();
    RetainedViewRenderPolicy::hide(container_);
}

FLASHMEM void SequencerView::createLayout(lv_obj_t* parent) {
    frame_ = core::app::makeExtmemUnique<MainViewFrame>(parent);
    if (!frame_ || !frame_->valid()) return;
    container_ = frame_->container();
    body_container_ = frame_->body();
    RetainedViewRenderPolicy::initializeHidden(container_);
}

FLASHMEM void SequencerView::createHeaderBar() {
    if (!frame_) return;
    header_bar_ = core::app::makeExtmemUnique<SequencerHeaderBar>(frame_->header());
}

FLASHMEM void SequencerView::createGrid() {
    if (!center_column_) return;
    step_grid_ = core::app::makeExtmemUnique<StepGrid>(
        center_column_,
        onStepGridGeometryInvalidated,
        this
    );
    cc_lane_grid_ = core::app::makeExtmemUnique<SequencerCcLaneGrid>(
        center_column_,
        SequencerCcLaneGridLayout::EMBEDDED
    );
#if defined(MS_DRUM_TRACK_UX_PROTOTYPE)
    createDrumTrackUxPrototypeGrid();
#endif
}

#if defined(MS_DRUM_TRACK_UX_PROTOTYPE)
FLASHMEM void SequencerView::createDrumTrackUxPrototypeGrid() {
    if (!center_column_) return;

    drum_track_ux_prototype_grid_ = lv_obj_create(center_column_);
    if (!drum_track_ux_prototype_grid_) return;
    lv_obj_remove_style_all(drum_track_ux_prototype_grid_);
    lv_obj_add_flag(drum_track_ux_prototype_grid_, LV_OBJ_FLAG_FLOATING);
    lv_obj_add_flag(drum_track_ux_prototype_grid_, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_add_flag(drum_track_ux_prototype_grid_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(drum_track_ux_prototype_grid_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(drum_track_ux_prototype_grid_, 0, 0);
    lv_obj_set_size(drum_track_ux_prototype_grid_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(
        drum_track_ux_prototype_grid_,
        lv_color_hex(theme::color::BACKGROUND),
        0
    );
    lv_obj_set_style_bg_opa(drum_track_ux_prototype_grid_, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(
        drum_track_ux_prototype_grid_,
        onDrumTrackUxPrototypeDrawEvent,
        LV_EVENT_DRAW_MAIN,
        this
    );
}

FLASHMEM void SequencerView::onDrumTrackUxPrototypeDrawEvent(
    lv_event_t* event
) {
    auto* self = static_cast<SequencerView*>(lv_event_get_user_data(event));
    if (!self) return;
    self->drawDrumTrackUxPrototypeGrid(lv_event_get_layer(event));
}

FLASHMEM void SequencerView::drawDrumTrackUxPrototypeGrid(
    lv_layer_t* layer
) {
    if (!layer || !drum_track_ux_prototype_grid_) return;
    const auto& prototype = state_refs_.sequencer.drumTrackUxPrototype;
    if (!prototype.gridVisible()) return;

    lv_area_t surface{};
    lv_obj_get_coords(drum_track_ux_prototype_grid_, &surface);
    const lv_coord_t width = static_cast<lv_coord_t>(
        surface.x2 - surface.x1 + 1
    );
    if (width <= DRUM_LABEL_WIDTH) return;
    const lv_coord_t cellWidth = static_cast<lv_coord_t>(
        (width - DRUM_LABEL_WIDTH) /
        core::state::sequencer::DrumTrackUxPrototypeState::STEPS_PER_PAGE
    );

    char header[40] = {};
    std::snprintf(
        header,
        sizeof(header),
        "DRUM  L%u/%u  %s",
        static_cast<unsigned>(prototype.selectedLane + 1U),
        static_cast<unsigned>(prototype.LANE_COUNT),
        prototype.property ==
                core::state::sequencer::DrumTrackUxPrototypeProperty::STATE
            ? "STATE"
            : "VELOCITY"
    );
    const lv_area_t headerArea{
        .x1 = static_cast<lv_coord_t>(surface.x1 + 2),
        .y1 = surface.y1,
        .x2 = static_cast<lv_coord_t>(surface.x2 - 2),
        .y2 = static_cast<lv_coord_t>(surface.y1 + DRUM_HEADER_HEIGHT - 1),
    };
    drawDrumPrototypeLabel(
        layer,
        headerArea,
        header,
        DRUM_ACCENT,
        LV_OPA_COVER,
        LV_TEXT_ALIGN_LEFT
    );

    for (uint8_t row = 0;
         row < core::state::sequencer::
             DrumTrackUxPrototypeState::VISIBLE_LANE_COUNT;
         ++row) {
        const uint8_t lane = prototype.visibleLane(row);
        const bool selected = lane == prototype.selectedLane;
        const lv_coord_t rowY = static_cast<lv_coord_t>(
            surface.y1 + DRUM_HEADER_HEIGHT + row * DRUM_LANE_HEIGHT
        );
        const lv_area_t rowArea{
            .x1 = surface.x1,
            .y1 = rowY,
            .x2 = surface.x2,
            .y2 = static_cast<lv_coord_t>(rowY + DRUM_LANE_HEIGHT - 1),
        };
        if (selected) {
            drawDrumPrototypeRect(
                layer,
                rowArea,
                DRUM_ACCENT,
                LV_OPA_10
            );
        }

        const lv_area_t nameArea{
            .x1 = static_cast<lv_coord_t>(surface.x1 + 2),
            .y1 = rowY,
            .x2 = static_cast<lv_coord_t>(
                surface.x1 + DRUM_LABEL_WIDTH - 3
            ),
            .y2 = static_cast<lv_coord_t>(rowY + 13),
        };
        char name[16] = {};
        const auto& descriptor = prototype.drumTrack->kit.lanes[lane];
        std::snprintf(
            name,
            sizeof(name),
            "%s %u",
            core::state::sequencer::drumLaneRoleLabel(descriptor.role),
            static_cast<unsigned>(descriptor.midiNote)
        );
        drawDrumPrototypeLabel(
            layer,
            nameArea,
            name,
            selected ? DRUM_ACCENT : theme::color::TEXT_SECONDARY,
            selected ? LV_OPA_COVER : LV_OPA_70,
            LV_TEXT_ALIGN_LEFT
        );

        const auto& lanePattern = prototype.drumTrack->pattern.lanes[lane];
        const uint8_t laneLength =
            prototype.drumTrack->pattern.effectiveLength(lane);
        const uint8_t stepsPerBeat =
            prototype.drumTrack->pattern.effectiveStepsPerBeat(lane);
        char timing[16] = {};
        std::snprintf(
            timing,
            sizeof(timing),
            "%ux1/%u%s",
            static_cast<unsigned>(laneLength),
            static_cast<unsigned>(stepsPerBeat * 4U),
            lanePattern.timing.mode == core::state::sequencer::
                    DrumLaneTimingMode::CUSTOM
                ? "*"
                : ""
        );
        const lv_area_t timingArea{
            .x1 = static_cast<lv_coord_t>(surface.x1 + 2),
            .y1 = static_cast<lv_coord_t>(rowY + 14),
            .x2 = static_cast<lv_coord_t>(
                surface.x1 + DRUM_LABEL_WIDTH - 3
            ),
            .y2 = static_cast<lv_coord_t>(rowY + DRUM_LANE_HEIGHT - 1),
        };
        drawDrumPrototypeLabel(
            layer,
            timingArea,
            timing,
            lanePattern.timing.mode == core::state::sequencer::
                    DrumLaneTimingMode::CUSTOM
                ? DRUM_TIMING
                : theme::color::TEXT_SECONDARY,
            selected ? LV_OPA_COVER : LV_OPA_60,
            LV_TEXT_ALIGN_LEFT
        );

        for (uint8_t column = 0;
             column < core::state::sequencer::
                 DrumTrackUxPrototypeState::STEPS_PER_PAGE;
            ++column) {
            const uint8_t step = prototype.visibleStep(column);
            const bool available = step < laneLength;
            const bool active = available &&
                prototype.drumTrack->pattern.stepEnabled(lane, step);
            const uint8_t velocity =
                prototype.drumTrack->pattern.lanes[lane].velocity[step];
            const lv_coord_t cellX = static_cast<lv_coord_t>(
                surface.x1 + DRUM_LABEL_WIDTH + column * cellWidth
            );
            const lv_area_t cellArea{
                .x1 = static_cast<lv_coord_t>(cellX + 1),
                .y1 = static_cast<lv_coord_t>(rowY + 1),
                .x2 = static_cast<lv_coord_t>(cellX + cellWidth - 2),
                .y2 = static_cast<lv_coord_t>(
                    rowY + DRUM_LANE_HEIGHT - 2
                ),
            };
            const lv_opa_t fillOpacity = !available
                ? LV_OPA_10
                : active
                ? static_cast<lv_opa_t>(
                      prototype.property == core::state::sequencer::
                              DrumTrackUxPrototypeProperty::VELOCITY
                          ? 45U + (static_cast<uint16_t>(velocity) * 175U) /
                                127U
                          : 155U
                  )
                : LV_OPA_TRANSP;
            drawDrumPrototypeRect(
                layer,
                cellArea,
                available ? DRUM_ACCENT : theme::color::INACTIVE,
                fillOpacity,
                selected && available ? 1 : 0,
                selected && available ? LV_OPA_70 : LV_OPA_TRANSP,
                2
            );

            if (selected && available &&
                prototype.property == core::state::sequencer::
                    DrumTrackUxPrototypeProperty::STATE) {
                char value[3] = {};
                std::snprintf(
                    value,
                    sizeof(value),
                    "%u",
                    static_cast<unsigned>(column + 1U)
                );
                drawDrumPrototypeLabel(
                    layer,
                    cellArea,
                    value,
                    active
                        ? theme::color::TEXT_PRIMARY
                        : theme::color::TEXT_SECONDARY,
                    active ? LV_OPA_COVER : LV_OPA_50
                );
            }
        }
    }
}
#endif

FLASHMEM void SequencerView::createPropertySelectionOverlay() {
    if (!container_) return;
    property_selection_overlay_ =
        core::app::makeExtmemUnique<StepPropertySelectionOverlay>(container_);
}

FLASHMEM void SequencerView::createHistoryToast() {
    if (!container_) return;

    history_toast_ = lv_obj_create(container_);
    if (!history_toast_) return;
    lv_obj_remove_style_all(history_toast_);
    lv_obj_add_flag(history_toast_, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_add_flag(history_toast_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(history_toast_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(history_toast_, 150, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(history_toast_, lv_color_hex(theme::color::BACKGROUND), 0);
    lv_obj_set_style_bg_opa(history_toast_, LV_OPA_90, 0);
    lv_obj_set_style_border_width(history_toast_, 1, 0);
    lv_obj_set_style_border_color(history_toast_, lv_color_hex(theme::color::TEXT_SECONDARY), 0);
    lv_obj_set_style_border_opa(history_toast_, LV_OPA_50, 0);
    lv_obj_set_style_radius(history_toast_, 5, 0);
    lv_obj_set_style_pad_left(history_toast_, 8, 0);
    lv_obj_set_style_pad_right(history_toast_, 8, 0);
    lv_obj_set_style_pad_top(history_toast_, 5, 0);
    lv_obj_set_style_pad_bottom(history_toast_, 5, 0);
    lv_obj_set_style_pad_row(history_toast_, 1, 0);
    lv_obj_set_layout(history_toast_, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(history_toast_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        history_toast_,
        LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_CENTER
    );
    lv_obj_align(history_toast_, LV_ALIGN_TOP_MID, 0, 34);

    history_toast_line1_ = lv_label_create(history_toast_);
    history_toast_line2_ = lv_label_create(history_toast_);
    history_toast_line3_ = lv_label_create(history_toast_);
    if (!history_toast_line1_ || !history_toast_line2_ || !history_toast_line3_) return;

    lv_obj_t* labels[] = {history_toast_line1_, history_toast_line2_, history_toast_line3_};
    for (lv_obj_t* label : labels) {
        lv_obj_set_width(label, 134);
        lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    }

    lv_obj_set_style_text_font(history_toast_line1_, fonts.inter_13_bold, 0);
    lv_obj_set_style_text_color(history_toast_line1_, lv_color_hex(theme::color::TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(history_toast_line2_, fonts.inter_13_medium, 0);
    lv_obj_set_style_text_color(history_toast_line2_, lv_color_hex(theme::color::TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(history_toast_line3_, fonts.inter_13_medium, 0);
    lv_obj_set_style_text_color(history_toast_line3_, lv_color_hex(theme::color::TEXT_SECONDARY), 0);
}

FLASHMEM void SequencerView::createTrackPastePreflightCard() {
    if (!container_) return;
    track_paste_preflight_card_ =
        core::app::makeExtmemUnique<
            core::ui::sequencer::SequencerTrackPastePreflightCard>(container_);
}

FLASHMEM void SequencerView::createActionStrips() {
    if (!frame_ || !body_container_) return;
    frame_->createInteractionRow();
    interaction_container_ = frame_->interactionRow();
    if (!interaction_container_) return;

    left_action_strip_ = core::app::makeExtmemUnique<ContextActionStrip>(
        interaction_container_,
        ContextActionStripOrientation::VERTICAL,
        ContextActionStripVerticalLayout::SPREAD
    );
    if (!left_action_strip_ || !left_action_strip_->getElement()) return;

    frame_->createCenterColumn();
    center_column_ = frame_->centerColumn();
    if (!center_column_) return;

    bottom_action_strip_ = core::app::makeExtmemUnique<ContextActionStrip>(
        body_container_,
        ContextActionStripOrientation::HORIZONTAL
    );
}

void SequencerView::onStepGridGeometryInvalidated(void* userData) {
    auto* self = static_cast<SequencerView*>(userData);
    if (!self) return;
    self->requestGridRender();
}

FLASHMEM bool SequencerView::bindToState() {
    bindHeaderState();
    bindHeaderStripState();
    const bool structureSelectionBound = bindStructureSelectionState();
    bindGridState();
    bindSelectorOverlayState();
    bindOverlayVisibilityState();
    bindLeftActionStripState();
    bindBottomActionStripState();
    bindHistoryFeedbackState();
    bindTrackSwitchReadyState();
    bindTrackPastePreflightState();
    bindClipboardState();

    const bool bound =
        structureSelectionBound &&
        header_watcher_.subscriptionCount() == header_watcher_.capacity() &&
        header_strip_watcher_.subscriptionCount() == header_strip_watcher_.capacity() &&
        structure_selection_watcher_.subscriptionCount() ==
            structure_selection_watcher_.capacity() &&
        grid_watcher_.subscriptionCount() == grid_watcher_.capacity() &&
        grid_tick_watcher_.subscriptionCount() == grid_tick_watcher_.capacity() &&
        selector_overlay_watcher_.subscriptionCount() == selector_overlay_watcher_.capacity() &&
        overlay_visibility_watcher_.subscriptionCount() == overlay_visibility_watcher_.capacity() &&
        left_action_strip_watcher_.subscriptionCount() == left_action_strip_watcher_.capacity() &&
        bottom_action_strip_watcher_.subscriptionCount() == bottom_action_strip_watcher_.capacity() &&
        history_feedback_watcher_.subscriptionCount() == history_feedback_watcher_.capacity() &&
        track_switch_ready_watcher_.subscriptionCount() == track_switch_ready_watcher_.capacity() &&
        track_paste_preflight_watcher_.subscriptionCount() ==
            track_paste_preflight_watcher_.capacity() &&
        clipboard_watcher_.subscriptionCount() == clipboard_watcher_.capacity();
    if (!bound) return false;

    markAllDirty();
    return true;
}

FLASHMEM void SequencerView::bindHeaderState() {
    header_watcher_.bind<&SequencerView::requestHeaderAndLeftRender>(
        *this, 0, "SequencerView.header"
    );
    header_watcher_.watchAll(
        state_refs_.sharedTrackActive,
        state_refs_.sharedTrackEnabledMask,
        state_refs_.structureNavigationFocus,
        state_refs_.trackNavigation.previewAddSlot,
        state_refs_.trackNavigation.previewTrackIndex,
        state_refs_.sequencer.structureUi.previewPageIndex,
        state_refs_.sequencer.structureUi.stepSelection.active,
        state_refs_.sequencer.structureUi.stepSelection.selectedMask,
        state_refs_.sequencer.contentView.kind,
        state_refs_.sequencer.contentView.length,
        state_refs_.sequencer.contentView.revision,
        state_refs_.sequencer.structureUi.trackPaste.revision,
        state_refs_.sequencer.ccLaneUi.revision,
        state_refs_.sequencer.patternQuickControls.previewRevision
    );
}

FLASHMEM void SequencerView::bindHeaderStripState() {
    header_strip_watcher_.bind<&SequencerView::requestHeaderStripAndLeftRender>(
        *this, 1, "SequencerView.headerStrip"
    );
    header_strip_watcher_.watchAll(
        state_refs_.sharedTrackActive,
        state_refs_.sharedTrackEnabledMask,
        state_refs_.sequencer.pattern.length,
        state_refs_.sequencer.page,
        state_refs_.structureNavigationFocus,
        state_refs_.trackNavigation.previewAddSlot,
        state_refs_.trackNavigation.previewTrackIndex,
        state_refs_.sequencer.structureUi.previewPageIndex,
        state_refs_.sequencer.structureUi.stepSelection.active,
        state_refs_.sequencer.structureUi.stepSelection.selectedMask,
        state_refs_.sequencer.contentView.kind,
        state_refs_.sequencer.contentView.length,
        state_refs_.sequencer.contentView.revision,
        state_refs_.sequencer.patternQuickControls.previewRevision
    );
}

FLASHMEM bool SequencerView::bindStructureSelectionState() {
    structure_selection_watcher_.bind<
        &SequencerView::requestStructureSelectionRender
    >(*this, 12, "SequencerView.structureSelection");
    return core::ui::watchStructureSelectionInvalidation(
               structure_selection_watcher_,
               state_refs_.trackNavigation.selection
           ) &&
           core::ui::watchStructureSelectionInvalidation(
               structure_selection_watcher_,
               state_refs_.sequencer.structureUi.pageSelection
           );
}

FLASHMEM void SequencerView::bindGridState() {
    grid_watcher_.bind<&SequencerView::requestGridRender>(
        *this, 2, "SequencerView.grid"
    );
    grid_watcher_.watchAll(
        state_refs_.sequencer.pattern.length,
        state_refs_.sequencer.page,
        state_refs_.sequencer.focusedStep,
        state_refs_.sequencer.pattern.enabledMask,
        state_refs_.sequencer.playheadStep,
        state_refs_.sequencer.pattern.stepDataRevision,
        state_refs_.sequencer.probabilityCycleRevision,
        state_refs_.sequencer.variationTelemetryRevision,
        state_refs_.sequencer.pattern.patternVariationRevision,
        state_refs_.sequencer.pattern.patternScaleRevision,
        state_refs_.tracks.projectScaleRevisionSignal(),
        state_refs_.sequencer.activeStepProperty,
        state_refs_.sequencer.stepStatePropertyActive,
        state_refs_.sequencer.stepPropertyInlineSelector.selecting,
        state_refs_.sequencer.stepInlineFeedback.visible,
        state_refs_.sequencer.stepInlineFeedback.touchedMask,
        state_refs_.sequencer.stepInlineFeedback.property,
        state_refs_.sequencer.patternVariationFeedback.visible,
        state_refs_.sequencer.patternVariationFeedback.property,
        state_refs_.structureNavigationFocus,
        state_refs_.projectNavigation.contentRevision,
        state_refs_.sequencer.structureUi.stepSelection.active,
        state_refs_.sequencer.structureUi.stepSelection.placing,
        state_refs_.sequencer.structureUi.stepSelection.cursorStep,
        state_refs_.sequencer.structureUi.stepSelection.selectedMask,
        state_refs_.sequencer.structureUi.stepSelection.pastePreviewActive,
        state_refs_.sequencer.structureUi.stepSelection.pastePreview,
        state_refs_.sequencer.contentView.kind,
        state_refs_.sequencer.contentView.length,
        state_refs_.sequencer.contentView.parentStep,
        state_refs_.sequencer.contentView.ownerNodeId,
        state_refs_.sequencer.contentView.sequenceId,
        state_refs_.sequencer.contentView.cycleSetId,
        state_refs_.sequencer.contentView.depth,
        state_refs_.sequencer.contentView.revision,
        state_refs_.sequencer.ccLaneUi.revision,
        state_refs_.sharedTrackActive,
        state_refs_.sharedTrackEnabledMask,
        state_refs_.trackNavigation.previewAddSlot,
        state_refs_.trackNavigation.previewTrackIndex,
        state_refs_.sequencer.structureUi.previewPageIndex,
        state_refs_.sequencer.patternQuickControls.previewRevision
    );
#if defined(MS_DRUM_TRACK_UX_PROTOTYPE)
    grid_watcher_.watch(
        state_refs_.sequencer.drumTrackUxPrototype.revision
    );
#endif
    grid_tick_watcher_.bind<&SequencerView::requestGridTickRender>(
        *this, 11, "SequencerView.gridTick"
    );
    grid_tick_watcher_.watch(
        state_refs_.sequencer.playheadStepTickOffset
    );
}

FLASHMEM void SequencerView::bindSelectorOverlayState() {
    selector_overlay_watcher_.bind<&SequencerView::requestSelectorOverlayRender>(
        *this, 3, "SequencerView.selectorOverlay"
    );
    selector_overlay_watcher_.watchAll(
        state_refs_.sequencer.contextSelector.revision,
        state_refs_.sequencer.activeStepProperty,
        state_refs_.sequencer.stepStatePropertyActive,
        state_refs_.sequencer.stepPropertyInlineSelector.selecting,
        state_refs_.sequencer.stepPropertyInlineSelector.selectedIndex,
        state_refs_.sequencer.stepPropertyInlineSelector.macroLocalVariationEditActive,
        state_refs_.sequencer.stepContentSelector.selecting,
        state_refs_.sequencer.stepContentSelector.focusedAction,
        state_refs_.sequencer.pattern.enabledMask,
        state_refs_.sequencer.pattern.graphRevision,
        state_refs_.sequencer.patternQuickControls.selecting,
        state_refs_.sequencer.patternQuickControls.focusedItem,
        state_refs_.sequencer.patternQuickControls.offsetSteps,
        state_refs_.sequencer.patternQuickControls.feedbackVisible,
        state_refs_.sequencer.pattern.stepsPerBeat,
        state_refs_.sequencer.pattern.swingOffsetPercent,
        state_refs_.sequencer.pattern.patternNudgePercent,
        state_refs_.sequencer.pattern.patternTimingRevision,
        state_refs_.projectNavigation.contentRevision,
        state_refs_.sequencer.pattern.length,
        state_refs_.sequencer.contentView.kind,
        state_refs_.sequencer.contentView.length,
        state_refs_.sequencer.contentView.revision,
        state_refs_.sequencer.ccLaneUi.revision,
        state_refs_.sequencer.patternQuickControls.previewRevision
    );
#if defined(MS_DRUM_TRACK_UX_PROTOTYPE)
    selector_overlay_watcher_.watch(
        state_refs_.sequencer.drumTrackUxPrototype.revision
    );
#endif
}

FLASHMEM void SequencerView::bindOverlayVisibilityState() {
    overlay_visibility_watcher_.bind<&SequencerView::handleOverlayVisibilityChanged>(
        *this, 4, "SequencerView.overlayVisibility"
    );
    overlay_visibility_watcher_.watchAll(
        state_refs_.viewSelector.visible,
        state_refs_.sequencer.stepEdit.visible,
        state_refs_.sequencer.patternEditor.active,
        state_refs_.deviceSettings.visible,
        state_refs_.deviceSettings.selector.visible,
        state_refs_.sequencerSettings.visible,
        state_refs_.sequencerSettings.selector.visible
    );
}

FLASHMEM void SequencerView::bindLeftActionStripState() {
    left_action_strip_watcher_.bind<&SequencerView::requestLeftActionStripRender>(
        *this, 5, "SequencerView.leftStrip"
    );
    left_action_strip_watcher_.watchAll(
        state_refs_.sequencer.patternQuickControls.selecting,
        state_refs_.sequencer.patternQuickControls.focusedItem,
        state_refs_.sequencer.activeStepProperty,
        state_refs_.sequencer.stepStatePropertyActive,
        state_refs_.sequencer.stepPropertyInlineSelector.selecting,
        state_refs_.sequencer.stepContentSelector.selecting,
        state_refs_.sequencer.stepContentSelector.focusedAction,
        state_refs_.sequencer.structureUi.stepSelection.active,
        state_refs_.sequencer.contentView.kind,
        state_refs_.sequencer.ccLaneUi.revision
    );
#if defined(MS_DRUM_TRACK_UX_PROTOTYPE)
    left_action_strip_watcher_.watch(
        state_refs_.sequencer.drumTrackUxPrototype.revision
    );
#endif
}

FLASHMEM void SequencerView::bindBottomActionStripState() {
    bottom_action_strip_watcher_.bind<&SequencerView::requestBottomActionStripRender>(
        *this, 6, "SequencerView.bottomStrip"
    );
    bottom_action_strip_watcher_.watchAll(
        state_refs_.structureNavigationFocus,
        state_refs_.trackNavigation.previewAddSlot,
        state_refs_.sequencer.structureUi.trackPaste.revision,
        state_refs_.trackNavigation.hold.action,
        state_refs_.trackNavigation.hold.startedAtMs,
        state_refs_.sequencer.structureUi.pageHold.action,
        state_refs_.sequencer.structureUi.pageHold.startedAtMs,
        state_refs_.sequencer.structureUi.stepSelection.active,
        state_refs_.sequencer.structureUi.stepSelection.placing,
        state_refs_.sequencer.structureUi.stepSelection.selectedMask,
        state_refs_.sequencer.structureUi.stepSelection.pastePreviewActive,
        state_refs_.sequencer.structureUi.stepSelection.pastePreview,
        state_refs_.sequencer.patternQuickControls.selecting,
        state_refs_.sequencer.stepPropertyInlineSelector.selecting,
        state_refs_.sequencer.activeStepProperty,
        state_refs_.sequencer.stepStatePropertyActive,
        state_refs_.sequencer.pattern.patternVariationRevision,
        state_refs_.sequencer.contentView.kind,
        state_refs_.sequencer.contentView.revision,
        state_refs_.trackActivations.telemetryRevision(),
        state_refs_.sequencer.ccLaneUi.revision,
        state_refs_.sequencer.ccLaneUi.actionGuard,
        state_refs_.sequencer.ccLaneUi.operationFeedback
    );
#if defined(MS_DRUM_TRACK_UX_PROTOTYPE)
    bottom_action_strip_watcher_.watch(
        state_refs_.sequencer.drumTrackUxPrototype.revision
    );
#endif
}

FLASHMEM void SequencerView::bindHistoryFeedbackState() {
    history_feedback_watcher_.bind<&SequencerView::requestHistoryFeedbackRender>(
        *this, 7, "SequencerView.historyFeedback"
    );
    history_feedback_watcher_.watchAll(
        state_refs_.sequencer.historyFeedback.visible,
        state_refs_.sequencer.historyFeedback.revision
    );
}

FLASHMEM void SequencerView::bindTrackSwitchReadyState() {
    track_switch_ready_watcher_.bind<&SequencerView::resumePendingRender>(
        *this, 8, "SequencerView.trackSwitchReady"
    );
    track_switch_ready_watcher_.watchAll(
        state_refs_.sharedTrackActive
    );
}

FLASHMEM void SequencerView::bindTrackPastePreflightState() {
    track_paste_preflight_watcher_.bind<
        &SequencerView::requestTrackPastePreflightRender>(
        *this,
        9,
        "SequencerView.trackPastePreflight"
    );
    track_paste_preflight_watcher_.watchAll(
        state_refs_.structureNavigationFocus,
        state_refs_.trackNavigation.previewAddSlot,
        state_refs_.trackNavigation.previewTrackIndex,
        state_refs_.sequencer.structureUi.trackPaste.revision,
        state_refs_.sharedTrackActive,
        state_refs_.tracks.activeTrackSignal(),
        state_refs_.tracks.enabledMaskSignal(),
        state_refs_.projectTracks.revision,
        state_refs_.trackActivations.telemetryRevision()
    );
}

FLASHMEM void SequencerView::bindClipboardState() {
    clipboard_watcher_.bind<&SequencerView::requestClipboardDependentRenders>(
        *this,
        10,
        "SequencerView.clipboard"
    );
    clipboard_watcher_.watch(state_refs_.structureClipboard.revision);
}

FLASHMEM void SequencerView::ensureRenderScheduler() {
    if (render_scheduler_) return;
    render_scheduler_ =
        core::app::makeExtmemUnique<core::ui::CoalescedLvglRenderScheduler>(
            core::ui::renderSchedulerDebugLabel("SequencerView"),
            &SequencerView::drainRender,
            this,
            core::ui::CoalescedLvglRenderScheduler::DEFAULT_PERIOD_MS,
            &SequencerView::canDrainRender
        );
}

bool SequencerView::canDrainRender(void* context) {
    const auto* self = static_cast<const SequencerView*>(context);
    return self && RetainedViewRenderPolicy::renderable(
        self->container_, self->hasBlockingOverlay()
    );
}

bool SequencerView::hasBlockingOverlay() const {
    return state_refs_.viewSelector.visible.get() ||
           state_refs_.sequencer.stepEdit.visible.get() ||
           state_refs_.sequencer.patternEditor.active.get() ||
           state_refs_.deviceSettings.visible.get() ||
           state_refs_.deviceSettings.selector.visible.get() ||
           state_refs_.sequencerSettings.visible.get() ||
           state_refs_.sequencerSettings.selector.visible.get();
}

void SequencerView::handleOverlayVisibilityChanged() {
    if (!render_scheduler_) return;
    if (hasBlockingOverlay()) {
        if (track_paste_preflight_card_) {
            track_paste_preflight_card_->render({});
        }
        render_scheduler_->pause();
        return;
    }

    render_scheduler_->resumePending(true);
}

void SequencerView::requestRender(uint32_t flags, bool ready) {
    ensureRenderScheduler();
    if (render_scheduler_) render_scheduler_->request(flags, ready);
}

void SequencerView::resumePendingRender() {
    if (render_scheduler_) render_scheduler_->resumePending(true);
}

void SequencerView::requestHeaderTopRender() {
    requestRender(RENDER_HEADER_TOP);
}

void SequencerView::requestHeaderStripRender() {
    requestRender(RENDER_HEADER_STRIP);
}

void SequencerView::requestHeaderAndLeftRender() {
    requestRender(RENDER_HEADER_TOP | RENDER_LEFT_ACTION_STRIP);
}

void SequencerView::requestHeaderStripAndLeftRender() {
    requestRender(RENDER_HEADER_STRIP | RENDER_LEFT_ACTION_STRIP);
}

void SequencerView::requestStructureSelectionRender() {
    requestRender(
        RENDER_HEADER_TOP |
        RENDER_HEADER_STRIP |
        RENDER_LEFT_ACTION_STRIP |
        RENDER_BOTTOM_ACTION_STRIP
    );
}

void SequencerView::requestSelectorOverlayRender() {
    requestRender(RENDER_SELECTOR_OVERLAY);
}

void SequencerView::requestLeftActionStripRender() {
    requestRender(RENDER_LEFT_ACTION_STRIP);
}

void SequencerView::requestBottomActionStripRender() {
    requestRender(RENDER_BOTTOM_ACTION_STRIP);
}

void SequencerView::requestHistoryFeedbackRender() {
    requestRender(RENDER_HISTORY_FEEDBACK);
}

void SequencerView::requestGridRender() {
    requestRender(RENDER_GRID);
}

void SequencerView::requestGridTickRender() {
    if (state_refs_.sequencer.ccLaneUi.mode !=
        core::state::sequencer::SequencerCcLaneUiMode::LANE_GRID) {
        requestRender(RENDER_GRID);
    }
}

void SequencerView::requestTrackPastePreflightRender() {
    requestRender(RENDER_TRACK_PASTE_PREFLIGHT);
}

void SequencerView::requestClipboardDependentRenders() {
    requestRender(
        RENDER_HEADER_TOP |
        RENDER_HEADER_STRIP |
        RENDER_LEFT_ACTION_STRIP |
        RENDER_BOTTOM_ACTION_STRIP |
        RENDER_GRID |
        RENDER_TRACK_PASTE_PREFLIGHT
    );
}

void SequencerView::markAllDirty() {
    requestRender(RENDER_ALL);
}

void SequencerView::drainRender(void* context, uint32_t flags) {
    auto* self = static_cast<SequencerView*>(context);
    if (self) self->render(flags);
}

void SequencerView::render(uint32_t flags) {
    if (!RetainedViewRenderPolicy::renderable(container_, hasBlockingOverlay())) {
        requestRender(flags);
        return;
    }
    OC_PERF_SCOPE(perfRender, "ui.sequencer.render");

    const bool needsSelectorOverlay = (flags & RENDER_SELECTOR_OVERLAY) != 0;
    const bool needsLeftActionStrip =
        (flags & RENDER_LEFT_ACTION_STRIP) != 0 && left_action_strip_;
    const bool needsBottomActionStrip =
        (flags & RENDER_BOTTOM_ACTION_STRIP) != 0 && bottom_action_strip_;
    const bool needsHistoryToast =
        (flags & RENDER_HISTORY_FEEDBACK) != 0 && history_toast_;
    const bool needsHeaderTop = (flags & RENDER_HEADER_TOP) != 0 && header_bar_;
    const bool needsHeaderStrip = (flags & RENDER_HEADER_STRIP) != 0 && header_bar_;
    const bool needsGrid =
        (flags & RENDER_GRID) != 0 && step_grid_ && cc_lane_grid_;
    const bool needsTrackPastePreflight =
        (flags & RENDER_TRACK_PASTE_PREFLIGHT) != 0 &&
        track_paste_preflight_card_;
    if (!needsSelectorOverlay && !needsLeftActionStrip &&
        !needsBottomActionStrip && !needsHistoryToast && !needsHeaderTop &&
        !needsHeaderStrip && !needsGrid && !needsTrackPastePreflight) {
        return;
    }

    const auto source = modelSource();

    if (needsLeftActionStrip) {
        const auto props = sequencer::buildLeftActionStripProps(source);
        OC_PERF_SCOPE(perfMutation, "ui.sequencer.mutation.left-actions");
        left_action_strip_->render(props);
    }

    if (needsBottomActionStrip) {
        const auto props = sequencer::buildBottomActionStripProps(source);
        OC_PERF_SCOPE(perfMutation, "ui.sequencer.mutation.bottom-actions");
        bottom_action_strip_->render(props);
    }

    if (needsSelectorOverlay) {
        if (property_selection_overlay_) {
            renderSelectorOverlay();
        }
    }

    if (needsHeaderTop || needsHeaderStrip) {
        const auto headerProps = sequencer::buildHeaderBarProps(source);
        OC_PERF_SCOPE(perfMutation, "ui.sequencer.mutation.header");
        if (needsHeaderTop) {
            header_bar_->renderTopRowOnly(headerProps);
        }
        if (needsHeaderStrip) {
            header_bar_->renderStripOnly(headerProps);
        }
    }

    if (needsGrid) {
#if defined(MS_DRUM_TRACK_UX_PROTOTYPE)
        if (state_refs_.sequencer.drumTrackUxPrototype.gridVisible()) {
            OC_PERF_SCOPE(perfMutation, "ui.sequencer.mutation.drum-prototype");
            lv_obj_add_flag(step_grid_->getElement(), LV_OBJ_FLAG_HIDDEN);
            cc_lane_grid_->render({.visible = false});
            lv_obj_clear_flag(
                drum_track_ux_prototype_grid_,
                LV_OBJ_FLAG_HIDDEN
            );
            lv_obj_invalidate(drum_track_ux_prototype_grid_);
        } else {
            lv_obj_add_flag(
                drum_track_ux_prototype_grid_,
                LV_OBJ_FLAG_HIDDEN
            );
#endif
        const auto ccLaneProps = sequencer::buildSequencerCcLaneGridProps(source);
        if (ccLaneProps.visible) {
            OC_PERF_SCOPE(perfMutation, "ui.sequencer.mutation.cc-lane");
            lv_obj_add_flag(step_grid_->getElement(), LV_OBJ_FLAG_HIDDEN);
            cc_lane_grid_->render(ccLaneProps);
        } else {
            const auto stepGridProps = sequencer::buildStepGridProps(source);
            OC_PERF_SCOPE(perfMutation, "ui.sequencer.mutation.step-grid");
            cc_lane_grid_->render({.visible = false});
            lv_obj_clear_flag(step_grid_->getElement(), LV_OBJ_FLAG_HIDDEN);
            step_grid_->render(stepGridProps);
        }
#if defined(MS_DRUM_TRACK_UX_PROTOTYPE)
        }
#endif
    }

    if (needsHistoryToast) {
        OC_PERF_SCOPE(perfMutation, "ui.sequencer.mutation.history-toast");
        renderHistoryToast();
    }
    if (needsTrackPastePreflight) {
        const auto props = [&]() {
            OC_PERF_SCOPE(
                perfProjection,
                "ui.sequencer.projection.track-paste"
            );
            return sequencer::projectSequencerTrackPastePreflight(source);
        }();
        OC_PERF_SCOPE(perfMutation, "ui.sequencer.mutation.track-paste");
        track_paste_preflight_card_->render(props);
    }
}

void SequencerView::renderSelectorOverlay() {
    if (!property_selection_overlay_) return;

    const auto props =
        sequencer::buildPropertySelectionOverlayProps(modelSource());
    OC_PERF_SCOPE(perfMutation, "ui.sequencer.mutation.selector");
    property_selection_overlay_->render(props);
}

void SequencerView::renderHistoryToast() {
    if (!history_toast_) return;

    const auto& feedback = state_refs_.sequencer.historyFeedback;
    if (!feedback.visible.get()) {
        lv_obj_add_flag(history_toast_, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    lv_label_set_text(history_toast_line1_, feedback.line1.data());
    lv_label_set_text(history_toast_line2_, feedback.line2.data());
    lv_label_set_text(history_toast_line3_, feedback.line3.data());
    lv_obj_align(history_toast_, LV_ALIGN_TOP_MID, 0, 34);
    // Outcome feedback has priority over any contextual paste card occupying
    // the same temporary-information zone.
    lv_obj_move_foreground(history_toast_);
    lv_obj_clear_flag(history_toast_, LV_OBJ_FLAG_HIDDEN);
}

sequencer::SequencerViewModelSource SequencerView::modelSource() const {
    return {
          .sequencer = state_refs_.sequencer,
          .tracks = state_refs_.tracks,
          .projectTracks = state_refs_.projectTracks,
        .trackNavigation = state_refs_.trackNavigation,
        .navigationFocus = state_refs_.structureNavigationFocus,
        .sharedTrackActive = state_refs_.sharedTrackActive,
        .sharedTrackEnabledMask = state_refs_.sharedTrackEnabledMask,
        .structureClipboard = state_refs_.structureClipboard,
        .statusBar = state_refs_.statusBar,
        .projectNavigation = state_refs_.projectNavigation,
        .trackActivations = state_refs_.trackActivations,
    };
}

}  // namespace core::ui
