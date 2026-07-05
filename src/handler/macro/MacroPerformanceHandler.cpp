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
    oc::type::ScopeID scopeId
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
#if defined(MS_UX_RECORDER)
    , ux_trace_state_(uxTraceState)
#endif
{
    setupBindings();
}

FLASHMEM void MacroPerformanceHandler::setupBindings() {
    buttons_.button(Config::ButtonID::LEFT_BOTTOM)
        .press()
        .latch()
        .scope(scope_id_)
        .when([this]() {
            left_bottom_held_ = true;
            return policyAllows(MacroAction::OPEN_SLOT_PROPERTIES);
        })
        .then([this]() { performance_workflow_.activateClutch(); });

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
            left_bottom_held_ = false;
            if (policyAllows(MacroAction::APPLY_SLOT_PROPERTIES)) {
                performance_workflow_.deactivateClutch();
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
        .then([this](float delta) { performance_workflow_.navigateProperty(delta); });

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
                   policyAllows(MacroAction::REMOVE_STRUCTURE);
        })
        .then([this]() {
#if defined(MS_UX_RECORDER)
            if (ux_trace_state_) ux_trace_state_->ignoreNextBottomLeftRelease = false;
#endif
            if (structure_workflow_.canRemoveCurrentStructure()) {
                structure_workflow_.beginHoldAction(core::state::StructureHoldAction::REMOVE);
            }
        });

    buttons_.button(Config::ButtonID::BOTTOM_LEFT)
        .release()
        .scope(scope_id_)
        .when([this]() { return policyAllows(MacroAction::DELETE_SELECTION); })
        .then([this]() {
            structure_workflow_.deleteSelection();
            performance_workflow_.refreshEncoders();
        });

    buttons_.button(Config::ButtonID::BOTTOM_LEFT)
        .release()
        .scope(scope_id_)
        .when([this]() { return policyAllows(MacroAction::CLEAR_STRUCTURE); })
        .then([this]() {
            structure_workflow_.clearHoldAction();
            if (ignore_next_bottom_left_release_) {
                ignore_next_bottom_left_release_ = false;
#if defined(MS_UX_RECORDER)
                if (ux_trace_state_) ux_trace_state_->ignoreNextBottomLeftRelease = false;
#endif
                return;
            }
            structure_workflow_.eraseCurrentStructure();
            performance_workflow_.refreshEncoders();
        });

    buttons_.button(Config::ButtonID::BOTTOM_LEFT)
        .longPress(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS)
        .scope(scope_id_)
        .when([this]() { return policyAllows(MacroAction::REMOVE_STRUCTURE); })
        .then([this]() {
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
        .then([this]() { performance_workflow_.cancelClutch(); });
}

bool MacroPerformanceHandler::selectionActive() const {
    return structure_workflow_.selectionActive() && !overlays_.hasVisible();
}

core::state::macro::MacroInteractionContext
MacroPerformanceHandler::interactionContext() const {
    return core::state::macro::MacroInteractionContext{
        .navigationFocus = navigation_focus_.get(),
        .blockingOverlay = overlays_.hasVisible(),
        .slotPropertySelecting = performance_workflow_.clutchActive(),
        .selectionActive = selectionActive(),
        .previewingAddSlot = structure_workflow_.previewingAddSlot(),
        .compatibleClipboardAvailable = structure_workflow_.canPasteCurrentStructure(),
        .canRemoveStructure = structure_workflow_.canRemoveCurrentStructure(),
    };
}

bool MacroPerformanceHandler::policyAllows(
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
            return MacroPolicy::bottomLeftRelease(context) == action;
        case MacroAction::DUPLICATE_SELECTION:
            return MacroPolicy::bottomRightRelease(context) == action;
        case MacroAction::NONE:
        default:
            return false;
    }
}

}  // namespace core::handler
