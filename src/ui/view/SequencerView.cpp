#include "SequencerView.hpp"

#include <oc/ui/lvgl/style/StyleBuilder.hpp>

#include <config/PlatformCompat.hpp>
#include "ui/sequencer/SequencerViewModelBuilder.hpp"
#include "ui/theme/StandaloneTheme.hpp"

namespace style = oc::ui::lvgl::style;
namespace theme = standalone::theme;

namespace core::ui {

SequencerView::SequencerView(lv_obj_t* parent, StateRefs stateRefs)
    : state_refs_(stateRefs) {
    createLayout(parent);
    createHeaderBar();
    createBottomControls();
    createActionStrips();
    createPropertyStrip();
    createGrid();
    ensureRenderTimer();
    bindToState();
}

SequencerView::~SequencerView() {
    render_timer_.reset();

    step_grid_.reset();
    bottom_action_strip_.reset();
    property_strip_.reset();
    bottom_controls_.reset();
    left_action_strip_.reset();
    header_bar_.reset();
    layout_.reset();
    container_ = nullptr;
    body_container_ = nullptr;
    interaction_container_ = nullptr;
    center_column_ = nullptr;
}

void SequencerView::onActivate() {
    if (!container_) return;

    lv_obj_clear_flag(container_, LV_OBJ_FLAG_HIDDEN);

    if (step_grid_) {
        step_grid_->forceRefresh();
    }
    markAllDirty();
    render();

    if (body_container_) {
        lv_obj_update_layout(body_container_);
    }

    dirty_ = false;
    pauseRenderTimerIfIdle();
}

void SequencerView::onDeactivate() {
    if (container_) {
        lv_obj_add_flag(container_, LV_OBJ_FLAG_HIDDEN);
    }

    if (render_timer_) {
        render_timer_->pause();
    }
}

FLASHMEM void SequencerView::createLayout(lv_obj_t* parent) {
    layout_ = std::make_unique<ms::ui::LayoutView>(parent);
    container_ = layout_->getElement();
    body_container_ = layout_->content();
    lv_obj_t* header = layout_->header();

    style::apply(header).transparent().pad(0);
    lv_obj_set_layout(header, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(header, 0, 0);

    style::apply(body_container_).transparent().pad(0);
    lv_obj_set_layout(body_container_, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(body_container_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(body_container_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(body_container_, 0, 0);
}

FLASHMEM void SequencerView::createHeaderBar() {
    if (!layout_) return;
    header_bar_ = std::make_unique<SequencerHeaderBar>(layout_->header());
}

FLASHMEM void SequencerView::createGrid() {
    if (!center_column_) return;
    step_grid_ = std::make_unique<StepGrid>(center_column_);
}

FLASHMEM void SequencerView::createPropertyStrip() {
    if (!interaction_container_) return;
    property_strip_ = std::make_unique<StepPropertyStrip>(interaction_container_);
}

FLASHMEM void SequencerView::createBottomControls() {
    if (!body_container_) return;
    bottom_controls_ = std::make_unique<SequencerBottomControls>(body_container_);
}

FLASHMEM void SequencerView::createActionStrips() {
    if (!body_container_) return;
    interaction_container_ = lv_obj_create(body_container_);
    style::apply(interaction_container_).size(LV_PCT(100), LV_PCT(100)).transparent().noBorder().pad(0).noScroll();
    lv_obj_set_flex_grow(interaction_container_, 1);
    lv_obj_set_layout(interaction_container_, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(interaction_container_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(
        interaction_container_,
        LV_FLEX_ALIGN_START,
        LV_FLEX_ALIGN_START,
        LV_FLEX_ALIGN_START
    );
    lv_obj_set_style_pad_column(interaction_container_, 0, 0);

    left_action_strip_ = std::make_unique<ContextActionStrip>(
        interaction_container_,
        ContextActionStripOrientation::VERTICAL,
        ContextActionStripVerticalLayout::SPREAD
    );

    center_column_ = lv_obj_create(interaction_container_);
    style::apply(center_column_).transparent().noBorder().pad(0).noScroll();
    lv_obj_set_width(center_column_, 0);
    lv_obj_set_height(center_column_, LV_PCT(100));
    lv_obj_set_flex_grow(center_column_, 1);
    lv_obj_set_layout(center_column_, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(center_column_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(center_column_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(center_column_, 0, 0);

    bottom_action_strip_ = std::make_unique<ContextActionStrip>(
        body_container_,
        ContextActionStripOrientation::HORIZONTAL
    );
}

FLASHMEM void SequencerView::bindToState() {
    bindBottomControlsState();
    bindHeaderState();
    bindGridState();
    bindPropertyStripState();
    bindQuickControlsState();

    markAllDirty();
    render();
    dirty_ = false;
    pauseRenderTimerIfIdle();
}

FLASHMEM void SequencerView::bindBottomControlsState() {
    watcher_.watchAll(
        [this]() {
            requestBottomControlsRender();
        },
        state_refs_.sequencer.stepsPerBeat,
        state_refs_.sequencer.patternQuickControls.offsetSteps,
        state_refs_.sequencer.length
    );
}

FLASHMEM void SequencerView::bindHeaderState() {
    watcher_.watchAll(
        [this]() {
            requestHeaderRender();
            track_tint_dirty_ = true;
        },
        state_refs_.tracks.activeTrack,
        state_refs_.tracks.enabledMask,
        state_refs_.tracks.selector.selecting,
        state_refs_.tracks.selector.selectedTrack,
        state_refs_.statusBar.trackNoteActivity[0],
        state_refs_.statusBar.trackNoteActivity[1],
        state_refs_.statusBar.trackNoteActivity[2],
        state_refs_.statusBar.trackNoteActivity[3],
        state_refs_.statusBar.trackNoteActivity[4],
        state_refs_.statusBar.trackNoteActivity[5],
        state_refs_.statusBar.trackNoteActivity[6],
        state_refs_.statusBar.trackNoteActivity[7],
        state_refs_.sequencer.length,
        state_refs_.sequencer.page,
        state_refs_.sequencer.playheadStep
    );
}

FLASHMEM void SequencerView::bindGridState() {
    watcher_.watchAll(
        [this]() {
            requestGridRender();
        },
        state_refs_.sequencer.length,
        state_refs_.sequencer.page,
        state_refs_.sequencer.enabledMask,
        state_refs_.sequencer.playheadStep,
        state_refs_.sequencer.stepDataRevision,
        state_refs_.sequencer.probabilityCycleRevision,
        state_refs_.sequencer.activeStepProperty,
        state_refs_.sequencer.stepInlineFeedback.visible,
        state_refs_.sequencer.stepInlineFeedback.touchedMask,
        state_refs_.sequencer.stepInlineFeedback.property,
        state_refs_.sequencer.rangeSelection.kind,
        state_refs_.sequencer.rangeSelection.phase,
        state_refs_.sequencer.rangeSelection.cursorStep,
        state_refs_.sequencer.rangeSelection.anchorStep,
        state_refs_.sequencer.rangeSelection.rangeStart,
        state_refs_.sequencer.rangeSelection.rangeEnd,
        state_refs_.sequencer.rangeSelection.rangeValid
    );
}

FLASHMEM void SequencerView::bindPropertyStripState() {
    watcher_.watchAll(
        [this]() {
            requestPropertyStripRender();
            requestActionStripsRender();
        },
        state_refs_.tracks.selector.selecting,
        state_refs_.sequencer.activeStepProperty,
        state_refs_.sequencer.stepPropertyInlineSelector.selecting,
        state_refs_.sequencer.stepPropertyInlineSelector.selectedIndex,
        state_refs_.sequencer.rangeSelection.kind,
        state_refs_.sequencer.rangeSelection.phase
    );
}

FLASHMEM void SequencerView::bindQuickControlsState() {
    watcher_.watchAll(
        [this]() {
            requestBottomControlsRender();
            requestActionStripsRender();
        },
        state_refs_.sequencer.patternQuickControls.selecting,
        state_refs_.sequencer.patternQuickControls.focusedItem
    );
}

void SequencerView::ensureRenderTimer() {
    if (render_timer_) return;
    render_timer_ = std::make_unique<PausableLvglTimer>(16, onRenderTimer, this);
}

void SequencerView::scheduleRender() {
    dirty_ = true;
    ensureRenderTimer();
    if (render_timer_) {
        render_timer_->resume(true);
    }
}

void SequencerView::pauseRenderTimerIfIdle() {
    if (!render_timer_) return;
    if (dirty_) return;
    render_timer_->pause();
}

void SequencerView::requestRender(bool& dirtyFlag) {
    dirtyFlag = true;
    scheduleRender();
}

void SequencerView::requestHeaderRender() {
    requestRender(header_dirty_);
}

void SequencerView::requestBottomControlsRender() {
    requestRender(bottom_controls_dirty_);
}

void SequencerView::requestPropertyStripRender() {
    requestRender(property_strip_dirty_);
}

void SequencerView::requestGridRender() {
    requestRender(grid_dirty_);
}

void SequencerView::requestActionStripsRender() {
    requestRender(action_strips_dirty_);
}

void SequencerView::markAllDirty() {
    header_dirty_ = true;
    bottom_controls_dirty_ = true;
    property_strip_dirty_ = true;
    action_strips_dirty_ = true;
    grid_dirty_ = true;
    track_tint_dirty_ = true;
}

void SequencerView::renderTrackTint() {
    if (!container_) return;

    const uint8_t previewTrack = state_refs_.tracks.selector.selecting.get()
        ? state_refs_.tracks.selector.selectedTrack.get()
        : state_refs_.tracks.activeTrack.get();
    const uint8_t enabledMask = state_refs_.tracks.enabledMask.get();
    const bool selecting = state_refs_.tracks.selector.selecting.get();

    if (!track_tint_dirty_ &&
        track_tint_cache_track_ == previewTrack &&
        track_tint_cache_enabled_mask_ == enabledMask &&
        track_tint_cache_selecting_ == selecting) {
        return;
    }

    const bool enabled = (enabledMask & static_cast<uint8_t>(1U << previewTrack)) != 0;
    const uint32_t bgColor = enabled ? theme::color::trackColor(previewTrack) : theme::color::INACTIVE;
    const lv_opa_t bgOpa = selecting ? static_cast<lv_opa_t>(26) : static_cast<lv_opa_t>(16);

    // Apply the track tint once at the view root only. Reapplying the same
    // translucent color on nested containers darkens some sequencer zones
    // (grid, quick controls, action strips) and breaks color consistency.
    lv_obj_set_style_bg_color(container_, lv_color_hex(bgColor), 0);
    lv_obj_set_style_bg_opa(container_, bgOpa, 0);

    if (body_container_) {
        lv_obj_set_style_bg_opa(body_container_, LV_OPA_TRANSP, 0);
    }
    if (interaction_container_) {
        lv_obj_set_style_bg_opa(interaction_container_, LV_OPA_TRANSP, 0);
    }
    if (center_column_) {
        lv_obj_set_style_bg_opa(center_column_, LV_OPA_TRANSP, 0);
    }

    track_tint_cache_track_ = previewTrack;
    track_tint_cache_enabled_mask_ = enabledMask;
    track_tint_cache_selecting_ = selecting;
    track_tint_dirty_ = false;
}

void SequencerView::onRenderTimer(lv_timer_t* timer) {
    auto* self = static_cast<SequencerView*>(lv_timer_get_user_data(timer));
    if (!self) return;

    if (!self->container_ || lv_obj_has_flag(self->container_, LV_OBJ_FLAG_HIDDEN)) {
        self->pauseRenderTimerIfIdle();
        return;
    }

    if (!self->dirty_) {
        self->pauseRenderTimerIfIdle();
        return;
    }

    self->render();
    self->dirty_ = false;
    self->pauseRenderTimerIfIdle();
}

void SequencerView::render() {
    if (!container_ || lv_obj_has_flag(container_, LV_OBJ_FLAG_HIDDEN)) return;

    const bool needsBottomControls = bottom_controls_dirty_ && bottom_controls_;
    const bool needsPropertyStrip = property_strip_dirty_ && property_strip_;
    const bool needsActionStrips = action_strips_dirty_ && left_action_strip_ && bottom_action_strip_;
    const bool needsHeader = header_dirty_ && header_bar_;
    const bool needsGrid = grid_dirty_ && step_grid_;
    if (!needsBottomControls && !needsPropertyStrip && !needsActionStrips && !needsHeader &&
        !needsGrid && !track_tint_dirty_) {
        return;
    }

    const auto source = modelSource();
    renderTrackTint();

    if (needsActionStrips) {
        left_action_strip_->render(sequencer::buildLeftActionStripProps(source));
        bottom_action_strip_->render(sequencer::buildBottomActionStripProps(source));
        action_strips_dirty_ = false;
    }

    if (needsBottomControls) {
        bottom_controls_->render(sequencer::buildBottomControlsProps(source));
        bottom_controls_dirty_ = false;
    }

    if (needsPropertyStrip) {
        property_strip_->render(sequencer::buildStepPropertyStripProps(source));
        property_strip_dirty_ = false;
    }

    if (needsHeader) {
        header_bar_->render(sequencer::buildHeaderBarProps(source));
        header_dirty_ = false;
    }

    if (needsGrid) {
        step_grid_->render(sequencer::buildStepGridProps(source));
        grid_dirty_ = false;
    }
}

sequencer::SequencerViewModelSource SequencerView::modelSource() const {
    return {
        .sequencer = state_refs_.sequencer,
        .tracks = state_refs_.tracks,
        .statusBar = state_refs_.statusBar,
    };
}

}  // namespace core::ui
