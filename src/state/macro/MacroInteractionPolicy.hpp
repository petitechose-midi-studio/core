#pragma once

#include <cstdint>

#include "state/StructureNavigationState.hpp"

namespace core::state::macro {

enum class MacroInteractionAction : uint8_t {
    NONE = 0,
    MOVE_STRUCTURE,
    COMMIT_OR_CYCLE_STRUCTURE,
    CREATE_PREVIEWED_STRUCTURE,
    OPEN_SLOT_PROPERTIES,
    APPLY_SLOT_PROPERTIES,
    CANCEL_SLOT_PROPERTIES,
    MOVE_SLOT_PROPERTY,
    EDIT_SLOT_PROPERTY,
    CLEAR_STRUCTURE,
    REMOVE_STRUCTURE,
    COPY_STRUCTURE,
    PASTE_STRUCTURE,
};

enum class MacroInteractionVisibility : uint8_t {
    HIDDEN = 0,
    DISABLED,
    DIM,
    ACTIVE,
};

struct MacroInteractionContext {
    core::state::StructureNavigationFocus navigationFocus =
        core::state::StructureNavigationFocus::PAGE;
    bool blockingOverlay = false;
    bool slotPropertySelecting = false;
    bool previewingAddSlot = false;
    bool compatibleClipboardAvailable = false;
    bool canRemoveStructure = false;
};

struct MacroActionStripPolicy {
    MacroInteractionVisibility leftCenter = MacroInteractionVisibility::DIM;
    MacroInteractionVisibility leftBottom = MacroInteractionVisibility::DIM;
    MacroInteractionVisibility bottomLeft = MacroInteractionVisibility::ACTIVE;
    MacroInteractionVisibility bottomRight = MacroInteractionVisibility::ACTIVE;
};

class MacroInteractionPolicy {
public:
    static bool performanceAvailable(const MacroInteractionContext& context);
    static MacroInteractionAction navTurn(const MacroInteractionContext& context);
    static MacroInteractionAction navRelease(const MacroInteractionContext& context);
    static MacroInteractionAction optTurn(const MacroInteractionContext& context);
    static MacroInteractionAction leftTopRelease(const MacroInteractionContext& context);
    static MacroInteractionAction leftCenterPress(const MacroInteractionContext& context);
    static MacroInteractionAction leftBottomPress(const MacroInteractionContext& context);
    static MacroInteractionAction leftBottomRelease(const MacroInteractionContext& context);
    static MacroInteractionAction bottomLeftRelease(const MacroInteractionContext& context);
    static MacroInteractionAction bottomLeftLongPress(const MacroInteractionContext& context);
    static MacroInteractionAction bottomRightRelease(const MacroInteractionContext& context);
    static MacroInteractionAction bottomRightLongPress(const MacroInteractionContext& context);
    static MacroActionStripPolicy actionStrip(const MacroInteractionContext& context);
};

}  // namespace core::state::macro
