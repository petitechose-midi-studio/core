#include "handler/macro/MacroAutomationHandler.hpp"

#include <algorithm>
#include <array>

#include <config/InputIDs.hpp>
#include <config/PlatformCompat.hpp>
#include <config/Timing.hpp>

#include "handler/common/ModalSelectionUtils.hpp"
#include "handler/common/NavigationUtils.hpp"
#include "handler/macro/MacroAutomationEditorModel.hpp"
#include "handler/macro/MacroGuardedActionWorkflow.hpp"

namespace core::handler {

namespace {

constexpr uint8_t ROW_STATE = 0;
constexpr uint8_t ROW_LENGTH = 1;
constexpr uint8_t ROW_OFFSET = 2;
constexpr uint8_t ROW_CURVE = 3;
constexpr uint8_t ROW_RESUME = 4;
constexpr uint8_t ROW_AUTO_MOD = 5;
constexpr uint8_t ROW_CONVERT = 6;
constexpr uint8_t AUTOMATION_ROW_COUNT = 7;

constexpr uint8_t MOD_ROW_STATE = 0;
constexpr uint8_t MOD_ROW_DEPTH = 1;
constexpr uint8_t MOD_ROW_SOURCE = 2;
constexpr uint8_t MOD_ROW_RESUME = 3;
constexpr uint8_t MOD_ROW_AUTO_MOD = 4;
constexpr uint8_t MOD_ROW_CONVERT = 5;
constexpr uint8_t MODULATION_ROW_COUNT = 6;

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
    const int count = modulationDetailActive()
        ? MODULATION_ROW_COUNT
        : AUTOMATION_ROW_COUNT;
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
    if (modulationDetailActive()) {
        const uint8_t row = macro_edit_.modulationFocusedRow.get();
        if (row == MOD_ROW_STATE) {
            if (clamped < 0.34f) services_.setManualOverride(index, true);
            else if (clamped > 0.66f) (void)services_.enableAutoMod(index);
            else (void)services_.resumeSources(index);
            return;
        }
        if (row == MOD_ROW_DEPTH) {
            (void)services_.setModulationDepth(index, clamped);
        }
        return;
    }
    if (!automationDetailActive() || !services_.automationActiveFor(index)) return;
    const uint8_t row = macro_edit_.automationFocusedRow.get();
    if (row == ROW_STATE) {
        services_.setManualOverride(index, clamped < 0.5f);
        return;
    }
    if (row == ROW_LENGTH) {
        const auto range = macroAutomationLengthEditRange(coarse_edit_active_);
        services_.setAutomationDurationBeats(
            index,
            macroAutomationEncoderPositionToBeat(clamped, range)
        );
        return;
    }
    if (row == ROW_OFFSET) {
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
    if (modulationDetailActive()) {
        const uint8_t row = macro_edit_.modulationFocusedRow.get();
        if (row == MOD_ROW_STATE) {
            steps = 3;
            const auto mode = services_.sourceModeFor(macroIndex());
            position = mode == MacroSourceMode::MANUAL
                ? 0.0f
                : (mode == MacroSourceMode::AUTO_MOD ? 1.0f : 0.5f);
        } else if (row == MOD_ROW_DEPTH) {
            steps = 101;
            position = services_.modulationDepth(macroIndex());
        }
        encoders_.setDiscreteSteps(Config::EncoderID::OPT, steps);
        encoders_.setPosition(Config::EncoderID::OPT, position);
        return;
    }
    const uint8_t row = macro_edit_.automationFocusedRow.get();
    if (row == ROW_STATE) {
        steps = 2;
        const uint8_t index = macroIndex();
        position = services_.manualOverrideActiveFor(index) ? 0.0f : 1.0f;
    } else if (row == ROW_LENGTH) {
        const auto range = macroAutomationLengthEditRange(coarse_edit_active_);
        steps = range.stepCount;
        const auto* slot = services_.automationSlot(macroIndex());
        if (slot != nullptr && slot->automation.active) {
            position = macroAutomationTicksToEncoderPosition(
                slot->automation.durationTicks,
                range
            );
        }
    } else if (row == ROW_OFFSET) {
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

FLASHMEM void MacroAutomationHandler::clearAutomation() {
    if (!active()) return;
    if (conversionPreviewActive()) return;
    if (modulationDetailActive()) {
        (void)services_.clearModulation(macroIndex());
    }
}

FLASHMEM void MacroAutomationHandler::copyAutomation() {
    if (!active()) return;
    if (conversionPreviewActive()) return;
    if (modulationDetailActive()) {
        (void)services_.copyModulation(macroIndex());
    } else {
        (void)services_.copySlot(macroIndex());
    }
}

FLASHMEM void MacroAutomationHandler::beginBottomLeftAction() {
    if (!active()) return;
    const auto action = automationDetailActive()
        ? core::state::contextual::ContextActionId::REMOVE
        : core::state::contextual::ContextActionId::NONE;
    const auto target = slotRef(pages_, macroIndex());
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
        clearAutomation();
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
        if (macro_edit_.conversionPreview.plan.overwritesModulation &&
            macro_edit_.conversionPreview.plan.actionable()) {
            action = core::state::contextual::ContextActionId::OVERWRITE;
        }
    } else {
        const auto plan = modulationDetailActive()
            ? services_.preflightModulationPaste(macroIndex())
            : services_.preflightSlotPaste(macroIndex());
        if (plan.actionable()) {
            action = plan.requiresOverwrite()
                ? core::state::contextual::ContextActionId::OVERWRITE
                : core::state::contextual::ContextActionId::PASTE;
        }
    }
    (void)macro::MacroGuardedActionWorkflow::begin(
        macro_edit_,
        core::state::MacroContextButton::BOTTOM_RIGHT,
        action,
        {.kind = modulationDetailActive()
             ? core::state::contextual::ContextEntityKind::MODULATION_LANE
             : core::state::contextual::ContextEntityKind::MACRO_SLOT},
        slotRef(pages_, macroIndex()),
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
        if (conversionPreviewActive()) (void)applyConversion(false);
        else copyAutomation();
    } else if (release ==
               core::state::contextual::GuardedActionRelease::COMMITTED) {
        commitGuardedAction(nowMs);
    }
}

FLASHMEM void MacroAutomationHandler::commitGuardedAction(uint32_t nowMs) {
    const auto feedback = macro_edit_.contextFeedback.get();
    const uint8_t index = macroIndex();
    if (feedback.target != slotRef(pages_, index)) {
        macro::MacroGuardedActionWorkflow::complete(macro_edit_, false, nowMs);
        return;
    }

    bool applied = false;
    if (feedback.action == core::state::contextual::ContextActionId::REMOVE &&
        automationDetailActive()) {
        applied = services_.removeSlot(index);
        macro::MacroGuardedActionWorkflow::complete(macro_edit_, applied, nowMs);
        if (applied) {
            // Removing the edited Slot invalidates both the detail surface and
            // its parent editor. Pop the complete owned stack so no inactive
            // Macro Edit surface remains presented over the grid.
            modal::hideWhileCurrentIn(
                overlays_,
                std::array{
                    core::ui::OverlayType::MACRO_AUTOMATION,
                    core::ui::OverlayType::MACRO_EDIT,
                }
            );
            macro_edit_.closeEditor();
        }
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
            const auto plan = services_.preflightSlotPaste(index);
            if (plan.actionable() &&
                plan.requiresOverwrite() == expectedOverwrite) {
                applied = services_.pasteSlot(index, expectedOverwrite);
                if (applied) {
                    const auto& config = services_.activeConfig(index);
                    macro_edit_.loadActiveConfig(index, config.channel, config.cc);
                }
            }
        }
    }
    macro::MacroGuardedActionWorkflow::complete(macro_edit_, applied, nowMs);
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
    const uint8_t row = modulationDetailActive()
        ? macro_edit_.modulationFocusedRow.get()
        : macro_edit_.automationFocusedRow.get();
    const bool resume = modulationDetailActive()
        ? row == MOD_ROW_RESUME
        : row == ROW_RESUME;
    const bool autoMod = modulationDetailActive()
        ? row == MOD_ROW_AUTO_MOD
        : row == ROW_AUTO_MOD;
    const bool convert = modulationDetailActive()
        ? row == MOD_ROW_CONVERT
        : row == ROW_CONVERT;
    if (resume) {
        (void)services_.resumeSources(macroIndex());
    } else if (autoMod) {
        (void)services_.enableAutoMod(macroIndex());
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
