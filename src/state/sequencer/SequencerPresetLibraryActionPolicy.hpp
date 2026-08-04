#pragma once

#include <config/PlatformCompat.hpp>

#include "config/Timing.hpp"
#include "state/contextual/ContextActionSpec.hpp"

namespace core::state::sequencer::preset_library_action_policy {

/**
 * Projects the shared Save-new / guarded-overwrite contract.
 *
 * Domain action builders initialize scope/source/target, then delegate this
 * branch before projecting their domain-specific Load behavior.
 */
FLASHMEM inline bool projectSaveAction(
    contextual::ContextActionSpec& spec,
    bool saveMode,
    bool selectedNewAsset,
    bool hasFocusedAsset,
    bool targetCanSave,
    contextual::ContextActionReason unavailableCreateReason
) {
    if (!saveMode) return false;

    if (selectedNewAsset) {
        spec.tap.action = contextual::ContextActionId::SAVE;
        spec.tap.impact = contextual::ContextActionImpact::CONSTRUCTIVE;
        spec.tap.availability = targetCanSave
            ? contextual::ContextActionAvailability::AVAILABLE
            : contextual::ContextActionAvailability::DISABLED;
        spec.tap.reason = targetCanSave
            ? contextual::ContextActionReason::NONE
            : unavailableCreateReason;
        spec.tap.visual = {
            contextual::ContextIconId::SAVE,
            contextual::ContextTone::GREEN,
        };
        return true;
    }

    spec.hold.action = contextual::ContextActionId::SAVE;
    spec.hold.impact = contextual::ContextActionImpact::OVERWRITE;
    spec.hold.availability = hasFocusedAsset && targetCanSave
        ? contextual::ContextActionAvailability::WARNING
        : contextual::ContextActionAvailability::DISABLED;
    spec.hold.reason = hasFocusedAsset && targetCanSave
        ? contextual::ContextActionReason::NONE
        : contextual::ContextActionReason::NO_ACTION;
    spec.hold.visual = {
        contextual::ContextIconId::SAVE,
        contextual::ContextTone::AMBER,
    };
    spec.guard = {
        contextual::ContextGuardKind::HOLD,
        static_cast<uint16_t>(
            Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS
        ),
    };
    return true;
}

}  // namespace core::state::sequencer::preset_library_action_policy
