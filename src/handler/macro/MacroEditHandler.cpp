#include "MacroEditHandler.hpp"

#include <algorithm>

#include <config/App.hpp>
#include <config/PlatformCompat.hpp>
#include "handler/common/ModalSelectionUtils.hpp"
#include "handler/common/NavigationUtils.hpp"
#include "handler/macro/MacroGuardedActionWorkflow.hpp"

namespace core::handler {

namespace {

constexpr uint32_t QUICK_RELEASE_WINDOW_MS = 450;
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

float clampNormalized(float value) {
    return std::clamp(value, 0.0f, 1.0f);
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
    oc::type::ScopeID pageSelectorScope,
    oc::type::ScopeID macroSelectorScope,
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
    , page_selector_scope_(pageSelectorScope)
    , macro_selector_scope_(macroSelectorScope)
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
    const oc::type::ScopeID pageScope = page_selector_scope_;
    const oc::type::ScopeID macroScope = macro_selector_scope_;

    const oc::type::ScopeID openingReleaseScopes[] = {mainScope, valueScope, pageScope, macroScope};

    // Long press opens MacroEdit for targeted macro; paired release decides latch/close.
    for (uint8_t i = 0; i < Config::MACRO_COUNT; ++i) {
        const auto macroButton = static_cast<oc::type::ButtonID>(Config::MACRO_BUTTONS[i]);

        buttons_.button(macroButton)
            .longPress(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS)
            .scope(macro_view_scope_)
            .when([this]() { return !macro_ui_.automationRecording.active; })
            .then([this, i]() { openEdit(i); });

        for (oc::type::ScopeID scopeId : openingReleaseScopes) {
            buttons_.button(macroButton)
                .release()
                .scope(scopeId)
                .then([this, i]() { handleOpeningMacroRelease(i); });
        }
    }

    // Canonical ADR-0037 entry. The short press remains the lightweight
    // performance clutch; holding promotes it to the persistent Slot selector.
    buttons_.button(leftBottomButton)
        .longPress(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS)
        .scope(macro_view_scope_)
        .when([this]() {
            const uint8_t index = macro_ui_.focusedMacroSlot.get();
            return !macro_ui_.automationRecording.active &&
                   index < core::state::macro::MACRO_COUNT &&
                   services_.isMacroSlotActive(index);
        })
        .then([this]() {
            // The physical release is delivered to the overlay scope that was
            // just opened. Consume it so the same hold cannot immediately open
            // the target selector as a second, unintended action.
            ignore_next_left_bottom_release_ = true;
            macro_ui_.clutchActive.set(false);
            macro_ui_.activeProperty.set(
                core::state::macro::MacroPerformanceProperty::VALUE
            );
            openEdit(macro_ui_.focusedMacroSlot.get());
        });

    // ===== MAIN MACRO EDIT OVERLAY SCOPE =====
    encoders_.encoder(static_cast<oc::type::EncoderID>(Config::EncoderID::NAV))
        .turn()
        .scope(mainScope)
        .then([this](float delta) { moveFocus(delta); });

    encoders_.encoder(static_cast<oc::type::EncoderID>(Config::EncoderID::OPT))
        .turn()
        .scope(mainScope)
        .then([this](float normalized) { setFocusedValue(normalized); });

    buttons_.button(navButton)
        .release()
        .scope(mainScope)
        .then([this]() { openValueSelector(); });

    buttons_.button(leftTopButton)
        .release()
        .scope(mainScope)
        .then([this]() { closeOverlay(); });

    buttons_.button(leftCenterButton)
        .release()
        .scope(mainScope)
        .then([this]() { openPageSelector(); });

    buttons_.button(leftBottomButton)
        .release()
        .scope(mainScope)
        .then([this]() {
            if (ignore_next_left_bottom_release_) {
                ignore_next_left_bottom_release_ = false;
                return;
            }
            openMacroTargetSelector();
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

    // ===== PAGE SELECTOR OVERLAY SCOPE =====
    encoders_.encoder(static_cast<oc::type::EncoderID>(Config::EncoderID::NAV))
        .turn()
        .scope(pageScope)
        .then([this](float delta) { navigatePageSelector(delta); });

    buttons_.button(leftCenterButton)
        .release()
        .scope(pageScope)
        .then([this]() { applyPageSelectorAndClose(); });

    // ===== MACRO TARGET SELECTOR OVERLAY SCOPE =====
    encoders_.encoder(static_cast<oc::type::EncoderID>(Config::EncoderID::NAV))
        .turn()
        .scope(macroScope)
        .then([this](float delta) { navigateMacroTargetSelector(delta); });

    buttons_.button(leftBottomButton)
        .release()
        .scope(macroScope)
        .then([this]() { applyMacroTargetSelectorAndClose(); });

    const oc::type::ScopeID selectorCloseScopes[] = {pageScope, macroScope};
    for (oc::type::ScopeID scopeId : selectorCloseScopes) {
        buttons_.button(leftTopButton)
            .release()
            .scope(scopeId)
            .then([this]() { closeOverlay(); });
    }

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

    overlays_.show(core::ui::OverlayType::MACRO_EDIT);

    configureOptForFocusedRow();

}

FLASHMEM void MacroEditHandler::handleOpeningMacroRelease(uint8_t macroIndex) {
    auto& edit = macro_edit_;
    const uint32_t nowMs = now_provider_ ? now_provider_() : edit.openedAtMs;
    if (edit.consumeOpeningReleaseDecision(macroIndex, nowMs, QUICK_RELEASE_WINDOW_MS)) {
        closeOverlay();
    }
}

FLASHMEM void MacroEditHandler::closeOverlay() {
    commitEditedConfig();

    // Close any stacked macro-edit related selector first, then the main overlay.
    modal::hideWhileCurrentIn(
        overlays_,
        std::array{
            core::ui::OverlayType::MACRO_EDIT_SELECTOR,
            core::ui::OverlayType::MACRO_EDIT_MACRO_SELECTOR,
            core::ui::OverlayType::PAGE_SELECTOR,
            core::ui::OverlayType::MACRO_AUTOMATION,
        }
    );
    modal::hideIfCurrent(overlays_, core::ui::OverlayType::MACRO_EDIT);

    macro_edit_.closeEditor();
    pages_.selector.visible.set(false);
    pages_.selector.selectedIndex.set(pages_.currentActivePage());
}

FLASHMEM void MacroEditHandler::moveFocus(float delta) {
    if (macro_edit_.flowPhase.get() != core::state::MacroEditFlowPhase::EDIT) return;
    if (!nav::hasTurnDelta(delta)) return;
    cancelContextAction(
        macro_edit_, now_provider_ ? now_provider_() : 0U, true
    );

    const int current = static_cast<int>(macro_edit_.focusedRow.get());
    const int next = nav::nextWrappedIndex(delta, current, ROW_COUNT);
    macro_edit_.focusedRow.set(static_cast<uint8_t>(next));

    configureOptForFocusedRow();
}

FLASHMEM void MacroEditHandler::setFocusedValue(float normalized) {
    if (macro_edit_.flowPhase.get() != core::state::MacroEditFlowPhase::EDIT) return;
    cancelContextAction(
        macro_edit_, now_provider_ ? now_provider_() : 0U, true
    );
    const uint8_t row = macro_edit_.focusedRow.get();
    if (row != ROW_CC) return;
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
        edit.openAutomation();
        overlays_.show(core::ui::OverlayType::MACRO_AUTOMATION, true);
        return;
    }
    if (row == ROW_MODULATION) {
        edit.openModulation();
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

FLASHMEM void MacroEditHandler::openPageSelector() {
    auto& edit = macro_edit_;
    if (edit.flowPhase.get() != core::state::MacroEditFlowPhase::EDIT) return;
    cancelContextAction(
        macro_edit_, now_provider_ ? now_provider_() : 0U, false
    );

    pages_.selector.selectedIndex.set(pages_.currentActivePage());
    pages_.selector.visible.set(true);
    edit.openPageSelector();
    overlays_.show(core::ui::OverlayType::PAGE_SELECTOR, true);

}

FLASHMEM void MacroEditHandler::navigatePageSelector(float delta) {
    if (macro_edit_.flowPhase.get() != core::state::MacroEditFlowPhase::PAGE_SELECTOR) return;
    const int current = static_cast<int>(pages_.selector.selectedIndex.get());
    int next = current;
    if (!modal::advanceWrappedSelection(
            delta,
            pages_.selector.visible.get(),
            current,
            core::state::macro::PAGE_COUNT,
            next
        )) {
        return;
    }
    pages_.selector.selectedIndex.set(static_cast<uint8_t>(next));
}

FLASHMEM void MacroEditHandler::applyPageSelectorAndClose() {
    if (macro_edit_.flowPhase.get() != core::state::MacroEditFlowPhase::PAGE_SELECTOR) return;
    if (!pages_.selector.visible.get()) return;

    const uint8_t targetPage = std::clamp(
        pages_.selector.selectedIndex.get(),
        static_cast<uint8_t>(0),
        static_cast<uint8_t>(core::state::macro::PAGE_COUNT - 1)
    );

    if (targetPage != pages_.currentActivePage()) {
        commitEditedConfig();
        services_.switchToPage(targetPage);
        macro_ui_.syncPreviewPage(pages_.currentActivePage());

        const uint8_t macroIndex = macro_edit_.editingIndex.get();
        const auto& config = services_.activeConfig(macroIndex);
        macro_edit_.loadActiveConfig(macroIndex, config.channel, config.cc);
    }

    modal::hideIfCurrent(overlays_, core::ui::OverlayType::PAGE_SELECTOR);
    pages_.selector.visible.set(false);
    macro_edit_.closePageSelector();
    configureOptForFocusedRow();
}

FLASHMEM void MacroEditHandler::openMacroTargetSelector() {
    auto& edit = macro_edit_;
    if (edit.flowPhase.get() != core::state::MacroEditFlowPhase::EDIT) return;
    cancelContextAction(
        macro_edit_, now_provider_ ? now_provider_() : 0U, false
    );

    edit.openTargetSelector(macro_edit_.editingIndex.get());
    overlays_.show(core::ui::OverlayType::MACRO_EDIT_MACRO_SELECTOR, true);
}

FLASHMEM void MacroEditHandler::navigateMacroTargetSelector(float delta) {
    if (macro_edit_.flowPhase.get() != core::state::MacroEditFlowPhase::TARGET_SELECTOR) return;
    auto& selector = macro_edit_.macroSelector;
    int next = selector.selectedIndex.get();
    if (!modal::advanceWrappedSelection(delta, selector, core::state::macro::MACRO_COUNT, next)) {
        return;
    }
    selector.selectedIndex.set(next);
}

FLASHMEM void MacroEditHandler::applyMacroTargetSelectorAndClose() {
    if (macro_edit_.flowPhase.get() != core::state::MacroEditFlowPhase::TARGET_SELECTOR) return;
    auto& selector = macro_edit_.macroSelector;
    if (!selector.visible.get()) return;

    const uint8_t targetMacro = static_cast<uint8_t>(std::clamp(
        selector.selectedIndex.get(),
        0,
        static_cast<int>(core::state::macro::MACRO_COUNT) - 1
    ));

    if (targetMacro != macro_edit_.editingIndex.get()) {
        commitEditedConfig();
        const auto& config = services_.activeConfig(targetMacro);
        macro_edit_.loadActiveConfig(targetMacro, config.channel, config.cc);
    }

    modal::hideIfCurrent(overlays_, core::ui::OverlayType::MACRO_EDIT_MACRO_SELECTOR);
    macro_edit_.closeTargetSelector();
    configureOptForFocusedRow();
}

FLASHMEM void MacroEditHandler::setValueForRow(uint8_t row, int value) {
    if (row == ROW_CC) {
        const int clamped = std::clamp(value, 0, 127);
        macro_edit_.tempCC.set(static_cast<uint8_t>(clamped));
    }
}

FLASHMEM int MacroEditHandler::valueForRow(uint8_t row) const {
    return static_cast<int>(macro_edit_.tempCC.get());
}

FLASHMEM int MacroEditHandler::valueCountForRow(uint8_t row) const {
    if (row != ROW_CC) return 1;
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

FLASHMEM void MacroEditHandler::copySlot() {
    if (macro_edit_.flowPhase.get() != core::state::MacroEditFlowPhase::EDIT) return;
    (void)services_.copySlot(macro_edit_.editingIndex.get());
}

FLASHMEM void MacroEditHandler::beginBottomRightAction() {
    if (macro_edit_.flowPhase.get() != core::state::MacroEditFlowPhase::EDIT) return;
    const uint8_t index = macro_edit_.editingIndex.get();
    const auto plan = services_.preflightSlotPaste(index);
    const auto action = plan.actionable()
        ? (plan.requiresOverwrite()
               ? core::state::contextual::ContextActionId::OVERWRITE
               : core::state::contextual::ContextActionId::PASTE)
        : core::state::contextual::ContextActionId::NONE;
    (void)macro::MacroGuardedActionWorkflow::begin(
        macro_edit_,
        core::state::MacroContextButton::BOTTOM_RIGHT,
        action,
        {.kind = core::state::contextual::ContextEntityKind::MACRO_SLOT},
        slotRef(pages_, index),
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
        copySlot();
    } else if (release ==
               core::state::contextual::GuardedActionRelease::COMMITTED) {
        commitGuardedAction(nowMs);
    }
}

FLASHMEM void MacroEditHandler::beginBottomLeftAction() {
    if (macro_edit_.flowPhase.get() != core::state::MacroEditFlowPhase::EDIT) return;
    const uint8_t index = macro_edit_.editingIndex.get();
    (void)macro::MacroGuardedActionWorkflow::begin(
        macro_edit_,
        core::state::MacroContextButton::BOTTOM_LEFT,
        core::state::contextual::ContextActionId::REMOVE,
        slotRef(pages_, index),
        slotRef(pages_, index),
        now_provider_ ? now_provider_() : 0U,
        static_cast<uint16_t>(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS)
    );
}

FLASHMEM void MacroEditHandler::releaseBottomLeftAction() {
    if (macro_edit_.flowPhase.get() != core::state::MacroEditFlowPhase::EDIT) return;
    const uint32_t nowMs = now_provider_ ? now_provider_() : 0U;
    if (macro::MacroGuardedActionWorkflow::release(
            macro_edit_,
            core::state::MacroContextButton::BOTTOM_LEFT,
            nowMs
        ) == core::state::contextual::GuardedActionRelease::COMMITTED) {
        commitGuardedAction(nowMs);
    }
}

FLASHMEM void MacroEditHandler::commitGuardedAction(uint32_t nowMs) {
    const auto feedback = macro_edit_.contextFeedback.get();
    const uint8_t index = macro_edit_.editingIndex.get();
    if (feedback.target != slotRef(pages_, index)) {
        macro::MacroGuardedActionWorkflow::complete(macro_edit_, false, nowMs);
        return;
    }

    bool applied = false;
    if (feedback.action == core::state::contextual::ContextActionId::REMOVE) {
        applied = services_.removeSlot(index);
        macro::MacroGuardedActionWorkflow::complete(macro_edit_, applied, nowMs);
        if (applied) closeOverlay();
        return;
    }
    if (feedback.action == core::state::contextual::ContextActionId::PASTE ||
        feedback.action == core::state::contextual::ContextActionId::OVERWRITE) {
        const auto plan = services_.preflightSlotPaste(index);
        const bool matchingPlan = plan.actionable() &&
            (plan.requiresOverwrite() ==
             (feedback.action == core::state::contextual::ContextActionId::OVERWRITE));
        if (matchingPlan) {
            applied = services_.pasteSlot(index, plan.requiresOverwrite());
            if (applied) {
                const auto& config = services_.activeConfig(index);
                macro_edit_.loadActiveConfig(index, config.channel, config.cc);
            }
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
