#include "SequencerStepHandler.hpp"

#include <config/App.hpp>
#include <config/InputIDs.hpp>
#include <config/PlatformCompat.hpp>

namespace core::handler {

SequencerStepHandler::SequencerStepHandler(StateRefs state,
                                           oc::api::EncoderAPI& encoders,
                                           oc::api::ButtonAPI& buttons,
                                           oc::type::ScopeID scopeId)
    : sequencer_(state.sequencer)
    , navigation_workflow_(
          SequencerStructureNavigationWorkflow::StateRefs{
              state.sequencer,
              state.tracks,
              state.navigationFocus,
              state.trackNavigation,
              state.sharedTracks,
          }
      )
    , edit_workflow_(
          SequencerStructureEditWorkflow::StateRefs{
              state.sequencer,
              state.tracks,
              state.navigationFocus,
              state.trackNavigation,
              state.structureClipboard,
              state.sharedTracks,
          }
      )
    , encoders_(encoders)
    , buttons_(buttons)
    , scope_id_(scopeId) {
    setupBindings();
}

FLASHMEM void SequencerStepHandler::setupBindings() {
    for (uint8_t i = 0; i < Config::MACRO_COUNT; ++i) {
        buttons_.button(Config::MACRO_BUTTONS[i])
            .release()
            .scope(scope_id_)
            .when([this]() { return navigation_workflow_.allowsMainBindings(); })
            .then([this, i]() { toggleStep(i); });
    }

    encoders_.encoder(Config::EncoderID::NAV)
        .turn()
        .scope(scope_id_)
        .when([this]() { return navigation_workflow_.selectionActive(); })
        .then([this](float delta) { navigation_workflow_.navigateSelection(delta); });

    encoders_.encoder(Config::EncoderID::NAV)
        .turn()
        .scope(scope_id_)
        .when([this]() { return navigation_workflow_.allowsMainBindings(); })
        .then([this](float delta) { navigation_workflow_.moveByFocus(delta); });

    buttons_.button(Config::ButtonID::NAV)
        .longPress(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS)
        .scope(scope_id_)
        .when([this]() { return navigation_workflow_.allowsMainBindings(); })
        .then([this]() {
            nav_long_press_used_ = true;
            navigation_workflow_.enterSelectionModeForCurrentFocus();
        });

    buttons_.button(Config::ButtonID::NAV)
        .release()
        .scope(scope_id_)
        .when([this]() { return navigation_workflow_.selectionActive(); })
        .then([this]() {
            if (nav_long_press_used_) {
                nav_long_press_used_ = false;
                return;
            }
            navigation_workflow_.toggleSelectionAtCursor();
        });

    buttons_.button(Config::ButtonID::NAV)
        .release()
        .scope(scope_id_)
        .when([this]() { return navigation_workflow_.allowsMainBindings(); })
        .then([this]() {
            if (nav_long_press_used_) {
                nav_long_press_used_ = false;
                return;
            }
            if (navigation_workflow_.previewingAddSlot()) {
                navigation_workflow_.createPreviewedStructure();
                return;
            }
            navigation_workflow_.cycleNavigationFocus();
        });

    buttons_.button(Config::ButtonID::LEFT_TOP)
        .release()
        .scope(scope_id_)
        .when([this]() { return navigation_workflow_.selectionActive(); })
        .then([this]() { navigation_workflow_.cancelSelectionMode(); });

    buttons_.button(Config::ButtonID::BOTTOM_LEFT)
        .press()
        .scope(scope_id_)
        .when([this]() { return navigation_workflow_.allowsMainBindings(); })
        .then([this]() {
            if (edit_workflow_.canRemoveCurrentStructure()) {
                edit_workflow_.beginHoldAction(core::state::StructureHoldAction::REMOVE);
            }
        });

    buttons_.button(Config::ButtonID::BOTTOM_LEFT)
        .release()
        .scope(scope_id_)
        .when([this]() { return navigation_workflow_.selectionActive(); })
        .then([this]() { edit_workflow_.deleteSelection(); });

    buttons_.button(Config::ButtonID::BOTTOM_LEFT)
        .release()
        .scope(scope_id_)
        .when([this]() { return navigation_workflow_.allowsMainBindings(); })
        .then([this]() {
            edit_workflow_.clearHoldAction();
            if (ignore_next_bottom_left_release_) {
                ignore_next_bottom_left_release_ = false;
                return;
            }
            edit_workflow_.eraseCurrentStructure();
        });

    buttons_.button(Config::ButtonID::BOTTOM_LEFT)
        .longPress(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS)
        .scope(scope_id_)
        .when([this]() { return navigation_workflow_.allowsMainBindings(); })
        .then([this]() {
            edit_workflow_.clearHoldAction();
            ignore_next_bottom_left_release_ = true;
            edit_workflow_.removeCurrentStructure();
        });

    buttons_.button(Config::ButtonID::BOTTOM_RIGHT)
        .press()
        .scope(scope_id_)
        .when([this]() { return navigation_workflow_.allowsMainBindings(); })
        .then([this]() {
            if (edit_workflow_.canPasteCurrentStructure()) {
                edit_workflow_.beginHoldAction(core::state::StructureHoldAction::PASTE);
            }
        });

    buttons_.button(Config::ButtonID::BOTTOM_RIGHT)
        .release()
        .scope(scope_id_)
        .when([this]() { return navigation_workflow_.selectionActive(); })
        .then([this]() { edit_workflow_.duplicateSelection(); });

    buttons_.button(Config::ButtonID::BOTTOM_RIGHT)
        .release()
        .scope(scope_id_)
        .when([this]() { return navigation_workflow_.allowsMainBindings(); })
        .then([this]() {
            edit_workflow_.clearHoldAction();
            if (ignore_next_bottom_right_release_) {
                ignore_next_bottom_right_release_ = false;
                return;
            }
            edit_workflow_.copyCurrentStructure();
        });

    buttons_.button(Config::ButtonID::BOTTOM_RIGHT)
        .longPress(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS)
        .scope(scope_id_)
        .when([this]() { return navigation_workflow_.allowsMainBindings(); })
        .then([this]() {
            edit_workflow_.clearHoldAction();
            ignore_next_bottom_right_release_ = true;
            edit_workflow_.pasteCurrentStructure();
        });
}

void SequencerStepHandler::toggleStep(uint8_t indexInPage) {
    uint8_t abs = 0;
    if (!sequencer_.resolveStepInPage(sequencer_.page.get(), indexInPage, abs)) return;

    sequencer_.focusedStep.set(abs);
    sequencer_.toggle(abs);
}

}  // namespace core::handler
