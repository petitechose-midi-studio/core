#include "handler/macro/MacroPerformanceHandler.hpp"

#include <config/PlatformCompat.hpp>

#if defined(MS_UX_RECORDER)
#include "validation/ux/SemanticUxTraceState.hpp"
#endif

namespace core::handler {

namespace {

using MacroAction = core::state::macro::MacroInteractionAction;
using MacroPolicy = core::state::macro::MacroInteractionPolicy;

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
    : structure_workflow_(
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
    , navigation_focus_(state.navigationFocus)
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
    buttons_.button(Config::ButtonID::LEFT_BOTTOM)
        .press()
        .scope(scope_id_)
        .when([this]() {
            return policyAllows(MacroAction::OPEN_SLOT_PROPERTIES);
        })
        .then([this]() { performance_workflow_.openEditPrompt(); });

    buttons_.button(Config::ButtonID::LEFT_CENTER)
        .press()
        .scope(scope_id_)
        .when([this]() {
            return MacroPolicy::performanceAvailable(interactionContext());
        })
        .then([this]() { (void)performance_services_.armAutomationTake(); });

    buttons_.button(Config::ButtonID::LEFT_CENTER)
        .release()
        .scope(scope_id_)
        .then([this]() {
            (void)performance_services_.releaseAutomationTake(time_provider_());
        });

    encoders_.encoder(Config::EncoderID::NAV)
        .turn()
        .scope(scope_id_)
        .when([this]() { return policyAllows(MacroAction::MOVE_SELECTION_CURSOR); })
        .then([this](float delta) { structure_workflow_.navigateSelection(delta); });

    buttons_.button(Config::ButtonID::NAV)
        .longPress(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS)
        .scope(scope_id_)
        .when([this]() { return policyAllows(MacroAction::ENTER_SELECTION); })
        .then([this]() {
            nav_long_press_used_ = true;
            structure_workflow_.enterSelectionModeForCurrentFocus();
        });

    buttons_.button(Config::ButtonID::NAV)
        .release()
        .scope(scope_id_)
        .when([this]() { return nav_long_press_used_ || policyAllows(MacroAction::TOGGLE_SELECTION); })
        .then([this]() {
            if (nav_long_press_used_) {
                nav_long_press_used_ = false;
                return;
            }
            structure_workflow_.toggleSelectionAtCursor();
        });

    buttons_.button(Config::ButtonID::LEFT_BOTTOM)
        .release()
        .scope(scope_id_)
        .then([this]() {
            if (policyAllows(MacroAction::APPLY_SLOT_PROPERTIES)) {
                performance_workflow_.closePerformanceOverlay();
            }
        });

    buttons_.button(Config::ButtonID::NAV)
        .release()
        .scope(scope_id_)
        .when([this]() {
            return policyAllows(MacroAction::COMMIT_OR_CYCLE_STRUCTURE) ||
                   policyAllows(MacroAction::CREATE_PREVIEWED_STRUCTURE);
        })
        .then([this]() {
            if (nav_long_press_used_) {
                nav_long_press_used_ = false;
                return;
            }
            if (structure_workflow_.previewingAddSlot()) {
                structure_workflow_.createPreviewedStructure();
                performance_workflow_.refreshEncoders();
                return;
            }
            if (structure_workflow_.commitPreviewedPageIfNeeded()) {
                performance_workflow_.refreshEncoders();
                return;
            }
            structure_workflow_.cycleNavigationFocus();
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
        .when([this]() { return policyAllows(MacroAction::MOVE_STRUCTURE); })
        .then([this](float delta) {
            structure_workflow_.moveByFocus(delta);
            performance_workflow_.refreshEncoders();
        });

    buttons_.button(Config::ButtonID::LEFT_TOP)
        .release()
        .scope(scope_id_)
        .when([this]() { return policyAllows(MacroAction::CANCEL_SELECTION); })
        .then([this]() { structure_workflow_.cancelSelectionMode(); });

    buttons_.button(Config::ButtonID::BOTTOM_LEFT)
        .press()
        .scope(scope_id_)
        .when([this]() {
            return policyAllows(MacroAction::CLEAR_STRUCTURE) ||
                   policyAllows(MacroAction::REMOVE_STRUCTURE) ||
                   policyAllows(MacroAction::DELETE_SELECTION);
        })
        .then([this]() {
            ignore_next_bottom_left_release_ = false;
            selection_delete_press_active_ = false;
#if defined(MS_UX_RECORDER)
            if (ux_trace_state_) ux_trace_state_->ignoreNextBottomLeftRelease = false;
#endif
            if (policyAllows(MacroAction::DELETE_SELECTION)) {
                selection_delete_press_active_ =
                    structure_workflow_.beginSelectionDeleteGuard(time_provider_());
                return;
            }
            if (structure_workflow_.canRemoveCurrentStructure()) {
                structure_workflow_.beginHoldAction(core::state::StructureHoldAction::REMOVE);
            }
        });

    buttons_.button(Config::ButtonID::BOTTOM_LEFT)
        .release()
        .scope(scope_id_)
        .when([this]() { return selection_delete_press_active_; })
        .then([this]() {
            selection_delete_press_active_ = false;
            structure_workflow_.cancelSelectionDeleteGuard(time_provider_());
            // The gesture began as selection deletion. Consume this physical
            // release even if another input changed the interaction context
            // while the button was held.
            ignore_next_bottom_left_release_ = true;
        });

    buttons_.button(Config::ButtonID::BOTTOM_LEFT)
        .release()
        .scope(scope_id_)
        .when([this]() {
            return ignore_next_bottom_left_release_ ||
                   structure_workflow_.hasHoldAction(
                       core::state::StructureHoldAction::REMOVE
                   ) ||
                   policyAllows(MacroAction::CLEAR_STRUCTURE);
        })
        .then([this]() {
            const bool clearAllowed = policyAllows(MacroAction::CLEAR_STRUCTURE);
            structure_workflow_.clearHoldAction();
            if (ignore_next_bottom_left_release_) {
                ignore_next_bottom_left_release_ = false;
#if defined(MS_UX_RECORDER)
                if (ux_trace_state_) ux_trace_state_->ignoreNextBottomLeftRelease = false;
#endif
                return;
            }
            // Macro Slot scope reserves this gesture for the guarded Remove
            // hold. Releasing early only cancels the pending hold; source-level
            // Clear remains an explicit action in the typed detail overlay.
            if (!clearAllowed) return;
            structure_workflow_.eraseCurrentStructure();
            performance_workflow_.refreshEncoders();
        });

    buttons_.button(Config::ButtonID::BOTTOM_LEFT)
        .longPress(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS)
        .scope(scope_id_)
        .when([this]() {
            return policyAllows(MacroAction::REMOVE_STRUCTURE) ||
                   policyAllows(MacroAction::DELETE_SELECTION);
        })
        .then([this]() {
            if (policyAllows(MacroAction::DELETE_SELECTION)) {
                const bool applied = structure_workflow_.commitSelectionDeleteGuard(
                    time_provider_()
                );
                if (applied) {
                    ignore_next_bottom_left_release_ = true;
#if defined(MS_UX_RECORDER)
                    if (ux_trace_state_) {
                        ux_trace_state_->ignoreNextBottomLeftRelease = true;
                    }
#endif
                    performance_workflow_.refreshEncoders();
                }
                return;
            }
            structure_workflow_.clearHoldAction();
            ignore_next_bottom_left_release_ = true;
#if defined(MS_UX_RECORDER)
            if (ux_trace_state_) ux_trace_state_->ignoreNextBottomLeftRelease = true;
#endif
            structure_workflow_.removeCurrentStructure();
            performance_workflow_.refreshEncoders();
        });

    buttons_.button(Config::ButtonID::BOTTOM_RIGHT)
        .press()
        .scope(scope_id_)
        .when([this]() {
            return policyAllows(MacroAction::COPY_STRUCTURE) ||
                   policyAllows(MacroAction::PASTE_STRUCTURE);
        })
        .then([this]() {
#if defined(MS_UX_RECORDER)
            if (ux_trace_state_) ux_trace_state_->ignoreNextBottomRightRelease = false;
#endif
            if (structure_workflow_.canPasteCurrentStructure()) {
                structure_workflow_.beginHoldAction(core::state::StructureHoldAction::PASTE);
            }
        });

    buttons_.button(Config::ButtonID::BOTTOM_RIGHT)
        .release()
        .scope(scope_id_)
        .when([this]() { return policyAllows(MacroAction::DUPLICATE_SELECTION); })
        .then([this]() {
            structure_workflow_.duplicateSelection();
            performance_workflow_.refreshEncoders();
        });

    buttons_.button(Config::ButtonID::BOTTOM_RIGHT)
        .release()
        .scope(scope_id_)
        .when([this]() { return policyAllows(MacroAction::COPY_STRUCTURE); })
        .then([this]() {
            structure_workflow_.clearHoldAction();
            if (ignore_next_bottom_right_release_) {
                ignore_next_bottom_right_release_ = false;
#if defined(MS_UX_RECORDER)
                if (ux_trace_state_) ux_trace_state_->ignoreNextBottomRightRelease = false;
#endif
                return;
            }
            structure_workflow_.copyCurrentStructure();
        });

    buttons_.button(Config::ButtonID::BOTTOM_RIGHT)
        .longPress(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS)
        .scope(scope_id_)
        .when([this]() { return policyAllows(MacroAction::PASTE_STRUCTURE); })
        .then([this]() {
            structure_workflow_.clearHoldAction();
            ignore_next_bottom_right_release_ = true;
#if defined(MS_UX_RECORDER)
            if (ux_trace_state_) ux_trace_state_->ignoreNextBottomRightRelease = true;
#endif
            structure_workflow_.pasteCurrentStructure();
            performance_workflow_.refreshEncoders();
        });

    buttons_.button(Config::ButtonID::LEFT_TOP)
        .release()
        .scope(scope_id_)
        .when([this]() { return policyAllows(MacroAction::CANCEL_SLOT_PROPERTIES); })
        .then([this]() {
            if (!performance_services_.cancelAutomationTake()) {
                performance_workflow_.closePerformanceOverlay();
            }
        });
}

FLASHMEM void MacroPerformanceHandler::update(uint32_t nowMs) {
    structure_workflow_.updateSelectionDeleteGuard(nowMs);
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
        case MacroAction::MOVE_SELECTION_CURSOR:
            return MacroPolicy::navTurn(context) == action;
        case MacroAction::MOVE_SLOT_PROPERTY:
            return MacroPolicy::navTurn(context) == action;
        case MacroAction::COMMIT_OR_CYCLE_STRUCTURE:
            return MacroPolicy::navRelease(context, nav_long_press_used_) == action;
        case MacroAction::CREATE_PREVIEWED_STRUCTURE:
            return MacroPolicy::navRelease(context, nav_long_press_used_) == action;
        case MacroAction::TOGGLE_SELECTION:
            return MacroPolicy::navRelease(context, nav_long_press_used_) == action;
        case MacroAction::ENTER_SELECTION:
            return MacroPolicy::navLongPress(context) == action;
        case MacroAction::EDIT_SLOT_PROPERTY:
            return MacroPolicy::optTurn(context) == action;
        case MacroAction::CANCEL_SLOT_PROPERTIES:
            return MacroPolicy::leftTopRelease(context) == action;
        case MacroAction::CANCEL_SELECTION:
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
        case MacroAction::DELETE_SELECTION:
            return MacroPolicy::bottomLeftLongPress(context) == action;
        case MacroAction::DUPLICATE_SELECTION:
            return MacroPolicy::bottomRightRelease(context) == action;
        case MacroAction::NONE:
        default:
            return false;
    }
}

}  // namespace core::handler
