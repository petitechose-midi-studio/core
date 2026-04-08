#include "MacroView.hpp"

#include <oc/log/Log.hpp>
#include <oc/ui/lvgl/style/StyleBuilder.hpp>
#include <config/App.hpp>
#include <config/PlatformCompat.hpp>
#include <config/TimeCompat.hpp>

#include "ui/view/MacroViewModelBuilder.hpp"
#include "ui/widget/MacroKnobWidget.hpp"

namespace core::ui {

namespace style = oc::ui::lvgl::style;

namespace {

#if defined(PERF_MON)
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
    createLayout(parent);
    createHeaderBar();
    createBottomControls();
    createActionStrips();
    createPropertyStrip();
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
    bottom_controls_.reset();
    property_strip_.reset();
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
        requestHeaderRender();
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
    subscriptions_.reserve(MACRO_COUNT + 16);

    subscriptions_.push_back(
        state_refs_.configRevision.subscribe([this](uint32_t) {
            markAllConfigDirty();
            requestHeaderRender();
        })
    );

    subscriptions_.push_back(
        state_refs_.statusBar.pageName.subscribe([this](const char*) {
            requestHeaderRender();
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
                requestPropertyStripRender();
            }
        )
    );

    subscriptions_.push_back(
        state_refs_.macroUi.clutchActive.subscribe([this](bool) {
            requestHeaderRender();
            requestLeftActionStripRender();
            requestPropertyStripRender();
        })
    );

    subscriptions_.push_back(
        state_refs_.macroUi.quickControlsSelecting.subscribe([this](bool) {
            requestLeftActionStripRender();
            requestBottomActionStripRender();
            scheduleUpdate();
        })
    );

    subscriptions_.push_back(
        state_refs_.macroUi.focusedQuickControl.subscribe([this](core::state::macro::MacroQuickControlItem) {
            scheduleUpdate();
        })
    );

    subscriptions_.push_back(
        state_refs_.macroUi.ccOffset.subscribe([this](int8_t) {
            scheduleUpdate();
        })
    );

    subscriptions_.push_back(
        state_refs_.macroUi.pageSelecting.subscribe([this](bool) {
            requestHeaderRender();
            requestLeftActionStripRender();
            requestBottomActionStripRender();
        })
    );

    subscriptions_.push_back(
        state_refs_.macroUi.selectedPage.subscribe([this](uint8_t) {
            requestHeaderRender();
            scheduleUpdate();
        })
    );

    subscriptions_.push_back(
        state_refs_.pages.enabledMask.subscribe([this](uint8_t) {
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

    header_dirty_ = true;
    left_action_strip_dirty_ = true;
    bottom_action_strip_dirty_ = true;
    property_strip_dirty_ = true;
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

FLASHMEM void MacroView::createBottomControls() {
    if (!body_container_) return;
    bottom_controls_ = std::make_unique<MacroBottomControls>(body_container_);
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

FLASHMEM void MacroView::createPropertyStrip() {
    if (!interaction_container_) return;
    property_strip_ = std::make_unique<MacroPropertyStrip>(interaction_container_);
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
    if (has_dirty_ || header_dirty_ || left_action_strip_dirty_ || bottom_action_strip_dirty_ ||
        property_strip_dirty_) return;
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

FLASHMEM void MacroView::requestPropertyStripRender() {
    property_strip_dirty_ = true;
    scheduleUpdate();
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
    property_strip_dirty_ = true;
    scheduleUpdate();
}

FLASHMEM void MacroView::markAllConfigDirty() {
    config_dirty_flags_.fill(true);
    has_dirty_ = true;
    scheduleUpdate();
}

FLASHMEM void MacroView::markDirty(uint8_t index) {
    if (index < MACRO_COUNT) {
        dirty_flags_[index] = true;
        has_dirty_ = true;
        scheduleUpdate();
    }
}

FLASHMEM void MacroView::processDirtyFlags() {
#if defined(PERF_MON)
    const uint32_t start_us = core::time_compat::micros();
#endif
    if (!container_ || lv_obj_has_flag(container_, LV_OBJ_FLAG_HIDDEN)) {
        pauseUpdateIfIdle();
        return;
    }

    const auto source = modelSource();

    if (header_dirty_ && header_bar_) {
        header_bar_->render(buildMacroHeaderBarProps(source));
        header_dirty_ = false;
    }

    if (left_action_strip_dirty_ && left_action_strip_) {
        left_action_strip_->render(buildMacroLeftActionStripProps(source));
        left_action_strip_dirty_ = false;
    }

    if (property_strip_dirty_ && property_strip_) {
        property_strip_->render(buildMacroPropertyStripProps(source));
        property_strip_dirty_ = false;
    }

    if (bottom_controls_) {
        bottom_controls_->render(buildMacroBottomControlsProps(source));
    }

    if (bottom_action_strip_dirty_ && bottom_action_strip_) {
        bottom_action_strip_->render(buildMacroBottomActionStripProps(source));
        bottom_action_strip_dirty_ = false;
    }

    if (!has_dirty_) {
        pauseUpdateIfIdle();
        return;
    }

    uint32_t value_updates = 0;
    uint32_t config_updates = 0;
    for (uint8_t i = 0; i < MACRO_COUNT; ++i) {
        if (dirty_flags_[i] || config_dirty_flags_[i]) {
            if (macros_[i]) {
                if (dirty_flags_[i]) {
                    macros_[i]->setValue(state_refs_.macros.slots[i].value.get());
                    dirty_flags_[i] = false;
                    value_updates += 1;
                }
                if (config_dirty_flags_[i]) {
                    const auto& config = state_refs_.pages.activeConfigs[i];
                    macros_[i]->setConfig(config.channel, config.cc);
                    config_dirty_flags_[i] = false;
                    config_updates += 1;
                }
            }
        }
    }

    has_dirty_ = false;
    pauseUpdateIfIdle();
#if defined(PERF_MON)
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
        .statusBar = state_refs_.statusBar,
    };
}

}  // namespace core::ui
