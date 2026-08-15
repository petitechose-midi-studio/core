#pragma once

#include <cstdint>

namespace core::state::interaction {

/**
 * Stable controller-wide intentions.
 *
 * Domain policies keep their precise actions (for example MOVE_TRACK or
 * EDIT_STEP_PROPERTY) and expose one of these intentions for cross-surface
 * validation. This vocabulary carries no runtime state and must not become a
 * generic interaction engine.
 */
enum class ControllerIntent : uint8_t {
    NONE = 0,
    MOVE_FOCUS,
    ACTIVATE,
    EDIT_VALUE,
    CHANGE_SCOPE,
    NAVIGATE_SECONDARY_AXIS,
    ENTER_SELECTION,
    OPEN_ADVANCED,
    BACK,
    CANCEL,
    APPLY,
    SOFT_ACTION,
    RESET,
    DELETE_STRUCTURE,
    COPY,
    PASTE,
    TRANSPORT,
    UNDO,
    REDO,
    CAPTURE,
    TEXT_EDIT,
};

enum class SurfaceArchetype : uint8_t {
    HIERARCHICAL = 0,
    RETAINED_EDITOR,
    MOMENTARY_SELECTOR,
    TRANSACTIONAL_EDITOR,
    PERFORMANCE,
    BROWSER,
    TEXT_ENTRY,
};

enum class TransactionMode : uint8_t {
    LIVE = 0,
    MOMENTARY,
    DRAFT,
    GUARDED,
};

enum class ControllerGesture : uint8_t {
    NAV_TURN = 0,
    NAV_TAP,
    NAV_HOLD_TURN,
    NAV_HOLD,
    OPT_TURN,
    LEFT_TOP,
    LEFT_CENTER,
    LEFT_CENTER_NAV,
    LEFT_BOTTOM,
    LEFT_BOTTOM_NAV,
    BOTTOM_LEFT_TAP,
    BOTTOM_LEFT_HOLD,
    BOTTOM_CENTER_TAP,
    BOTTOM_RIGHT_TAP,
    BOTTOM_RIGHT_HOLD,
    MACRO_CONTROL,
};

constexpr bool isDestructiveIntent(ControllerIntent intent) {
    return intent == ControllerIntent::DELETE_STRUCTURE;
}

constexpr bool isValueAuthoringIntent(ControllerIntent intent) {
    return intent == ControllerIntent::EDIT_VALUE ||
           intent == ControllerIntent::TEXT_EDIT;
}

/**
 * Mechanical guardrail shared by policy tests.
 *
 * NONE is always valid because availability remains contextual. The allowed
 * set is deliberately semantic rather than feature-specific: exceptions such
 * as View Selector history, Macro capture and Project text entry are explicit
 * members instead of weakening the common NAV/OPT/Back rules.
 */
constexpr bool gestureAllowsIntent(
    ControllerGesture gesture,
    ControllerIntent intent
) {
    if (intent == ControllerIntent::NONE) return true;

    switch (gesture) {
        case ControllerGesture::NAV_TURN:
            return intent == ControllerIntent::MOVE_FOCUS ||
                   intent == ControllerIntent::NAVIGATE_SECONDARY_AXIS;
        case ControllerGesture::NAV_TAP:
            return intent == ControllerIntent::ACTIVATE ||
                   intent == ControllerIntent::APPLY ||
                   intent == ControllerIntent::TEXT_EDIT;
        case ControllerGesture::NAV_HOLD_TURN:
            return intent == ControllerIntent::CHANGE_SCOPE ||
                   intent == ControllerIntent::NAVIGATE_SECONDARY_AXIS;
        case ControllerGesture::NAV_HOLD:
            return intent == ControllerIntent::ENTER_SELECTION ||
                   intent == ControllerIntent::OPEN_ADVANCED;
        case ControllerGesture::OPT_TURN:
            return isValueAuthoringIntent(intent);
        case ControllerGesture::LEFT_TOP:
            return intent == ControllerIntent::BACK ||
                   intent == ControllerIntent::CANCEL ||
                   intent == ControllerIntent::APPLY;
        case ControllerGesture::LEFT_CENTER:
            return intent == ControllerIntent::OPEN_ADVANCED ||
                   intent == ControllerIntent::UNDO ||
                   intent == ControllerIntent::CAPTURE ||
                   intent == ControllerIntent::TEXT_EDIT;
        case ControllerGesture::LEFT_CENTER_NAV:
            return intent == ControllerIntent::NAVIGATE_SECONDARY_AXIS ||
                   intent == ControllerIntent::CHANGE_SCOPE ||
                   intent == ControllerIntent::EDIT_VALUE;
        case ControllerGesture::LEFT_BOTTOM:
            return intent == ControllerIntent::OPEN_ADVANCED ||
                   intent == ControllerIntent::REDO ||
                   intent == ControllerIntent::APPLY ||
                   intent == ControllerIntent::TEXT_EDIT;
        case ControllerGesture::LEFT_BOTTOM_NAV:
            return intent == ControllerIntent::NAVIGATE_SECONDARY_AXIS ||
                   intent == ControllerIntent::EDIT_VALUE;
        case ControllerGesture::BOTTOM_LEFT_TAP:
            return intent == ControllerIntent::SOFT_ACTION ||
                   intent == ControllerIntent::RESET ||
                   intent == ControllerIntent::TEXT_EDIT;
        case ControllerGesture::BOTTOM_LEFT_HOLD:
            return intent == ControllerIntent::DELETE_STRUCTURE ||
                   intent == ControllerIntent::RESET;
        case ControllerGesture::BOTTOM_CENTER_TAP:
            return intent == ControllerIntent::TRANSPORT;
        case ControllerGesture::BOTTOM_RIGHT_TAP:
            return intent == ControllerIntent::SOFT_ACTION ||
                   intent == ControllerIntent::APPLY ||
                   intent == ControllerIntent::COPY ||
                   intent == ControllerIntent::TEXT_EDIT;
        case ControllerGesture::BOTTOM_RIGHT_HOLD:
            return intent == ControllerIntent::APPLY ||
                   intent == ControllerIntent::PASTE;
        case ControllerGesture::MACRO_CONTROL:
            return intent == ControllerIntent::ACTIVATE ||
                   intent == ControllerIntent::EDIT_VALUE ||
                   intent == ControllerIntent::CAPTURE;
    }
    return false;
}

static_assert(sizeof(ControllerIntent) == 1U);
static_assert(sizeof(SurfaceArchetype) == 1U);
static_assert(sizeof(TransactionMode) == 1U);

}  // namespace core::state::interaction
