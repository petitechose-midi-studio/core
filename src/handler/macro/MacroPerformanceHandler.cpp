#include "handler/macro/MacroPerformanceHandler.hpp"

#include <config/PlatformCompat.hpp>
#include "handler/common/NavigationUtils.hpp"
#include "handler/macro/MacroEditHandler.hpp"
#include "handler/sequencer/ProjectTrackEditorHandler.hpp"

#if defined(MS_UX_RECORDER)
#include "validation/ux/SemanticUxTraceState.hpp"
#endif

namespace core::handler {

namespace {

using MacroAction = core::state::macro::MacroInteractionAction;
using MacroPolicy = core::state::macro::MacroInteractionPolicy;
using SelectionAction =
    core::state::StructureSelectionInteractionAction;

}  // namespace

FLASHMEM MacroPerformanceHandler::MacroPerformanceHandler(
    StateRefs state,
    MacroPerformanceDomainServices performanceServices,
    MacroStructureDomainServices structureServices,
    oc::context::OverlayManager<core::ui::OverlayType>& overlays,
    oc::api::EncoderAPI& encoders,
    oc::api::ButtonAPI& buttons,
    oc::type::ScopeID scopeId,
    TimeProviderFn timeProvider
#if defined(MS_UX_RECORDER)
    ,
    core::validation::ux::StructureUxTraceState* uxTraceState
#endif
)
    : macro_ui_(state.macroUi)
    , structure_workflow_(
          MacroStructureWorkflow::StateRefs{
              state.macroUi,
              state.pages,
              state.trackNavigation,
              state.sharedTrackActive,
              state.navigationFocus,
              state.structureClipboard,
          },
          structureServices
      )
    , performance_services_(performanceServices)
    , performance_workflow_(
          MacroPerformanceModeWorkflow::StateRefs{
              state.macroUi,
              state.pages,
              state.trackNavigation,
          },
          performanceServices,
          overlays,
          encoders
      )
    , overlays_(overlays)
    , encoders_(encoders)
    , buttons_(buttons)
    , scope_id_(scopeId)
    , time_provider_(timeProvider ? timeProvider : core::time_compat::millis)
#if defined(MS_UX_RECORDER)
    , ux_trace_state_(uxTraceState)
#endif
{
    setupBindings();
}

FLASHMEM void MacroPerformanceHandler::setupBindings() {
    for (uint8_t i = 0; i < core::state::macro::MACRO_COUNT; ++i) {
        buttons_.button(Config::MACRO_BUTTONS[i])
            .press()
            .scope(scope_id_)
            .then([this, i]() { beginMacroButtonGesture(i); });

        buttons_.button(Config::MACRO_BUTTONS[i])
            .release()
            .scope(scope_id_)
            .then([this, i]() { releaseMacroButtonGesture(i); });
    }

    buttons_.button(Config::ButtonID::LEFT_BOTTOM)
        .press()
        .scope(scope_id_)
        .when([this]() {
            return !structure_workflow_.selectionActive() &&
                   policyAllows(MacroAction::OPEN_SLOT_PROPERTIES);
        })
        .then([this]() { performance_workflow_.openEditPrompt(); });

    buttons_.button(Config::ButtonID::LEFT_CENTER)
        .press()
        .scope(scope_id_)
        .when([this]() {
            return !structure_workflow_.selectionActive() &&
                   MacroPolicy::performanceAvailable(interactionContext());
        })
        .then([this]() { (void)performance_services_.armAutomationTake(); });

    buttons_.button(Config::ButtonID::LEFT_CENTER)
        .release()
        .scope(scope_id_)
        .then([this]() {
            (void)performance_services_.releaseAutomationTake(time_provider_());
        });

    buttons_.button(Config::ButtonID::NAV)
        .press()
        .scope(scope_id_)
        .when([this]() {
            return !structure_workflow_.selectionActive() &&
                   MacroPolicy::performanceAvailable(interactionContext());
        })
        .then([this]() { beginContextSelector(); });

    encoders_.encoder(Config::EncoderID::NAV)
        .turn()
        .scope(scope_id_)
        .when([this]() { return context_selector_gesture_.active(); })
        .then([this](float delta) { moveContextSelector(delta); });

    buttons_.button(Config::ButtonID::NAV)
        .longPress(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS)
        .scope(scope_id_)
        .when([this]() { return context_selector_gesture_.active(); })
        .then([this]() { context_selector_gesture_.hold(); });

    buttons_.button(Config::ButtonID::NAV)
        .release()
        .scope(scope_id_)
        .when([this]() {
            return context_selector_gesture_.active() ||
                   structure_workflow_.selectionActive() ||
                   policyAllows(MacroAction::COMMIT_OR_CYCLE_STRUCTURE) ||
                   policyAllows(MacroAction::CREATE_PREVIEWED_STRUCTURE);
        })
        .then([this]() {
            if (context_selector_gesture_.active()) {
                releaseContextSelector();
#if defined(MS_UX_RECORDER)
                if (ux_trace_state_) ux_trace_state_->ignoreNextNavRelease = false;
#endif
                return;
            }
            if (structure_workflow_.selectionActive()) {
                if (structure_workflow_.selectionInteractionPolicy().navRelease ==
                    SelectionAction::TOGGLE_ITEM) {
                    structure_workflow_.toggleSelectionAtCursor();
                }
                return;
            }
            const auto action = MacroPolicy::navRelease(interactionContext());
            if (action == MacroAction::CREATE_PREVIEWED_STRUCTURE) {
                structure_workflow_.createPreviewedStructure();
                performance_workflow_.refreshEncoders();
                return;
            }
            if (action != MacroAction::COMMIT_OR_CYCLE_STRUCTURE) return;
            if (structure_workflow_.commitPreviewedPageIfNeeded()) {
                performance_workflow_.refreshEncoders();
                return;
            }
            structure_workflow_.cycleNavigationFocus();
        });

    buttons_.button(Config::ButtonID::LEFT_BOTTOM)
        .release()
        .scope(scope_id_)
        .then([this]() {
            if (!structure_workflow_.selectionActive() &&
                policyAllows(MacroAction::APPLY_SLOT_PROPERTIES)) {
                performance_workflow_.closePerformanceOverlay();
            }
        });

    encoders_.encoder(Config::EncoderID::NAV)
        .turn()
        .scope(scope_id_)
        .when([this]() { return policyAllows(MacroAction::MOVE_SLOT_PROPERTY); })
        .then([this](float delta) {
            if (performance_services_.automationTakeArmed()) {
                performance_workflow_.navigateTakeTiming(delta);
            }
        });

    encoders_.encoder(Config::EncoderID::NAV)
        .turn()
        .scope(scope_id_)
        .when([this]() {
            return !structure_workflow_.selectionActive() &&
                   !context_selector_gesture_.active() &&
                   policyAllows(MacroAction::MOVE_STRUCTURE);
        })
        .then([this](float delta) {
            structure_workflow_.moveByFocus(delta);
            performance_workflow_.refreshEncoders();
        });

    encoders_.encoder(Config::EncoderID::NAV)
        .turn()
        .scope(scope_id_)
        .when([this]() {
            return structure_workflow_.selectionInteractionPolicy().navTurn ==
                   SelectionAction::MOVE_CURSOR;
        })
        .then([this](float delta) {
            structure_workflow_.navigateSelection(delta);
        });

    buttons_.button(Config::ButtonID::BOTTOM_LEFT)
        .press()
        .scope(scope_id_)
        .when([this]() {
            return !structure_workflow_.selectionActive() &&
                   (policyAllows(MacroAction::CLEAR_STRUCTURE) ||
                    policyAllows(MacroAction::REMOVE_STRUCTURE));
        })
        .then([this]() {
            ignore_next_bottom_left_release_ = false;
#if defined(MS_UX_RECORDER)
            if (ux_trace_state_) ux_trace_state_->ignoreNextBottomLeftRelease = false;
#endif
            (void)structure_workflow_.beginHoldAction(
                core::state::StructureHoldAction::REMOVE,
                structure_workflow_.canRemoveCurrentStructure()
            );
        });

    buttons_.button(Config::ButtonID::BOTTOM_LEFT)
        .release()
        .scope(scope_id_)
        .when([this]() {
            return !structure_workflow_.selectionActive() &&
                   (ignore_next_bottom_left_release_ ||
                   structure_workflow_.hasCapturedAction(
                       core::state::StructureHoldAction::REMOVE
                   ) ||
                   policyAllows(MacroAction::CLEAR_STRUCTURE));
        })
        .then([this]() {
            const bool clearAllowed = policyAllows(MacroAction::CLEAR_STRUCTURE);
            if (ignore_next_bottom_left_release_) {
                ignore_next_bottom_left_release_ = false;
#if defined(MS_UX_RECORDER)
                if (ux_trace_state_) ux_trace_state_->ignoreNextBottomLeftRelease = false;
#endif
                return;
            }
            if (!structure_workflow_.releaseShortHoldAction(
                    core::state::StructureHoldAction::REMOVE
                )) {
                return;
            }
            // Macro Slot scope reserves this gesture for the guarded Remove
            // hold. Releasing early only cancels the pending hold; source-level
            // Clear remains an explicit action in the typed detail overlay.
            if (!clearAllowed) return;
            structure_workflow_.applyCurrentStructureShortPress();
            performance_workflow_.refreshEncoders();
        });

    buttons_.button(Config::ButtonID::BOTTOM_LEFT)
        .longPress(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS)
        .scope(scope_id_)
        .when([this]() {
            return !structure_workflow_.selectionActive() &&
                   policyAllows(MacroAction::REMOVE_STRUCTURE);
        })
        .then([this]() {
            ignore_next_bottom_left_release_ = true;
#if defined(MS_UX_RECORDER)
            if (ux_trace_state_) ux_trace_state_->ignoreNextBottomLeftRelease = true;
#endif
            if (structure_workflow_.commitHoldAction(
                    core::state::StructureHoldAction::REMOVE
                )) {
                performance_workflow_.refreshEncoders();
            }
        });

    buttons_.button(Config::ButtonID::BOTTOM_RIGHT)
        .press()
        .scope(scope_id_)
        .when([this]() {
            return !structure_workflow_.selectionActive() &&
                   (policyAllows(MacroAction::COPY_STRUCTURE) ||
                    policyAllows(MacroAction::PASTE_STRUCTURE));
        })
        .then([this]() {
#if defined(MS_UX_RECORDER)
            if (ux_trace_state_) ux_trace_state_->ignoreNextBottomRightRelease = false;
#endif
            paste_only_press_active_ = false;
            const bool canPaste =
                structure_workflow_.canPasteCurrentStructure();
            const bool captured = structure_workflow_.beginHoldAction(
                core::state::StructureHoldAction::PASTE,
                canPaste
            );
            paste_only_press_active_ = captured && canPaste &&
                !policyAllows(MacroAction::COPY_STRUCTURE);
        });

    buttons_.button(Config::ButtonID::BOTTOM_RIGHT)
        .release()
        .scope(scope_id_)
        .when([this]() {
            return !structure_workflow_.selectionActive() &&
                   (ignore_next_bottom_right_release_ ||
                   structure_workflow_.hasCapturedAction(
                       core::state::StructureHoldAction::PASTE
                   ) ||
                   policyAllows(MacroAction::COPY_STRUCTURE));
        })
        .then([this]() {
            const bool copyAllowed = policyAllows(MacroAction::COPY_STRUCTURE);
            const bool pasteOnlyPress = paste_only_press_active_;
            paste_only_press_active_ = false;
            if (ignore_next_bottom_right_release_) {
                ignore_next_bottom_right_release_ = false;
#if defined(MS_UX_RECORDER)
                if (ux_trace_state_) ux_trace_state_->ignoreNextBottomRightRelease = false;
#endif
                return;
            }
            if (!structure_workflow_.releaseShortHoldAction(
                    core::state::StructureHoldAction::PASTE
                )) {
                return;
            }
            // An early release while Paste is armed only cancels the guarded
            // hold. It must never fall through to Copy after focus changes.
            if (pasteOnlyPress || !copyAllowed) return;
            structure_workflow_.copyCurrentStructure();
        });

    buttons_.button(Config::ButtonID::BOTTOM_RIGHT)
        .longPress(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS)
        .scope(scope_id_)
        .when([this]() {
            return !structure_workflow_.selectionActive() &&
                   policyAllows(MacroAction::PASTE_STRUCTURE);
        })
        .then([this]() {
            ignore_next_bottom_right_release_ = true;
#if defined(MS_UX_RECORDER)
            if (ux_trace_state_) ux_trace_state_->ignoreNextBottomRightRelease = true;
#endif
            if (structure_workflow_.commitHoldAction(
                    core::state::StructureHoldAction::PASTE
                )) {
                performance_workflow_.refreshEncoders();
            }
        });

    buttons_.button(Config::ButtonID::BOTTOM_RIGHT)
        .press()
        .scope(scope_id_)
        .when([this]() {
            return structure_workflow_.selectionActive();
        })
        .then([this]() {
            ignore_next_bottom_right_release_ = false;
            selection_paste_press_active_ = false;
            if (structure_workflow_.selectionInteractionPolicy()
                    .bottomRightLongPress ==
                SelectionAction::PASTE_SELECTION) {
                selection_paste_press_active_ = true;
                selection_paste_anchor_ =
                    structure_workflow_.selectionCursor();
                selection_paste_clipboard_revision_ =
                    structure_workflow_.selectionClipboardRevision();
                macro_ui_.pageHold.begin(
                    core::state::StructureHoldAction::PASTE,
                    time_provider_()
                );
            }
        });

    buttons_.button(Config::ButtonID::BOTTOM_RIGHT)
        .release()
        .scope(scope_id_)
        .when([this]() {
            return structure_workflow_.selectionActive();
        })
        .then([this]() {
            macro_ui_.pageHold.clear();
            selection_paste_press_active_ = false;
            if (ignore_next_bottom_right_release_) {
                ignore_next_bottom_right_release_ = false;
                return;
            }
            if (structure_workflow_.selectionInteractionPolicy()
                    .bottomRightRelease ==
                SelectionAction::COPY_SELECTION) {
                (void)structure_workflow_.copySelection();
            }
        });

    buttons_.button(Config::ButtonID::BOTTOM_RIGHT)
        .longPress(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS)
        .scope(scope_id_)
        .when([this]() {
            return selection_paste_press_active_ &&
                   structure_workflow_.selectionInteractionPolicy()
                           .bottomRightLongPress ==
                       SelectionAction::PASTE_SELECTION &&
                   selection_paste_anchor_ ==
                       structure_workflow_.selectionCursor() &&
                   selection_paste_clipboard_revision_ ==
                       structure_workflow_.selectionClipboardRevision() &&
                   structure_workflow_.canPasteSelection();
        })
        .then([this]() {
            ignore_next_bottom_right_release_ = true;
            selection_paste_press_active_ = false;
            macro_ui_.pageHold.clear();
            if (structure_workflow_.pasteSelection()) {
                performance_workflow_.refreshEncoders();
            }
        });

    buttons_.button(Config::ButtonID::LEFT_TOP)
        .release()
        .scope(scope_id_)
        .when([this]() {
            return !structure_workflow_.selectionActive() &&
                   policyAllows(MacroAction::CANCEL_SLOT_PROPERTIES);
        })
        .then([this]() {
            if (!performance_services_.cancelAutomationTake()) {
                performance_workflow_.closePerformanceOverlay();
            }
        });

    buttons_.button(Config::ButtonID::LEFT_TOP)
        .release()
        .scope(scope_id_)
        .when([this]() {
            return structure_workflow_.selectionInteractionPolicy()
                       .leftTopRelease != SelectionAction::NONE;
        })
        .then([this]() {
            (void)structure_workflow_.backSelectionMode();
            performance_workflow_.refreshEncoders();
        });
}

FLASHMEM void MacroPerformanceHandler::attachEditors(
    MacroEditHandler& macroEditor,
    ProjectTrackEditorHandler& trackEditor
) {
    macro_editor_ = &macroEditor;
    track_editor_ = &trackEditor;
}

FLASHMEM void MacroPerformanceHandler::beginContextSelector() {
    context_selector_gesture_.press();
    // The shared navigation focus is already the applied authority.
    auto focus = interactionContext().navigationFocus;
    macro_ui_.contextSelector.show(focus);
}

FLASHMEM void MacroPerformanceHandler::moveContextSelector(float delta) {
    if (!context_selector_gesture_.turn(nav::hasTurnDelta(delta))) return;
    constexpr core::state::StructureNavigationFocus order[] = {
        core::state::StructureNavigationFocus::TRACK,
        core::state::StructureNavigationFocus::PAGE,
        core::state::StructureNavigationFocus::STEP,
    };
    int current = 1;
    for (int i = 0; i < 3; ++i) {
        if (order[i] == macro_ui_.contextSelector.previewFocus) current = i;
    }
    const int next = (current + (delta > 0.0f ? 1 : 2)) % 3;
    macro_ui_.contextSelector.preview(order[next]);
}

FLASHMEM void MacroPerformanceHandler::releaseContextSelector() {
    const auto selected = macro_ui_.contextSelector.previewFocus;
    const auto release = context_selector_gesture_.release();
    macro_ui_.contextSelector.hide();
    if (release == PressHoldTurnReleaseGesture::Release::TURN) {
        structure_workflow_.setNavigationFocus(selected);
        performance_workflow_.refreshEncoders();
        return;
    }
    if (release == PressHoldTurnReleaseGesture::Release::HOLD) {
        structure_workflow_.setNavigationFocus(selected);
        structure_workflow_.enterSelectionModeForCurrentFocus();
        performance_workflow_.refreshEncoders();
        return;
    }
    if (release != PressHoldTurnReleaseGesture::Release::TAP) return;
    if (selected == core::state::StructureNavigationFocus::STEP) {
        if (structure_workflow_.interactionContext(false, false).previewingAddSlot) {
            structure_workflow_.createPreviewedStructure();
            performance_workflow_.refreshEncoders();
            return;
        }
        if (macro_editor_ != nullptr) {
            macro_editor_->openFocusedMacro(
                macro_ui_.focusedMacroSlot.get()
            );
        }
        return;
    }
    if (selected == core::state::StructureNavigationFocus::PAGE) {
        if (structure_workflow_.interactionContext(false, false).previewingAddSlot) {
            structure_workflow_.createPreviewedStructure();
            performance_workflow_.refreshEncoders();
        }
        return;
    }
    if (selected == core::state::StructureNavigationFocus::TRACK) {
        if (structure_workflow_.interactionContext(false, false).previewingAddSlot) {
            structure_workflow_.createPreviewedStructure();
            performance_workflow_.refreshEncoders();
        } else if (track_editor_ != nullptr) {
            (void)track_editor_->openActiveTrack();
        }
    }
}

FLASHMEM void MacroPerformanceHandler::beginMacroButtonGesture(uint8_t index) {
    if (index >= core::state::macro::MACRO_COUNT) return;
    const uint16_t bit = static_cast<uint16_t>(1U << index);
    owned_macro_button_mask_ = static_cast<uint16_t>(owned_macro_button_mask_ & ~bit);
    selection_macro_button_mask_ = static_cast<uint16_t>(
        selection_macro_button_mask_ & ~bit
    );

    if (structure_workflow_.slotSelectionActive()) {
        selection_macro_button_mask_ = static_cast<uint16_t>(
            selection_macro_button_mask_ | bit
        );
        return;
    }
    if (structure_workflow_.selectionActive()) return;

    const bool editChord =
        buttons_.isPressed(Config::ButtonID::LEFT_BOTTOM) ||
        macro_ui_.performanceOverlayMode.get() ==
            core::state::macro::MacroPerformanceOverlayMode::EDIT;
    if (editChord) {
        macro_ui_.focusedMacroSlot.set(index);
        if (performance_services_.isMacroSlotActive(index) &&
            macro_editor_ != nullptr &&
            !overlays_.hasVisible()) {
            macro_editor_->openFocusedMacro(index);
        }
        return;
    }

    if (!MacroPolicy::performanceAvailable(interactionContext())) {
        return;
    }
    owned_macro_button_mask_ = static_cast<uint16_t>(owned_macro_button_mask_ | bit);
}

FLASHMEM void MacroPerformanceHandler::releaseMacroButtonGesture(uint8_t index) {
    if (index >= core::state::macro::MACRO_COUNT) return;
    const uint16_t bit = static_cast<uint16_t>(1U << index);
    const bool selectionOwned =
        (selection_macro_button_mask_ & bit) != 0U;
    selection_macro_button_mask_ = static_cast<uint16_t>(
        selection_macro_button_mask_ & ~bit
    );
    if (selectionOwned) {
        structure_workflow_.toggleSlotSelectionAtPageIndex(index);
        return;
    }
    const bool owned = (owned_macro_button_mask_ & bit) != 0U;
    owned_macro_button_mask_ = static_cast<uint16_t>(owned_macro_button_mask_ & ~bit);
    if (!owned) return;

    macro_ui_.focusedMacroSlot.set(index);
    macro_ui_.armPostTakeInputGuard(bit, time_provider_());
    if (!performance_services_.isMacroSlotActive(index)) {
        if (performance_services_.activateMacroSlot(index)) {
            performance_workflow_.refreshEncoders();
        }
        return;
    }
    if (performance_services_.manualOverrideActiveFor(index)) {
        (void)performance_services_.resumeComputedSources(index);
        return;
    }
    if (performance_services_.automationPlaybackActiveFor(index)) {
        (void)performance_services_.takeManualControl(
            index,
            performance_services_.currentPlaybackBaseValue(index),
            false
        );
    }
}

core::state::macro::MacroInteractionContext
FLASHMEM MacroPerformanceHandler::interactionContext() const {
    return structure_workflow_.interactionContext(
        overlays_.hasVisible(),
        performance_workflow_.performanceOverlayActive()
    );
}

FLASHMEM bool MacroPerformanceHandler::policyAllows(
    core::state::macro::MacroInteractionAction action
) const {
    const auto context = interactionContext();
    switch (action) {
        case MacroAction::MOVE_STRUCTURE:
            return MacroPolicy::navTurn(context) == action;
        case MacroAction::MOVE_SLOT_PROPERTY:
            return MacroPolicy::navTurn(context) == action;
        case MacroAction::COMMIT_OR_CYCLE_STRUCTURE:
            return MacroPolicy::navRelease(context) == action;
        case MacroAction::CREATE_PREVIEWED_STRUCTURE:
            return MacroPolicy::navRelease(context) == action;
        case MacroAction::EDIT_SLOT_PROPERTY:
            return MacroPolicy::optTurn(context) == action;
        case MacroAction::CANCEL_SLOT_PROPERTIES:
            return MacroPolicy::leftTopRelease(context) == action;
        case MacroAction::OPEN_SLOT_PROPERTIES:
            return MacroPolicy::leftBottomPress(context) == action;
        case MacroAction::APPLY_SLOT_PROPERTIES:
            return MacroPolicy::leftBottomRelease(context) == action;
        case MacroAction::CLEAR_STRUCTURE:
            return MacroPolicy::bottomLeftRelease(context) == action;
        case MacroAction::REMOVE_STRUCTURE:
            return MacroPolicy::bottomLeftLongPress(context) == action;
        case MacroAction::COPY_STRUCTURE:
            return MacroPolicy::bottomRightRelease(context) == action;
        case MacroAction::PASTE_STRUCTURE:
            return MacroPolicy::bottomRightLongPress(context) == action;
        case MacroAction::NONE:
        default:
            return false;
    }
}

}  // namespace core::handler
