#include "SequencerPropertySelectorHandler.hpp"

#include <algorithm>

#include <oc/log/Log.hpp>
#include <oc/ui/lvgl/Scope.hpp>

#include <config/InputIDs.hpp>

namespace core::handler {

using oc::ui::lvgl::scope;
using ButtonID = Config::ButtonID;
using EncoderID = Config::EncoderID;

namespace {
constexpr int PROPERTY_COUNT = 3;  // NOTE, VEL, GATE

int wrapIndex(int idx, int count) {
    if (count <= 0) return 0;
    idx %= count;
    if (idx < 0) idx += count;
    return idx;
}

}  // namespace

SequencerPropertySelectorHandler::SequencerPropertySelectorHandler(
    core::state::CoreState& state,
    oc::context::OverlayManager<core::ui::OverlayType>& overlays,
    oc::api::EncoderAPI& encoders,
    oc::api::ButtonAPI& buttons,
    lv_obj_t* sequencerViewScope,
    lv_obj_t* overlayScope
)
    : state_(state)
    , overlays_(overlays)
    , encoders_(encoders)
    , buttons_(buttons)
    , sequencer_view_scope_(sequencerViewScope)
    , overlay_scope_(overlayScope) {
    setupBindings();
}

void SequencerPropertySelectorHandler::setupBindings() {
    // Open selector (latch toggle)
    buttons_.button(ButtonID::LEFT_BOTTOM)
        .press()
        .latch()
        .scope(scope(sequencer_view_scope_))
        .then([this]() { open(); });

    // Close + apply on release
    buttons_.button(ButtonID::LEFT_BOTTOM)
        .release()
        .scope(scope(overlay_scope_))
        .then([this]() { closeApply(); });

    // Navigate selection
    encoders_.encoder(EncoderID::NAV)
        .turn()
        .scope(scope(overlay_scope_))
        .then([this](float delta) { navigate(delta); });

    // Apply without closing
    buttons_.button(ButtonID::NAV)
        .release()
        .scope(scope(overlay_scope_))
        .then([this]() { applySelection(); });

    // Cancel + close
    buttons_.button(ButtonID::LEFT_TOP)
        .release()
        .scope(scope(overlay_scope_))
        .then([this]() { closeCancel(); });

    OC_LOG_DEBUG("[SequencerPropertySelectorHandler] Bindings setup complete");
}

void SequencerPropertySelectorHandler::open() {
    OC_LOG_DEBUG("[SequencerPropertySelectorHandler] open");

    auto& o = state_.sequencer.propertySelector;
    o.reset();

    const int active = static_cast<int>(state_.sequencer.activeStepProperty.get());
    o.snapshotIndex = active;
    o.snapshotValid = true;
    o.selectedIndex.set(active);

    overlays_.show(core::ui::OverlayType::SEQ_PROPERTY_SELECTOR, false);
}

void SequencerPropertySelectorHandler::applySelection() {
    int idx = state_.sequencer.propertySelector.selectedIndex.get();
    idx = std::clamp(idx, 0, PROPERTY_COUNT - 1);
    state_.sequencer.activeStepProperty.set(static_cast<core::state::sequencer::StepProperty>(idx));
}

void SequencerPropertySelectorHandler::navigate(float delta) {
    if (delta == 0.0f) return;
    const int step = (delta > 0.0f) ? 1 : -1;

    const int current = state_.sequencer.propertySelector.selectedIndex.get();
    const int next = wrapIndex(current + step, PROPERTY_COUNT);
    state_.sequencer.propertySelector.selectedIndex.set(next);
}

void SequencerPropertySelectorHandler::closeApply() {
    applySelection();
    overlays_.hide();
    state_.sequencer.propertySelector.reset();
}

void SequencerPropertySelectorHandler::closeCancel() {
    auto& o = state_.sequencer.propertySelector;
    if (o.snapshotValid) {
        state_.sequencer.propertySelector.selectedIndex.set(o.snapshotIndex);
        state_.sequencer.activeStepProperty.set(
            static_cast<core::state::sequencer::StepProperty>(std::clamp(o.snapshotIndex, 0, PROPERTY_COUNT - 1))
        );
    }
    overlays_.hide();
    state_.sequencer.propertySelector.reset();
}

}  // namespace core::handler
