#include "SequencerView.hpp"

#include <oc/ui/lvgl/style/StyleBuilder.hpp>

#include <config/PlatformCompat.hpp>
#include "ui/sequencer/SequencerViewModelBuilder.hpp"

namespace style = oc::ui::lvgl::style;

namespace core::ui {

SequencerView::SequencerView(lv_obj_t* parent, core::state::CoreState& coreState)
    : core_state_(coreState) {
    createLayout(parent);
    createQuickControls();
    createHeaderBar();
    createActionStrips();
    createGrid();
    ensureRenderTimer();
    bindToState();
}

SequencerView::~SequencerView() {
    if (render_timer_) {
        lv_timer_delete(render_timer_);
        render_timer_ = nullptr;
    }

    step_grid_.reset();
    bottom_action_strip_.reset();
    property_strip_.reset();
    left_action_strip_.reset();
    pattern_quick_controls_.reset();
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
        lv_timer_pause(render_timer_);
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
}

FLASHMEM void SequencerView::createHeaderBar() {
    if (!layout_) return;
    header_bar_ = std::make_unique<SequencerHeaderBar>(layout_->header());
}

FLASHMEM void SequencerView::createGrid() {
    if (!center_column_) return;

    property_strip_ = std::make_unique<StepPropertyStrip>(center_column_);
    step_grid_ = std::make_unique<StepGrid>(center_column_);
}

FLASHMEM void SequencerView::createQuickControls() {
    if (!layout_) return;
    pattern_quick_controls_ = std::make_unique<PatternQuickControls>(layout_->header());
}

FLASHMEM void SequencerView::createActionStrips() {
    if (!interaction_container_ || !body_container_) return;
    left_action_strip_ = std::make_unique<ContextActionStrip>(
        interaction_container_,
        ContextActionStripOrientation::VERTICAL
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
    watcher_.watchAll(
        [this]() {
            requestQuickControlsRender();
        },
        core_state_.sequencer.stepsPerBeat,
        core_state_.sequencer.midiChannel,
        core_state_.sequencer.length
    );

    watcher_.watchAll(
        [this]() {
            requestHeaderRender();
        },
        core_state_.sequencer.length,
        core_state_.sequencer.page,
        core_state_.sequencer.playheadStep
    );

    watcher_.watchAll(
        [this]() {
            requestGridRender();
        },
        core_state_.sequencer.length,
        core_state_.sequencer.page,
        core_state_.sequencer.enabledMask,
        core_state_.sequencer.playheadStep,
        core_state_.sequencer.stepDataRevision,
        core_state_.sequencer.probabilityCycleRevision,
        core_state_.sequencer.activeStepProperty,
        core_state_.sequencer.stepInlineFeedback.visible,
        core_state_.sequencer.stepInlineFeedback.touchedMask,
        core_state_.sequencer.stepInlineFeedback.property,
        core_state_.sequencer.rangeSelection.kind,
        core_state_.sequencer.rangeSelection.phase,
        core_state_.sequencer.rangeSelection.cursorStep,
        core_state_.sequencer.rangeSelection.anchorStep,
        core_state_.sequencer.rangeSelection.rangeStart,
        core_state_.sequencer.rangeSelection.rangeEnd,
        core_state_.sequencer.rangeSelection.rangeValid
    );

    watcher_.watchAll(
        [this]() {
            requestStripRender();
            requestActionStripsRender();
        },
        core_state_.sequencer.activeStepProperty,
        core_state_.sequencer.stepPropertyInlineSelector.selecting,
        core_state_.sequencer.stepPropertyInlineSelector.selectedIndex,
        core_state_.sequencer.rangeSelection.kind,
        core_state_.sequencer.rangeSelection.phase
    );

    watcher_.watchAll(
        [this]() {
            requestQuickControlsRender();
            requestActionStripsRender();
        },
        core_state_.sequencer.patternQuickControls.selecting,
        core_state_.sequencer.patternQuickControls.focusedItem
    );

    markAllDirty();
    render();
    dirty_ = false;
    pauseRenderTimerIfIdle();
}

void SequencerView::ensureRenderTimer() {
    if (render_timer_) return;
    render_timer_ = lv_timer_create(onRenderTimer, 16, this);
    if (render_timer_) {
        lv_timer_pause(render_timer_);
    }
}

void SequencerView::scheduleRender() {
    dirty_ = true;
    ensureRenderTimer();
    if (render_timer_) {
        lv_timer_resume(render_timer_);
        lv_timer_ready(render_timer_);
    }
}

void SequencerView::pauseRenderTimerIfIdle() {
    if (!render_timer_) return;
    if (dirty_) return;
    lv_timer_pause(render_timer_);
}

void SequencerView::requestHeaderRender() {
    header_dirty_ = true;
    scheduleRender();
}

void SequencerView::requestStripRender() {
    strip_dirty_ = true;
    scheduleRender();
}

void SequencerView::requestQuickControlsRender() {
    quick_controls_dirty_ = true;
    scheduleRender();
}

void SequencerView::requestGridRender() {
    grid_dirty_ = true;
    scheduleRender();
}

void SequencerView::requestActionStripsRender() {
    action_strips_dirty_ = true;
    scheduleRender();
}

void SequencerView::markAllDirty() {
    header_dirty_ = true;
    quick_controls_dirty_ = true;
    strip_dirty_ = true;
    action_strips_dirty_ = true;
    grid_dirty_ = true;
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

    const bool needsQuickControls = quick_controls_dirty_ && pattern_quick_controls_;
    const bool needsStrip = strip_dirty_ && property_strip_;
    const bool needsActionStrips = action_strips_dirty_ && left_action_strip_ && bottom_action_strip_;
    const bool needsHeader = header_dirty_ && header_bar_;
    const bool needsGrid = grid_dirty_ && step_grid_;
    if (!needsQuickControls && !needsStrip && !needsActionStrips && !needsHeader && !needsGrid) {
        return;
    }

    if (needsStrip) {
        property_strip_->render(sequencer::buildStepPropertyStripProps(core_state_));
        strip_dirty_ = false;
    }

    if (needsActionStrips) {
        left_action_strip_->render(sequencer::buildLeftActionStripProps(core_state_));
        bottom_action_strip_->render(sequencer::buildBottomActionStripProps(core_state_));
        action_strips_dirty_ = false;
    }

    if (needsQuickControls) {
        pattern_quick_controls_->render(sequencer::buildPatternQuickControlsProps(core_state_));
        quick_controls_dirty_ = false;
    }

    if (needsHeader) {
        header_bar_->render(sequencer::buildHeaderBarProps(core_state_));
        header_dirty_ = false;
    }

    if (needsGrid) {
        step_grid_->render(sequencer::buildStepGridProps(core_state_));
        grid_dirty_ = false;
    }
}

}  // namespace core::ui
