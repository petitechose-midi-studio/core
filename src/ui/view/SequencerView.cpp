#include "SequencerView.hpp"

#include <cstdio>

#include <oc/ui/lvgl/style/StyleBuilder.hpp>

#include "state/sequencer/StepPropertyDisplay.hpp"

namespace style = oc::ui::lvgl::style;

namespace core::ui {

SequencerView::SequencerView(lv_obj_t* parent, core::state::CoreState& coreState)
    : core_state_(coreState) {
    createLayout(parent);
    createHeaderBar();
    createGrid();
    bindToState();
}

SequencerView::~SequencerView() {
    if (render_timer_) {
        lv_timer_delete(render_timer_);
        render_timer_ = nullptr;
    }

    step_grid_.reset();
    property_strip_.reset();
    header_bar_.reset();
    layout_.reset();
    container_ = nullptr;
    body_container_ = nullptr;
}

void SequencerView::onActivate() {
    if (!container_) return;

    lv_obj_clear_flag(container_, LV_OBJ_FLAG_HIDDEN);

    if (!render_timer_) {
        render_timer_ = lv_timer_create(onRenderTimer, 16, this);
    }

    if (step_grid_) {
        step_grid_->forceRefresh();
    }
    header_dirty_ = true;
    strip_dirty_ = true;
    grid_dirty_ = true;
    render();

    if (body_container_) {
        lv_obj_update_layout(body_container_);
    }

    if (step_grid_) {
        step_grid_->forceRefresh();
    }
    header_dirty_ = true;
    strip_dirty_ = true;
    grid_dirty_ = true;
    render();

    dirty_ = false;
}

void SequencerView::onDeactivate() {
    if (container_) {
        lv_obj_add_flag(container_, LV_OBJ_FLAG_HIDDEN);
    }

    if (render_timer_) {
        lv_timer_delete(render_timer_);
        render_timer_ = nullptr;
    }
}

lv_obj_t* SequencerView::getPropertySelectorScopeElement() const {
    return property_strip_ ? property_strip_->getSelectorScopeElement() : nullptr;
}

void SequencerView::setPropertySelectorScopeVisible(bool visible) {
    if (property_strip_) {
        property_strip_->setSelectorScopeVisible(visible);
    }
}

void SequencerView::createLayout(lv_obj_t* parent) {
    layout_ = std::make_unique<ms::ui::LayoutView>(parent);
    container_ = layout_->getElement();
    body_container_ = layout_->content();

    style::apply(body_container_).transparent().pad(0);
    lv_obj_set_layout(body_container_, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(body_container_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(body_container_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(body_container_, 0, 0);
}

void SequencerView::createHeaderBar() {
    if (!layout_) return;
    header_bar_ = std::make_unique<SequencerHeaderBar>(layout_->header());
}

void SequencerView::createGrid() {
    if (!body_container_) return;

    property_strip_ = std::make_unique<StepPropertyStrip>(body_container_);
    step_grid_ = std::make_unique<StepGrid>(body_container_, core_state_);
}

void SequencerView::bindToState() {
    watcher_.watchAll(
        [this]() {
            header_dirty_ = true;
            grid_dirty_ = true;
            strip_dirty_ = true;
            requestRender();
        },
        core_state_.sequencer.length,
        core_state_.sequencer.stepsPerBeat,
        core_state_.sequencer.page,
        core_state_.sequencer.activeStepProperty,
        core_state_.sequencer.enabledMask,
        core_state_.sequencer.playheadStep,
        core_state_.sequencer.stepDataRevision
    );

    watcher_.watchAll(
        [this]() { requestHeaderRender(); },
        core_state_.sequencer.focusedStep
    );

    watcher_.watchAll(
        [this]() { requestStripRender(); },
        core_state_.sequencer.propertySelector.visible,
        core_state_.sequencer.propertySelector.selectedIndex
    );

    header_dirty_ = true;
    strip_dirty_ = true;
    grid_dirty_ = true;
    render();
    dirty_ = false;
}

void SequencerView::renderHeader(
    uint8_t len,
    uint8_t page,
    uint8_t focused,
    uint64_t enabledMask,
    int16_t playhead,
    core::state::sequencer::StepProperty property
) {
    const bool focusedInPattern = (len > 0) && (focused < len);
    const bool focusedEnabled = focusedInPattern && ((enabledMask & (1ULL << focused)) != 0);

    char focusedValue[16];
    std::snprintf(focusedValue, sizeof(focusedValue), "--");
    if (focusedInPattern) {
        core::state::sequencer::formatStepPropertyValue(
            focusedValue,
            sizeof(focusedValue),
            property,
            core_state_.sequencer.note[focused],
            core_state_.sequencer.velocity[focused],
            core_state_.sequencer.gate[focused],
            core_state_.sequencer.nudge[focused],
            core_state_.sequencer.probability[focused]
        );
    }

    char leftText[24];
    std::snprintf(
        leftText,
        sizeof(leftText),
        "%s",
        core::state::sequencer::stepPropertyName(property)
    );

    char rightText[16];
    if (focusedInPattern) {
        std::snprintf(rightText, sizeof(rightText), "Step %u", static_cast<unsigned>(focused) + 1);
    } else {
        std::snprintf(rightText, sizeof(rightText), "Step --");
    }

    if (!header_bar_) return;

    header_bar_->render({
        .length = len,
        .viewedPage = page,
        .playheadStep = playhead,
        .leftText = leftText,
        .centerText = focusedValue,
        .rightText = rightText,
        .dimmed = !focusedEnabled,
    });
}

void SequencerView::requestRender() {
    dirty_ = true;
}

void SequencerView::requestHeaderRender() {
    header_dirty_ = true;
    requestRender();
}

void SequencerView::requestStripRender() {
    strip_dirty_ = true;
    requestRender();
}

void SequencerView::requestGridRender() {
    grid_dirty_ = true;
    requestRender();
}

void SequencerView::onRenderTimer(lv_timer_t* timer) {
    auto* self = static_cast<SequencerView*>(lv_timer_get_user_data(timer));
    if (!self) return;

    if (!self->dirty_) return;
    if (!self->container_) return;
    if (lv_obj_has_flag(self->container_, LV_OBJ_FLAG_HIDDEN)) return;

    self->render();
    self->dirty_ = false;
}

void SequencerView::render() {
    if (!container_ || lv_obj_has_flag(container_, LV_OBJ_FLAG_HIDDEN)) return;

    const bool needsStrip = strip_dirty_ && property_strip_;
    const bool needsHeader = header_dirty_ && header_bar_;
    const bool needsGrid = grid_dirty_ && step_grid_;
    if (!needsStrip && !needsHeader && !needsGrid) {
        return;
    }

    const uint8_t len = needsHeader ? core_state_.sequencer.length.get() : 0;
    const uint8_t page =
        needsHeader ? core_state_.sequencer.normalizePage(core_state_.sequencer.page.get()) : 0;
    const uint64_t mask = needsHeader ? core_state_.sequencer.enabledMask.get() : 0;
    const uint8_t focused = needsHeader ? core_state_.sequencer.focusedStep.get() : 0;
    const int16_t playhead = needsHeader ? core_state_.sequencer.playheadStep.get() : -1;
    const auto property = (needsStrip || needsHeader)
        ? core_state_.sequencer.activeStepProperty.get()
        : core::state::sequencer::StepProperty::NOTE;

    if (needsStrip) {
        property_strip_->render({
            .activeProperty = property,
            .selecting = core_state_.sequencer.propertySelector.visible.get(),
            .selectedIndex = core_state_.sequencer.propertySelector.selectedIndex.get(),
        });
        strip_dirty_ = false;
    }

    if (needsHeader) {
        renderHeader(len, page, focused, mask, playhead, property);
        header_dirty_ = false;
    }

    if (needsGrid) {
        step_grid_->render();
        grid_dirty_ = false;
    }
}

}  // namespace core::ui
