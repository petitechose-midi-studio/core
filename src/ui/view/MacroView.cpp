#include "MacroView.hpp"

#include <algorithm>
#include <cstddef>

#include <oc/ui/lvgl/style/StyleBuilder.hpp>
#include <oc/ui/lvgl/StaticSurfaceInvalidation.hpp>
#include <oc/diagnostics/Performance.hpp>
#include <oc/log/Log.hpp>
#include <config/App.hpp>
#include <config/PlatformCompat.hpp>
#include "state/macro/MacroWorkflow.hpp"
#include "ui/view/MacroViewModelBuilder.hpp"
#include "ui/view/RetainedViewRenderPolicy.hpp"
#include "ui/widget/MacroKnobWidget.hpp"

namespace core::ui {

namespace style = oc::ui::lvgl::style;

namespace {

uint8_t sourceStateBits(const MacroWidgetProps& props) {
    return static_cast<uint8_t>(
        (props.automationStored ? 1U << 0U : 0U) |
        (props.automationActive ? 1U << 1U : 0U) |
        (props.modulationStored ? 1U << 2U : 0U) |
        (props.modulationActive ? 1U << 3U : 0U) |
        (props.modulationPaused ? 1U << 4U : 0U) |
        (static_cast<uint8_t>(std::min<uint8_t>(
             props.modulationSourceCount,
             7U
         )) << 5U)
    );
}

}  // namespace

FLASHMEM MacroView::MacroView(lv_obj_t* parent, StateRefs stateRefs)
    : state_refs_(stateRefs) {
    rendered_ccs_.fill(0xFF);
    rendered_automation_active_.fill(false);
    rendered_automation_recording_.fill(false);
    rendered_automation_manual_override_.fill(false);
    rendered_source_state_.fill(0xFF);
    rendered_active_.fill(true);
    rendered_add_slot_.fill(false);
    rendered_focused_.fill(false);
    createLayout(parent);
    createHeaderBar();
    createActionStrips();
    createSlotPropertyOverlay();
    createMacros();
    if (!frame_ || !frame_->valid() || !container_ || !top_bar_container_ ||
        !body_container_ || !interaction_container_ || !center_column_ ||
        !macro_grid_container_ || !header_bar_ || !header_bar_->getElement() ||
        !left_action_strip_ || !left_action_strip_->getElement() ||
        !bottom_action_strip_ || !bottom_action_strip_->getElement() ||
        !slot_property_overlay_ || !slot_property_overlay_->getElement()) {
        return;
    }
    for (const auto& macro : macros_) {
        if (!macro || !macro->valid()) return;
    }

    // Prime retained header styles during construction so first activation only
    // resolves geometry through LVGL's normal layout pass.
    if (header_bar_) {
        header_bar_->render(buildMacroHeaderBarProps(modelSource()));
    }

    // Macro rendering is sampled at the global LVGL cadence.
    constexpr uint32_t targetHz = Config::Timing::LVGL_HZ;
    constexpr uint32_t periodMs = (targetHz > 1000)
        ? 1
        : ((1000 + targetHz - 1) / targetHz);
    render_scheduler_ =
        core::app::makeExtmemUnique<core::ui::CoalescedLvglRenderScheduler>(
            core::ui::renderSchedulerDebugLabel("MacroView"),
            &MacroView::drainRender,
            this,
            periodMs,
            &MacroView::canDrainRender
        );
    if (!render_scheduler_ || !render_scheduler_->valid()) {
        OC_LOG_ERROR("MacroView: render scheduler allocation failed");
        return;
    }

    if (!bindToState()) {
        OC_LOG_ERROR("MacroView: state binding failed");
        return;
    }
    initialized_ = true;
}

FLASHMEM MacroView::~MacroView() {
    render_scheduler_.reset();
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
    if (!container_) return;

    RetainedViewRenderPolicy::show(container_);
    markConfigDirtyIfChanged();
    if (render_scheduler_) render_scheduler_->resumePending(true);
}

FLASHMEM void MacroView::onDeactivate() {
    if (render_scheduler_) render_scheduler_->pause();
    RetainedViewRenderPolicy::hide(container_);
}

FLASHMEM bool MacroView::bindToState() {
    subscriptions_.push_back(
        state_refs_.configRevision.subscribe([this](uint32_t revision) {
            if (core::state::macro::macroConfigRevisionTargetsAll(revision)) {
                markAllConfigDirty();
                return;
            }

            const int dirtyIndex = core::state::macro::macroConfigRevisionDirtyIndex(revision);
            if (dirtyIndex >= 0) {
                requestRender(configRenderFlag(static_cast<uint8_t>(dirtyIndex)));
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
            markAutomationRecordingDirtyIfChanged();
        })
    );

    subscriptions_.push_back(
        state_refs_.macroUi.automationRecordingStatus.subscribe(
            [this](core::state::macro::MacroAutomationRecordingStatus) {
                requestHeaderRender();
            }
        )
    );

    subscriptions_.push_back(
        state_refs_.macroUi.automationManualOverrideMask.subscribe([this](uint16_t) {
            markConfigDirtyIfChanged();
        })
    );

    subscriptions_.push_back(
        state_refs_.macroUi.runtimeProjectionRevision.subscribe([this](uint32_t revision) {
            if (core::state::macro::macroRuntimeProjectionRevisionTargetsConfig(
                    revision
                )) {
                markConfigDirtyIfChanged();
                return;
            }
            if (core::state::macro::macroRuntimeProjectionRevisionTargetsAll(
                    revision
                )) {
                requestRender(RENDER_VALUE_MASK);
                return;
            }

            const int dirtyIndex =
                core::state::macro::macroRuntimeProjectionRevisionDirtyIndex(
                    revision
                );
            if (dirtyIndex >= 0) {
                markDirty(static_cast<uint8_t>(dirtyIndex));
                return;
            }

            requestRender(RENDER_VALUE_MASK);
        })
    );

    subscriptions_.push_back(
        state_refs_.structureNavigationFocus.subscribe([this](core::state::StructureNavigationFocus) {
            requestHeaderRender();
            requestLeftActionStripRender();
            requestBottomActionStripRender();
            markConfigDirtyIfChanged();
        })
    );

    subscriptions_.push_back(
        state_refs_.macroUi.focusedMacroSlot.subscribe([this](uint8_t) {
            markConfigDirtyIfChanged();
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
        state_refs_.macroUi.selectionDeleteGuard.subscribe(
            [this](const core::state::contextual::GuardedActionState&) {
                requestBottomActionStripRender();
            }
        )
    );

    subscriptions_.push_back(
        state_refs_.macroUi.selectionDeleteFeedback.subscribe(
            [this](const core::state::contextual::OperationFeedbackState&) {
                requestBottomActionStripRender();
            }
        )
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
            markConfigDirtyIfChanged();
        })
    );

    subscriptions_.push_back(
        state_refs_.pages.activePageIndexSignal().subscribe([this](uint8_t) {
            requestHeaderRender();
            requestBottomActionStripRender();
            markConfigDirtyIfChanged();
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

    markAllDirty();

    if (!subscriptions_.valid()) {
        OC_LOG_ERROR(
            "MacroView: subscriptions invalid count={} capacity={}",
            static_cast<unsigned>(subscriptions_.size()),
            static_cast<unsigned>(subscriptions_.capacity())
        );
        return false;
    }
    return true;
}

FLASHMEM void MacroView::createLayout(lv_obj_t* parent) {
    frame_ = core::app::makeExtmemUnique<MainViewFrame>(parent);
    if (!frame_ || !frame_->valid()) return;
    container_ = frame_->container();
    top_bar_container_ = frame_->header();
    body_container_ = frame_->body();
    RetainedViewRenderPolicy::initializeHidden(container_);
}

FLASHMEM void MacroView::createHeaderBar() {
    if (!top_bar_container_) return;
    header_bar_ = core::app::makeExtmemUnique<MacroHeaderBar>(top_bar_container_);
}

FLASHMEM void MacroView::createActionStrips() {
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

    macro_grid_container_ = lv_obj_create(center_column_);
    if (!macro_grid_container_) return;
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

    bottom_action_strip_ = core::app::makeExtmemUnique<ContextActionStrip>(
        body_container_,
        ContextActionStripOrientation::HORIZONTAL
    );
}

FLASHMEM void MacroView::createSlotPropertyOverlay() {
    if (!container_) return;
    slot_property_overlay_ =
        core::app::makeExtmemUnique<StepPropertySelectionOverlay>(container_);
}

FLASHMEM void MacroView::createMacros() {
    if (!macro_grid_container_) return;
    for (uint8_t i = 0; i < MACRO_COUNT; ++i) {
        uint8_t col = i % COLS;
        uint8_t row = i / COLS;

        macros_[i] = core::app::makeExtmemUnique<MacroKnobWidget>(macro_grid_container_);
        if (!macros_[i] || !macros_[i]->valid()) return;
        lv_obj_set_grid_cell(macros_[i]->getElement(),
            LV_GRID_ALIGN_STRETCH, col, 1,
            LV_GRID_ALIGN_STRETCH, row, 1);
    }
}

void MacroView::requestRender(uint32_t flags, bool ready) {
    if (render_scheduler_) render_scheduler_->request(flags, ready);
}

void MacroView::requestHeaderRender() {
    requestRender(RENDER_HEADER);
}

void MacroView::requestLeftActionStripRender() {
    requestRender(RENDER_LEFT_ACTION_STRIP);
}

void MacroView::requestBottomActionStripRender() {
    requestRender(RENDER_BOTTOM_ACTION_STRIP);
}

void MacroView::requestSlotPropertyOverlayRender() {
    requestRender(RENDER_SLOT_PROPERTY_OVERLAY);
}

bool MacroView::hasBlockingOverlay() const {
    return state_refs_.macroEdit.visible.get() ||
           state_refs_.viewSelector.visible.get() ||
           state_refs_.deviceSettings.visible.get() ||
           state_refs_.deviceSettings.selector.visible.get() ||
           state_refs_.dataManager.visible.get() ||
           state_refs_.dataManager.dialog.visible.get();
}

void MacroView::handleOverlayVisibilityChanged() {
    if (!render_scheduler_) return;
    if (hasBlockingOverlay()) {
        render_scheduler_->pause();
        return;
    }

    render_scheduler_->resumePending(true);
}

void MacroView::markAllDirty() {
    requestRender(RENDER_ALL);
}

void MacroView::markAllConfigDirty() {
    requestRender(RENDER_CONFIG_MASK);
}

void MacroView::markAutomationRecordingDirtyIfChanged() {
    const auto& recording = state_refs_.macroUi.automationRecording;
    const uint8_t activeTrack = state_refs_.pages.currentActiveTrack();
    const uint8_t activePage = state_refs_.pages.currentActivePage();
    uint32_t flags = 0;
    for (uint8_t i = 0; i < MACRO_COUNT; ++i) {
        const bool recordingThisSlot =
            state_refs_.pages.isMacroSlotActive(i) &&
            recording.active &&
            recording.address.track == activeTrack &&
            recording.address.page == activePage &&
            recording.address.macro == i;
        if (rendered_automation_recording_[i] != recordingThisSlot) {
            flags |= configRenderFlag(i);
        }
    }
    if (flags == 0) return;
    requestRender(flags | RENDER_HEADER);
}

void MacroView::markConfigDirtyIfChanged() {
    const auto frame = buildMacroViewFrameState(modelSource());
    uint32_t flags = 0;
    for (uint8_t i = 0; i < MACRO_COUNT; ++i) {
        const auto& props = frame.macros[i];
        if (rendered_ccs_[i] != props.cc ||
            rendered_active_[i] != props.active ||
            rendered_add_slot_[i] != props.addSlot ||
            rendered_focused_[i] != props.focused ||
            rendered_source_state_[i] != sourceStateBits(props) ||
            rendered_automation_recording_[i] != props.automationRecording ||
            rendered_automation_manual_override_[i] != props.automationManualOverride) {
            flags |= configRenderFlag(i);
        }
    }
    requestRender(flags);
}

void MacroView::markDirty(uint8_t index) {
    if (index < MACRO_COUNT) requestRender(valueRenderFlag(index));
}

bool MacroView::canDrainRender(void* context) {
    const auto* self = static_cast<const MacroView*>(context);
    return self && RetainedViewRenderPolicy::renderable(
        self->container_, self->hasBlockingOverlay()
    );
}

void MacroView::drainRender(void* context, uint32_t flags) {
    auto* self = static_cast<MacroView*>(context);
    if (self) self->processRenderFlags(flags);
}

void MacroView::processRenderFlags(uint32_t flags) {
    if (!RetainedViewRenderPolicy::renderable(container_, hasBlockingOverlay())) {
        requestRender(flags);
        return;
    }
    OC_PERF_SCOPE(perfRender, "ui.macro.render");

    const bool headerDirty = (flags & RENDER_HEADER) != 0;
    const bool leftActionStripDirty = (flags & RENDER_LEFT_ACTION_STRIP) != 0;
    const bool bottomActionStripDirty = (flags & RENDER_BOTTOM_ACTION_STRIP) != 0;
    const bool slotPropertyOverlayDirty =
        (flags & RENDER_SLOT_PROPERTY_OVERLAY) != 0;
    const bool hasValueDirty = (flags & RENDER_VALUE_MASK) != 0;
    const bool hasConfigDirty = (flags & RENDER_CONFIG_MASK) != 0;
    const bool needsStructuralInvalidationBatch =
        headerDirty || leftActionStripDirty || bottomActionStripDirty ||
        slotPropertyOverlayDirty || hasConfigDirty;

    const auto source = modelSource();
    oc::ui::lvgl::StaticSurfaceInvalidationBatch<> invalidation(
        container_, needsStructuralInvalidationBatch
    );

    if (headerDirty && header_bar_) {
        invalidation.include(header_bar_->getElement());
        header_bar_->render(buildMacroHeaderBarProps(source));
    }

    if (leftActionStripDirty && left_action_strip_) {
        invalidation.include(left_action_strip_->getElement());
        left_action_strip_->render(buildMacroLeftActionStripProps(source));
    }

    if (slotPropertyOverlayDirty && slot_property_overlay_) {
        invalidation.include(slot_property_overlay_->getElement());
        slot_property_overlay_->render(buildMacroSlotPropertyOverlayProps(source));
        invalidation.include(slot_property_overlay_->getElement());
    }

    if (bottomActionStripDirty && bottom_action_strip_) {
        invalidation.include(bottom_action_strip_->getElement());
        bottom_action_strip_->render(buildMacroBottomActionStripProps(source));
    }

    if (!hasValueDirty && !hasConfigDirty) {
        invalidation.flush();
        return;
    }

    MacroViewFrameState frame{};
    if (hasConfigDirty || hasValueDirty) {
        frame = buildMacroViewFrameState(source);
    }

    for (uint8_t i = 0; i < MACRO_COUNT; ++i) {
        const bool valueDirty = (flags & valueRenderFlag(i)) != 0;
        const bool configDirty = (flags & configRenderFlag(i)) != 0;
        if (valueDirty || configDirty) {
            if (macros_[i]) {
                // Hot value changes perform their own exact arc invalidation.
                // Structural batching is only required when retained labels,
                // visibility, focus, or source styling can change.
                if (configDirty) {
                    invalidation.include(macros_[i]->getElement());
                }
                if (valueDirty) {
                    const auto& props = frame.macros[i];
                    macros_[i]->setResolvedComponents(
                        props.baseValue,
                        props.modulationDelta,
                        props.modulationDepth,
                        props.value,
                        props.clippedLow,
                        props.clippedHigh
                    );
                }
                if (configDirty) {
                    const auto& props = frame.macros[i];
                    if (rendered_active_[i] != props.active ||
                        rendered_add_slot_[i] != props.addSlot) {
                        macros_[i]->setSlotState(props.active, props.addSlot);
                        rendered_active_[i] = props.active;
                        rendered_add_slot_[i] = props.addSlot;
                    }
                    if (rendered_ccs_[i] != props.cc) {
                        macros_[i]->setConfig(props.cc);
                        rendered_ccs_[i] = props.cc;
                    }
                    const uint8_t nextSourceState = sourceStateBits(props);
                    if (rendered_source_state_[i] != nextSourceState) {
                        macros_[i]->setSourceIndicators(
                            props.automationStored,
                            props.automationActive,
                            props.modulationStored,
                            props.modulationActive,
                            props.modulationPaused,
                            props.modulationSourceCount
                        );
                        rendered_source_state_[i] = nextSourceState;
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
                }
            }
        }
    }

    invalidation.flush();
}

MacroViewModelSource MacroView::modelSource() const {
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
