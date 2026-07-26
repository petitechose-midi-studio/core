#include "MacroEditHandler.hpp"

#include <algorithm>
#include <cmath>

#include <config/App.hpp>
#include <config/PlatformCompat.hpp>
#include "handler/common/ModalSelectionUtils.hpp"
#include "handler/common/NavigationUtils.hpp"
#include "handler/macro/MacroAutomationTakeInputWorkflow.hpp"
#include "handler/macro/MacroGuardedActionWorkflow.hpp"
#include "state/macro/MacroEditMenuModel.hpp"
#include "state/modulation/ProjectControlRuntime.hpp"
#include "state/modulation/ProjectModulationDomainOps.hpp"
#include "ui/modulation/ModulationDepthUiModel.hpp"

namespace core::handler {

namespace {

namespace menu = core::state::macro;
namespace depth_ui = core::ui::modulation::depth;

bool contextActionInProgress(const core::state::MacroEditState& state) {
    const auto phase = state.contextGuard.get().phase;
    return phase == core::state::contextual::GuardedActionPhase::PRESSED ||
           phase == core::state::contextual::GuardedActionPhase::ARMED ||
           phase == core::state::contextual::GuardedActionPhase::COMMITTED;
}

FLASHMEM void cancelContextAction(
    core::state::MacroEditState& state,
    uint32_t nowMs,
    bool keepFeedback
) {
    if (!contextActionInProgress(state)) return;
    macro::MacroGuardedActionWorkflow::cancel(state, nowMs);
    if (!keepFeedback) {
        state.contextGuard.set({});
        state.contextFeedback.set({});
        state.contextButton.set(core::state::MacroContextButton::NONE);
    }
}

core::state::contextual::ContextEntityRef slotRef(
    const core::state::macro::MacroPagesState& pages,
    uint8_t macroIndex
) {
    return {
        .kind = core::state::contextual::ContextEntityKind::MACRO_SLOT,
        .track = pages.currentActiveTrack(),
        .page = pages.currentActivePage(),
        .item = macroIndex,
    };
}

core::state::contextual::ContextEntityRef rowRef(
    const core::state::macro::MacroPagesState& pages,
    uint8_t macroIndex,
    menu::MacroRootItem item
) {
    auto ref = slotRef(pages, macroIndex);
    if (item == menu::MacroRootItem::AUTOMATION) {
        ref.kind = core::state::contextual::ContextEntityKind::AUTOMATION_LANE;
    } else if (item == menu::MacroRootItem::MODULATION) {
        ref.kind = core::state::contextual::ContextEntityKind::MODULATION_LANE;
    }
    return ref;
}

float clampNormalized(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

menu::MacroModulationRows modulationRows(
    const core::state::macro::MacroPagesState& pages,
    uint8_t macroIndex
) {
    const auto destination =
        core::state::modulation::projectControlDestination({
            .track = pages.currentActiveTrack(),
            .page = pages.currentActivePage(),
            .macro = macroIndex,
        });
    return menu::buildMacroModulationRows(
        pages.control.authored.modulation,
        destination
    );
}

void markRecordedShapeProjectMutated(void* context) {
    auto* services = static_cast<MacroEditDomainServices*>(context);
    if (services != nullptr) services->markProjectMutated();
}

void publishRecordedShapeAudition(
    void* context,
    const core::state::modulation::ProjectRecordedShapeAuditionDescriptor&
        descriptor
) {
    auto* pages = static_cast<core::state::macro::MacroPagesState*>(context);
    if (pages == nullptr) return;
    using core::state::modulation::ProjectRecordedShapeCaptureMode;
    if (descriptor.mode == ProjectRecordedShapeCaptureMode::REPLACE_EXISTING) {
        (void)core::state::modulation::setProjectRecordedShapeSourceAudition(
            pages->control.runtime,
            descriptor.sourceId,
            descriptor.sourceValueQ15
        );
        return;
    }
    if (descriptor.mode == ProjectRecordedShapeCaptureMode::CREATE_ASSIGNED) {
        const uint16_t scale =
            core::state::modulation::projectModulationDestinationScaleQ15(
                pages->control.authored.modulation,
                descriptor.destination
            );
        (void)core::state::modulation::setProjectRecordedShapeDestinationAudition(
            pages->control.runtime,
            descriptor.destination,
            descriptor.amountQ15,
            descriptor.sourceValueQ15,
            scale
        );
    }
}

void clearRecordedShapeAudition(void* context) {
    auto* pages = static_cast<core::state::macro::MacroPagesState*>(context);
    if (pages != nullptr) {
        core::state::modulation::clearProjectRecordedShapeRuntimeAudition(
            pages->control.runtime
        );
    }
}

uint16_t recordedShapeDurationTicks(
    const MacroEditDomainServices& services,
    const core::state::macro::MacroUiState& macroUi,
    uint8_t macroIndex
) {
    const auto* slot = services.controlDestination(macroIndex);
    if (slot != nullptr && slot->automation.stored() &&
        slot->automation.spec.durationTicks > 0U) {
        return slot->automation.spec.durationTicks;
    }
    uint16_t duration = core::state::macro::macroAutomationTakeFixedDurationTicks(
        macroUi.automationTakeTiming.get()
    );
    if (duration == 0U) {
        duration = core::state::macro::macroAutomationTakeFixedDurationTicks(
            core::state::macro::MacroAutomationTakeTiming::BAR_1
        );
    }
    return duration;
}

}  // namespace

FLASHMEM MacroEditHandler::MacroEditHandler(
    StateRefs state,
    MacroEditDomainServices services,
    MacroPerformanceDomainServices performanceServices,
    MacroMidiCcRuntimeAdapter& midiRuntime,
    oc::context::OverlayManager<core::ui::OverlayType>& overlays,
    oc::api::EncoderAPI& encoders,
    oc::api::ButtonAPI& buttons,
    oc::type::ScopeID macroViewScope,
    oc::type::ScopeID overlayScope,
    oc::type::ScopeID selectorScope,
    NowProvider nowProvider
)
    : macro_edit_(state.macroEdit)
    , pages_(state.pages)
    , macro_ui_(state.macroUi)
    , services_(services)
    , performance_services_(performanceServices)
    , recorded_shape_capture_(
          ProjectRecordedShapeCaptureWorkflow::StateRefs{
              state.pages,
              state.macroUi,
              state.statusBar,
              state.history,
          },
          ProjectRecordedShapeCaptureWorkflow::Operations{
              .context = &services_,
              .auditionContext = &state.pages,
              .markProjectMutated = markRecordedShapeProjectMutated,
              .publishAudition = publishRecordedShapeAudition,
              .clearAudition = clearRecordedShapeAudition,
          }
      )
    , midi_runtime_(midiRuntime)
    , overlays_(overlays)
    , encoders_(encoders)
    , buttons_(buttons)
    , macro_view_scope_(macroViewScope)
    , overlay_scope_(overlayScope)
    , selector_scope_(selectorScope)
    , now_provider_(nowProvider)
{
    setupBindings();
}

FLASHMEM void MacroEditHandler::setupBindings() {
    const auto navButton = static_cast<oc::type::ButtonID>(Config::ButtonID::NAV);
    const auto leftTopButton = static_cast<oc::type::ButtonID>(Config::ButtonID::LEFT_TOP);
    const auto leftCenterButton = static_cast<oc::type::ButtonID>(Config::ButtonID::LEFT_CENTER);
    const auto leftBottomButton = static_cast<oc::type::ButtonID>(Config::ButtonID::LEFT_BOTTOM);
    const auto bottomLeftButton = static_cast<oc::type::ButtonID>(Config::ButtonID::BOTTOM_LEFT);
    const auto bottomRightButton = static_cast<oc::type::ButtonID>(Config::ButtonID::BOTTOM_RIGHT);

    const oc::type::ScopeID mainScope = overlay_scope_;
    const oc::type::ScopeID valueScope = selector_scope_;
    // LEFT_BOTTOM owns Edit intent before a Macro button is pressed. There is
    // no motion-sensitive long press and therefore no record/edit ambiguity.
    for (uint8_t i = 0; i < Config::MACRO_COUNT; ++i) {
        const auto macroButton = static_cast<oc::type::ButtonID>(Config::MACRO_BUTTONS[i]);

        buttons_.button(macroButton)
            .press()
            .scope(macro_view_scope_)
            .when([this, i]() {
                return macro_ui_.performanceOverlayMode.get() ==
                           core::state::macro::MacroPerformanceOverlayMode::EDIT &&
                       services_.isMacroSlotActive(i);
            })
            .then([this, i]() {
                openEdit(i);
            });
    }

    // The edit-intent overlay already carries a focused macro. NAV confirms
    // that target exactly like pressing its physical macro encoder, keeping
    // the learned "focus then confirm" grammar available one-handed.
    buttons_.button(navButton)
        .release()
        .scope(macro_view_scope_)
        .when([this]() {
            const uint8_t focused = macro_ui_.focusedMacroSlot.get();
            return macro_ui_.performanceOverlayMode.get() ==
                       core::state::macro::MacroPerformanceOverlayMode::EDIT &&
                   focused < Config::MACRO_COUNT &&
                   services_.isMacroSlotActive(focused);
        })
        .then([this]() {
            openEdit(macro_ui_.focusedMacroSlot.get());
        });

    // ===== MAIN MACRO EDIT OVERLAY SCOPE =====
    encoders_.encoder(static_cast<oc::type::EncoderID>(Config::EncoderID::NAV))
        .turn()
        .scope(mainScope)
        .when([this]() {
            return !macro_edit_.contextSelectorActive.get() &&
                   !macro_edit_.macroCycleActive.get();
        })
        .then([this](float delta) { moveFocus(delta); });

    encoders_.encoder(static_cast<oc::type::EncoderID>(Config::EncoderID::NAV))
        .turn()
        .scope(mainScope)
        .when([this]() { return macro_edit_.contextSelectorActive.get(); })
        .then([this](float delta) { navigateContextProperty(delta); });

    encoders_.encoder(static_cast<oc::type::EncoderID>(Config::EncoderID::NAV))
        .turn()
        .scope(mainScope)
        .when([this]() { return macro_edit_.macroCycleActive.get(); })
        .then([this](float delta) { cycleActiveMacro(delta); });

    encoders_.encoder(static_cast<oc::type::EncoderID>(Config::EncoderID::OPT))
        .turn()
        .scope(mainScope)
        .then([this](float normalized) { setFocusedValue(normalized); });

    buttons_.button(navButton)
        .release()
        .scope(mainScope)
        .when([this]() {
            return !macro_edit_.contextSelectorActive.get() &&
                   !macro_edit_.macroCycleActive.get();
        })
        .then([this]() { openValueSelector(); });

    buttons_.button(leftTopButton)
        .release()
        .scope(mainScope)
        .then([this]() { closeOverlay(); });

    buttons_.button(leftCenterButton)
        .press()
        .scope(mainScope)
        .then([this]() { beginMacroCycle(); });

    buttons_.button(leftCenterButton)
        .release()
        .scope(mainScope)
        .then([this]() { endMacroCycle(); });

    buttons_.button(leftBottomButton)
        .press()
        .scope(mainScope)
        .then([this]() { beginContextSelector(); });

    buttons_.button(leftBottomButton)
        .release()
        .scope(mainScope)
        .then([this]() { endContextSelector(); });

    buttons_.button(bottomRightButton)
        .press()
        .scope(mainScope)
        .then([this]() { beginBottomRightAction(); });

    buttons_.button(bottomRightButton)
        .release()
        .scope(mainScope)
        .then([this]() { releaseBottomRightAction(); });

    buttons_.button(bottomLeftButton)
        .press()
        .scope(mainScope)
        .then([this]() { beginBottomLeftAction(); });

    buttons_.button(bottomLeftButton)
        .release()
        .scope(mainScope)
        .then([this]() { releaseBottomLeftAction(); });

    // ===== VALUE SELECTOR OVERLAY SCOPE =====
    encoders_.encoder(static_cast<oc::type::EncoderID>(Config::EncoderID::NAV))
        .turn()
        .scope(valueScope)
        .then([this](float delta) { navigateValueSelector(delta); });

    buttons_.button(navButton)
        .release()
        .scope(valueScope)
        .then([this]() { applyValueSelectorAndClose(); });

    buttons_.button(leftTopButton)
        .release()
        .scope(valueScope)
        .then([this]() { closeOverlay(); });

}

FLASHMEM void MacroEditHandler::openEdit(uint8_t macroIndex) {
    if (!services_.isMacroSlotActive(macroIndex)) return;
    const auto& config = services_.activeConfig(macroIndex);

    auto& edit = macro_edit_;
    edit.openEditor(
        macroIndex,
        performance_services_.activeTrackChannel(),
        config.cc,
        now_provider_ ? now_provider_() : 0
    );
    edit.pendingOpenReleaseDecision = false;

    overlays_.show(core::ui::OverlayType::MACRO_EDIT);
    // The editor is now authoritative. Clear the performance-side entry
    // prompt immediately instead of waiting for a release whose press owner
    // may remain in the previous scope.
    macro_ui_.performanceOverlayMode.set(
        core::state::macro::MacroPerformanceOverlayMode::NONE
    );

    configureOptForFocusedRow();

}

FLASHMEM void MacroEditHandler::closeOverlay() {
    if (track_channel_gesture_active_) {
        (void)performance_services_.endTrackChannelGesture();
        track_channel_gesture_active_ = false;
    }
    if (context_record_active_) {
        (void)performance_services_.cancelAutomationTake();
        context_record_active_ = false;
    }
    if (context_recorded_shape_active_) {
        if (recorded_shape_capture_.active()) {
            (void)recorded_shape_capture_.cancel();
        }
        context_recorded_shape_active_ = false;
        publishRecordedShapeCaptureRevision();
    }
    services_.endDepthGesture();
    commitEditedConfig();

    // Close any stacked macro-edit related selector first, then the main overlay.
    modal::hideWhileCurrentIn(
        overlays_,
        std::array{
            core::ui::OverlayType::MACRO_EDIT_SELECTOR,
            core::ui::OverlayType::MACRO_AUTOMATION,
        }
    );
    modal::hideIfCurrent(overlays_, core::ui::OverlayType::MACRO_EDIT);

    macro_edit_.closeEditor();
    macro_ui_.performanceOverlayMode.set(
        core::state::macro::MacroPerformanceOverlayMode::NONE
    );
}

FLASHMEM void MacroEditHandler::moveFocus(float delta) {
    if (macro_edit_.flowPhase.get() != core::state::MacroEditFlowPhase::EDIT) return;
    if (!nav::hasTurnDelta(delta)) return;
    cancelContextAction(
        macro_edit_, now_provider_ ? now_provider_() : 0U, true
    );

    const int current = static_cast<int>(macro_edit_.focusedRow.get());
    const int next = nav::nextWrappedIndex(
        delta,
        current,
        menu::MACRO_ROOT_ITEM_COUNT
    );
    const auto currentItem = menu::macroRootItemAt(
        static_cast<uint8_t>(current)
    );
    if (currentItem == menu::MacroRootItem::DESTINATION && next != current) {
        commitEditedConfig();
    }
    if (currentItem == menu::MacroRootItem::MODULATION && next != current) {
        services_.endDepthGesture();
    }
    macro_edit_.focusedRow.set(static_cast<uint8_t>(next));

    configureOptForFocusedRow();
}

FLASHMEM void MacroEditHandler::setFocusedValue(float normalized) {
    if (macro_edit_.flowPhase.get() != core::state::MacroEditFlowPhase::EDIT) return;
    cancelContextAction(
        macro_edit_, now_provider_ ? now_provider_() : 0U, true
    );
    if (macro_edit_.contextSelectorActive.get()) {
        setContextValue(normalized);
        return;
    }
    const auto item = menu::macroRootItemAt(macro_edit_.focusedRow.get());
    if (item == menu::MacroRootItem::INVALID) return;
    const int count = valueCountForRow(item);

    const float clamped = clampNormalized(normalized);
    const int index = static_cast<int>(clamped * static_cast<float>(count - 1) + 0.5f);
    setValueForRow(item, index);
}

FLASHMEM void MacroEditHandler::openValueSelector() {
    auto& edit = macro_edit_;
    if (edit.flowPhase.get() != core::state::MacroEditFlowPhase::EDIT) return;
    cancelContextAction(
        macro_edit_, now_provider_ ? now_provider_() : 0U, false
    );

    const uint8_t row = macro_edit_.focusedRow.get();
    const auto item = menu::macroRootItemAt(row);
    if (item == menu::MacroRootItem::INVALID) return;
    if (item == menu::MacroRootItem::AUTOMATION) {
        services_.endDepthGesture();
        edit.openAutomation();
        overlays_.show(core::ui::OverlayType::MACRO_AUTOMATION, true);
        return;
    }
    if (item == menu::MacroRootItem::MODULATION) {
        services_.endDepthGesture();
        uint8_t focusedRow = 0;
        core::state::modulation::ProjectControlMacroDestinationView slot{};
        const uint8_t macroIndex = macro_edit_.editingIndex.get();
        const auto address = services_.automationAddress(macroIndex);
        if (core::state::modulation::readProjectControlMacroDestination(
                pages_.control,
                address,
                slot
            ) && slot.modulationCount > 0U) {
            const auto focused = services_.focusedModulationBinding(macroIndex);
            const auto& graph = pages_.control.authored.modulation;
            const auto rows = menu::buildMacroModulationRows(
                graph,
                core::state::modulation::projectControlDestination(address)
            );
            focusedRow = static_cast<uint8_t>(
                menu::macroModulationRowForBinding(graph, rows, focused)
            );
        }
        edit.openModulation(focusedRow);
        overlays_.show(core::ui::OverlayType::MACRO_AUTOMATION, true);
        return;
    }

    edit.openValueSelector(row, valueForRow(item));
    overlays_.show(core::ui::OverlayType::MACRO_EDIT_SELECTOR, true);
}

FLASHMEM void MacroEditHandler::navigateValueSelector(float delta) {
    if (macro_edit_.flowPhase.get() != core::state::MacroEditFlowPhase::VALUE_SELECTOR) return;
    auto& selector = macro_edit_.selector;
    const int count = valueCountForRow(
        menu::macroRootItemAt(selector.editingRow.get())
    );
    int next = selector.selectedIndex.get();
    if (!modal::advanceWrappedSelection(delta, selector, count, next)) {
        return;
    }
    selector.selectedIndex.set(next);
}

FLASHMEM void MacroEditHandler::applyValueSelectorAndClose() {
    if (macro_edit_.flowPhase.get() != core::state::MacroEditFlowPhase::VALUE_SELECTOR) return;
    auto& selector = macro_edit_.selector;
    if (!selector.visible.get()) return;

    setValueForRow(
        menu::macroRootItemAt(selector.editingRow.get()),
        selector.selectedIndex.get()
    );

    modal::hideIfCurrent(overlays_, core::ui::OverlayType::MACRO_EDIT_SELECTOR);
    macro_edit_.closeValueSelector();
    configureOptForFocusedRow();
}

FLASHMEM void MacroEditHandler::setValueForRow(
    menu::MacroRootItem item,
    int value
) {
    if (item == menu::MacroRootItem::DESTINATION) {
        const int clamped = std::clamp(value, 0, 127);
        macro_edit_.tempCC.set(static_cast<uint8_t>(clamped));
        return;
    }

    const uint8_t index = macro_edit_.editingIndex.get();
    if (item == menu::MacroRootItem::AUTOMATION &&
        services_.automationStoredFor(index)) {
        const bool active = value != 0;
        (void)services_.setAutomationPlayback(index, active);
        if (active) (void)services_.resumeSources(index);
    } else if (item == menu::MacroRootItem::MODULATION &&
               services_.modulationStoredFor(index)) {
        const int clamped = std::clamp(value, 0, 100);
        (void)services_.setModulationDepth(
            index,
            static_cast<float>(clamped) / 100.0f
        );
    }
}

FLASHMEM int MacroEditHandler::valueForRow(menu::MacroRootItem item) const {
    const uint8_t index = macro_edit_.editingIndex.get();
    if (item == menu::MacroRootItem::AUTOMATION) {
        return services_.automationPlaybackActiveFor(index) &&
                !services_.manualOverrideActiveFor(index)
            ? 1
            : 0;
    }
    if (item == menu::MacroRootItem::MODULATION) {
        return static_cast<int>(services_.modulationDepth(index) * 100.0f + 0.5f);
    }
    return item == menu::MacroRootItem::DESTINATION
        ? static_cast<int>(macro_edit_.tempCC.get())
        : 0;
}

FLASHMEM int MacroEditHandler::valueCountForRow(
    menu::MacroRootItem item
) const {
    if (item == menu::MacroRootItem::AUTOMATION) return 2;
    if (item == menu::MacroRootItem::MODULATION) return 101;
    return item == menu::MacroRootItem::DESTINATION ? 128 : 1;
}

FLASHMEM void MacroEditHandler::commitEditedConfig() {
    if (!macro_edit_.visible.get()) return;

    const uint8_t macroIndex = macro_edit_.editingIndex.get();
    const uint8_t channel = performance_services_.activeTrackChannel();
    const uint8_t cc = macro_edit_.tempCC.get();

    services_.setConfig(macroIndex, channel, cc);
}

FLASHMEM void MacroEditHandler::configureOptForFocusedRow() {
    if (macro_edit_.contextSelectorActive.get()) {
        const auto rows = modulationRows(
            pages_, macro_edit_.editingIndex.get()
        );
        const auto descriptor = menu::macroContextActionAt(
            pages_.control.authored.modulation,
            rows,
            menu::macroRootItemAt(macro_edit_.focusedRow.get()),
            macro_edit_.contextPropertyIndex.get()
        );
        const auto encoder =
            static_cast<oc::type::EncoderID>(Config::EncoderID::OPT);
        if (descriptor.action ==
            menu::MacroContextAction::MODULATION_RECORD_NEW_SHAPE) {
            encoders_.setMode(encoder, oc::interface::EncoderMode::RAW);
            encoders_.setPosition(encoder, 0.0f);
            return;
        }
        const int count = contextValueCount();
        const int current = std::clamp(contextValue(), 0, std::max(count - 1, 0));
        encoders_.setMode(encoder, oc::interface::EncoderMode::NORMALIZED);
        if (count > 255) {
            // Recorded Shape exposes 401 one-percent positions. The hardware
            // discrete-step API is uint8_t, so retain the full range with a
            // continuous encoder and quantize semantically in setContextValue.
            encoders_.setContinuous(encoder);
        } else {
            encoders_.setDiscreteSteps(
                encoder,
                static_cast<uint8_t>(std::clamp(count, 1, 255))
            );
        }
        encoders_.setPosition(
            encoder,
            count > 1
                ? static_cast<float>(current) / static_cast<float>(count - 1)
                : 0.0f
        );
        return;
    }
    const auto item = menu::macroRootItemAt(macro_edit_.focusedRow.get());
    if (item == menu::MacroRootItem::INVALID) return;
    const int count = valueCountForRow(item);
    const int current = valueForRow(item);

    encoders_.setMode(
        static_cast<oc::type::EncoderID>(Config::EncoderID::OPT),
        oc::interface::EncoderMode::NORMALIZED
    );
    encoders_.setDiscreteSteps(static_cast<oc::type::EncoderID>(Config::EncoderID::OPT),
                               static_cast<uint8_t>(count));

    const float position = (count > 1)
                               ? static_cast<float>(current) / static_cast<float>(count - 1)
                               : 0.0f;
    encoders_.setPosition(static_cast<oc::type::EncoderID>(Config::EncoderID::OPT), position);
}

FLASHMEM void MacroEditHandler::beginContextSelector() {
    if (macro_edit_.flowPhase.get() != core::state::MacroEditFlowPhase::EDIT ||
        macro_edit_.macroCycleActive.get()) {
        return;
    }
    macro_edit_.contextSelectorActive.set(true);
    uint8_t selected = 0U;
    const auto item = menu::macroRootItemAt(macro_edit_.focusedRow.get());
    if (item == menu::MacroRootItem::INVALID) {
        macro_edit_.contextSelectorActive.set(false);
        return;
    }
    if (item == menu::MacroRootItem::MODULATION) {
        const auto focused = services_.focusedModulationBinding(
            macro_edit_.editingIndex.get()
        );
        const auto rows = modulationRows(
            pages_,
            macro_edit_.editingIndex.get()
        );
        selected = menu::macroContextActionIndexForBinding(
            pages_.control.authored.modulation,
            rows,
            focused
        );
    }
    macro_edit_.contextPropertyIndex.set(selected);
    configureOptForFocusedRow();
}

FLASHMEM void MacroEditHandler::endContextSelector() {
    if (!macro_edit_.contextSelectorActive.get()) return;
    if (context_record_active_) {
        (void)performance_services_.releaseAutomationTake(
            now_provider_ ? now_provider_() : 0U
        );
        context_record_active_ = false;
    }
    if (context_recorded_shape_active_) {
        const auto result = recorded_shape_capture_.release(
            now_provider_ ? now_provider_() : 0U
        );
        context_recorded_shape_active_ = false;
        if (result.changed() &&
            core::state::modulation::valid(result.bindingId)) {
            (void)services_.focusModulationBinding(
                macro_edit_.editingIndex.get(),
                result.bindingId
            );
        }
        publishRecordedShapeCaptureRevision();
    }
    services_.endDepthGesture();
    if (track_channel_gesture_active_) {
        (void)performance_services_.endTrackChannelGesture();
        track_channel_gesture_active_ = false;
    }
    macro_edit_.contextSelectorActive.set(false);
    configureOptForFocusedRow();
}

FLASHMEM int MacroEditHandler::contextPropertyCount() const {
    const auto item = menu::macroRootItemAt(macro_edit_.focusedRow.get());
    const auto rows = modulationRows(pages_, macro_edit_.editingIndex.get());
    return menu::macroContextActionCount(item, rows.assignmentCount);
}

FLASHMEM void MacroEditHandler::navigateContextProperty(float delta) {
    if (!macro_edit_.contextSelectorActive.get() || !nav::hasTurnDelta(delta)) {
        return;
    }
    if (context_record_active_ || context_recorded_shape_active_) return;
    if (track_channel_gesture_active_) {
        (void)performance_services_.endTrackChannelGesture();
        track_channel_gesture_active_ = false;
    }
    services_.endDepthGesture();
    const int count = contextPropertyCount();
    if (count <= 0) return;
    const int next = nav::nextWrappedIndex(
        delta,
        macro_edit_.contextPropertyIndex.get(),
        count
    );
    macro_edit_.contextPropertyIndex.set(static_cast<uint8_t>(next));
    configureOptForFocusedRow();
}

FLASHMEM int MacroEditHandler::contextValueCount() const {
    const auto item = menu::macroRootItemAt(macro_edit_.focusedRow.get());
    const auto rows = modulationRows(pages_, macro_edit_.editingIndex.get());
    const auto descriptor = menu::macroContextActionAt(
        pages_.control.authored.modulation,
        rows,
        item,
        macro_edit_.contextPropertyIndex.get()
    );
    if (descriptor.action == menu::MacroContextAction::NONE) return 1;
    switch (descriptor.action) {
        case menu::MacroContextAction::DESTINATION_CC:
            return 128;
        case menu::MacroContextAction::DESTINATION_CHANNEL:
            return 16;
        case menu::MacroContextAction::AUTOMATION_RECORD:
            return 128;
        case menu::MacroContextAction::AUTOMATION_PLAYBACK:
        case menu::MacroContextAction::AUTOMATION_CONVERT:
            return 2;
        case menu::MacroContextAction::AUTOMATION_LENGTH:
        case menu::MacroContextAction::AUTOMATION_OFFSET:
            return 64;
        case menu::MacroContextAction::MODULATION_EDGE_DEPTH: {
            const auto* binding = menu::macroModulationBinding(
                pages_.control.authored.modulation,
                {menu::MacroModulationRowKind::ASSIGNMENT,
                 descriptor.bindingId,
                 rows.destination}
            );
            const auto scale = binding != nullptr
                ? depth_ui::scaleFor(
                      pages_.control.authored.modulation,
                      pages_.control.authored.curves,
                      *binding
                  )
                : depth_ui::Scale::STANDARD;
            return depth_ui::stepCount(scale);
        }
        case menu::MacroContextAction::MODULATION_GLOBAL_DEPTH:
            return 201;
        case menu::MacroContextAction::MODULATION_RECORD_NEW_SHAPE:
            return 1;
        case menu::MacroContextAction::NONE:
            return 1;
    }
    return 1;
}

FLASHMEM int MacroEditHandler::contextValue() const {
    const uint8_t macroIndex = macro_edit_.editingIndex.get();
    const auto item = menu::macroRootItemAt(macro_edit_.focusedRow.get());
    const auto rows = modulationRows(pages_, macroIndex);
    const auto descriptor = menu::macroContextActionAt(
        pages_.control.authored.modulation,
        rows,
        item,
        macro_edit_.contextPropertyIndex.get()
    );
    if (descriptor.action == menu::MacroContextAction::NONE) return 0;
    if (descriptor.action == menu::MacroContextAction::DESTINATION_CC) {
        return macro_edit_.tempCC.get();
    }
    if (descriptor.action == menu::MacroContextAction::DESTINATION_CHANNEL) {
        return performance_services_.activeTrackChannel();
    }
    if (item == menu::MacroRootItem::AUTOMATION) {
        const auto* slot = services_.controlDestination(macroIndex);
        if (descriptor.action == menu::MacroContextAction::AUTOMATION_RECORD) {
            return std::clamp<int>(
                static_cast<int>(std::lround(
                    performance_services_.absoluteBaseValue(macroIndex) * 127.0f
                )),
                0,
                127
            );
        }
        if (descriptor.action == menu::MacroContextAction::AUTOMATION_PLAYBACK) {
            return services_.automationPlaybackActiveFor(macroIndex) &&
                    !services_.manualOverrideActiveFor(macroIndex)
                ? 1
                : 0;
        }
        if (descriptor.action == menu::MacroContextAction::AUTOMATION_LENGTH) {
            return slot != nullptr
                ? std::clamp<int>(
                    static_cast<int>(std::lround(
                        core::state::macro::macroAutomationBeatsFromTicks(
                            slot->automation.spec.durationTicks
                        )
                    )),
                    1,
                    64
                ) - 1
                : 0;
        }
        if (descriptor.action == menu::MacroContextAction::AUTOMATION_OFFSET) {
            return slot != nullptr
                ? std::clamp<int>(
                    static_cast<int>(std::lround(
                        core::state::macro::macroAutomationBeatsFromTicks(
                            slot->automation.spec.windowOffsetTicks
                        )
                    )),
                    0,
                    63
                )
                : 0;
        }
        return 0;
    }
    if (descriptor.action == menu::MacroContextAction::MODULATION_EDGE_DEPTH) {
        const auto* binding = menu::macroModulationBinding(
            pages_.control.authored.modulation,
            {menu::MacroModulationRowKind::ASSIGNMENT,
             descriptor.bindingId,
             rows.destination}
        );
        if (binding != nullptr) {
            const auto scale = depth_ui::scaleFor(
                pages_.control.authored.modulation,
                pages_.control.authored.curves,
                *binding
            );
            return depth_ui::amountQ15ToPercent(binding->amountQ15, scale) +
                   depth_ui::maximumPercent(scale);
        }
    }
    return std::clamp<int>(
        static_cast<int>(std::lround(
            static_cast<float>(services_.modulationGlobalDepthQ15(macroIndex)) /
            65535.0f * 200.0f
        )),
        0,
        200
    );
}

FLASHMEM void MacroEditHandler::setContextValue(float normalized) {
    const uint8_t macroIndex = macro_edit_.editingIndex.get();
    const auto item = menu::macroRootItemAt(macro_edit_.focusedRow.get());
    const auto rows = modulationRows(pages_, macroIndex);
    const auto descriptor = menu::macroContextActionAt(
        pages_.control.authored.modulation,
        rows,
        item,
        macro_edit_.contextPropertyIndex.get()
    );
    if (descriptor.action == menu::MacroContextAction::NONE) return;
    if (descriptor.action ==
        menu::MacroContextAction::MODULATION_RECORD_NEW_SHAPE) {
        const int32_t rawPosition = static_cast<int32_t>(std::lround(normalized));
        const uint32_t nowMs = now_provider_ ? now_provider_() : 0U;
        if (!context_recorded_shape_active_) {
            if (!recorded_shape_capture_.armCreateAssigned(
                    nowMs,
                    recordedShapeDurationTicks(services_, macro_ui_, macroIndex),
                    services_.automationAddress(macroIndex),
                    ProjectRecordedShapeCaptureWorkflow::DEPTH_100_PERCENT_Q15
                )) {
                publishRecordedShapeCaptureRevision();
                return;
            }
            context_recorded_shape_active_ = true;
            (void)recorded_shape_capture_.configureRawEncoderOrigin(0);
        }
        if (!recorded_shape_capture_.touchRawEncoder(rawPosition, nowMs)) {
            (void)recorded_shape_capture_.cancel();
            context_recorded_shape_active_ = false;
            configureOptForFocusedRow();
        }
        publishRecordedShapeCaptureRevision();
        return;
    }
    const float clamped = clampNormalized(normalized);
    if (item == menu::MacroRootItem::DESTINATION) {
        if (descriptor.action == menu::MacroContextAction::DESTINATION_CC) {
            macro_edit_.tempCC.set(static_cast<uint8_t>(std::lround(clamped * 127.0f)));
        } else if (descriptor.action ==
                   menu::MacroContextAction::DESTINATION_CHANNEL) {
            const uint8_t channel = static_cast<uint8_t>(std::lround(clamped * 15.0f));
            if (!track_channel_gesture_active_) {
                track_channel_gesture_active_ =
                    performance_services_.beginTrackChannelGesture();
            }
            if (performance_services_.setTrackChannel(channel)) {
                macro_edit_.tempChannel.set(channel);
            }
        }
        return;
    }
    if (item == menu::MacroRootItem::AUTOMATION) {
        if (descriptor.action == menu::MacroContextAction::AUTOMATION_RECORD) {
            const float current = performance_services_.absoluteBaseValue(
                macroIndex
            );
            if (!context_record_active_ && std::abs(current - clamped) < 0.0005f) {
                return;
            }
            if (!context_record_active_) {
                const auto* slot = services_.controlDestination(macroIndex);
                uint16_t duration = slot != nullptr &&
                        slot->automation.stored()
                    ? slot->automation.spec.durationTicks
                    : core::state::macro::macroAutomationTakeFixedDurationTicks(
                          macro_ui_.automationTakeTiming.get()
                      );
                if (duration == 0U) {
                    duration = core::state::macro::macroAutomationTakeFixedDurationTicks(
                        core::state::macro::MacroAutomationTakeTiming::BAR_1
                    );
                }
                if (!performance_services_.armAutomationTakeForMacro(
                        macroIndex,
                        duration
                    )) {
                    return;
                }
                context_record_active_ = true;
            }
            if (!MacroAutomationTakeInputWorkflow::recordAndPublish(
                    performance_services_,
                    midi_runtime_,
                    macroIndex,
                    now_provider_ ? now_provider_() : 0U,
                    clamped
                )) {
                (void)performance_services_.cancelAutomationTake();
                context_record_active_ = false;
            }
            return;
        }
        if (!services_.automationStoredFor(macroIndex)) return;
        if (descriptor.action == menu::MacroContextAction::AUTOMATION_PLAYBACK) {
            const bool active = clamped >= 0.5f;
            (void)services_.setAutomationPlayback(macroIndex, active);
            if (active) (void)services_.resumeSources(macroIndex);
        } else if (descriptor.action ==
                   menu::MacroContextAction::AUTOMATION_LENGTH) {
            (void)services_.setAutomationDurationBeats(
                macroIndex,
                1.0f + std::lround(clamped * 63.0f)
            );
        } else if (descriptor.action ==
                   menu::MacroContextAction::AUTOMATION_OFFSET) {
            (void)services_.setAutomationWindowOffsetBeats(
                macroIndex,
                std::lround(clamped * 63.0f)
            );
        } else if (descriptor.action ==
                       menu::MacroContextAction::AUTOMATION_CONVERT &&
                   clamped >= 0.5f) {
            const auto plan = services_.preflightConversion(
                macroIndex,
                core::state::modulation::
                    ProjectAutomationConversionPolicy::MEAN
            );
            if (plan.actionable()) {
                macro_edit_.openConvertPreview(plan);
                overlays_.show(core::ui::OverlayType::MACRO_AUTOMATION, true);
            }
        }
        return;
    }
    if (descriptor.action == menu::MacroContextAction::MODULATION_EDGE_DEPTH) {
        const auto* binding = menu::macroModulationBinding(
            pages_.control.authored.modulation,
            {menu::MacroModulationRowKind::ASSIGNMENT,
             descriptor.bindingId,
             rows.destination}
        );
        if (binding == nullptr) return;
        const auto scale = depth_ui::scaleFor(
            pages_.control.authored.modulation,
            pages_.control.authored.curves,
            *binding
        );
        const int16_t amount = depth_ui::amountQ15AtNormalized(
            clamped,
            scale
        );
        (void)services_.focusModulationBinding(macroIndex, binding->id);
        (void)services_.setModulationDepth(
            macroIndex,
            static_cast<float>(amount) / 32767.0f
        );
        return;
    }
    if (descriptor.action == menu::MacroContextAction::MODULATION_GLOBAL_DEPTH) {
        (void)services_.setModulationGlobalDepthQ15(
            macroIndex,
            static_cast<uint16_t>(std::lround(clamped * 65535.0f))
        );
    }
}

FLASHMEM void MacroEditHandler::beginMacroCycle() {
    if (macro_edit_.flowPhase.get() != core::state::MacroEditFlowPhase::EDIT ||
        macro_edit_.contextSelectorActive.get()) {
        return;
    }
    macro_edit_.macroCycleActive.set(true);
}

FLASHMEM void MacroEditHandler::endMacroCycle() {
    macro_edit_.macroCycleActive.set(false);
}

FLASHMEM void MacroEditHandler::cycleActiveMacro(float delta) {
    if (!macro_edit_.macroCycleActive.get() || !nav::hasTurnDelta(delta)) return;
    const int direction = delta > 0.0f ? 1 : -1;
    const uint8_t current = macro_edit_.editingIndex.get();
    uint8_t next = current;
    for (uint8_t attempt = 0U; attempt < core::state::macro::MACRO_COUNT; ++attempt) {
        next = static_cast<uint8_t>(
            (static_cast<int>(next) + direction +
             core::state::macro::MACRO_COUNT) %
            core::state::macro::MACRO_COUNT
        );
        if (services_.isMacroSlotActive(next)) break;
    }
    if (next == current || !services_.isMacroSlotActive(next)) return;
    commitEditedConfig();
    services_.endDepthGesture();
    const auto& config = services_.activeConfig(next);
    macro_edit_.loadActiveConfig(
        next,
        performance_services_.activeTrackChannel(),
        config.cc
    );
    configureOptForFocusedRow();
}

FLASHMEM void MacroEditHandler::copyFocusedDomain() {
    if (macro_edit_.flowPhase.get() != core::state::MacroEditFlowPhase::EDIT) return;
    const uint8_t index = macro_edit_.editingIndex.get();
    const auto item = menu::macroRootItemAt(macro_edit_.focusedRow.get());
    if (item == menu::MacroRootItem::INVALID) return;
    if (item == menu::MacroRootItem::DESTINATION) {
        commitEditedConfig();
        (void)services_.copyDestination(index);
    } else if (item == menu::MacroRootItem::AUTOMATION) {
        (void)services_.copyAutomation(index);
    } else if (item == menu::MacroRootItem::MODULATION) {
        services_.endDepthGesture();
        (void)services_.copyModulation(index);
    }
}

FLASHMEM void MacroEditHandler::beginBottomRightAction() {
    if (macro_edit_.flowPhase.get() != core::state::MacroEditFlowPhase::EDIT) return;
    const uint8_t index = macro_edit_.editingIndex.get();
    const auto item = menu::macroRootItemAt(macro_edit_.focusedRow.get());
    if (item == menu::MacroRootItem::INVALID) return;
    if (item == menu::MacroRootItem::DESTINATION) commitEditedConfig();
    if (item == menu::MacroRootItem::MODULATION) services_.endDepthGesture();
    const auto plan = item == menu::MacroRootItem::DESTINATION
        ? services_.preflightDestinationPaste(index)
        : (item == menu::MacroRootItem::AUTOMATION
               ? services_.preflightAutomationPaste(index)
               : services_.preflightModulationPaste(index));
    const auto action = plan.actionable()
        ? (plan.requiresOverwrite()
               ? core::state::contextual::ContextActionId::OVERWRITE
               : core::state::contextual::ContextActionId::PASTE)
        : core::state::contextual::ContextActionId::NONE;
    (void)macro::MacroGuardedActionWorkflow::begin(
        macro_edit_,
        core::state::MacroContextButton::BOTTOM_RIGHT,
        action,
        rowRef(pages_, index, item),
        rowRef(pages_, index, item),
        now_provider_ ? now_provider_() : 0U,
        static_cast<uint16_t>(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS)
    );
}

FLASHMEM void MacroEditHandler::releaseBottomRightAction() {
    if (macro_edit_.flowPhase.get() != core::state::MacroEditFlowPhase::EDIT) return;
    const uint32_t nowMs = now_provider_ ? now_provider_() : 0U;
    const auto release = macro::MacroGuardedActionWorkflow::release(
        macro_edit_,
        core::state::MacroContextButton::BOTTOM_RIGHT,
        nowMs
    );
    if (release == core::state::contextual::GuardedActionRelease::TAP) {
        copyFocusedDomain();
    } else if (release ==
               core::state::contextual::GuardedActionRelease::COMMITTED) {
        commitGuardedAction(nowMs);
    }
}

FLASHMEM void MacroEditHandler::beginBottomLeftAction() {
    if (macro_edit_.flowPhase.get() != core::state::MacroEditFlowPhase::EDIT) return;
    const uint8_t index = macro_edit_.editingIndex.get();
    const auto item = menu::macroRootItemAt(macro_edit_.focusedRow.get());
    if (item == menu::MacroRootItem::INVALID) return;
    if (item == menu::MacroRootItem::DESTINATION) commitEditedConfig();
    if (item == menu::MacroRootItem::MODULATION) services_.endDepthGesture();
    const bool sourceStored = item == menu::MacroRootItem::AUTOMATION
        ? services_.automationStoredFor(index)
        : (item == menu::MacroRootItem::MODULATION &&
           services_.modulationStoredFor(index));
    const auto action = item == menu::MacroRootItem::DESTINATION
        ? core::state::contextual::ContextActionId::REMOVE
        : (sourceStored ? core::state::contextual::ContextActionId::CLEAR
                        : core::state::contextual::ContextActionId::NONE);
    const auto target = rowRef(pages_, index, item);
    (void)macro::MacroGuardedActionWorkflow::begin(
        macro_edit_,
        core::state::MacroContextButton::BOTTOM_LEFT,
        action,
        target,
        target,
        now_provider_ ? now_provider_() : 0U,
        static_cast<uint16_t>(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS)
    );
}

FLASHMEM void MacroEditHandler::releaseBottomLeftAction() {
    if (macro_edit_.flowPhase.get() != core::state::MacroEditFlowPhase::EDIT) return;
    const uint32_t nowMs = now_provider_ ? now_provider_() : 0U;
    const auto release = macro::MacroGuardedActionWorkflow::release(
            macro_edit_,
            core::state::MacroContextButton::BOTTOM_LEFT,
            nowMs
        );
    if (release == core::state::contextual::GuardedActionRelease::TAP) {
        const uint8_t index = macro_edit_.editingIndex.get();
        const auto item = menu::macroRootItemAt(macro_edit_.focusedRow.get());
        if (item == menu::MacroRootItem::AUTOMATION &&
            services_.automationStoredFor(index)) {
            (void)services_.setAutomationPlayback(
                index,
                !services_.automationPlaybackActiveFor(index)
            );
            configureOptForFocusedRow();
        } else if (item == menu::MacroRootItem::MODULATION &&
                   services_.modulationStoredFor(index)) {
            (void)services_.setModulationPlayback(
                index,
                !services_.modulationPlaybackActiveFor(index)
            );
        }
    } else if (release ==
               core::state::contextual::GuardedActionRelease::COMMITTED) {
        commitGuardedAction(nowMs);
    }
}

FLASHMEM void MacroEditHandler::commitGuardedAction(uint32_t nowMs) {
    const auto feedback = macro_edit_.contextFeedback.get();
    const uint8_t index = macro_edit_.editingIndex.get();
    const auto item = menu::macroRootItemAt(macro_edit_.focusedRow.get());
    if (item == menu::MacroRootItem::INVALID) {
        macro::MacroGuardedActionWorkflow::complete(macro_edit_, false, nowMs);
        return;
    }
    if (feedback.target != rowRef(pages_, index, item)) {
        macro::MacroGuardedActionWorkflow::complete(macro_edit_, false, nowMs);
        return;
    }

    bool applied = false;
    if (feedback.action == core::state::contextual::ContextActionId::REMOVE) {
        if (item != menu::MacroRootItem::DESTINATION) {
            macro::MacroGuardedActionWorkflow::complete(macro_edit_, false, nowMs);
            return;
        }
        applied = services_.removeSlot(index);
        macro::MacroGuardedActionWorkflow::complete(macro_edit_, applied, nowMs);
        if (applied) closeOverlay();
        return;
    }
    if (feedback.action == core::state::contextual::ContextActionId::CLEAR) {
        if (item == menu::MacroRootItem::AUTOMATION) {
            applied = services_.clearAutomation(index);
        } else if (item == menu::MacroRootItem::MODULATION) {
            services_.endDepthGesture();
            applied = services_.clearModulation(index);
        }
        macro::MacroGuardedActionWorkflow::complete(macro_edit_, applied, nowMs);
        configureOptForFocusedRow();
        return;
    }
    if (feedback.action == core::state::contextual::ContextActionId::PASTE ||
        feedback.action == core::state::contextual::ContextActionId::OVERWRITE) {
        const auto plan = item == menu::MacroRootItem::DESTINATION
            ? services_.preflightDestinationPaste(index)
            : (item == menu::MacroRootItem::AUTOMATION
                   ? services_.preflightAutomationPaste(index)
                   : services_.preflightModulationPaste(index));
        const bool matchingPlan = plan.actionable() &&
            (plan.requiresOverwrite() ==
             (feedback.action == core::state::contextual::ContextActionId::OVERWRITE));
        if (matchingPlan) {
            if (item == menu::MacroRootItem::DESTINATION) {
                applied = services_.pasteDestination(index, plan.requiresOverwrite());
            } else if (item == menu::MacroRootItem::AUTOMATION) {
                applied = services_.pasteAutomation(index, plan.requiresOverwrite());
            } else {
                services_.endDepthGesture();
                applied = services_.pasteModulation(index, plan.requiresOverwrite());
            }
            if (applied && item == menu::MacroRootItem::DESTINATION) {
                const auto& config = services_.activeConfig(index);
                macro_edit_.loadActiveConfig(
                    index,
                    performance_services_.activeTrackChannel(),
                    config.cc
                );
            }
            if (applied) configureOptForFocusedRow();
        }
    }
    macro::MacroGuardedActionWorkflow::complete(macro_edit_, applied, nowMs);
}

FLASHMEM void MacroEditHandler::update(uint32_t nowMs) {
    if (macro_edit_.flowPhase.get() != core::state::MacroEditFlowPhase::EDIT) {
        if (context_recorded_shape_active_) {
            if (recorded_shape_capture_.active()) {
                (void)recorded_shape_capture_.cancel();
            }
            context_recorded_shape_active_ = false;
            publishRecordedShapeCaptureRevision();
        }
        return;
    }
    if (context_recorded_shape_active_) {
        if (!recorded_shape_capture_.active() ||
            !recorded_shape_capture_.sample(nowMs)) {
            context_recorded_shape_active_ = false;
            configureOptForFocusedRow();
        }
        publishRecordedShapeCaptureRevision();
    }
    // Reaching 100% only arms the release. Deferring mutation keeps this
    // overlay authoritative until the physical button release is consumed.
    (void)macro::MacroGuardedActionWorkflow::update(macro_edit_, nowMs);
}

FLASHMEM void MacroEditHandler::publishRecordedShapeCaptureRevision() {
    const uint32_t revision = recorded_shape_capture_.revision();
    if (macro_ui_.recordedShapeCaptureRevision.get() != revision) {
        macro_ui_.recordedShapeCaptureRevision.set(revision);
    }
}

}  // namespace core::handler
