#pragma once

#include <cstdint>

namespace core::state::contextual {

enum class ContextActionId : uint8_t {
    NONE = 0,
    OPEN,
    ENTER,
    CREATE,
    EDIT,
    SELECT,
    MUTE,
    UNMUTE,
    CLEAR,
    RESET,
    REMOVE,
    COPY,
    PASTE,
    LOAD,
    APPLY,
    SAVE,
    RENAME,
    DELETE_ASSET,
    UNDO,
    REDO,
    PREVIEW,
    CANCEL,
    RESUME,
    OVERWRITE,
    TOGGLE_MODE,
    OPEN_SETTINGS,
};

enum class ContextScope : uint8_t {
    NONE = 0,
    GLOBAL,
    PROJECT,
    TRACK,
    PATTERN,
    PAGE,
    STEP,
    MACRO_SET,
    MACRO_SLOT,
    AUTOMATION,
    MODULATION,
    CC_LANE,
    SELECTION,
};

enum class ContextEntityKind : uint8_t {
    NONE = 0,
    PROJECT,
    TRACK,
    PATTERN,
    PAGE,
    STEP,
    MACRO_SET,
    MACRO_SLOT,
    AUTOMATION_LANE,
    MODULATION_LANE,
    CC_LANE,
    ASSET,
    SELECTION,
};

/**
 * Stable, allocation-free reference used by contextual UI projections.
 * Each index is semantic and zero-based; UNUSED_INDEX means "not applicable".
 */
struct ContextEntityRef {
    static constexpr uint16_t UNUSED_INDEX = UINT16_MAX;

    ContextEntityKind kind = ContextEntityKind::NONE;
    uint16_t track = UNUSED_INDEX;
    uint16_t page = UNUSED_INDEX;
    uint16_t item = UNUSED_INDEX;
    uint16_t node = UNUSED_INDEX;
};

constexpr bool operator==(
    const ContextEntityRef& lhs,
    const ContextEntityRef& rhs
) {
    return lhs.kind == rhs.kind && lhs.track == rhs.track &&
           lhs.page == rhs.page && lhs.item == rhs.item &&
           lhs.node == rhs.node;
}

constexpr bool operator!=(
    const ContextEntityRef& lhs,
    const ContextEntityRef& rhs
) {
    return !(lhs == rhs);
}

enum class ContextActionImpact : uint8_t {
    NONE = 0,
    INFORMATIONAL,
    NON_MUTATING,
    VALUE_EDIT,
    CONSTRUCTIVE,
    OVERWRITE,
    SHALLOW_RESET,
    DESTRUCTIVE,
};

enum class ContextActionAvailability : uint8_t {
    AVAILABLE = 0,
    WARNING,
    DISABLED,
};

enum class ContextActionReason : uint8_t {
    NONE = 0,
    NO_ACTION,
    EMPTY_SELECTION,
    MINIMUM_CARDINALITY,
    EMPTY_CLIPBOARD,
    WRONG_PAYLOAD,
    INVALID_PAYLOAD,
    ADAPTED,
    CORRUPT_ASSET,
    UNSUPPORTED_VERSION,
    STALE_TARGET,
    SAME_SOURCE_TARGET,
    OUT_OF_RANGE,
    CAPACITY,
    PENDING,
    NO_ROUTE,
    INCOMPATIBLE,
    HISTORY_UNAVAILABLE,
    STORAGE_UNAVAILABLE,
    ALLOCATION_UNAVAILABLE,
    CONFLICT,
    READ_ONLY,
    TRANSPORT_STATE,
    FAILED,
};

enum class ContextIconId : uint8_t {
    NONE = 0,
    OPEN,
    ENTER,
    CREATE,
    EDIT,
    SELECT,
    MUTE,
    UNMUTE,
    CLEAR,
    RESET,
    REMOVE,
    COPY,
    PASTE,
    LOAD,
    APPLY,
    SAVE,
    RENAME,
    UNDO,
    REDO,
    PREVIEW,
    QUEUED,
    APPLIED,
    WARNING,
    ERROR,
    HOLD,
    ROUTE_INHERITED,
    ROUTE_PINNED,
    AUTOMATION,
    MODULATION,
    MANUAL,
    RESUME,
    CONFLICT,
    NO_ROUTE,
};

enum class ContextTone : uint8_t {
    DEFAULT = 0,
    NEUTRAL,
    BLUE,
    GREEN,
    AMBER,
    RED,
};

enum class ContextGuardKind : uint8_t {
    NONE = 0,
    HOLD,
};

struct ContextActionVisualPolicy {
    ContextIconId icon = ContextIconId::NONE;
    ContextTone tone = ContextTone::DEFAULT;
};

/** One executable gesture and its complete semantic projection. */
struct ContextActionVariant {
    ContextActionId action = ContextActionId::NONE;
    ContextActionImpact impact = ContextActionImpact::NONE;
    ContextActionAvailability availability =
        ContextActionAvailability::DISABLED;
    ContextActionReason reason = ContextActionReason::NO_ACTION;
    ContextActionVisualPolicy visual{};
};

struct ContextGuardSpec {
    ContextGuardKind kind = ContextGuardKind::NONE;
    uint16_t durationMs = 0;
};

/**
 * Shared tap/hold action contract. Tap and hold remain independent because
 * their availability, impact and visual semantics can legitimately differ.
 */
struct ContextActionSpec {
    ContextActionVariant tap{};
    ContextActionVariant hold{};
    ContextScope scope = ContextScope::NONE;
    ContextEntityRef source{};
    ContextEntityRef target{};
    ContextGuardSpec guard{};
};

constexpr bool hasAction(const ContextActionVariant& variant) {
    return variant.action != ContextActionId::NONE;
}

constexpr bool canExecute(const ContextActionVariant& variant) {
    return hasAction(variant) &&
           variant.availability != ContextActionAvailability::DISABLED;
}

constexpr bool hasTapAction(const ContextActionSpec& spec) {
    return hasAction(spec.tap);
}

constexpr bool hasHoldAction(const ContextActionSpec& spec) {
    return hasAction(spec.hold);
}

constexpr bool requiresGuard(const ContextActionSpec& spec) {
    return hasHoldAction(spec) && spec.guard.kind == ContextGuardKind::HOLD;
}

}  // namespace core::state::contextual
