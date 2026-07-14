#include "handler/macro/MacroAutomationHandler.hpp"

#include <algorithm>

#include <config/InputIDs.hpp>
#include <config/PlatformCompat.hpp>
#include <config/Timing.hpp>

#include "handler/common/ModalSelectionUtils.hpp"
#include "handler/common/NavigationUtils.hpp"
#include "handler/macro/MacroAutomationEditorModel.hpp"
#include "handler/macro/MacroGuardedActionWorkflow.hpp"
#include "ui/macro/MacroSourceDetailLayout.hpp"

namespace core::handler {

namespace {

namespace detail_ui = core::ui::macro;

detail_ui::MacroSourceDetailContext detailContext(
    const MacroEditDomainServices& services,
    uint8_t macroIndex
) {
    const auto* slot = services.automationSlot(macroIndex);
    if (slot == nullptr) return {};
    return {
        .automationStored =
            core::state::macro::macroCurveStored(slot->automation),
        .modulationStored =
            core::state::macro::macroCurveStored(slot->modulation),
        .automationPlayback =
            core::state::macro::macroCurvePlaybackActive(slot->automation),
        .modulationPlayback =
            core::state::macro::macroCurvePlaybackActive(slot->modulation),
        .manualOverride = services.manualOverrideActiveFor(macroIndex),
    };
}

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
    : macro_edit_(state.macroEdit)
    , pages_(state.pages)
    , services_(services)
    , overlays_(overlays)
    , encoders_(encoders)
    , buttons_(buttons)
    , automation_scope_(automationScope)
    , now_provider_(nowProvider) {
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
            conversionPreviewActive());
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

uint8_t MacroAutomationHandler::macroIndex() const {
    return macro_edit_.editingIndex.get();
}

FLASHMEM void MacroAutomationHandler::moveFocus(float delta) {
    if (!active() || !nav::hasTurnDelta(delta)) return;
    cancelContextAction(
        macro_edit_, now_provider_ ? now_provider_() : 0U, true
    );
    services_.endDepthGesture();
    auto& focus = modulationDetailActive()
        ? macro_edit_.modulationFocusedRow
        : macro_edit_.automationFocusedRow;
    const int current = static_cast<int>(focus.get());
    const auto context = detailContext(services_, macroIndex());
    const int count = modulationDetailActive()
        ? static_cast<int>(detail_ui::buildModulationDetailLayout(context).count)
        : static_cast<int>(detail_ui::buildAutomationDetailLayout(context).count);
    const int next = nav::nextWrappedIndex(delta, current, count);
    focus.set(static_cast<uint8_t>(next));
    configureOptForFocusedRow();
}

FLASHMEM void MacroAutomationHandler::editFocusedValue(float normalized) {
    if (!active()) return;
    cancelContextAction(
        macro_edit_, now_provider_ ? now_provider_() : 0U, true
    );
    const uint8_t index = macroIndex();
    const float clamped = std::clamp(normalized, 0.0f, 1.0f);
    const auto context = detailContext(services_, index);
    if (modulationDetailActive()) {
        const auto layout = detail_ui::buildModulationDetailLayout(context);
        const auto item = layout.at(macro_edit_.modulationFocusedRow.get());
        if (item == detail_ui::ModulationDetailItem::PLAYBACK) {
            (void)services_.setModulationPlayback(index, clamped >= 0.5f);
        } else if (item == detail_ui::ModulationDetailItem::DEPTH) {
            (void)services_.setModulationDepth(index, clamped);
        }
        return;
    }
    if (!automationDetailActive()) return;
    const auto layout = detail_ui::buildAutomationDetailLayout(context);
    const auto item = layout.at(macro_edit_.automationFocusedRow.get());
    if (item == detail_ui::AutomationDetailItem::PLAYBACK) {
        (void)services_.setAutomationPlayback(index, clamped >= 0.5f);
        return;
    }
    if (item == detail_ui::AutomationDetailItem::LENGTH) {
        const auto range = macroAutomationLengthEditRange(coarse_edit_active_);
        services_.setAutomationDurationBeats(
            index,
            macroAutomationEncoderPositionToBeat(clamped, range)
        );
        return;
    }
    if (item == detail_ui::AutomationDetailItem::OFFSET) {
        const auto* slot = services_.automationSlot(index);
        const auto range = macroAutomationOffsetEditRange(slot, coarse_edit_active_);
        services_.setAutomationWindowOffsetBeats(
            index,
            macroAutomationEncoderPositionToBeat(clamped, range)
        );
    }
}

FLASHMEM void MacroAutomationHandler::configureOptForFocusedRow() {
    uint8_t steps = 1;
    float position = 0.0f;
    const auto context = detailContext(services_, macroIndex());
    if (modulationDetailActive()) {
        const auto layout = detail_ui::buildModulationDetailLayout(context);
        const auto item = layout.at(macro_edit_.modulationFocusedRow.get());
        if (item == detail_ui::ModulationDetailItem::PLAYBACK) {
            steps = 2;
            position = context.modulationPlayback ? 1.0f : 0.0f;
        } else if (item == detail_ui::ModulationDetailItem::DEPTH) {
            steps = 101;
            position = services_.modulationDepth(macroIndex());
        }
        encoders_.setDiscreteSteps(Config::EncoderID::OPT, steps);
        encoders_.setPosition(Config::EncoderID::OPT, position);
        return;
    }
    const auto layout = detail_ui::buildAutomationDetailLayout(context);
    const auto item = layout.at(macro_edit_.automationFocusedRow.get());
    if (item == detail_ui::AutomationDetailItem::PLAYBACK) {
        steps = 2;
        position = context.automationPlayback ? 1.0f : 0.0f;
    } else if (item == detail_ui::AutomationDetailItem::LENGTH) {
        const auto range = macroAutomationLengthEditRange(coarse_edit_active_);
        steps = range.stepCount;
        const auto* slot = services_.automationSlot(macroIndex());
        if (slot != nullptr && slot->automation.active) {
            position = macroAutomationTicksToEncoderPosition(
                slot->automation.durationTicks,
                range
            );
        }
    } else if (item == detail_ui::AutomationDetailItem::OFFSET) {
        const auto* slot = services_.automationSlot(macroIndex());
        const auto range = macroAutomationOffsetEditRange(slot, coarse_edit_active_);
        steps = range.stepCount;
        if (slot != nullptr && slot->automation.active) {
            position = macroAutomationTicksToEncoderPosition(
                slot->automation.windowOffsetTicks,
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
    if (conversionPreviewActive()) {
        macro_edit_.closeConvertPreview();
        configureOptForFocusedRow();
        return;
    }
    modal::hideIfCurrent(overlays_, core::ui::OverlayType::MACRO_AUTOMATION);
    if (modulationDetailActive()) macro_edit_.closeModulation();
    else macro_edit_.closeAutomation();
}

FLASHMEM void MacroAutomationHandler::toggleFocusedPlayback() {
    if (!active()) return;
    if (conversionPreviewActive()) return;
    const uint8_t index = macroIndex();
    if (modulationDetailActive()) {
        if (services_.modulationStoredFor(index)) {
            (void)services_.setModulationPlayback(
                index,
                !services_.modulationPlaybackActiveFor(index)
            );
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
    if (conversionPreviewActive()) return;
    if (modulationDetailActive()) {
        (void)services_.copyModulation(macroIndex());
    } else {
        (void)services_.copyAutomation(macroIndex());
    }
}

FLASHMEM void MacroAutomationHandler::beginBottomLeftAction() {
    if (!active()) return;
    if (conversionPreviewActive()) return;
    services_.endDepthGesture();
    const uint8_t index = macroIndex();
    const bool modulation = modulationDetailActive();
    const bool stored = modulation ? services_.modulationStoredFor(index)
                                   : services_.automationStoredFor(index);
    const auto action = stored
        ? core::state::contextual::ContextActionId::CLEAR
        : core::state::contextual::ContextActionId::NONE;
    const auto target = sourceRef(pages_, index, modulation);
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
    } else if (release ==
               core::state::contextual::GuardedActionRelease::COMMITTED) {
        commitGuardedAction(nowMs);
    }
}

FLASHMEM void MacroAutomationHandler::beginBottomRightAction() {
    if (!active()) return;
    core::state::contextual::ContextActionId action =
        core::state::contextual::ContextActionId::NONE;
    if (conversionPreviewActive()) {
        if (macro_edit_.conversionPreview.plan.actionable()) {
            action = macro_edit_.conversionPreview.plan.overwritesModulation
                ? core::state::contextual::ContextActionId::OVERWRITE
                : core::state::contextual::ContextActionId::APPLY;
        }
    } else {
        const auto plan = modulationDetailActive()
            ? services_.preflightModulationPaste(macroIndex())
            : services_.preflightAutomationPaste(macroIndex());
        if (plan.actionable()) {
            action = plan.requiresOverwrite()
                ? core::state::contextual::ContextActionId::OVERWRITE
                : core::state::contextual::ContextActionId::PASTE;
        }
    }
    const auto target = conversionPreviewActive()
        ? sourceRef(pages_, macroIndex(), true)
        : sourceRef(pages_, macroIndex(), modulationDetailActive());
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
    } else if (release ==
               core::state::contextual::GuardedActionRelease::COMMITTED) {
        commitGuardedAction(nowMs);
    }
}

FLASHMEM void MacroAutomationHandler::commitGuardedAction(uint32_t nowMs) {
    const auto feedback = macro_edit_.contextFeedback.get();
    const uint8_t index = macroIndex();
    const auto expectedTarget = conversionPreviewActive()
        ? sourceRef(pages_, index, true)
        : sourceRef(pages_, index, modulationDetailActive());
    if (feedback.target != expectedTarget) {
        macro::MacroGuardedActionWorkflow::complete(macro_edit_, false, nowMs);
        return;
    }

    bool applied = false;
    if (feedback.action == core::state::contextual::ContextActionId::CLEAR &&
        !conversionPreviewActive()) {
        services_.endDepthGesture();
        applied = modulationDetailActive()
            ? services_.clearModulation(index)
            : services_.clearAutomation(index);
        macro::MacroGuardedActionWorkflow::complete(macro_edit_, applied, nowMs);
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
            }
        } else if (automationDetailActive()) {
            const auto plan = services_.preflightAutomationPaste(index);
            if (plan.actionable() &&
                plan.requiresOverwrite() == expectedOverwrite) {
                applied = services_.pasteAutomation(index, expectedOverwrite);
            }
        }
    }
    macro::MacroGuardedActionWorkflow::complete(macro_edit_, applied, nowMs);
    if (applied && !conversionPreviewActive()) configureOptForFocusedRow();
}

FLASHMEM void MacroAutomationHandler::update(uint32_t nowMs) {
    const auto flowPhase = macro_edit_.flowPhase.get();
    if (flowPhase != observed_flow_phase_) {
        observed_flow_phase_ = flowPhase;
        coarse_edit_active_ = false;
        if (automationDetailActive() || modulationDetailActive()) {
            // This handler does not own the parent Macro Edit transition that
            // opens the detail overlay. Synchronize the physical encoder once
            // when that transition becomes observable so the first user turn
            // starts from the value shown on screen.
            configureOptForFocusedRow();
        }
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
    if (modulationDetailActive()) {
        return;
    } else {
        const auto layout = detail_ui::buildAutomationDetailLayout(context);
        const auto item = layout.at(macro_edit_.automationFocusedRow.get());
        resumeAutomation = item == detail_ui::AutomationDetailItem::RESUME;
        convert =
            item == detail_ui::AutomationDetailItem::CONVERT_TO_MODULATION;
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
        core::state::macro::MacroAutomationConversionPolicy::MEAN
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
        core::state::macro::MacroAutomationConversionPolicy>(next);
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
    configureOptForFocusedRow();
    return true;
}

}  // namespace core::handler
