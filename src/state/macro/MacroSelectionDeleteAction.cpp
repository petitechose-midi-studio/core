#include "state/macro/MacroSelectionDeleteAction.hpp"

#include <config/PlatformCompat.hpp>

#include "config/Timing.hpp"
#include "state/macro/MacroPagesState.hpp"
#include "state/shared/StructureSlotOps.hpp"

namespace core::state::macro {

namespace contextual = core::state::contextual;
namespace structure_slots = core::state::shared;

namespace {

FLASHMEM bool feedbackTargetsAction(
    const contextual::OperationFeedbackState& feedback,
    const contextual::ContextActionSpec& action
) {
    return feedback.active && feedback.action == action.hold.action &&
           feedback.source == action.source && feedback.target == action.target;
}

}  // namespace

FLASHMEM contextual::ContextActionSpec buildMacroSelectionDeleteActionSpec(
    const MacroSelectionDeleteSource& source
) {
    contextual::ContextActionSpec spec;
    const bool trackScope =
        source.scope == core::state::StructureSelectionScope::TRACK;
    const uint8_t count = trackScope ? TRACK_COUNT : PAGE_COUNT;

    spec.scope = trackScope ? contextual::ContextScope::TRACK
                            : contextual::ContextScope::PAGE;
    spec.source.kind = contextual::ContextEntityKind::SELECTION;
    spec.source.track = source.activeTrack;
    spec.source.page = trackScope
        ? contextual::ContextEntityRef::UNUSED_INDEX
        : source.activePage;
    spec.source.item = source.selectedMask;
    spec.target.kind = trackScope ? contextual::ContextEntityKind::TRACK
                                  : contextual::ContextEntityKind::PAGE;
    spec.target.track = source.activeTrack;
    spec.target.page = trackScope ? contextual::ContextEntityRef::UNUSED_INDEX
                                  : source.activePage;
    spec.target.item = source.selectedMask;

    spec.hold.action = contextual::ContextActionId::REMOVE;
    spec.hold.impact = contextual::ContextActionImpact::DESTRUCTIVE;
    spec.hold.visual = {
        contextual::ContextIconId::REMOVE,
        contextual::ContextTone::RED,
    };
    spec.guard = {
        contextual::ContextGuardKind::HOLD,
        static_cast<uint16_t>(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS),
    };

    if (!source.active) {
        spec.hold.availability = contextual::ContextActionAvailability::DISABLED;
        spec.hold.reason = contextual::ContextActionReason::NO_ACTION;
        return spec;
    }

    const uint16_t selectedEnabled =
        static_cast<uint16_t>(source.selectedMask & source.enabledMask);
    if (selectedEnabled == 0) {
        spec.hold.availability = contextual::ContextActionAvailability::DISABLED;
        spec.hold.reason = contextual::ContextActionReason::EMPTY_SELECTION;
        return spec;
    }

    const auto preflight = structure_slots::removeSelected(
        source.enabledMask,
        selectedEnabled,
        source.currentIndex,
        count
    );
    if (!preflight.changed) {
        spec.hold.availability = contextual::ContextActionAvailability::DISABLED;
        spec.hold.reason = contextual::ContextActionReason::MINIMUM_CARDINALITY;
        return spec;
    }

    spec.hold.availability = contextual::ContextActionAvailability::AVAILABLE;
    spec.hold.reason = contextual::ContextActionReason::NONE;
    return spec;
}

FLASHMEM MacroSelectionDeletePresentationState
macroSelectionDeletePresentationState(
    const contextual::ContextActionSpec& action,
    const contextual::GuardedActionState& guard,
    const contextual::OperationFeedbackState& feedback
) {
    switch (guard.phase) {
        case contextual::GuardedActionPhase::PRESSED:
            return MacroSelectionDeletePresentationState::PRESSED;
        case contextual::GuardedActionPhase::ARMED:
        case contextual::GuardedActionPhase::COMMITTED:
            return MacroSelectionDeletePresentationState::ARMED;
        case contextual::GuardedActionPhase::CANCELLED:
            return MacroSelectionDeletePresentationState::CANCELLED;
        case contextual::GuardedActionPhase::IDLE:
        default:
            break;
    }

    if (feedbackTargetsAction(feedback, action)) {
        if (feedback.status == contextual::OperationFeedbackStatus::CANCELLED) {
            return MacroSelectionDeletePresentationState::CANCELLED;
        }
        if (feedback.status == contextual::OperationFeedbackStatus::APPLIED) {
            return MacroSelectionDeletePresentationState::APPLIED;
        }
    }

    return contextual::canExecute(action.hold)
        ? MacroSelectionDeletePresentationState::AVAILABLE
        : MacroSelectionDeletePresentationState::DISABLED;
}

}  // namespace core::state::macro
