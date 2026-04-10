#include "SequencerView.hpp"

#include <config/PlatformCompat.hpp>
#include "ui/sequencer/SequencerViewModelBuilder.hpp"

namespace core::ui {

FLASHMEM SequencerView::SequencerView(lv_obj_t* parent, StateRefs stateRefs)
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

FLASHMEM SequencerView::~SequencerView() {
    render_timer_.reset();

    step_grid_.reset();
    bottom_action_strip_.reset();
    track_strip_.reset();
    property_strip_.reset();
    bottom_controls_.reset();
    left_action_strip_.reset();
    header_bar_.reset();
    frame_.reset();
    container_ = nullptr;
    body_container_ = nullptr;
    interaction_container_ = nullptr;
    center_column_ = nullptr;
    structure_row_container_ = nullptr;
}

FLASHMEM void SequencerView::onActivate() {
    if (!container_) return;

    lv_obj_clear_flag(container_, LV_OBJ_FLAG_HIDDEN);

    if (step_grid_) {
        step_grid_->forceRefresh();
    }
    markAllDirty();
    scheduleRender();
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
    frame_ = std::make_unique<MainViewFrame>(parent);
    container_ = frame_->container();
    body_container_ = frame_->body();
    lv_obj_set_style_bg_opa(container_, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(container_, LV_OBJ_FLAG_HIDDEN);
}

FLASHMEM void SequencerView::createHeaderBar() {
    if (!frame_) return;
    header_bar_ = std::make_unique<SequencerHeaderBar>(frame_->header());
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
    if (!frame_ || !body_container_) return;
    frame_->createInteractionRow();
    interaction_container_ = frame_->interactionRow();

    left_action_strip_ = std::make_unique<ContextActionStrip>(
        interaction_container_,
        ContextActionStripOrientation::VERTICAL,
        ContextActionStripVerticalLayout::SPREAD
    );

    frame_->createCenterColumn();
    center_column_ = frame_->centerColumn();

    frame_->createStructureRow();
    structure_row_container_ = frame_->structureRow();
    track_strip_ = std::make_unique<TrackNavigationStrip>(structure_row_container_);

    bottom_action_strip_ = std::make_unique<ContextActionStrip>(
        body_container_,
        ContextActionStripOrientation::HORIZONTAL
    );
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
        },
        state_refs_.tracks.activeTrack,
        state_refs_.tracks.enabledMask,
        state_refs_.structureNavigationFocus,
        state_refs_.structureClipboard.revision,
        state_refs_.sequencer.structureUi.previewAddSlot,
        state_refs_.sequencer.structureUi.selection.active,
        state_refs_.sequencer.structureUi.selection.scope,
        state_refs_.sequencer.structureUi.selection.cursorIndex,
        state_refs_.sequencer.structureUi.selection.selectedMask
    );
    watcher_.watchAll(
        [this]() {
            requestTrackStripRender();
        },
        state_refs_.tracks.activeTrack,
        state_refs_.tracks.enabledMask,
        state_refs_.structureNavigationFocus,
        state_refs_.sequencer.structureUi.previewAddSlot,
        state_refs_.sequencer.structureUi.selection.active,
        state_refs_.sequencer.structureUi.selection.scope,
        state_refs_.sequencer.structureUi.selection.cursorIndex,
        state_refs_.sequencer.structureUi.selection.selectedMask,
        state_refs_.statusBar.trackNoteActivity[0],
        state_refs_.statusBar.trackNoteActivity[1],
        state_refs_.statusBar.trackNoteActivity[2],
        state_refs_.statusBar.trackNoteActivity[3],
        state_refs_.statusBar.trackNoteActivity[4],
        state_refs_.statusBar.trackNoteActivity[5],
        state_refs_.statusBar.trackNoteActivity[6],
        state_refs_.statusBar.trackNoteActivity[7]
    );
    watcher_.watchAll(
        [this]() {
            requestTrackStripRender();
        },
        state_refs_.statusBar.trackNoteActivity[8],
        state_refs_.statusBar.trackNoteActivity[9],
        state_refs_.statusBar.trackNoteActivity[10],
        state_refs_.statusBar.trackNoteActivity[11],
        state_refs_.statusBar.trackNoteActivity[12],
        state_refs_.statusBar.trackNoteActivity[13],
        state_refs_.statusBar.trackNoteActivity[14],
        state_refs_.statusBar.trackNoteActivity[15]
    );
}

FLASHMEM void SequencerView::bindHeaderStripState() {
    watcher_.watchAll(
        [this]() {
            requestHeaderTopRender();
            requestHeaderStripRender();
        },
        state_refs_.sequencer.length,
        state_refs_.sequencer.page,
        state_refs_.structureNavigationFocus,
        state_refs_.sequencer.structureUi.previewAddSlot,
        state_refs_.sequencer.playheadStep,
        state_refs_.sequencer.structureUi.selection.active,
        state_refs_.sequencer.structureUi.selection.scope,
        state_refs_.sequencer.structureUi.selection.cursorIndex,
        state_refs_.sequencer.structureUi.selection.selectedMask
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
        state_refs_.sequencer.patternQuickControls.selecting,
        state_refs_.sequencer.activeStepProperty,
        state_refs_.sequencer.stepPropertyInlineSelector.selecting,
        state_refs_.sequencer.rangeSelection.kind,
        state_refs_.sequencer.structureUi.selection.active,
        state_refs_.sequencer.structureUi.selection.scope
    );
}

FLASHMEM void SequencerView::bindBottomActionStripState() {
    watcher_.watchAll(
        [this]() {
            requestBottomActionStripRender();
        },
        state_refs_.structureNavigationFocus,
        state_refs_.structureClipboard.revision,
        state_refs_.sequencer.structureUi.previewAddSlot,
        state_refs_.sequencer.structureUi.hold.action,
        state_refs_.sequencer.structureUi.hold.startedAtMs,
        state_refs_.sequencer.rangeSelection.kind,
        state_refs_.sequencer.rangeSelection.phase,
        state_refs_.sequencer.structureUi.selection.active,
        state_refs_.sequencer.structureUi.selection.selectedMask
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

FLASHMEM void SequencerView::ensureRenderTimer() {
    if (render_timer_) return;
    render_timer_ = std::make_unique<PausableLvglTimer>(16, onRenderTimer, this);
}

FLASHMEM bool SequencerView::hasBlockingOverlay() const {
    return state_refs_.viewSelector.visible.get() ||
           state_refs_.sequencer.stepEdit.visible.get() ||
           state_refs_.globalSettings.visible.get() ||
           state_refs_.globalSettings.selector.visible.get() ||
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

FLASHMEM void SequencerView::requestTrackStripRender() {
    requestRender(track_strip_dirty_);
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

FLASHMEM void SequencerView::requestGridRender() {
    requestRender(grid_dirty_);
}

FLASHMEM void SequencerView::markAllDirty() {
    header_top_dirty_ = true;
    header_strip_dirty_ = true;
    track_strip_dirty_ = true;
    bottom_controls_dirty_ = true;
    property_strip_dirty_ = true;
    left_action_strip_dirty_ = true;
    bottom_action_strip_dirty_ = true;
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
    const bool needsTrackStrip = track_strip_dirty_ && track_strip_;
    const bool needsPropertyStrip = property_strip_dirty_ && property_strip_;
    const bool needsLeftActionStrip = left_action_strip_dirty_ && left_action_strip_;
    const bool needsBottomActionStrip = bottom_action_strip_dirty_ && bottom_action_strip_;
    const bool needsHeaderTop = header_top_dirty_ && header_bar_;
    const bool needsHeaderStrip = header_strip_dirty_ && header_bar_;
    const bool needsGrid = grid_dirty_ && step_grid_;
    if (!needsBottomControls && !needsTrackStrip && !needsPropertyStrip && !needsLeftActionStrip &&
        !needsBottomActionStrip && !needsHeaderTop &&
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

    if (needsTrackStrip) {
        track_strip_->render(sequencer::buildTrackNavigationStripProps(source));
        track_strip_dirty_ = false;
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

FLASHMEM sequencer::SequencerViewModelSource SequencerView::modelSource() const {
    return {
        .sequencer = state_refs_.sequencer,
        .tracks = state_refs_.tracks,
        .navigationFocus = state_refs_.structureNavigationFocus,
        .structureClipboard = state_refs_.structureClipboard,
        .statusBar = state_refs_.statusBar,
    };
}

}  // namespace core::ui
