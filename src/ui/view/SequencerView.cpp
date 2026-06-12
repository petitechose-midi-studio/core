#include "SequencerView.hpp"

#include <config/PlatformCompat.hpp>
#include <ms/ui/font/CoreFonts.hpp>
#include "ui/sequencer/SequencerViewModelBuilder.hpp"
#include "ui/theme/StandaloneTheme.hpp"

namespace core::ui {

namespace theme = standalone::theme;

FLASHMEM SequencerView::SequencerView(lv_obj_t* parent, StateRefs stateRefs)
    : state_refs_(stateRefs) {
    createLayout(parent);
    createHistoryToast();
    createHeaderBar();
    createBottomControls();
    createActionStrips();
    createPropertyStrip();
    createGrid();
    ensureRenderTimer();
    bindToState();
}

FLASHMEM SequencerView::~SequencerView() {
    render_timer_.reset();

    step_grid_.reset();
    bottom_action_strip_.reset();
    property_strip_.reset();
    bottom_controls_.reset();
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

    lv_obj_clear_flag(container_, LV_OBJ_FLAG_HIDDEN);

    if (step_grid_) {
        step_grid_->prepareForActivationLayoutRefresh();
        grid_dirty_ = true;
    }
    scheduleRender(true);
}

FLASHMEM void SequencerView::onDeactivate() {
    if (container_) {
        lv_obj_add_flag(container_, LV_OBJ_FLAG_HIDDEN);
    }

    if (render_timer_) {
        render_timer_->pause();
    }
}

FLASHMEM void SequencerView::createLayout(lv_obj_t* parent) {
    frame_ = core::app::makeExtmemUnique<MainViewFrame>(parent);
    container_ = frame_->container();
    body_container_ = frame_->body();
    lv_obj_set_style_bg_opa(container_, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(container_, LV_OBJ_FLAG_HIDDEN);
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
}

FLASHMEM void SequencerView::createPropertyStrip() {
    if (!interaction_container_) return;
    property_strip_ = core::app::makeExtmemUnique<StepPropertyStrip>(interaction_container_);
}

FLASHMEM void SequencerView::createBottomControls() {
    if (!body_container_) return;
    bottom_controls_ = core::app::makeExtmemUnique<SequencerBottomControls>(body_container_);
}

FLASHMEM void SequencerView::createHistoryToast() {
    if (!container_) return;

    history_toast_ = lv_obj_create(container_);
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

FLASHMEM void SequencerView::createActionStrips() {
    if (!frame_ || !body_container_) return;
    frame_->createInteractionRow();
    interaction_container_ = frame_->interactionRow();

    left_action_strip_ = core::app::makeExtmemUnique<ContextActionStrip>(
        interaction_container_,
        ContextActionStripOrientation::VERTICAL,
        ContextActionStripVerticalLayout::SPREAD
    );

    frame_->createCenterColumn();
    center_column_ = frame_->centerColumn();

    bottom_action_strip_ = core::app::makeExtmemUnique<ContextActionStrip>(
        body_container_,
        ContextActionStripOrientation::HORIZONTAL
    );
}

FLASHMEM void SequencerView::onStepGridGeometryInvalidated(void* userData) {
    auto* self = static_cast<SequencerView*>(userData);
    if (!self) return;
    self->requestGridRender();
}

FLASHMEM void SequencerView::bindToState() {
    bindBottomControlsState();
    bindHeaderState();
    bindHeaderStripState();
    bindGridState();
    bindPropertyStripState();
    bindOverlayVisibilityState();
    bindLeftActionStripState();
    bindBottomActionStripState();
    bindHistoryFeedbackState();

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
        state_refs_.sequencer.pattern.stepsPerBeat,
        state_refs_.sequencer.patternQuickControls.selecting,
        state_refs_.sequencer.patternQuickControls.focusedItem,
        state_refs_.sequencer.patternQuickControls.offsetSteps,
        state_refs_.sequencer.pattern.length,
        state_refs_.sequencer.contentView.kind,
        state_refs_.sequencer.contentView.length,
        state_refs_.sequencer.contentView.revision
    );
}

FLASHMEM void SequencerView::bindHeaderState() {
    watcher_.watchAll(
        [this]() {
            requestHeaderTopRender();
        },
        state_refs_.sharedTrackActive,
        state_refs_.sharedTrackEnabledMask,
        state_refs_.structureNavigationFocus,
        state_refs_.structureClipboard.revision,
        state_refs_.trackNavigation.previewAddSlot,
        state_refs_.trackNavigation.previewTrackIndex,
        state_refs_.sequencer.structureUi.previewPageIndex,
        state_refs_.sequencer.structureUi.previewAddPageSlot,
        state_refs_.trackNavigation.selection.active,
        state_refs_.trackNavigation.selection.scope,
        state_refs_.trackNavigation.selection.cursorIndex,
        state_refs_.trackNavigation.selection.selectedMask,
        state_refs_.sequencer.structureUi.pageSelection.active,
        state_refs_.sequencer.structureUi.pageSelection.scope,
        state_refs_.sequencer.structureUi.pageSelection.cursorIndex,
        state_refs_.sequencer.structureUi.pageSelection.selectedMask,
        state_refs_.sequencer.contentView.kind,
        state_refs_.sequencer.contentView.length,
        state_refs_.sequencer.contentView.revision
    );
}

FLASHMEM void SequencerView::bindHeaderStripState() {
    watcher_.watchAll(
        [this]() {
            requestHeaderStripRender();
        },
        state_refs_.sharedTrackActive,
        state_refs_.sharedTrackEnabledMask,
        state_refs_.sequencer.pattern.length,
        state_refs_.sequencer.page,
        state_refs_.structureClipboard.revision,
        state_refs_.structureNavigationFocus,
        state_refs_.trackNavigation.previewAddSlot,
        state_refs_.trackNavigation.previewTrackIndex,
        state_refs_.sequencer.structureUi.previewPageIndex,
        state_refs_.sequencer.structureUi.previewAddPageSlot,
        state_refs_.trackNavigation.selection.active,
        state_refs_.trackNavigation.selection.scope,
        state_refs_.trackNavigation.selection.cursorIndex,
        state_refs_.trackNavigation.selection.selectedMask,
        state_refs_.sequencer.structureUi.pageSelection.active,
        state_refs_.sequencer.structureUi.pageSelection.scope,
        state_refs_.sequencer.structureUi.pageSelection.cursorIndex,
        state_refs_.sequencer.structureUi.pageSelection.selectedMask,
        state_refs_.sequencer.contentView.kind,
        state_refs_.sequencer.contentView.length,
        state_refs_.sequencer.contentView.revision
    );
}

FLASHMEM void SequencerView::bindGridState() {
    watcher_.watchAll(
        [this]() {
            requestGridRender();
        },
        state_refs_.sequencer.pattern.length,
        state_refs_.sequencer.page,
        state_refs_.sequencer.pattern.enabledMask,
        state_refs_.sequencer.playheadStep,
        state_refs_.sequencer.playheadStepTickOffset,
        state_refs_.sequencer.pattern.stepDataRevision,
        state_refs_.sequencer.probabilityCycleRevision,
        state_refs_.sequencer.variationTelemetryRevision,
        state_refs_.sequencer.pattern.patternVariationRevision,
        state_refs_.sequencer.pattern.patternScaleRevision,
        state_refs_.tracks.projectScaleRevisionSignal(),
        state_refs_.sequencer.activeStepProperty,
        state_refs_.sequencer.stepPropertyInlineSelector.selecting,
        state_refs_.sequencer.stepInlineFeedback.visible,
        state_refs_.sequencer.stepInlineFeedback.touchedMask,
        state_refs_.sequencer.stepInlineFeedback.property,
        state_refs_.sequencer.patternVariationFeedback.visible,
        state_refs_.sequencer.patternVariationFeedback.property,
        state_refs_.sequencer.contentView.kind,
        state_refs_.sequencer.contentView.length,
        state_refs_.sequencer.contentView.parentStep,
        state_refs_.sequencer.contentView.ownerNodeId,
        state_refs_.sequencer.contentView.sequenceId,
        state_refs_.sequencer.contentView.cycleSetId,
        state_refs_.sequencer.contentView.depth,
        state_refs_.sequencer.contentView.revision
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
        state_refs_.deviceSettings.visible,
        state_refs_.deviceSettings.selector.visible,
        state_refs_.sequencerSettings.visible,
        state_refs_.sequencerSettings.selector.visible,
        state_refs_.dataManager.visible,
        state_refs_.dataManager.dialog.visible
    );
}

FLASHMEM void SequencerView::bindLeftActionStripState() {
    watcher_.watchAll(
        [this]() {
            requestLeftActionStripRender();
        },
        state_refs_.sequencer.patternQuickControls.selecting,
        state_refs_.sequencer.patternQuickControls.physicalHoldActive,
        state_refs_.sequencer.activeStepProperty,
        state_refs_.sequencer.stepPropertyInlineSelector.selecting,
        state_refs_.trackNavigation.selection.active,
        state_refs_.trackNavigation.selection.scope,
        state_refs_.sequencer.structureUi.pageSelection.active,
        state_refs_.sequencer.structureUi.pageSelection.scope,
        state_refs_.sequencer.contentView.kind
    );
}

FLASHMEM void SequencerView::bindBottomActionStripState() {
    watcher_.watchAll(
        [this]() {
            requestBottomActionStripRender();
        },
        state_refs_.structureNavigationFocus,
        state_refs_.structureClipboard.revision,
        state_refs_.trackNavigation.previewAddSlot,
        state_refs_.sequencer.structureUi.previewAddPageSlot,
        state_refs_.trackNavigation.hold.action,
        state_refs_.trackNavigation.hold.startedAtMs,
        state_refs_.sequencer.structureUi.pageHold.action,
        state_refs_.sequencer.structureUi.pageHold.startedAtMs,
        state_refs_.trackNavigation.selection.active,
        state_refs_.trackNavigation.selection.selectedMask,
        state_refs_.sequencer.structureUi.pageSelection.active,
        state_refs_.sequencer.structureUi.pageSelection.selectedMask,
        state_refs_.sequencer.patternQuickControls.selecting,
        state_refs_.sequencer.stepPropertyInlineSelector.selecting,
        state_refs_.sequencer.activeStepProperty,
        state_refs_.sequencer.pattern.patternVariationRevision,
        state_refs_.sequencer.contentView.kind,
        state_refs_.sequencer.contentView.revision
    );
}

FLASHMEM void SequencerView::bindHistoryFeedbackState() {
    watcher_.watchAll(
        [this]() {
            requestHistoryFeedbackRender();
        },
        state_refs_.sequencer.historyFeedback.visible,
        state_refs_.sequencer.historyFeedback.revision
    );
}

FLASHMEM void SequencerView::ensureRenderTimer() {
    if (render_timer_) return;
    render_timer_ = core::app::makeExtmemUnique<PausableLvglTimer>(16, onRenderTimer, this);
}

FLASHMEM bool SequencerView::hasBlockingOverlay() const {
    return state_refs_.viewSelector.visible.get() ||
           state_refs_.sequencer.stepEdit.visible.get() ||
           state_refs_.deviceSettings.visible.get() ||
           state_refs_.deviceSettings.selector.visible.get() ||
           state_refs_.sequencerSettings.visible.get() ||
           state_refs_.sequencerSettings.selector.visible.get() ||
           state_refs_.dataManager.visible.get() ||
           state_refs_.dataManager.dialog.visible.get();
}

FLASHMEM void SequencerView::handleOverlayVisibilityChanged() {
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

FLASHMEM void SequencerView::scheduleRender(bool ready) {
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

FLASHMEM void SequencerView::pauseRenderTimerIfIdle() {
    if (!render_timer_) return;
    if (dirty_) return;
    render_timer_->pause();
}

FLASHMEM void SequencerView::requestRender(bool& dirtyFlag) {
    dirtyFlag = true;
    scheduleRender();
}

FLASHMEM void SequencerView::requestHeaderTopRender() {
    requestRender(header_top_dirty_);
}

FLASHMEM void SequencerView::requestHeaderStripRender() {
    requestRender(header_strip_dirty_);
}

FLASHMEM void SequencerView::requestBottomControlsRender() {
    requestRender(bottom_controls_dirty_);
}

FLASHMEM void SequencerView::requestPropertyStripRender() {
    requestRender(property_strip_dirty_);
}

FLASHMEM void SequencerView::requestLeftActionStripRender() {
    requestRender(left_action_strip_dirty_);
}

FLASHMEM void SequencerView::requestBottomActionStripRender() {
    requestRender(bottom_action_strip_dirty_);
}

FLASHMEM void SequencerView::requestHistoryFeedbackRender() {
    requestRender(history_feedback_dirty_);
}

FLASHMEM void SequencerView::requestGridRender() {
    requestRender(grid_dirty_);
}

FLASHMEM void SequencerView::markAllDirty() {
    header_top_dirty_ = true;
    header_strip_dirty_ = true;
    bottom_controls_dirty_ = true;
    property_strip_dirty_ = true;
    left_action_strip_dirty_ = true;
    bottom_action_strip_dirty_ = true;
    history_feedback_dirty_ = true;
    grid_dirty_ = true;
}

FLASHMEM void SequencerView::onRenderTimer(lv_timer_t* timer) {
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

FLASHMEM void SequencerView::render() {
    if (!container_ || lv_obj_has_flag(container_, LV_OBJ_FLAG_HIDDEN) || hasBlockingOverlay()) return;

    const bool needsBottomControls = bottom_controls_dirty_ && bottom_controls_;
    const bool needsPropertyStrip = property_strip_dirty_ && property_strip_;
    const bool needsLeftActionStrip = left_action_strip_dirty_ && left_action_strip_;
    const bool needsBottomActionStrip = bottom_action_strip_dirty_ && bottom_action_strip_;
    const bool needsHistoryToast = history_feedback_dirty_ && history_toast_;
    const bool needsHeaderTop = header_top_dirty_ && header_bar_;
    const bool needsHeaderStrip = header_strip_dirty_ && header_bar_;
    const bool needsGrid = grid_dirty_ && step_grid_;
    if (!needsBottomControls && !needsPropertyStrip && !needsLeftActionStrip &&
        !needsBottomActionStrip && !needsHistoryToast && !needsHeaderTop &&
        !needsHeaderStrip && !needsGrid) {
        return;
    }

    const auto source = modelSource();

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
        const auto gridProps = sequencer::buildStepGridProps(source);
        step_grid_->render(gridProps);
        grid_dirty_ = false;
    }

    if (needsHistoryToast) {
        renderHistoryToast();
        history_feedback_dirty_ = false;
    }
}

FLASHMEM void SequencerView::renderHistoryToast() {
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
    lv_obj_clear_flag(history_toast_, LV_OBJ_FLAG_HIDDEN);
}

FLASHMEM sequencer::SequencerViewModelSource SequencerView::modelSource() const {
    return {
        .sequencer = state_refs_.sequencer,
        .tracks = state_refs_.tracks,
        .trackNavigation = state_refs_.trackNavigation,
        .navigationFocus = state_refs_.structureNavigationFocus,
        .sharedTrackActive = state_refs_.sharedTrackActive,
        .sharedTrackEnabledMask = state_refs_.sharedTrackEnabledMask,
        .structureClipboard = state_refs_.structureClipboard,
        .statusBar = state_refs_.statusBar,
    };
}

}  // namespace core::ui
