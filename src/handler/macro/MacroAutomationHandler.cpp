#include "handler/macro/MacroAutomationHandler.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include <config/InputIDs.hpp>
#include <config/PlatformCompat.hpp>
#include <config/Timing.hpp>

#include "handler/common/ModalSelectionUtils.hpp"
#include "handler/common/ModulatorNavigationWorkflow.hpp"
#include "handler/common/NavigationUtils.hpp"
#include "handler/macro/MacroAutomationEditorModel.hpp"
#include "handler/macro/MacroGuardedActionWorkflow.hpp"
#include "state/contextual/OperationFeedbackState.hpp"
#include "state/macro/MacroEditMenuModel.hpp"
#include "state/macro/MacroSourceDetailPolicy.hpp"
#include "state/modulation/ModulationDepthParameterMapping.hpp"

namespace core::handler {

namespace {

namespace detail_policy = core::state::macro;
namespace menu = core::state::macro;
namespace depth_parameter = core::state::modulation::depth;

FLASHMEM detail_policy::MacroSourceDetailContext detailContext(
    const MacroEditDomainServices& services,
    uint8_t macroIndex
) {
    const auto* slot = services.controlDestination(macroIndex);
    if (slot == nullptr) return {};
    return {
        .automationStored = slot->automation.stored(),
        .modulationStored = services.modulationStoredFor(macroIndex),
        .automationPlayback =
            slot->automation.stored() && slot->automation.enabled,
        .modulationPlayback = services.modulationPlaybackActiveFor(macroIndex),
        .manualOverride = services.manualOverrideActiveFor(macroIndex),
    };
}

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

core::state::contextual::ContextEntityRef sourceRef(
    const core::state::macro::MacroPagesState& pages,
    uint8_t macroIndex,
    bool modulation
) {
    auto ref = slotRef(pages, macroIndex);
    ref.kind = modulation
        ? core::state::contextual::ContextEntityKind::MODULATION_LANE
        : core::state::contextual::ContextEntityKind::AUTOMATION_LANE;
    return ref;
}

FLASHMEM menu::MacroModulationRows modulationRows(
    const core::state::macro::MacroPagesState& pages,
    uint8_t macroIndex
) {
    const auto destination = core::state::modulation::projectControlDestination({
        .track = pages.currentActiveTrack(),
        .page = pages.currentActivePage(),
        .macro = macroIndex,
    });
    return menu::buildMacroModulationRows(
        pages.control.authored.modulation,
        destination
    );
}

FLASHMEM menu::MacroModulationRowDescriptor modulationRowAt(
    const core::state::macro::MacroPagesState& pages,
    const menu::MacroModulationRows& rows,
    int row
) {
    return menu::macroModulationRowAt(
        pages.control.authored.modulation,
        rows,
        row
    );
}

FLASHMEM const core::state::modulation::ModulationBindingState*
bindingAtModulationRow(
    const core::state::macro::MacroPagesState& pages,
    const menu::MacroModulationRows& rows,
    int row
) {
    return menu::macroModulationBinding(
        pages.control.authored.modulation,
        modulationRowAt(pages, rows, row)
    );
}

}  // namespace

FLASHMEM MacroAutomationHandler::MacroAutomationHandler(
    StateRefs state,
    MacroEditDomainServices services,
    oc::context::OverlayManager<core::ui::OverlayType>& overlays,
    oc::api::EncoderAPI& encoders,
    oc::api::ButtonAPI& buttons,
    oc::type::ScopeID automationScope,
    NowProvider nowProvider
)
    : overlays_state_(state.overlays)
    , active_view_(state.activeView)
    , project_navigation_(state.projectNavigation)
    , macro_edit_(state.macroEdit)
    , pages_(state.pages)
    , project_tracks_(state.projectTracks)
    , services_(services)
    , overlays_(overlays)
    , encoders_(encoders)
    , buttons_(buttons)
    , automation_scope_(automationScope)
    , now_provider_(nowProvider)
    , macro_view_was_active_(
          state.activeView.get() == core::ui::ViewType::MACRO
      ) {
    setupBindings();
}

FLASHMEM void MacroAutomationHandler::setupBindings() {
    using ButtonID = Config::ButtonID;
    using EncoderID = Config::EncoderID;

    encoders_.encoder(EncoderID::NAV)
        .turn()
        .scope(automation_scope_)
        .then([this](float delta) {
            if (conversionPreviewActive()) selectConversionPolicy(delta);
            else moveFocus(delta);
        });

    buttons_.button(ButtonID::NAV)
        .release()
        .scope(automation_scope_)
        .then([this]() { activateFocusedRow(); });

    encoders_.encoder(EncoderID::OPT)
        .turn()
        .scope(automation_scope_)
        .then([this](float normalized) { editFocusedValue(normalized); });

    buttons_.button(ButtonID::LEFT_TOP)
        .release()
        .scope(automation_scope_)
        .then([this]() { backToMacroEdit(); });

    buttons_.button(ButtonID::LEFT_CENTER)
        .press()
        .scope(automation_scope_)
        .then([this]() { setCoarseEditActive(true); });

    buttons_.button(ButtonID::LEFT_CENTER)
        .release()
        .scope(automation_scope_)
        .then([this]() { setCoarseEditActive(false); });

    buttons_.button(ButtonID::BOTTOM_LEFT)
        .press()
        .scope(automation_scope_)
        .then([this]() { beginBottomLeftAction(); });

    buttons_.button(ButtonID::BOTTOM_LEFT)
        .release()
        .scope(automation_scope_)
        .then([this]() { releaseBottomLeftAction(); });

    buttons_.button(ButtonID::BOTTOM_RIGHT)
        .press()
        .scope(automation_scope_)
        .then([this]() { beginBottomRightAction(); });

    buttons_.button(ButtonID::BOTTOM_RIGHT)
        .release()
        .scope(automation_scope_)
        .then([this]() { releaseBottomRightAction(); });
}

bool MacroAutomationHandler::active() const {
    return macro_edit_.visible.get() &&
           (automationDetailActive() || modulationDetailActive() ||
             conversionPreviewActive() || modulatorCreateActive() ||
             modulatorPickerActive());
}

bool MacroAutomationHandler::automationDetailActive() const {
    return macro_edit_.flowPhase.get() ==
           core::state::MacroEditFlowPhase::AUTOMATION;
}

bool MacroAutomationHandler::modulationDetailActive() const {
    return macro_edit_.flowPhase.get() ==
           core::state::MacroEditFlowPhase::MODULATION;
}

bool MacroAutomationHandler::conversionPreviewActive() const {
    return macro_edit_.flowPhase.get() ==
           core::state::MacroEditFlowPhase::CONVERT_PREVIEW;
}

bool MacroAutomationHandler::modulatorCreateActive() const {
    return macro_edit_.flowPhase.get() ==
           core::state::MacroEditFlowPhase::MODULATOR_CREATE;
}

bool MacroAutomationHandler::modulatorPickerActive() const {
    return macro_edit_.flowPhase.get() ==
           core::state::MacroEditFlowPhase::MODULATOR_PICKER;
}

uint8_t MacroAutomationHandler::macroIndex() const {
    return macro_edit_.editingIndex.get();
}

FLASHMEM void MacroAutomationHandler::moveFocus(float delta) {
    if (!active() || !nav::hasTurnDelta(delta)) return;
    cancelContextAction(
        macro_edit_, now_provider_ ? now_provider_() : 0U, true
    );
    services_.endDepthGesture();
    if (modulatorPickerActive()) {
        const int count = static_cast<int>(
            pages_.control.authored.modulation.sourceCount
        );
        if (count <= 0) return;
        const int current = macro_edit_.modulatorPickerIndex.get();
        macro_edit_.modulatorPickerIndex.set(
            nav::nextWrappedIndex(delta, current, count)
        );
        configureOptForFocusedRow();
        return;
    }
    auto& focus = (modulationDetailActive() || modulatorCreateActive())
        ? macro_edit_.modulationFocusedRow
        : macro_edit_.automationFocusedRow;
    const int current = static_cast<int>(focus.get());
    const auto context = detailContext(services_, macroIndex());
    int count = 0;
    if (modulatorCreateActive()) {
        count = 3;
    } else if (modulationDetailActive() && !context.modulationStored) {
        count = 4;
    } else if (modulationDetailActive()) {
        count = modulationRows(pages_, macroIndex()).rowCount();
    } else {
        count = static_cast<int>(
            detail_policy::buildAutomationDetailPolicy(context).count
        );
    }
    const int next = nav::nextWrappedIndex(delta, current, count);
    focus.set(static_cast<uint8_t>(next));
    if (modulationDetailActive() && context.modulationStored) {
        const auto rows = modulationRows(pages_, macroIndex());
        const auto* binding = bindingAtModulationRow(pages_, rows, next);
        if (binding != nullptr) {
            (void)services_.focusModulationBinding(
                macroIndex(),
                binding->id
            );
        }
    }
    configureOptForFocusedRow();
}

FLASHMEM void MacroAutomationHandler::editFocusedValue(float normalized) {
    if (!active()) return;
    cancelContextAction(
        macro_edit_, now_provider_ ? now_provider_() : 0U, true
    );
    const uint8_t index = macroIndex();
    const float clamped = std::clamp(normalized, 0.0f, 1.0f);
    if (modulatorPickerActive() || modulatorCreateActive()) return;
    const auto context = detailContext(services_, index);
    if (modulationDetailActive()) {
        if (!context.modulationStored) return;
        const auto rows = modulationRows(pages_, index);
        const auto row = modulationRowAt(
            pages_, rows, macro_edit_.modulationFocusedRow.get()
        );
        if (row.kind == menu::MacroModulationRowKind::ALL) {
            const auto scaled = static_cast<uint16_t>(std::lround(
                clamped * static_cast<float>(
                    std::numeric_limits<uint16_t>::max()
                )
            ));
            (void)services_.setModulationGlobalDepthQ15(index, scaled);
            return;
        }
        const auto* binding = menu::macroModulationBinding(
            pages_.control.authored.modulation,
            row
        );
        if (binding == nullptr) return;
        const auto scale = depth_parameter::scaleFor(
            pages_.control.authored.modulation,
            pages_.control.authored.curves,
            *binding
        );
        const int16_t amount = depth_parameter::amountQ15AtNormalized(
            clamped,
            scale
        );
        (void)services_.focusModulationBinding(index, binding->id);
        (void)services_.setModulationDepth(
            index,
            static_cast<float>(amount) / 32767.0f
        );
        return;
    }
    if (!automationDetailActive()) return;
    const auto policy = detail_policy::buildAutomationDetailPolicy(context);
    const auto item = policy.at(macro_edit_.automationFocusedRow.get());
    if (item == detail_policy::AutomationDetailItem::PLAYBACK) {
        (void)services_.setAutomationPlayback(index, clamped >= 0.5f);
        return;
    }
    if (item == detail_policy::AutomationDetailItem::LENGTH) {
        const auto range = macroAutomationLengthEditRange(coarse_edit_active_);
        services_.setAutomationDurationBeats(
            index,
            macroAutomationEncoderPositionToBeat(clamped, range)
        );
        return;
    }
    if (item == detail_policy::AutomationDetailItem::OFFSET) {
        const auto* slot = services_.controlDestination(index);
        const auto range = macroAutomationOffsetEditRange(
            slot != nullptr ? &slot->automation : nullptr,
            coarse_edit_active_
        );
        services_.setAutomationWindowOffsetBeats(
            index,
            macroAutomationEncoderPositionToBeat(clamped, range)
        );
    }
}

FLASHMEM void MacroAutomationHandler::configureOptForFocusedRow() {
    uint8_t steps = 1;
    float position = 0.0f;
    if (modulatorPickerActive() || modulatorCreateActive()) {
        encoders_.setDiscreteSteps(Config::EncoderID::OPT, 1);
        encoders_.setPosition(Config::EncoderID::OPT, 0.0f);
        return;
    }
    const auto context = detailContext(services_, macroIndex());
    if (modulationDetailActive()) {
        if (!context.modulationStored) {
            encoders_.setDiscreteSteps(Config::EncoderID::OPT, 1);
            encoders_.setPosition(Config::EncoderID::OPT, 0.0f);
            return;
        }
        const auto rows = modulationRows(pages_, macroIndex());
        const auto row = modulationRowAt(
            pages_, rows, macro_edit_.modulationFocusedRow.get()
        );
        if (row.kind == menu::MacroModulationRowKind::ALL) {
            steps = 201U;
            position = static_cast<float>(
                services_.modulationGlobalDepthQ15(macroIndex())
            ) / static_cast<float>(std::numeric_limits<uint16_t>::max());
            encoders_.setDiscreteSteps(Config::EncoderID::OPT, steps);
            encoders_.setPosition(Config::EncoderID::OPT, position);
            return;
        }
        const auto* binding = menu::macroModulationBinding(
            pages_.control.authored.modulation,
            row
        );
        if (binding != nullptr) {
            (void)services_.focusModulationBinding(macroIndex(), binding->id);
            const auto scale = depth_parameter::scaleFor(
                pages_.control.authored.modulation,
                pages_.control.authored.curves,
                *binding
            );
            position =
                depth_parameter::normalizedPosition(binding->amountQ15);
            const int depthSteps = depth_parameter::stepCount(scale);
            if (depthSteps > 255) {
                encoders_.setContinuous(Config::EncoderID::OPT);
            } else {
                encoders_.setDiscreteSteps(
                    Config::EncoderID::OPT,
                    static_cast<uint8_t>(depthSteps)
                );
            }
            encoders_.setPosition(Config::EncoderID::OPT, position);
            return;
        }
        encoders_.setDiscreteSteps(Config::EncoderID::OPT, steps);
        encoders_.setPosition(Config::EncoderID::OPT, position);
        return;
    }
    const auto policy = detail_policy::buildAutomationDetailPolicy(context);
    const auto item = policy.at(macro_edit_.automationFocusedRow.get());
    if (item == detail_policy::AutomationDetailItem::PLAYBACK) {
        steps = 2;
        position = context.automationPlayback ? 1.0f : 0.0f;
    } else if (item == detail_policy::AutomationDetailItem::LENGTH) {
        const auto range = macroAutomationLengthEditRange(coarse_edit_active_);
        steps = range.stepCount;
        const auto* slot = services_.controlDestination(macroIndex());
        if (slot != nullptr && slot->automation.stored()) {
            position = macroAutomationTicksToEncoderPosition(
                slot->automation.spec.durationTicks,
                range
            );
        }
    } else if (item == detail_policy::AutomationDetailItem::OFFSET) {
        const auto* slot = services_.controlDestination(macroIndex());
        const auto range = macroAutomationOffsetEditRange(
            slot != nullptr ? &slot->automation : nullptr,
            coarse_edit_active_
        );
        steps = range.stepCount;
        if (slot != nullptr && slot->automation.stored()) {
            position = macroAutomationTicksToEncoderPosition(
                slot->automation.spec.windowOffsetTicks,
                range
            );
        }
    }
    encoders_.setDiscreteSteps(Config::EncoderID::OPT, steps);
    encoders_.setPosition(Config::EncoderID::OPT, position);
}

FLASHMEM void MacroAutomationHandler::setCoarseEditActive(bool active) {
    if (!this->active() || coarse_edit_active_ == active) return;
    coarse_edit_active_ = active;
    configureOptForFocusedRow();
}

FLASHMEM void MacroAutomationHandler::backToMacroEdit() {
    if (!active()) return;
    cancelContextAction(
        macro_edit_, now_provider_ ? now_provider_() : 0U, false
    );
    services_.endDepthGesture();
    if (modulatorCreateActive()) {
        const auto rows = modulationRows(pages_, macroIndex());
        macro_edit_.closeModulatorCreate(rows.addSourceRow());
        configureOptForFocusedRow();
        return;
    }
    if (modulatorPickerActive()) {
        const auto rows = modulationRows(pages_, macroIndex());
        macro_edit_.closeModulatorPicker(rows.addSourceRow());
        configureOptForFocusedRow();
        return;
    }
    if (conversionPreviewActive()) {
        macro_edit_.closeConvertPreview();
        configureOptForFocusedRow();
        return;
    }
    modal::hideIfCurrent(overlays_, core::ui::OverlayType::MACRO_AUTOMATION);
    if (modulationDetailActive()) macro_edit_.closeModulation();
    else macro_edit_.closeAutomation();
    // A cross-view deep-link restores the detail child without retaining a
    // hidden parent in the overlay stack. Materialize that parent only when
    // Back actually returns to Macro Edit.
    if (!overlays_.hasVisible() && macro_edit_.visible.get()) {
        overlays_.show(core::ui::OverlayType::MACRO_EDIT, false);
    }
}

FLASHMEM void MacroAutomationHandler::toggleFocusedPlayback() {
    if (!active()) return;
    if (conversionPreviewActive() || modulatorCreateActive() ||
        modulatorPickerActive()) return;
    const uint8_t index = macroIndex();
    if (modulationDetailActive()) {
        const auto rows = modulationRows(pages_, index);
        const int row = macro_edit_.modulationFocusedRow.get();
        const auto descriptor = modulationRowAt(pages_, rows, row);
        if (descriptor.kind == menu::MacroModulationRowKind::ALL) {
            (void)services_.setModulationPlayback(
                index,
                !services_.modulationPlaybackActiveFor(index)
            );
        } else {
            const auto* binding = menu::macroModulationBinding(
                pages_.control.authored.modulation,
                descriptor
            );
            if (binding != nullptr) {
                (void)services_.focusModulationBinding(index, binding->id);
                const bool enabled =
                    (binding->flags & core::state::modulation::
                        PROJECT_MODULATION_BINDING_FLAG_ENABLED) != 0U;
                (void)services_.setFocusedModulationPlayback(index, !enabled);
            }
        }
    } else if (services_.automationStoredFor(index)) {
        (void)services_.setAutomationPlayback(
            index,
            !services_.automationPlaybackActiveFor(index)
        );
    }
    configureOptForFocusedRow();
}

FLASHMEM void MacroAutomationHandler::copyFocusedSource() {
    if (!active()) return;
    if (conversionPreviewActive() || modulatorCreateActive() ||
        modulatorPickerActive()) return;
    if (modulationDetailActive()) {
        const uint8_t index = macroIndex();
        const auto rows = modulationRows(pages_, index);
        const auto* binding = bindingAtModulationRow(
            pages_, rows, macro_edit_.modulationFocusedRow.get()
        );
        if (binding == nullptr) return;
        (void)services_.focusModulationBinding(index, binding->id);
        (void)services_.copyModulation(macroIndex());
    } else {
        (void)services_.copyAutomation(macroIndex());
    }
}

FLASHMEM void MacroAutomationHandler::beginBottomLeftAction() {
    if (!active()) return;
    if (conversionPreviewActive() || modulatorCreateActive() ||
        modulatorPickerActive()) return;
    services_.endDepthGesture();
    const uint8_t index = macroIndex();
    const bool modulation = modulationDetailActive();
    macro_edit_.guardedModulationBinding = {};
    macro_edit_.guardedModulationRevision = modulation
        ? pages_.control.authoredRevision
        : 0U;
    core::state::contextual::ContextActionId action =
        core::state::contextual::ContextActionId::NONE;
    auto target = sourceRef(pages_, index, modulation);
    if (modulation) {
        const auto rows = modulationRows(pages_, index);
        const int row = macro_edit_.modulationFocusedRow.get();
        target.node = static_cast<uint16_t>(row);
        const auto descriptor = modulationRowAt(pages_, rows, row);
        if (descriptor.kind == menu::MacroModulationRowKind::ALL) {
            action = core::state::contextual::ContextActionId::CLEAR;
        } else if (descriptor.kind ==
                   menu::MacroModulationRowKind::ASSIGNMENT) {
            const auto* binding = menu::macroModulationBinding(
                pages_.control.authored.modulation,
                descriptor
            );
            if (binding != nullptr) {
                macro_edit_.guardedModulationBinding = binding->id;
                action = core::state::contextual::ContextActionId::REMOVE;
            }
        }
    } else if (services_.automationStoredFor(index)) {
        action = core::state::contextual::ContextActionId::CLEAR;
    }
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

FLASHMEM void MacroAutomationHandler::releaseBottomLeftAction() {
    if (!active()) return;
    const uint32_t nowMs = now_provider_ ? now_provider_() : 0U;
    const auto release = macro::MacroGuardedActionWorkflow::release(
        macro_edit_,
        core::state::MacroContextButton::BOTTOM_LEFT,
        nowMs
    );
    if (release == core::state::contextual::GuardedActionRelease::TAP) {
        toggleFocusedPlayback();
        macro_edit_.guardedModulationBinding = {};
        macro_edit_.guardedModulationRevision = 0U;
    } else if (release ==
               core::state::contextual::GuardedActionRelease::COMMITTED) {
        commitGuardedAction(nowMs);
    } else if (release !=
               core::state::contextual::GuardedActionRelease::NONE) {
        macro_edit_.guardedModulationBinding = {};
        macro_edit_.guardedModulationRevision = 0U;
    }
}

FLASHMEM void MacroAutomationHandler::beginBottomRightAction() {
    if (!active()) return;
    macro_edit_.guardedModulationBinding = {};
    macro_edit_.guardedModulationRevision = modulationDetailActive()
        ? pages_.control.authoredRevision
        : 0U;
    core::state::contextual::ContextActionId action =
        core::state::contextual::ContextActionId::NONE;
    if (modulatorCreateActive() || modulatorPickerActive()) {
        action = core::state::contextual::ContextActionId::NONE;
    } else if (conversionPreviewActive()) {
        if (macro_edit_.conversionPreview.plan.actionable()) {
            action = macro_edit_.conversionPreview.plan.overwritesModulation
                ? core::state::contextual::ContextActionId::OVERWRITE
                : core::state::contextual::ContextActionId::APPLY;
        }
    } else {
        bool pasteContext = true;
        if (modulationDetailActive()) {
            const auto rows = modulationRows(pages_, macroIndex());
            const auto descriptor = modulationRowAt(
                pages_,
                rows,
                macro_edit_.modulationFocusedRow.get()
            );
            pasteContext = services_.hasModulationAssignmentClipboard() &&
                (rows.assignmentCount == 0U ||
                 descriptor.kind ==
                     menu::MacroModulationRowKind::ASSIGNMENT);
            if (pasteContext && descriptor.kind ==
                    menu::MacroModulationRowKind::ASSIGNMENT) {
                const auto* binding = menu::macroModulationBinding(
                    pages_.control.authored.modulation,
                    descriptor
                );
                if (binding == nullptr) {
                    pasteContext = false;
                } else {
                    macro_edit_.guardedModulationBinding = binding->id;
                }
            }
        }
        if (pasteContext) {
            const auto plan = modulationDetailActive()
                ? services_.preflightModulationPaste(macroIndex())
                : services_.preflightAutomationPaste(macroIndex());
            if (plan.actionable()) {
                action = plan.requiresOverwrite()
                    ? core::state::contextual::ContextActionId::OVERWRITE
                    : core::state::contextual::ContextActionId::PASTE;
            }
        }
    }
    auto target = (conversionPreviewActive() ||
                   modulatorCreateActive() ||
                   modulatorPickerActive())
        ? sourceRef(pages_, macroIndex(), true)
        : sourceRef(pages_, macroIndex(), modulationDetailActive());
    if (modulationDetailActive()) {
        target.node = macro_edit_.modulationFocusedRow.get();
    }
    (void)macro::MacroGuardedActionWorkflow::begin(
        macro_edit_,
        core::state::MacroContextButton::BOTTOM_RIGHT,
        action,
        target,
        target,
        now_provider_ ? now_provider_() : 0U,
        static_cast<uint16_t>(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS)
    );
}

FLASHMEM void MacroAutomationHandler::releaseBottomRightAction() {
    if (!active()) return;
    const uint32_t nowMs = now_provider_ ? now_provider_() : 0U;
    const auto release = macro::MacroGuardedActionWorkflow::release(
        macro_edit_,
        core::state::MacroContextButton::BOTTOM_RIGHT,
        nowMs
    );
    if (release == core::state::contextual::GuardedActionRelease::TAP) {
        if (conversionPreviewActive()) {
            const bool applied = applyConversion(false);
            macro::MacroGuardedActionWorkflow::complete(
                macro_edit_, applied, nowMs
            );
        } else {
            copyFocusedSource();
        }
        macro_edit_.guardedModulationBinding = {};
        macro_edit_.guardedModulationRevision = 0U;
    } else if (release ==
               core::state::contextual::GuardedActionRelease::COMMITTED) {
        commitGuardedAction(nowMs);
    } else if (release !=
               core::state::contextual::GuardedActionRelease::NONE) {
        macro_edit_.guardedModulationBinding = {};
        macro_edit_.guardedModulationRevision = 0U;
    }
}

FLASHMEM void MacroAutomationHandler::commitGuardedAction(uint32_t nowMs) {
    const auto feedback = macro_edit_.contextFeedback.get();
    const uint8_t index = macroIndex();
    const auto complete = [this, nowMs](bool applied) {
        macro::MacroGuardedActionWorkflow::complete(
            macro_edit_, applied, nowMs
        );
        macro_edit_.guardedModulationBinding = {};
        macro_edit_.guardedModulationRevision = 0U;
    };
    const auto expectedTarget = (conversionPreviewActive() ||
                                 modulatorCreateActive() ||
                                 modulatorPickerActive())
        ? sourceRef(pages_, index, true)
        : sourceRef(pages_, index, modulationDetailActive());
    auto contextualTarget = expectedTarget;
    if (modulationDetailActive()) {
        contextualTarget.node = macro_edit_.modulationFocusedRow.get();
    }
    if (feedback.target != contextualTarget) {
        complete(false);
        return;
    }
    const bool guardedModulationMutation = modulationDetailActive() &&
        (feedback.action == core::state::contextual::ContextActionId::CLEAR ||
         feedback.action == core::state::contextual::ContextActionId::REMOVE ||
         feedback.action == core::state::contextual::ContextActionId::PASTE ||
         feedback.action == core::state::contextual::ContextActionId::OVERWRITE);
    if (guardedModulationMutation &&
        macro_edit_.guardedModulationRevision !=
            pages_.control.authoredRevision) {
        complete(false);
        return;
    }
    if (core::state::modulation::valid(
            macro_edit_.guardedModulationBinding
        )) {
        const auto rows = modulationRows(pages_, index);
        const auto descriptor = modulationRowAt(
            pages_,
            rows,
            macro_edit_.modulationFocusedRow.get()
        );
        if (descriptor.kind != menu::MacroModulationRowKind::ASSIGNMENT ||
            descriptor.bindingId != macro_edit_.guardedModulationBinding ||
            menu::macroModulationBinding(
                pages_.control.authored.modulation,
                descriptor
            ) == nullptr) {
            complete(false);
            return;
        }
    } else if (feedback.action ==
                   core::state::contextual::ContextActionId::REMOVE) {
        complete(false);
        return;
    }

    bool applied = false;
    if (feedback.action == core::state::contextual::ContextActionId::CLEAR &&
        !conversionPreviewActive()) {
        services_.endDepthGesture();
        applied = modulationDetailActive()
            ? services_.clearModulation(index)
            : services_.clearAutomation(index);
        if (applied && modulationDetailActive()) {
            macro_edit_.modulationFocusedRow.set(0);
        }
        complete(applied);
        configureOptForFocusedRow();
        return;
    }
    if (feedback.action == core::state::contextual::ContextActionId::REMOVE &&
        modulationDetailActive()) {
        services_.endDepthGesture();
        const auto beforeRows = modulationRows(pages_, index);
        const int row = macro_edit_.modulationFocusedRow.get();
        const auto selectedRow = modulationRowAt(pages_, beforeRows, row);
        const auto* selected = menu::macroModulationBinding(
            pages_.control.authored.modulation,
            selectedRow
        );
        auto nextRow = modulationRowAt(pages_, beforeRows, row + 1);
        if (nextRow.kind != menu::MacroModulationRowKind::ASSIGNMENT) {
            nextRow = modulationRowAt(pages_, beforeRows, row - 1);
        }
        const auto* next = menu::macroModulationBinding(
            pages_.control.authored.modulation,
            nextRow
        );
        const auto nextId = next != nullptr
            ? next->id
            : core::state::modulation::ModulationBindingId{};
        if (selected != nullptr) {
            (void)services_.focusModulationBinding(index, selected->id);
            applied = services_.removeFocusedModulation(index);
        }
        if (applied) {
            const auto afterRows = modulationRows(pages_, index);
            if (core::state::modulation::valid(nextId)) {
                (void)services_.focusModulationBinding(index, nextId);
                macro_edit_.modulationFocusedRow.set(static_cast<uint8_t>(
                    menu::macroModulationRowForBinding(
                        pages_.control.authored.modulation,
                        afterRows,
                        nextId
                    )
                ));
            } else {
                macro_edit_.modulationFocusedRow.set(0);
            }
        }
        complete(applied);
        configureOptForFocusedRow();
        return;
    }

    if (feedback.action == core::state::contextual::ContextActionId::OVERWRITE &&
        conversionPreviewActive()) {
        applied = applyConversion(true);
    } else if (feedback.action == core::state::contextual::ContextActionId::PASTE ||
               feedback.action == core::state::contextual::ContextActionId::OVERWRITE) {
        const bool expectedOverwrite =
            feedback.action == core::state::contextual::ContextActionId::OVERWRITE;
        if (modulationDetailActive()) {
            const auto plan = services_.preflightModulationPaste(index);
            if (plan.actionable() &&
                plan.requiresOverwrite() == expectedOverwrite) {
                applied = services_.pasteModulation(index, expectedOverwrite);
                if (applied) {
                    const auto rows = modulationRows(pages_, index);
                    const auto focused = services_.focusedModulationBinding(index);
                    if (core::state::modulation::valid(focused)) {
                        macro_edit_.modulationFocusedRow.set(
                            static_cast<uint8_t>(
                                menu::macroModulationRowForBinding(
                                    pages_.control.authored.modulation,
                                    rows,
                                    focused
                                )
                            )
                        );
                    }
                }
            }
        } else if (automationDetailActive()) {
            const auto plan = services_.preflightAutomationPaste(index);
            if (plan.actionable() &&
                plan.requiresOverwrite() == expectedOverwrite) {
                applied = services_.pasteAutomation(index, expectedOverwrite);
            }
        }
    }
    complete(applied);
    if (applied && !conversionPreviewActive()) configureOptForFocusedRow();
}

FLASHMEM void MacroAutomationHandler::update(uint32_t nowMs) {
    macro_edit_.updateModulatorNavigationFeedback(nowMs);
    const bool macroViewActive =
        active_view_.get() == core::ui::ViewType::MACRO;
    const bool macroViewReentered = macroViewActive && !macro_view_was_active_;
    macro_view_was_active_ = macroViewActive;
    const auto flowPhase = macro_edit_.flowPhase.get();
    if (flowPhase != observed_flow_phase_) {
        observed_flow_phase_ = flowPhase;
        coarse_edit_active_ = false;
        if (automationDetailActive() || modulationDetailActive() ||
            modulatorCreateActive() || modulatorPickerActive()) {
            // This handler does not own the parent Macro Edit transition that
            // opens the detail overlay. Synchronize the physical encoder once
            // when that transition becomes observable so the first user turn
            // starts from the value shown on screen.
            configureOptForFocusedRow();
        }
    } else if (macroViewReentered && active()) {
        // The global view lifecycle restores generic OPT mechanics. Reapply
        // the focused source value before the first post-return user turn.
        configureOptForFocusedRow();
    }
    if (!active()) return;
    // Progress may reach COMMITTED while held, but the domain mutation waits
    // for release so this overlay still owns and consumes that release.
    (void)macro::MacroGuardedActionWorkflow::update(macro_edit_, nowMs);
}

FLASHMEM void MacroAutomationHandler::activateFocusedRow() {
    if (!active() || conversionPreviewActive()) return;
    cancelContextAction(
        macro_edit_, now_provider_ ? now_provider_() : 0U, false
    );
    const auto context = detailContext(services_, macroIndex());
    bool resumeAutomation = false;
    bool convert = false;
    const auto openRecordedShapeContext = [this]() {
        const auto rows = modulationRows(pages_, macroIndex());
        const uint8_t actionCount = menu::macroContextActionCount(
            menu::MacroRootItem::MODULATION,
            rows.assignmentCount
        );
        if (actionCount == 0U) return;
        backToMacroEdit();
        macro_edit_.focusedRow.set(
            menu::macroRootRow(menu::MacroRootItem::MODULATION)
        );
        macro_edit_.contextPropertyIndex.set(
            static_cast<uint8_t>(actionCount - 1U)
        );
        macro_edit_.contextSelectorActive.set(true);
        encoders_.setMode(
            Config::EncoderID::OPT,
            oc::interface::EncoderMode::RAW
        );
        encoders_.setPosition(Config::EncoderID::OPT, 0.0f);
    };
    if (modulatorCreateActive()) {
        const uint8_t row = macro_edit_.modulationFocusedRow.get();
        if (row == 0U) {
            (void)startLfoAudition();
        } else if (row == 1U) {
            (void)startAdsrAudition();
        } else if (row == 2U) {
            (void)openModulatorPicker();
        }
        return;
    } else if (modulatorPickerActive()) {
        (void)startExistingModulatorAudition();
        return;
    } else if (modulationDetailActive()) {
        if (!context.modulationStored) {
            const uint8_t row = macro_edit_.modulationFocusedRow.get();
            if (row == 0U) {
                (void)startLfoAudition();
            } else if (row == 1U) {
                (void)startAdsrAudition();
            } else if (row == 2U) {
                (void)openModulatorPicker();
            } else if (row == 3U) {
                openRecordedShapeContext();
            }
        } else {
            const auto rows = modulationRows(pages_, macroIndex());
            const int row = macro_edit_.modulationFocusedRow.get();
            const auto descriptor = modulationRowAt(pages_, rows, row);
            const auto* binding = menu::macroModulationBinding(
                pages_.control.authored.modulation,
                descriptor
            );
            if (binding != nullptr) {
                (void)services_.focusModulationBinding(
                    macroIndex(),
                    binding->id
                );
                services_.endDepthGesture();
                (void)modulator_navigation::openSourceFromMacro(
                    {
                        overlays_state_,
                        active_view_,
                        project_navigation_,
                        macro_edit_,
                        pages_,
                        project_tracks_,
                    },
                    macroIndex(),
                    binding->id,
                    static_cast<uint8_t>(row)
                );
            } else if (descriptor.kind ==
                       menu::MacroModulationRowKind::ADD_SOURCE) {
                macro_edit_.openModulatorCreate();
                configureOptForFocusedRow();
            } else if (descriptor.kind ==
                       menu::MacroModulationRowKind::RECORD_SHAPE) {
                openRecordedShapeContext();
            }
        }
        return;
    } else {
        const auto policy =
            detail_policy::buildAutomationDetailPolicy(context);
        const auto item =
            policy.at(macro_edit_.automationFocusedRow.get());
        resumeAutomation =
            item == detail_policy::AutomationDetailItem::RESUME;
        convert =
            item ==
            detail_policy::AutomationDetailItem::CONVERT_TO_MODULATION;
    }
    if (resumeAutomation) {
        if (services_.resumeSources(macroIndex())) {
            macro_edit_.automationFocusedRow.set(0);
            configureOptForFocusedRow();
        }
    } else if (convert) {
        openConversionPreview();
    }
}

FLASHMEM void MacroAutomationHandler::openConversionPreview() {
    const auto plan = services_.preflightConversion(
        macroIndex(),
        core::state::modulation::ProjectAutomationConversionPolicy::MEAN
    );
    if (!plan.actionable()) return;
    services_.endDepthGesture();
    macro_edit_.openConvertPreview(plan);
}

FLASHMEM void MacroAutomationHandler::selectConversionPolicy(float delta) {
    if (!conversionPreviewActive() || !nav::hasTurnDelta(delta)) return;
    cancelContextAction(
        macro_edit_, now_provider_ ? now_provider_() : 0U, false
    );
    const int current = static_cast<int>(macro_edit_.conversionPreview.policy);
    const int next = nav::nextWrappedIndex(delta, current, 3);
    const auto policy = static_cast<
        core::state::modulation::ProjectAutomationConversionPolicy>(next);
    macro_edit_.conversionPreview.setPlan(
        services_.preflightConversion(macroIndex(), policy)
    );
}

FLASHMEM bool MacroAutomationHandler::applyConversion(bool overwriteGesture) {
    if (!conversionPreviewActive()) return false;
    const auto plan = macro_edit_.conversionPreview.plan;
    if (!plan.actionable()) return false;
    if (plan.overwritesModulation && !overwriteGesture) return false;
    if (!services_.applyConversion(
            macroIndex(),
            plan,
            overwriteGesture
        )) {
        macro_edit_.conversionPreview.setPlan(
            services_.preflightConversion(
                macroIndex(),
                macro_edit_.conversionPreview.policy
            )
        );
        return false;
    }
    macro_edit_.closeConvertPreview();
    const auto rows = modulationRows(pages_, macroIndex());
    const auto* createdBinding = bindingAtModulationRow(
        pages_, rows, rows.firstAssignmentRow()
    );
    if (createdBinding != nullptr) {
        // Conversion lands on the concrete source it just created/replaced.
        // This keeps source editing and contextual delete one gesture away,
        // instead of silently selecting the aggregate "All" row.
        (void)services_.focusModulationBinding(
            macroIndex(),
            createdBinding->id
        );
        macro_edit_.modulationFocusedRow.set(static_cast<uint8_t>(
            rows.firstAssignmentRow()
        ));
    }
    configureOptForFocusedRow();
    return true;
}

FLASHMEM bool MacroAutomationHandler::startLfoAudition() {
    if (!modulatorCreateActive()) {
        macro_edit_.openModulatorCreate();
    }
    macro_edit_.modulationFocusedRow.set(0U);
    const auto result = services_.beginDefaultLfoAudition(macroIndex());
    if (!result.changed()) return false;
    if (modulator_navigation::openAuditionSourceFromMacro(
            {
                overlays_state_,
                active_view_,
                project_navigation_,
                macro_edit_,
                pages_,
                project_tracks_,
            },
            macroIndex()
        )) {
        return true;
    }
    (void)services_.cancelModulatorAudition(macroIndex());
    project_navigation_.modulatorReturn = {};
    configureOptForFocusedRow();
    return false;
}

FLASHMEM bool MacroAutomationHandler::startAdsrAudition() {
    if (!modulatorCreateActive()) {
        macro_edit_.openModulatorCreate();
    }
    macro_edit_.modulationFocusedRow.set(1U);
    const auto result = services_.beginDefaultAdsrAudition(macroIndex());
    if (!result.changed()) return false;
    if (modulator_navigation::openAuditionSourceFromMacro(
            {
                overlays_state_,
                active_view_,
                project_navigation_,
                macro_edit_,
                pages_,
                project_tracks_,
            },
            macroIndex()
        )) {
        return true;
    }
    (void)services_.cancelModulatorAudition(macroIndex());
    project_navigation_.modulatorReturn = {};
    configureOptForFocusedRow();
    return false;
}

FLASHMEM bool MacroAutomationHandler::openModulatorPicker() {
    const int count = static_cast<int>(
        pages_.control.authored.modulation.sourceCount
    );
    if (count <= 0) return false;
    const int selected = std::clamp(
        macro_edit_.modulatorPickerIndex.get(),
        0,
        count - 1
    );
    macro_edit_.openModulatorPicker(selected);
    configureOptForFocusedRow();
    return true;
}

FLASHMEM bool MacroAutomationHandler::startExistingModulatorAudition() {
    if (!modulatorPickerActive()) return false;
    const auto& graph = pages_.control.authored.modulation;
    const int selected = macro_edit_.modulatorPickerIndex.get();
    if (selected < 0 || selected >= static_cast<int>(graph.sourceCount)) {
        return false;
    }
    const auto result = services_.beginExistingModulatorAudition(
        macroIndex(),
        graph.sources[static_cast<uint16_t>(selected)].id
    );
    if (!result.changed()) return false;
    if (modulator_navigation::openAuditionSourceFromMacro(
            {
                overlays_state_,
                active_view_,
                project_navigation_,
                macro_edit_,
                pages_,
                project_tracks_,
            },
            macroIndex()
        )) {
        return true;
    }
    (void)services_.cancelModulatorAudition(macroIndex());
    project_navigation_.modulatorReturn = {};
    configureOptForFocusedRow();
    return false;
}

}  // namespace core::handler
