#include "MacroEditHandler.hpp"

#include <algorithm>
#include <cmath>

#include <config/App.hpp>
#include <config/PlatformCompat.hpp>
#include "handler/common/ModalSelectionUtils.hpp"
#include "handler/common/NavigationUtils.hpp"
#include "handler/macro/MacroGuardedActionWorkflow.hpp"

namespace core::handler {

namespace {

constexpr uint8_t ROW_CC = 0;
constexpr uint8_t ROW_AUTOMATION = 1;
constexpr uint8_t ROW_MODULATION = 2;
constexpr uint8_t ROW_COUNT = 3;

bool contextActionInProgress(const core::state::MacroEditState& state) {
    const auto phase = state.contextGuard.get().phase;
    return phase == core::state::contextual::GuardedActionPhase::PRESSED ||
           phase == core::state::contextual::GuardedActionPhase::ARMED ||
           phase == core::state::contextual::GuardedActionPhase::COMMITTED;
}

void cancelContextAction(
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
    uint8_t row
) {
    auto ref = slotRef(pages, macroIndex);
    if (row == ROW_AUTOMATION) {
        ref.kind = core::state::contextual::ContextEntityKind::AUTOMATION_LANE;
    } else if (row == ROW_MODULATION) {
        ref.kind = core::state::contextual::ContextEntityKind::MODULATION_LANE;
    }
    return ref;
}

float clampNormalized(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

uint16_t modulationAssignmentCount(
    const core::state::macro::MacroPagesState& pages,
    uint8_t macroIndex
) {
    const auto destination =
        core::state::modulation::projectControlDestination({
            .track = pages.currentActiveTrack(),
            .page = pages.currentActivePage(),
            .macro = macroIndex,
        });
    uint16_t count = 0U;
    for (uint16_t index = 0U;
         index < pages.control.authored.modulation.outputBindingCount;
         ++index) {
        if (pages.control.authored.modulation.outputBindings[index].destination ==
            destination) {
            ++count;
        }
    }
    return count;
}

const core::state::modulation::ModulationBindingState* modulationBindingAt(
    const core::state::macro::MacroPagesState& pages,
    uint8_t macroIndex,
    uint16_t ordinal
) {
    const auto destination =
        core::state::modulation::projectControlDestination({
            .track = pages.currentActiveTrack(),
            .page = pages.currentActivePage(),
            .macro = macroIndex,
        });
    uint16_t found = 0U;
    for (uint16_t index = 0U;
         index < pages.control.authored.modulation.outputBindingCount;
         ++index) {
        const auto& binding =
            pages.control.authored.modulation.outputBindings[index];
        if (binding.destination != destination) continue;
        if (found++ == ordinal) return &binding;
    }
    return nullptr;
}

}  // namespace

FLASHMEM MacroEditHandler::MacroEditHandler(
    StateRefs state,
    MacroEditDomainServices services,
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
                edit_entry_chord_active_ = true;
                openEdit(i);
            });
    }

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
        .then([this]() {
            if (edit_entry_chord_active_) {
                edit_entry_chord_active_ = false;
                macro_ui_.performanceOverlayMode.set(
                    core::state::macro::MacroPerformanceOverlayMode::NONE
                );
                return;
            }
            endContextSelector();
        });

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
        config.channel,
        config.cc,
        now_provider_ ? now_provider_() : 0
    );
    edit.pendingOpenReleaseDecision = false;

    overlays_.show(core::ui::OverlayType::MACRO_EDIT);

    configureOptForFocusedRow();

}

FLASHMEM void MacroEditHandler::closeOverlay() {
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
    edit_entry_chord_active_ = false;
}

FLASHMEM void MacroEditHandler::moveFocus(float delta) {
    if (macro_edit_.flowPhase.get() != core::state::MacroEditFlowPhase::EDIT) return;
    if (!nav::hasTurnDelta(delta)) return;
    cancelContextAction(
        macro_edit_, now_provider_ ? now_provider_() : 0U, true
    );

    const int current = static_cast<int>(macro_edit_.focusedRow.get());
    const int next = nav::nextWrappedIndex(delta, current, ROW_COUNT);
    if (current == ROW_CC && next != current) {
        commitEditedConfig();
    }
    if (current == ROW_MODULATION && next != current) {
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
    const uint8_t row = macro_edit_.focusedRow.get();
    const int count = valueCountForRow(row);

    const float clamped = clampNormalized(normalized);
    const int index = static_cast<int>(clamped * static_cast<float>(count - 1) + 0.5f);
    setValueForRow(row, index);
}

FLASHMEM void MacroEditHandler::openValueSelector() {
    auto& edit = macro_edit_;
    if (edit.flowPhase.get() != core::state::MacroEditFlowPhase::EDIT) return;
    cancelContextAction(
        macro_edit_, now_provider_ ? now_provider_() : 0U, false
    );

    const uint8_t row = macro_edit_.focusedRow.get();
    if (row == ROW_AUTOMATION) {
        services_.endDepthGesture();
        edit.openAutomation();
        overlays_.show(core::ui::OverlayType::MACRO_AUTOMATION, true);
        return;
    }
    if (row == ROW_MODULATION) {
        services_.endDepthGesture();
        uint8_t focusedRow = 0;
        core::state::modulation::ProjectControlMacroSlotView slot{};
        const uint8_t macroIndex = macro_edit_.editingIndex.get();
        const auto address = services_.automationAddress(macroIndex);
        if (core::state::modulation::readProjectControlMacroSlot(
                pages_.control,
                address,
                slot
            ) && slot.modulationCount > 0U) {
            const auto focused = services_.focusedModulationBinding(macroIndex);
            const auto destination =
                core::state::modulation::projectControlDestination(address);
            const uint8_t firstRow = 1U;
            uint16_t ordinal = 0;
            const auto& graph = pages_.control.authored.modulation;
            for (uint16_t bindingIndex = 0;
                 bindingIndex < graph.outputBindingCount;
                 ++bindingIndex) {
                const auto& binding = graph.outputBindings[bindingIndex];
                if (binding.destination != destination) continue;
                if (binding.id == focused) {
                    focusedRow = static_cast<uint8_t>(firstRow + ordinal);
                    break;
                }
                ++ordinal;
            }
        }
        edit.openModulation(focusedRow);
        overlays_.show(core::ui::OverlayType::MACRO_AUTOMATION, true);
        return;
    }

    edit.openValueSelector(row, valueForRow(row));
    overlays_.show(core::ui::OverlayType::MACRO_EDIT_SELECTOR, true);
}

FLASHMEM void MacroEditHandler::navigateValueSelector(float delta) {
    if (macro_edit_.flowPhase.get() != core::state::MacroEditFlowPhase::VALUE_SELECTOR) return;
    auto& selector = macro_edit_.selector;
    const int count = valueCountForRow(selector.editingRow.get());
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

    setValueForRow(selector.editingRow.get(), selector.selectedIndex.get());

    modal::hideIfCurrent(overlays_, core::ui::OverlayType::MACRO_EDIT_SELECTOR);
    macro_edit_.closeValueSelector();
    configureOptForFocusedRow();
}

FLASHMEM void MacroEditHandler::setValueForRow(uint8_t row, int value) {
    if (row == ROW_CC) {
        const int clamped = std::clamp(value, 0, 127);
        macro_edit_.tempCC.set(static_cast<uint8_t>(clamped));
        return;
    }

    const uint8_t index = macro_edit_.editingIndex.get();
    if (row == ROW_AUTOMATION && services_.automationStoredFor(index)) {
        const bool active = value != 0;
        (void)services_.setAutomationPlayback(index, active);
        if (active) (void)services_.resumeSources(index);
    } else if (row == ROW_MODULATION && services_.modulationStoredFor(index)) {
        const int clamped = std::clamp(value, 0, 100);
        (void)services_.setModulationDepth(
            index,
            static_cast<float>(clamped) / 100.0f
        );
    }
}

FLASHMEM int MacroEditHandler::valueForRow(uint8_t row) const {
    const uint8_t index = macro_edit_.editingIndex.get();
    if (row == ROW_AUTOMATION) {
        return services_.automationPlaybackActiveFor(index) &&
                !services_.manualOverrideActiveFor(index)
            ? 1
            : 0;
    }
    if (row == ROW_MODULATION) {
        return static_cast<int>(services_.modulationDepth(index) * 100.0f + 0.5f);
    }
    return static_cast<int>(macro_edit_.tempCC.get());
}

FLASHMEM int MacroEditHandler::valueCountForRow(uint8_t row) const {
    if (row == ROW_AUTOMATION) return 2;
    if (row == ROW_MODULATION) return 101;
    return 128;
}

FLASHMEM void MacroEditHandler::commitEditedConfig() {
    if (!macro_edit_.visible.get()) return;

    const uint8_t macroIndex = macro_edit_.editingIndex.get();
    const uint8_t channel = services_.activeConfig(macroIndex).channel;
    const uint8_t cc = macro_edit_.tempCC.get();

    services_.setConfig(macroIndex, channel, cc);
}

FLASHMEM void MacroEditHandler::configureOptForFocusedRow() {
    if (macro_edit_.contextSelectorActive.get()) {
        const int count = contextValueCount();
        const int current = std::clamp(contextValue(), 0, std::max(count - 1, 0));
        encoders_.setDiscreteSteps(
            static_cast<oc::type::EncoderID>(Config::EncoderID::OPT),
            static_cast<uint8_t>(std::clamp(count, 1, 255))
        );
        encoders_.setPosition(
            static_cast<oc::type::EncoderID>(Config::EncoderID::OPT),
            count > 1
                ? static_cast<float>(current) / static_cast<float>(count - 1)
                : 0.0f
        );
        return;
    }
    const uint8_t row = macro_edit_.focusedRow.get();
    const int count = valueCountForRow(row);
    const int current = valueForRow(row);

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
    if (macro_edit_.focusedRow.get() == ROW_MODULATION) {
        const auto focused = services_.focusedModulationBinding(
            macro_edit_.editingIndex.get()
        );
        const uint16_t count = modulationAssignmentCount(
            pages_,
            macro_edit_.editingIndex.get()
        );
        for (uint16_t ordinal = 0U; ordinal < count; ++ordinal) {
            const auto* binding = modulationBindingAt(
                pages_,
                macro_edit_.editingIndex.get(),
                ordinal
            );
            if (binding != nullptr && binding->id == focused) {
                selected = static_cast<uint8_t>(ordinal);
                break;
            }
        }
    }
    macro_edit_.contextPropertyIndex.set(selected);
    configureOptForFocusedRow();
}

FLASHMEM void MacroEditHandler::endContextSelector() {
    if (!macro_edit_.contextSelectorActive.get()) return;
    services_.endDepthGesture();
    macro_edit_.contextSelectorActive.set(false);
    configureOptForFocusedRow();
}

FLASHMEM int MacroEditHandler::contextPropertyCount() const {
    switch (macro_edit_.focusedRow.get()) {
        case ROW_AUTOMATION:
            return 4;
        case ROW_MODULATION:
            return static_cast<int>(modulationAssignmentCount(
                pages_,
                macro_edit_.editingIndex.get()
            )) + 1;
        case ROW_CC:
        default:
            return 2;
    }
}

FLASHMEM void MacroEditHandler::navigateContextProperty(float delta) {
    if (!macro_edit_.contextSelectorActive.get() || !nav::hasTurnDelta(delta)) {
        return;
    }
    services_.endDepthGesture();
    const int next = nav::nextWrappedIndex(
        delta,
        macro_edit_.contextPropertyIndex.get(),
        contextPropertyCount()
    );
    macro_edit_.contextPropertyIndex.set(static_cast<uint8_t>(next));
    configureOptForFocusedRow();
}

FLASHMEM int MacroEditHandler::contextValueCount() const {
    const uint8_t property = macro_edit_.contextPropertyIndex.get();
    switch (macro_edit_.focusedRow.get()) {
        case ROW_AUTOMATION:
            if (property == 0U || property == 3U) return 2;
            if (property == 1U) return 64;
            return 64;
        case ROW_MODULATION:
            return 201;
        case ROW_CC:
        default:
            return property == 0U ? 128 : 16;
    }
}

FLASHMEM int MacroEditHandler::contextValue() const {
    const uint8_t macroIndex = macro_edit_.editingIndex.get();
    const uint8_t property = macro_edit_.contextPropertyIndex.get();
    if (macro_edit_.focusedRow.get() == ROW_CC) {
        return property == 0U
            ? macro_edit_.tempCC.get()
            : services_.activeConfig(macroIndex).channel;
    }
    if (macro_edit_.focusedRow.get() == ROW_AUTOMATION) {
        const auto* slot = services_.automationSlot(macroIndex);
        if (property == 0U) {
            return services_.automationPlaybackActiveFor(macroIndex) &&
                    !services_.manualOverrideActiveFor(macroIndex)
                ? 1
                : 0;
        }
        if (property == 1U) {
            return slot != nullptr
                ? std::clamp<int>(
                    static_cast<int>(std::lround(
                        core::state::macro::macroAutomationBeatsFromTicks(
                            slot->automation.durationTicks
                        )
                    )),
                    1,
                    64
                ) - 1
                : 0;
        }
        if (property == 2U) {
            return slot != nullptr
                ? std::clamp<int>(
                    static_cast<int>(std::lround(
                        core::state::macro::macroAutomationBeatsFromTicks(
                            slot->automation.windowOffsetTicks
                        )
                    )),
                    0,
                    63
                )
                : 0;
        }
        return 0;
    }
    const uint16_t count = modulationAssignmentCount(pages_, macroIndex);
    if (property < count) {
        const auto* binding = modulationBindingAt(
            pages_,
            macroIndex,
            property
        );
        if (binding != nullptr) {
            return std::clamp<int>(
                static_cast<int>(std::lround(
                    (static_cast<float>(binding->amountQ15) / 32767.0f + 1.0f) *
                    100.0f
                )),
                0,
                200
            );
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
    const float clamped = clampNormalized(normalized);
    const uint8_t macroIndex = macro_edit_.editingIndex.get();
    const uint8_t property = macro_edit_.contextPropertyIndex.get();
    if (macro_edit_.focusedRow.get() == ROW_CC) {
        if (property == 0U) {
            macro_edit_.tempCC.set(static_cast<uint8_t>(std::lround(clamped * 127.0f)));
        } else {
            const uint8_t channel = static_cast<uint8_t>(std::lround(clamped * 15.0f));
            (void)services_.setConfig(
                macroIndex,
                channel,
                macro_edit_.tempCC.get()
            );
            macro_edit_.tempChannel.set(channel);
        }
        return;
    }
    if (macro_edit_.focusedRow.get() == ROW_AUTOMATION) {
        if (!services_.automationStoredFor(macroIndex)) return;
        if (property == 0U) {
            const bool active = clamped >= 0.5f;
            (void)services_.setAutomationPlayback(macroIndex, active);
            if (active) (void)services_.resumeSources(macroIndex);
        } else if (property == 1U) {
            (void)services_.setAutomationDurationBeats(
                macroIndex,
                1.0f + std::lround(clamped * 63.0f)
            );
        } else if (property == 2U) {
            (void)services_.setAutomationWindowOffsetBeats(
                macroIndex,
                std::lround(clamped * 63.0f)
            );
        } else if (clamped >= 0.5f) {
            const auto plan = services_.preflightConversion(
                macroIndex,
                core::state::macro::MacroAutomationConversionPolicy::MEAN
            );
            if (plan.actionable()) {
                macro_edit_.openConvertPreview(plan);
                overlays_.show(core::ui::OverlayType::MACRO_AUTOMATION, true);
            }
        }
        return;
    }
    const uint16_t count = modulationAssignmentCount(pages_, macroIndex);
    if (property < count) {
        const auto* binding = modulationBindingAt(pages_, macroIndex, property);
        if (binding == nullptr) return;
        (void)services_.focusModulationBinding(macroIndex, binding->id);
        (void)services_.setModulationDepth(macroIndex, clamped * 2.0f - 1.0f);
        return;
    }
    (void)services_.setModulationGlobalDepthQ15(
        macroIndex,
        static_cast<uint16_t>(std::lround(clamped * 65535.0f))
    );
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
    macro_edit_.loadActiveConfig(next, config.channel, config.cc);
    configureOptForFocusedRow();
}

FLASHMEM void MacroEditHandler::copyFocusedDomain() {
    if (macro_edit_.flowPhase.get() != core::state::MacroEditFlowPhase::EDIT) return;
    const uint8_t index = macro_edit_.editingIndex.get();
    const uint8_t row = macro_edit_.focusedRow.get();
    if (row == ROW_CC) {
        commitEditedConfig();
        (void)services_.copyDestination(index);
    } else if (row == ROW_AUTOMATION) {
        (void)services_.copyAutomation(index);
    } else if (row == ROW_MODULATION) {
        services_.endDepthGesture();
        (void)services_.copyModulation(index);
    }
}

FLASHMEM void MacroEditHandler::beginBottomRightAction() {
    if (macro_edit_.flowPhase.get() != core::state::MacroEditFlowPhase::EDIT) return;
    const uint8_t index = macro_edit_.editingIndex.get();
    const uint8_t row = macro_edit_.focusedRow.get();
    if (row == ROW_CC) commitEditedConfig();
    if (row == ROW_MODULATION) services_.endDepthGesture();
    const auto plan = row == ROW_CC
        ? services_.preflightDestinationPaste(index)
        : (row == ROW_AUTOMATION
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
        rowRef(pages_, index, row),
        rowRef(pages_, index, row),
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
    const uint8_t row = macro_edit_.focusedRow.get();
    if (row == ROW_CC) commitEditedConfig();
    if (row == ROW_MODULATION) services_.endDepthGesture();
    const bool sourceStored = row == ROW_AUTOMATION
        ? services_.automationStoredFor(index)
        : (row == ROW_MODULATION && services_.modulationStoredFor(index));
    const auto action = row == ROW_CC
        ? core::state::contextual::ContextActionId::REMOVE
        : (sourceStored ? core::state::contextual::ContextActionId::CLEAR
                        : core::state::contextual::ContextActionId::NONE);
    const auto target = rowRef(pages_, index, row);
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
        const uint8_t row = macro_edit_.focusedRow.get();
        if (row == ROW_AUTOMATION && services_.automationStoredFor(index)) {
            (void)services_.setAutomationPlayback(
                index,
                !services_.automationPlaybackActiveFor(index)
            );
            configureOptForFocusedRow();
        } else if (row == ROW_MODULATION &&
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
    const uint8_t row = macro_edit_.focusedRow.get();
    if (feedback.target != rowRef(pages_, index, row)) {
        macro::MacroGuardedActionWorkflow::complete(macro_edit_, false, nowMs);
        return;
    }

    bool applied = false;
    if (feedback.action == core::state::contextual::ContextActionId::REMOVE) {
        if (row != ROW_CC) {
            macro::MacroGuardedActionWorkflow::complete(macro_edit_, false, nowMs);
            return;
        }
        applied = services_.removeSlot(index);
        macro::MacroGuardedActionWorkflow::complete(macro_edit_, applied, nowMs);
        if (applied) closeOverlay();
        return;
    }
    if (feedback.action == core::state::contextual::ContextActionId::CLEAR) {
        if (row == ROW_AUTOMATION) {
            applied = services_.clearAutomation(index);
        } else if (row == ROW_MODULATION) {
            services_.endDepthGesture();
            applied = services_.clearModulation(index);
        }
        macro::MacroGuardedActionWorkflow::complete(macro_edit_, applied, nowMs);
        configureOptForFocusedRow();
        return;
    }
    if (feedback.action == core::state::contextual::ContextActionId::PASTE ||
        feedback.action == core::state::contextual::ContextActionId::OVERWRITE) {
        const auto plan = row == ROW_CC
            ? services_.preflightDestinationPaste(index)
            : (row == ROW_AUTOMATION
                   ? services_.preflightAutomationPaste(index)
                   : services_.preflightModulationPaste(index));
        const bool matchingPlan = plan.actionable() &&
            (plan.requiresOverwrite() ==
             (feedback.action == core::state::contextual::ContextActionId::OVERWRITE));
        if (matchingPlan) {
            if (row == ROW_CC) {
                applied = services_.pasteDestination(index, plan.requiresOverwrite());
            } else if (row == ROW_AUTOMATION) {
                applied = services_.pasteAutomation(index, plan.requiresOverwrite());
            } else {
                services_.endDepthGesture();
                applied = services_.pasteModulation(index, plan.requiresOverwrite());
            }
            if (applied && row == ROW_CC) {
                const auto& config = services_.activeConfig(index);
                macro_edit_.loadActiveConfig(index, config.channel, config.cc);
            }
            if (applied) configureOptForFocusedRow();
        }
    }
    macro::MacroGuardedActionWorkflow::complete(macro_edit_, applied, nowMs);
}

FLASHMEM void MacroEditHandler::update(uint32_t nowMs) {
    if (macro_edit_.flowPhase.get() != core::state::MacroEditFlowPhase::EDIT) return;
    // Reaching 100% only arms the release. Deferring mutation keeps this
    // overlay authoritative until the physical button release is consumed.
    (void)macro::MacroGuardedActionWorkflow::update(macro_edit_, nowMs);
}

}  // namespace core::handler
