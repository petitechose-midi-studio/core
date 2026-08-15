#pragma once

#include <cstdint>

#include "state/interaction/ControllerInteractionContract.hpp"

namespace core::state {

enum class StructureSelectionPhase : uint8_t {
    INACTIVE = 0,
    SELECTING,
    PLACING,
};

enum class StructureSelectionInteractionAction : uint8_t {
    NONE = 0,
    ENTER_SELECTION,
    MOVE_CURSOR,
    TOGGLE_ITEM,
    CLEAR_CURRENT,
    EXIT_SELECTION,
    COPY_SELECTION,
    PASTE_SELECTION,
};

enum class StructureSelectionInteractionVisibility : uint8_t {
    HIDDEN = 0,
    DISABLED,
    ACTIVE,
};

/**
 * Domain-neutral facts required to resolve the shared selection grammar.
 *
 * `pasteAvailable` means the current clipboard, destination and capacity have
 * already passed the owning domain's preflight. The policy deliberately does
 * not inspect or mutate domain data.
 */
struct StructureSelectionInteractionContext {
    bool entryAvailable = false;
    bool active = false;
    bool placing = false;
    bool selectedItemsAvailable = false;
    bool pasteAvailable = false;
};

struct StructureSelectionInteractionPolicy {
    StructureSelectionPhase phase = StructureSelectionPhase::INACTIVE;
    StructureSelectionInteractionAction navLongPress =
        StructureSelectionInteractionAction::NONE;
    StructureSelectionInteractionAction navTurn =
        StructureSelectionInteractionAction::NONE;
    StructureSelectionInteractionAction navRelease =
        StructureSelectionInteractionAction::NONE;
    StructureSelectionInteractionAction leftTopRelease =
        StructureSelectionInteractionAction::NONE;
    StructureSelectionInteractionAction bottomRightRelease =
        StructureSelectionInteractionAction::NONE;
    StructureSelectionInteractionAction bottomRightLongPress =
        StructureSelectionInteractionAction::NONE;
    StructureSelectionInteractionVisibility bottomRightVisibility =
        StructureSelectionInteractionVisibility::HIDDEN;
};

/**
 * Resolves the mechanical selection/placement lifecycle shared by Macro and
 * Sequencer. Domain-specific cursor bounds, paste plans and mutations stay in
 * their owning workflows.
 */
StructureSelectionInteractionPolicy buildStructureSelectionInteractionPolicy(
    const StructureSelectionInteractionContext& context
);

core::state::interaction::ControllerIntent controllerIntentFor(
    StructureSelectionInteractionAction action
);

}  // namespace core::state
