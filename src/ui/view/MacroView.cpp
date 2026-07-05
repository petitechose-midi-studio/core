#include "MacroView.hpp"

#include <oc/log/Log.hpp>
#include <oc/ui/lvgl/style/StyleBuilder.hpp>
#include <config/App.hpp>
#include <config/PlatformCompat.hpp>
#include <config/TimeCompat.hpp>

#include "state/macro/MacroWorkflow.hpp"
#include "ui/view/MacroViewModelBuilder.hpp"
#include "ui/widget/MacroKnobWidget.hpp"

namespace core::ui {

namespace style = oc::ui::lvgl::style;

namespace {

#if defined(PERF_LOG)
struct MacroRenderProfiling {
    uint32_t window_start_ms = 0;
    uint32_t pass_count = 0;
    uint32_t total_us = 0;
    uint32_t max_us = 0;
    uint32_t total_value_updates = 0;
    uint32_t total_config_updates = 0;

    void record(uint32_t elapsed_us, uint32_t value_updates, uint32_t config_updates) {
        const uint32_t now = core::time_compat::millis();
        if (window_start_ms == 0) {
            window_start_ms = now;
        }

        pass_count += 1;
        total_us += elapsed_us;
        max_us = std::max(max_us, elapsed_us);
        total_value_updates += value_updates;
        total_config_updates += config_updates;

        if ((now - window_start_ms) < 500) return;

        const uint32_t avg_us = pass_count > 0 ? (total_us / pass_count) : 0;
        if (max_us >= 2000 || avg_us >= 1000) {
            OC_LOG_INFO("[Perf][MacroView] passes={} avg={}us max={}us valueUpdates={} configUpdates={}",
                        pass_count,
                        avg_us,
                        max_us,
                        total_value_updates,
                        total_config_updates);
        }

        window_start_ms = now;
        pass_count = 0;
        total_us = 0;
        max_us = 0;
        total_value_updates = 0;
        total_config_updates = 0;
    }
};

MacroRenderProfiling g_macro_render_profiling;
#endif

}  // namespace

FLASHMEM MacroView::MacroView(lv_obj_t* parent, StateRefs stateRefs)
    : state_refs_(stateRefs) {
    rendered_channels_.fill(0xFF);
    rendered_ccs_.fill(0xFF);
    rendered_automation_active_.fill(false);
    rendered_automation_recording_.fill(false);
    rendered_automation_manual_override_.fill(false);
    rendered_active_.fill(true);
    rendered_add_slot_.fill(false);
    rendered_focused_.fill(false);
    createLayout(parent);
    createHeaderBar();
    createActionStrips();
    createSlotPropertyOverlay();
    createMacros();

    // Macro rendering is sampled at the global LVGL cadence.
    constexpr uint32_t targetHz = Config::Timing::LVGL_HZ;
    constexpr uint32_t periodMs = (targetHz > 1000)
        ? 1
        : ((1000 + targetHz - 1) / targetHz);
    update_timer_ = std::make_unique<PausableLvglTimer>(periodMs, onUpdateTimer, this);

    bindToState();
}

FLASHMEM MacroView::~MacroView() {
    update_timer_.reset();
    subscriptions_.clear();
    slot_property_overlay_.reset();
    bottom_action_strip_.reset();
    left_action_strip_.reset();
    header_bar_.reset();
    for (auto& macro : macros_) {
        macro.reset();
    }

    // Destroy the LVGL tree last (child widgets must be gone first)
    frame_.reset();
    container_ = nullptr;
    top_bar_container_ = nullptr;
    body_container_ = nullptr;
    interaction_container_ = nullptr;
    center_column_ = nullptr;
    macro_grid_container_ = nullptr;
}

FLASHMEM void MacroView::onActivate() {
    if (container_) {
        lv_obj_clear_flag(container_, LV_OBJ_FLAG_HIDDEN);
        dirty_flags_.fill(true);
        has_dirty_ = true;
        header_dirty_ = true;
        left_action_strip_dirty_ = true;
        bottom_action_strip_dirty_ = true;
        slot_property_overlay_dirty_ = true;
        markConfigDirtyIfChanged();
        scheduleUpdate();
        processDirtyFlags();
    }
}

FLASHMEM void MacroView::onDeactivate() {
    if (container_) {
        lv_obj_add_flag(container_, LV_OBJ_FLAG_HIDDEN);
    }

    if (update_timer_) {
        update_timer_->pause();
    }
}

FLASHMEM void MacroView::bindToState() {
    subscriptions_.reserve(MACRO_COUNT + 24);

    subscriptions_.push_back(
        state_refs_.configRevision.subscribe([this](uint32_t revision) {
            if (core::state::macro::macroConfigRevisionTargetsAll(revision)) {
                markAllConfigDirty();
                return;
            }

            const int dirtyIndex = core::state::macro::macroConfigRevisionDirtyIndex(revision);
            if (dirtyIndex >= 0) {
                config_dirty_flags_[dirtyIndex] = true;
                has_dirty_ = true;
                scheduleUpdate();
                return;
            }

            markAllConfigDirty();
        })
    );

    subscriptions_.push_back(
        state_refs_.statusBar.ccOutActive.subscribe([this](bool) {
            requestHeaderRender();
        })
    );

    subscriptions_.push_back(
        state_refs_.macroUi.activeProperty.subscribe(
            [this](core::state::macro::MacroPerformanceProperty) {
                requestSlotPropertyOverlayRender();
            }
        )
    );

    subscriptions_.push_back(
        state_refs_.macroUi.clutchActive.subscribe([this](bool) {
            requestHeaderRender();
            requestLeftActionStripRender();
            requestSlotPropertyOverlayRender();
        })
    );

    subscriptions_.push_back(
        state_refs_.macroUi.automationRecordingRevision.subscribe([this](uint32_t) {
            requestHeaderRender();
            markAllConfigDirty();
        })
    );

    subscriptions_.push_back(
        state_refs_.macroUi.automationManualOverrideMask.subscribe([this](uint16_t) {
            markAllConfigDirty();
        })
    );

    subscriptions_.push_back(
        state_refs_.structureNavigationFocus.subscribe([this](core::state::StructureNavigationFocus) {
            requestHeaderRender();
            requestLeftActionStripRender();
            requestBottomActionStripRender();
            markAllConfigDirty();
        })
    );

    subscriptions_.push_back(
        state_refs_.macroUi.focusedMacroSlot.subscribe([this](uint8_t) {
            markAllConfigDirty();
        })
    );

    subscriptions_.push_back(
        state_refs_.structureClipboard.revision.subscribe([this](uint32_t) {
            requestBottomActionStripRender();
        })
    );

    subscriptions_.push_back(
        state_refs_.trackNavigation.previewAddSlot.subscribe([this](bool) {
            requestHeaderRender();
            requestBottomActionStripRender();
        })
    );

    subscriptions_.push_back(
        state_refs_.trackNavigation.previewTrackIndex.subscribe([this](uint8_t) {
            requestHeaderRender();
            requestBottomActionStripRender();
        })
    );

    subscriptions_.push_back(
        state_refs_.macroUi.previewPageIndex.subscribe([this](uint8_t) {
            requestHeaderRender();
            requestBottomActionStripRender();
        })
    );

    subscriptions_.push_back(
        state_refs_.trackNavigation.hold.action.subscribe([this](core::state::StructureHoldAction) {
            requestBottomActionStripRender();
        })
    );

    subscriptions_.push_back(
        state_refs_.trackNavigation.hold.startedAtMs.subscribe([this](uint32_t) {
            requestBottomActionStripRender();
        })
    );

    subscriptions_.push_back(
        state_refs_.trackNavigation.selection.active.subscribe([this](bool) {
            requestHeaderRender();
            requestLeftActionStripRender();
            requestBottomActionStripRender();
        })
    );

    subscriptions_.push_back(
        state_refs_.trackNavigation.selection.scope.subscribe([this](core::state::StructureSelectionScope) {
            requestHeaderRender();
            requestLeftActionStripRender();
            requestBottomActionStripRender();
        })
    );

    subscriptions_.push_back(
        state_refs_.trackNavigation.selection.cursorIndex.subscribe([this](uint8_t) {
            requestHeaderRender();
        })
    );

    subscriptions_.push_back(
        state_refs_.trackNavigation.selection.selectedMask.subscribe([this](uint16_t) {
            requestHeaderRender();
            requestBottomActionStripRender();
        })
    );

    subscriptions_.push_back(
        state_refs_.macroUi.previewAddPageSlot.subscribe([this](bool) {
            requestHeaderRender();
            requestBottomActionStripRender();
        })
    );

    subscriptions_.push_back(
        state_refs_.macroUi.pageHold.action.subscribe([this](core::state::StructureHoldAction) {
            requestBottomActionStripRender();
        })
    );

    subscriptions_.push_back(
        state_refs_.macroUi.pageHold.startedAtMs.subscribe([this](uint32_t) {
            requestBottomActionStripRender();
        })
    );

    subscriptions_.push_back(
        state_refs_.macroUi.pageSelection.active.subscribe([this](bool) {
            requestHeaderRender();
            requestLeftActionStripRender();
            requestBottomActionStripRender();
        })
    );

    subscriptions_.push_back(
        state_refs_.macroUi.pageSelection.scope.subscribe([this](core::state::StructureSelectionScope) {
            requestHeaderRender();
            requestLeftActionStripRender();
            requestBottomActionStripRender();
        })
    );

    subscriptions_.push_back(
        state_refs_.macroUi.pageSelection.cursorIndex.subscribe([this](uint8_t) {
            requestHeaderRender();
        })
    );

    subscriptions_.push_back(
        state_refs_.macroUi.pageSelection.selectedMask.subscribe([this](uint16_t) {
            requestHeaderRender();
            requestBottomActionStripRender();
        })
    );

    subscriptions_.push_back(
        state_refs_.pages.enabledPageMaskSignal().subscribe([this](uint16_t) {
            requestHeaderRender();
        })
    );

    subscriptions_.push_back(
        state_refs_.sharedTrackActive.subscribe([this](uint8_t) {
            requestHeaderRender();
            requestBottomActionStripRender();
        })
    );

    subscriptions_.push_back(
        state_refs_.pages.activePageIndexSignal().subscribe([this](uint8_t) {
            requestHeaderRender();
            requestBottomActionStripRender();
        })
    );

    subscriptions_.push_back(
        state_refs_.sharedTrackEnabledMask.subscribe([this](uint16_t) {
            requestHeaderRender();
        })
    );

    for (uint8_t i = 0; i < MACRO_COUNT; ++i) {
        auto& slot = state_refs_.macros.slots[i];

        subscriptions_.push_back(
            slot.value.subscribe([this, i](float) {
                markDirty(i);
            })
        );
    }

    subscriptions_.push_back(
        state_refs_.macroEdit.visible.subscribe([this](bool) {
            handleOverlayVisibilityChanged();
        })
    );
    subscriptions_.push_back(
        state_refs_.viewSelector.visible.subscribe([this](bool) {
            handleOverlayVisibilityChanged();
        })
    );
    subscriptions_.push_back(
        state_refs_.deviceSettings.visible.subscribe([this](bool) {
            handleOverlayVisibilityChanged();
        })
    );
    subscriptions_.push_back(
        state_refs_.deviceSettings.selector.visible.subscribe([this](bool) {
            handleOverlayVisibilityChanged();
        })
    );
    subscriptions_.push_back(
        state_refs_.dataManager.visible.subscribe([this](bool) {
            handleOverlayVisibilityChanged();
        })
    );
    subscriptions_.push_back(
        state_refs_.dataManager.dialog.visible.subscribe([this](bool) {
            handleOverlayVisibilityChanged();
        })
    );

    header_dirty_ = true;
    left_action_strip_dirty_ = true;
    bottom_action_strip_dirty_ = true;
    slot_property_overlay_dirty_ = true;
    markAllDirty();
    processDirtyFlags();
}

FLASHMEM void MacroView::createLayout(lv_obj_t* parent) {
    frame_ = std::make_unique<MainViewFrame>(parent);
    container_ = frame_->container();
    top_bar_container_ = frame_->header();
    body_container_ = frame_->body();
}

FLASHMEM void MacroView::createHeaderBar() {
    header_bar_ = std::make_unique<MacroHeaderBar>(top_bar_container_);
}

FLASHMEM void MacroView::createActionStrips() {
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

    macro_grid_container_ = lv_obj_create(center_column_);
    style::apply(macro_grid_container_)
        .size(LV_PCT(100), 0)
        .transparent()
        .noBorder()
        .pad(0)
        .noScroll();
    lv_obj_set_flex_grow(macro_grid_container_, 1);

    static lv_coord_t col_dsc[] = {
        LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST
    };
    static lv_coord_t row_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};

    lv_obj_set_grid_dsc_array(macro_grid_container_, col_dsc, row_dsc);
    lv_obj_set_layout(macro_grid_container_, LV_LAYOUT_GRID);
    lv_obj_set_style_pad_column(macro_grid_container_, 0, 0);
    lv_obj_set_style_pad_row(macro_grid_container_, 0, 0);

    bottom_action_strip_ = std::make_unique<ContextActionStrip>(
        body_container_,
        ContextActionStripOrientation::HORIZONTAL
    );
}

FLASHMEM void MacroView::createSlotPropertyOverlay() {
    if (!container_) return;
    slot_property_overlay_ = std::make_unique<StepPropertySelectionOverlay>(container_);
}

FLASHMEM void MacroView::createMacros() {
    for (uint8_t i = 0; i < MACRO_COUNT; ++i) {
        uint8_t col = i % COLS;
        uint8_t row = i / COLS;

        macros_[i] = std::make_unique<MacroKnobWidget>(macro_grid_container_, i);
        lv_obj_set_grid_cell(macros_[i]->getElement(),
            LV_GRID_ALIGN_STRETCH, col, 1,
            LV_GRID_ALIGN_STRETCH, row, 1);
    }
}

FLASHMEM void MacroView::scheduleUpdate() {
    if (update_timer_) {
        update_timer_->resume();
    }
}

FLASHMEM void MacroView::pauseUpdateIfIdle() {
    if (!update_timer_) return;
    if (has_dirty_ || header_dirty_ || left_action_strip_dirty_ ||
        bottom_action_strip_dirty_ || slot_property_overlay_dirty_) return;
    update_timer_->pause();
}

FLASHMEM void MacroView::requestHeaderRender() {
    header_dirty_ = true;
    scheduleUpdate();
}

FLASHMEM void MacroView::requestLeftActionStripRender() {
    left_action_strip_dirty_ = true;
    scheduleUpdate();
}

FLASHMEM void MacroView::requestBottomActionStripRender() {
    bottom_action_strip_dirty_ = true;
    scheduleUpdate();
}

FLASHMEM void MacroView::requestSlotPropertyOverlayRender() {
    slot_property_overlay_dirty_ = true;
    scheduleUpdate();
}

FLASHMEM bool MacroView::hasBlockingOverlay() const {
    return state_refs_.macroEdit.visible.get() ||
           state_refs_.viewSelector.visible.get() ||
           state_refs_.deviceSettings.visible.get() ||
           state_refs_.deviceSettings.selector.visible.get() ||
           state_refs_.dataManager.visible.get() ||
           state_refs_.dataManager.dialog.visible.get();
}

FLASHMEM void MacroView::handleOverlayVisibilityChanged() {
    if (hasBlockingOverlay()) {
        if (update_timer_) {
            update_timer_->pause();
        }
        return;
    }

    if (has_dirty_ || header_dirty_ || left_action_strip_dirty_ || bottom_action_strip_dirty_ ||
        slot_property_overlay_dirty_) {
        scheduleUpdate();
    }
}

// =============================================================================
// Debounced Update System
// =============================================================================

FLASHMEM void MacroView::markAllDirty() {
    dirty_flags_.fill(true);
    config_dirty_flags_.fill(true);
    has_dirty_ = true;
    header_dirty_ = true;
    left_action_strip_dirty_ = true;
    bottom_action_strip_dirty_ = true;
    slot_property_overlay_dirty_ = true;
    scheduleUpdate();
}

FLASHMEM void MacroView::markAllConfigDirty() {
    config_dirty_flags_.fill(true);
    has_dirty_ = true;
    scheduleUpdate();
}

FLASHMEM void MacroView::markConfigDirtyIfChanged() {
    bool anyDirty = false;
    for (uint8_t i = 0; i < MACRO_COUNT; ++i) {
        const auto& config = state_refs_.pages.activeConfigs[i];
        const bool active = state_refs_.pages.isMacroSlotActive(i);
        const bool addSlot = !active && state_refs_.pages.isMacroAddSlot(i);
        const bool focused =
            state_refs_.structureNavigationFocus.get() == core::state::StructureNavigationFocus::STEP &&
            state_refs_.macroUi.focusedMacroSlot.get() == i;
        const bool recording =
            state_refs_.macroUi.automationRecording.active &&
            state_refs_.macroUi.automationRecording.address.track == state_refs_.pages.currentActiveTrack() &&
            state_refs_.macroUi.automationRecording.address.page == state_refs_.pages.currentActivePage() &&
            state_refs_.macroUi.automationRecording.address.macro == i;
        const bool manualOverride =
            (state_refs_.macroUi.automationManualOverrideMask.get() &
             static_cast<uint16_t>(1U << i)) != 0;
        const bool dirty = rendered_channels_[i] != config.channel ||
                           rendered_ccs_[i] != config.cc ||
                           rendered_active_[i] != active ||
                           rendered_add_slot_[i] != addSlot ||
                           rendered_focused_[i] != focused ||
                           rendered_automation_recording_[i] != recording ||
                           rendered_automation_manual_override_[i] != manualOverride;
        config_dirty_flags_[i] = dirty;
        anyDirty = anyDirty || dirty;
    }
    if (anyDirty) {
        has_dirty_ = true;
    }
}

FLASHMEM void MacroView::markDirty(uint8_t index) {
    if (index < MACRO_COUNT) {
        dirty_flags_[index] = true;
        has_dirty_ = true;
        scheduleUpdate();
    }
}

FLASHMEM void MacroView::processDirtyFlags() {
#if defined(PERF_LOG)
    const uint32_t start_us = core::time_compat::micros();
    uint32_t value_updates = 0;
    uint32_t config_updates = 0;
#endif
    if (!container_ || lv_obj_has_flag(container_, LV_OBJ_FLAG_HIDDEN) || hasBlockingOverlay()) {
        pauseUpdateIfIdle();
        return;
    }

    const auto source = modelSource();
    const auto frame = buildMacroViewFrameState(source);

    if (header_dirty_ && header_bar_) {
        header_bar_->render(buildMacroHeaderBarProps(source));
        header_dirty_ = false;
    }

    if (left_action_strip_dirty_ && left_action_strip_) {
        left_action_strip_->render(buildMacroLeftActionStripProps(source));
        left_action_strip_dirty_ = false;
    }

    if (slot_property_overlay_dirty_ && slot_property_overlay_) {
        slot_property_overlay_->render(buildMacroSlotPropertyOverlayProps(source));
        slot_property_overlay_dirty_ = false;
    }

    if (bottom_action_strip_dirty_ && bottom_action_strip_) {
        bottom_action_strip_->render(buildMacroBottomActionStripProps(source));
        bottom_action_strip_dirty_ = false;
    }

    if (!has_dirty_) {
        pauseUpdateIfIdle();
        return;
    }

    for (uint8_t i = 0; i < MACRO_COUNT; ++i) {
        if (dirty_flags_[i] || config_dirty_flags_[i]) {
            if (macros_[i]) {
                if (dirty_flags_[i]) {
                    macros_[i]->setValue(frame.macros[i].value);
                    dirty_flags_[i] = false;
#if defined(PERF_LOG)
                    value_updates += 1;
#endif
                }
                if (config_dirty_flags_[i]) {
                    const auto& props = frame.macros[i];
                    if (rendered_active_[i] != props.active ||
                        rendered_add_slot_[i] != props.addSlot) {
                        macros_[i]->setSlotState(props.active, props.addSlot);
                        rendered_active_[i] = props.active;
                        rendered_add_slot_[i] = props.addSlot;
#if defined(PERF_LOG)
                        config_updates += 1;
#endif
                    }
                    if (rendered_channels_[i] != props.channel ||
                        rendered_ccs_[i] != props.cc) {
                        macros_[i]->setConfig(props.channel, props.cc);
                        rendered_channels_[i] = props.channel;
                        rendered_ccs_[i] = props.cc;
#if defined(PERF_LOG)
                        config_updates += 1;
#endif
                    }
                    if (rendered_automation_active_[i] != props.automationActive) {
                        macros_[i]->setAutomationActive(props.automationActive);
                        rendered_automation_active_[i] = props.automationActive;
                    }
                    if (rendered_automation_recording_[i] != props.automationRecording) {
                        macros_[i]->setAutomationRecording(props.automationRecording);
                        rendered_automation_recording_[i] = props.automationRecording;
                    }
                    if (rendered_automation_manual_override_[i] !=
                        props.automationManualOverride) {
                        macros_[i]->setAutomationManualOverride(props.automationManualOverride);
                        rendered_automation_manual_override_[i] = props.automationManualOverride;
                    }
                    if (rendered_focused_[i] != props.focused) {
                        macros_[i]->setFocused(props.focused);
                        rendered_focused_[i] = props.focused;
                    }
                    config_dirty_flags_[i] = false;
                }
            }
        }
    }

    has_dirty_ = false;
    pauseUpdateIfIdle();
#if defined(PERF_LOG)
    g_macro_render_profiling.record(
        core::time_compat::micros() - start_us,
        value_updates,
        config_updates
    );
#endif
}

FLASHMEM void MacroView::onUpdateTimer(lv_timer_t* timer) {
    auto* self = static_cast<MacroView*>(lv_timer_get_user_data(timer));
    if (self) {
        self->processDirtyFlags();
    }
}

FLASHMEM MacroViewModelSource MacroView::modelSource() const {
    return {
        .macros = state_refs_.macros,
        .pages = state_refs_.pages,
        .macroUi = state_refs_.macroUi,
        .trackNavigation = state_refs_.trackNavigation,
        .navigationFocus = state_refs_.structureNavigationFocus,
        .sharedTrackActive = state_refs_.sharedTrackActive,
        .sharedTrackEnabledMask = state_refs_.sharedTrackEnabledMask,
        .structureClipboard = state_refs_.structureClipboard,
        .statusBar = state_refs_.statusBar,
    };
}

}  // namespace core::ui
