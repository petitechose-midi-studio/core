#include <cassert>
#include <iostream>

#include "state/StructureSelectionInteractionPolicy.hpp"
#include "state/interaction/ControllerInteractionContract.hpp"
#include "state/macro/MacroInteractionPolicy.hpp"
#include "state/sequencer/SequencerInteractionPolicy.hpp"
#include "ui/interaction/ControllerUiPresentationContract.hpp"

namespace interaction = core::state::interaction;
namespace macro = core::state::macro;
namespace sequencer = core::state::sequencer;

using Gesture = interaction::ControllerGesture;
using Intent = interaction::ControllerIntent;

namespace {

void expectAllowed(Gesture gesture, sequencer::SequencerInteractionAction action) {
    assert(interaction::gestureAllowsIntent(
        gesture,
        sequencer::controllerIntentFor(action)
    ));
}

void expectAllowed(Gesture gesture, macro::MacroInteractionAction action) {
    assert(interaction::gestureAllowsIntent(
        gesture,
        macro::controllerIntentFor(action)
    ));
}

void expectAllowed(
    Gesture gesture,
    core::state::StructureSelectionInteractionAction action
) {
    assert(interaction::gestureAllowsIntent(
        gesture,
        core::state::controllerIntentFor(action)
    ));
}

void test_reserved_and_exception_boundaries() {
    assert(interaction::gestureAllowsIntent(
        Gesture::BOTTOM_CENTER_TAP,
        Intent::TRANSPORT
    ));
    assert(!interaction::gestureAllowsIntent(
        Gesture::BOTTOM_CENTER_TAP,
        Intent::APPLY
    ));
    assert(!interaction::gestureAllowsIntent(
        Gesture::OPT_TURN,
        Intent::MOVE_FOCUS
    ));
    assert(!interaction::gestureAllowsIntent(
        Gesture::LEFT_TOP,
        Intent::EDIT_VALUE
    ));

    // The few deliberate cross-surface exceptions stay explicit.
    assert(interaction::gestureAllowsIntent(Gesture::LEFT_CENTER, Intent::UNDO));
    assert(interaction::gestureAllowsIntent(Gesture::LEFT_BOTTOM, Intent::REDO));
    assert(interaction::gestureAllowsIntent(Gesture::LEFT_CENTER, Intent::CAPTURE));
    assert(interaction::gestureAllowsIntent(Gesture::LEFT_CENTER, Intent::TEXT_EDIT));
}

void test_sequencer_hierarchical_surface() {
    sequencer::SequencerInteractionContext context{};
    context.navigationFocus = core::state::StructureNavigationFocus::PAGE;
    context.compatibleClipboardAvailable = true;

    const auto policy = sequencer::buildSequencerInteractionPolicy(context);
    expectAllowed(Gesture::NAV_TURN, policy.navTurn);
    expectAllowed(Gesture::NAV_TAP, policy.navTap);
    expectAllowed(Gesture::NAV_HOLD, policy.navLongPress);
    expectAllowed(Gesture::OPT_TURN, policy.optTurn);
    expectAllowed(Gesture::LEFT_CENTER, policy.leftCenterPress);
    expectAllowed(Gesture::LEFT_BOTTOM, policy.leftBottomPress);
    expectAllowed(Gesture::BOTTOM_LEFT_TAP, policy.bottomLeftTap);
    expectAllowed(Gesture::BOTTOM_LEFT_HOLD, policy.bottomLeftHold);
    expectAllowed(Gesture::BOTTOM_RIGHT_TAP, policy.bottomRightTap);
    expectAllowed(Gesture::BOTTOM_RIGHT_HOLD, policy.bottomRightHold);

    assert(sequencer::controllerIntentFor(policy.navTurn) == Intent::MOVE_FOCUS);
    assert(sequencer::controllerIntentFor(policy.navTap) == Intent::ACTIVATE);
    assert(sequencer::controllerIntentFor(policy.navLongPress) ==
           Intent::ENTER_SELECTION);
    assert(sequencer::controllerIntentFor(policy.optTurn) == Intent::EDIT_VALUE);
}

void test_sequencer_retained_step_editor() {
    sequencer::SequencerInteractionContext context{};
    context.navigationFocus = core::state::StructureNavigationFocus::STEP;
    context.stepEditorVisible = true;

    const auto policy = sequencer::buildSequencerInteractionPolicy(context);
    expectAllowed(Gesture::NAV_TURN, policy.navTurn);
    expectAllowed(Gesture::NAV_TAP, policy.navTap);
    expectAllowed(Gesture::OPT_TURN, policy.optTurn);
    expectAllowed(Gesture::LEFT_CENTER_NAV, policy.leftCenterPress);
    expectAllowed(Gesture::LEFT_BOTTOM_NAV, policy.leftBottomPress);
    expectAllowed(Gesture::LEFT_TOP, policy.leftTopTap);

    assert(sequencer::controllerIntentFor(policy.leftCenterPress) ==
           Intent::NAVIGATE_SECONDARY_AXIS);
    assert(sequencer::controllerIntentFor(policy.leftTopTap) == Intent::CANCEL);

    context.stepEditorDrumRoot = true;
    context.stepEditorLaneRetargetAvailable = true;
    const auto drumPolicy = sequencer::buildSequencerInteractionPolicy(context);
    expectAllowed(Gesture::LEFT_BOTTOM_NAV, drumPolicy.leftBottomPress);
    assert(sequencer::controllerIntentFor(drumPolicy.leftBottomPress) ==
           Intent::NAVIGATE_SECONDARY_AXIS);
}

void test_macro_performance_and_momentary_selector() {
    macro::MacroInteractionContext context{};
    context.compatibleClipboardAvailable = true;
    context.canRemoveStructure = true;

    expectAllowed(Gesture::NAV_TURN, macro::MacroInteractionPolicy::navTurn(context));
    expectAllowed(Gesture::NAV_TAP, macro::MacroInteractionPolicy::navRelease(context));
    expectAllowed(
        Gesture::LEFT_BOTTOM,
        macro::MacroInteractionPolicy::leftBottomPress(context)
    );
    expectAllowed(
        Gesture::BOTTOM_LEFT_TAP,
        macro::MacroInteractionPolicy::bottomLeftRelease(context)
    );
    expectAllowed(
        Gesture::BOTTOM_LEFT_HOLD,
        macro::MacroInteractionPolicy::bottomLeftLongPress(context)
    );
    expectAllowed(
        Gesture::BOTTOM_RIGHT_TAP,
        macro::MacroInteractionPolicy::bottomRightRelease(context)
    );
    expectAllowed(
        Gesture::BOTTOM_RIGHT_HOLD,
        macro::MacroInteractionPolicy::bottomRightLongPress(context)
    );

    context.slotPropertySelecting = true;
    expectAllowed(Gesture::NAV_TURN, macro::MacroInteractionPolicy::navTurn(context));
    expectAllowed(Gesture::OPT_TURN, macro::MacroInteractionPolicy::optTurn(context));
    expectAllowed(
        Gesture::LEFT_TOP,
        macro::MacroInteractionPolicy::leftTopRelease(context)
    );
    expectAllowed(
        Gesture::LEFT_BOTTOM,
        macro::MacroInteractionPolicy::leftBottomRelease(context)
    );
}

void test_shared_selection_lifecycle() {
    auto policy = core::state::buildStructureSelectionInteractionPolicy({
        .entryAvailable = true,
    });
    expectAllowed(Gesture::NAV_HOLD, policy.navLongPress);

    policy = core::state::buildStructureSelectionInteractionPolicy({
        .active = true,
        .selectedItemsAvailable = true,
    });
    expectAllowed(Gesture::NAV_TURN, policy.navTurn);
    expectAllowed(Gesture::NAV_TAP, policy.navRelease);
    expectAllowed(Gesture::LEFT_TOP, policy.leftTopRelease);
    expectAllowed(Gesture::BOTTOM_RIGHT_TAP, policy.bottomRightRelease);

    policy = core::state::buildStructureSelectionInteractionPolicy({
        .active = true,
        .placing = true,
        .selectedItemsAvailable = true,
        .pasteAvailable = true,
    });
    expectAllowed(Gesture::LEFT_TOP, policy.leftTopRelease);
    expectAllowed(Gesture::BOTTOM_RIGHT_HOLD, policy.bottomRightLongPress);
}

void test_every_product_surface_has_a_presentation_contract() {
    for (uint8_t raw = 0;
         raw < static_cast<uint8_t>(core::ui::ViewType::COUNT);
         ++raw) {
        assert(core::ui::interaction::presentationFor(
            static_cast<core::ui::ViewType>(raw)
        ).valid);
    }

    for (uint8_t raw = static_cast<uint8_t>(core::ui::OverlayType::NONE) + 1U;
         raw < static_cast<uint8_t>(core::ui::OverlayType::COUNT);
         ++raw) {
        const auto overlay = static_cast<core::ui::OverlayType>(raw);
        const auto contract = core::ui::interaction::presentationFor(overlay);
        assert(contract.valid);
        assert(core::ui::interaction::replacesRootChrome(overlay));
    }
}

}  // namespace

int main() {
    test_reserved_and_exception_boundaries();
    test_sequencer_hierarchical_surface();
    test_sequencer_retained_step_editor();
    test_macro_performance_and_momentary_selector();
    test_shared_selection_lifecycle();
    test_every_product_surface_has_a_presentation_contract();
    std::cout << "All ControllerInteractionContract tests passed\n";
    return 0;
}
