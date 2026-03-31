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
    scheduleRender();
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
    lv_obj_add_flag(container_, LV_OBJ_FLAG_HIDDEN);

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
    bindHeaderActivityState();
    bindHeaderStripState();
    bindGridState();
    bindPropertyStripState();
    bindOverlayVisibilityState();
    bindLeftActionStripState();
    bindBottomActionStripState();
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
            requestHeaderTopRender();
            track_tint_dirty_ = true;
        },
        state_refs_.tracks.activeTrack,
        state_refs_.tracks.enabledMask,
        state_refs_.tracks.selector.selecting,
        state_refs_.tracks.selector.selectedTrack
    );
}

FLASHMEM void SequencerView::bindHeaderActivityState() {
    watcher_.watchAll(
        [this]() {
            requestHeaderTopRender();
        },
        state_refs_.statusBar.trackNoteActivity[0],
        state_refs_.statusBar.trackNoteActivity[1],
        state_refs_.statusBar.trackNoteActivity[2],
        state_refs_.statusBar.trackNoteActivity[3],
        state_refs_.statusBar.trackNoteActivity[4],
        state_refs_.statusBar.trackNoteActivity[5],
        state_refs_.statusBar.trackNoteActivity[6],
        state_refs_.statusBar.trackNoteActivity[7]
    );
}

FLASHMEM void SequencerView::bindHeaderStripState() {
    watcher_.watchAll(
        [this]() {
            requestHeaderStripRender();
        },
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
        },
        state_refs_.sequencer.activeStepProperty,
        state_refs_.sequencer.stepPropertyInlineSelector.selecting,
        state_refs_.sequencer.stepPropertyInlineSelector.selectedIndex
    );
}

FLASHMEM void SequencerView::bindOverlayVisibilityState() {
    watcher_.watchAll(
        [this]() {
            handleOverlayVisibilityChanged();
        },
        state_refs_.viewSelector.visible,
        state_refs_.sequencer.stepEdit.visible,
        state_refs_.globalSettings.visible,
        state_refs_.globalSettings.selector.visible,
        state_refs_.dataManager.visible,
        state_refs_.dataManager.dialog.visible
    );
}

FLASHMEM void SequencerView::bindLeftActionStripState() {
    watcher_.watchAll(
        [this]() {
            requestLeftActionStripRender();
        },
        state_refs_.tracks.selector.selecting,
        state_refs_.sequencer.patternQuickControls.selecting,
        state_refs_.sequencer.activeStepProperty,
        state_refs_.sequencer.stepPropertyInlineSelector.selecting,
        state_refs_.sequencer.rangeSelection.kind
    );
}

FLASHMEM void SequencerView::bindBottomActionStripState() {
    watcher_.watchAll(
        [this]() {
            requestBottomActionStripRender();
        },
        state_refs_.sequencer.rangeSelection.kind,
        state_refs_.sequencer.rangeSelection.phase
    );
}

FLASHMEM void SequencerView::bindQuickControlsState() {
    watcher_.watchAll(
        [this]() {
            requestBottomControlsRender();
        },
        state_refs_.sequencer.patternQuickControls.selecting,
        state_refs_.sequencer.patternQuickControls.focusedItem
    );
}

void SequencerView::ensureRenderTimer() {
    if (render_timer_) return;
    render_timer_ = std::make_unique<PausableLvglTimer>(16, onRenderTimer, this);
}

bool SequencerView::hasBlockingOverlay() const {
    return state_refs_.viewSelector.visible.get() ||
           state_refs_.sequencer.stepEdit.visible.get() ||
           state_refs_.globalSettings.visible.get() ||
           state_refs_.globalSettings.selector.visible.get() ||
           state_refs_.dataManager.visible.get() ||
           state_refs_.dataManager.dialog.visible.get();
}

void SequencerView::handleOverlayVisibilityChanged() {
    if (hasBlockingOverlay()) {
        pauseRenderTimerIfIdle();
        if (render_timer_) {
            render_timer_->pause();
        }
        return;
    }

    if (dirty_) {
        scheduleRender(true);
    }
}

void SequencerView::scheduleRender(bool ready) {
    ensureRenderTimer();
    const bool wasDirty = dirty_;
    dirty_ = true;
    if (render_timer_) {
        if ((container_ && lv_obj_has_flag(container_, LV_OBJ_FLAG_HIDDEN)) || hasBlockingOverlay()) {
            return;
        }
        render_timer_->resume(!wasDirty && ready);
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

void SequencerView::requestHeaderTopRender() {
    requestRender(header_top_dirty_);
}

void SequencerView::requestHeaderStripRender() {
    requestRender(header_strip_dirty_);
}

void SequencerView::requestBottomControlsRender() {
    requestRender(bottom_controls_dirty_);
}

void SequencerView::requestPropertyStripRender() {
    requestRender(property_strip_dirty_);
}

void SequencerView::requestLeftActionStripRender() {
    requestRender(left_action_strip_dirty_);
}

void SequencerView::requestBottomActionStripRender() {
    requestRender(bottom_action_strip_dirty_);
}

void SequencerView::requestGridRender() {
    requestRender(grid_dirty_);
}

void SequencerView::markAllDirty() {
    header_top_dirty_ = true;
    header_strip_dirty_ = true;
    bottom_controls_dirty_ = true;
    property_strip_dirty_ = true;
    left_action_strip_dirty_ = true;
    bottom_action_strip_dirty_ = true;
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

    track_tint_cache_track_ = previewTrack;
    track_tint_cache_enabled_mask_ = enabledMask;
    track_tint_cache_selecting_ = selecting;
    track_tint_dirty_ = false;
}

void SequencerView::onRenderTimer(lv_timer_t* timer) {
    auto* self = static_cast<SequencerView*>(lv_timer_get_user_data(timer));
    if (!self) return;

    if (!self->container_ || lv_obj_has_flag(self->container_, LV_OBJ_FLAG_HIDDEN) ||
        self->hasBlockingOverlay()) {
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
    if (!container_ || lv_obj_has_flag(container_, LV_OBJ_FLAG_HIDDEN) || hasBlockingOverlay()) return;

    const bool needsBottomControls = bottom_controls_dirty_ && bottom_controls_;
    const bool needsPropertyStrip = property_strip_dirty_ && property_strip_;
    const bool needsLeftActionStrip = left_action_strip_dirty_ && left_action_strip_;
    const bool needsBottomActionStrip = bottom_action_strip_dirty_ && bottom_action_strip_;
    const bool needsHeaderTop = header_top_dirty_ && header_bar_;
    const bool needsHeaderStrip = header_strip_dirty_ && header_bar_;
    const bool needsGrid = grid_dirty_ && step_grid_;
    const bool needsTint = track_tint_dirty_;
    if (!needsBottomControls && !needsPropertyStrip && !needsLeftActionStrip &&
        !needsBottomActionStrip && !needsHeaderTop &&
        !needsHeaderStrip && !needsGrid && !needsTint) {
        return;
    }

    const auto source = modelSource();
    renderTrackTint();

    if (needsLeftActionStrip) {
        left_action_strip_->render(sequencer::buildLeftActionStripProps(source));
        left_action_strip_dirty_ = false;
    }

    if (needsBottomActionStrip) {
        bottom_action_strip_->render(sequencer::buildBottomActionStripProps(source));
        bottom_action_strip_dirty_ = false;
    }

    if (needsBottomControls) {
        bottom_controls_->render(sequencer::buildBottomControlsProps(source));
        bottom_controls_dirty_ = false;
    }

    if (needsPropertyStrip) {
        property_strip_->render(sequencer::buildStepPropertyStripProps(source));
        property_strip_dirty_ = false;
    }

    if (needsHeaderTop || needsHeaderStrip) {
        const auto headerProps = sequencer::buildHeaderBarProps(source);
        if (needsHeaderTop) {
            header_bar_->renderTopRowOnly(headerProps);
            header_top_dirty_ = false;
        }
        if (needsHeaderStrip) {
            header_bar_->renderStripOnly(headerProps);
            header_strip_dirty_ = false;
        }
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
