#include "handler/macro/MacroPerformanceHandler.hpp"

#include <config/PlatformCompat.hpp>

namespace core::handler {

FLASHMEM MacroPerformanceHandler::MacroPerformanceHandler(
    StateRefs state,
    MacroPerformanceDomainServices performanceServices,
    MacroStructureDomainServices structureServices,
    oc::context::OverlayManager<core::ui::OverlayType>& overlays,
    oc::api::EncoderAPI& encoders,
    oc::api::ButtonAPI& buttons,
    oc::type::ScopeID scopeId)
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
    , overlays_(overlays)
    , encoders_(encoders)
    , buttons_(buttons)
    , scope_id_(scopeId) {
    setupBindings();
}

FLASHMEM void MacroPerformanceHandler::setupBindings() {
    buttons_.button(Config::ButtonID::LEFT_CENTER)
        .press()
        .latch()
        .scope(scope_id_)
        .when([this]() {
            left_center_held_ = true;
            return performance_workflow_.performanceAvailable() &&
                   !left_bottom_held_;
        })
        .then([this]() { performance_workflow_.openQuickControls(); });

    buttons_.button(Config::ButtonID::LEFT_CENTER)
        .release()
        .scope(scope_id_)
        .then([this]() {
            left_center_held_ = false;
            if (performance_workflow_.quickControlsSelecting()) {
                performance_workflow_.closeQuickControlsApply();
            }
        });

    buttons_.button(Config::ButtonID::LEFT_BOTTOM)
        .press()
        .latch()
        .scope(scope_id_)
        .when([this]() {
            left_bottom_held_ = true;
            return performance_workflow_.performanceAvailable() &&
                   !left_center_held_ &&
                   !performance_workflow_.quickControlsSelecting();
        })
        .then([this]() { performance_workflow_.activateClutch(); });

    encoders_.encoder(Config::EncoderID::NAV)
        .turn()
        .scope(scope_id_)
        .when([this]() { return selectionActive(); })
        .then([this](float delta) { structure_workflow_.navigateSelection(delta); });

    buttons_.button(Config::ButtonID::NAV)
        .longPress(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS)
        .scope(scope_id_)
        .when([this]() { return performance_workflow_.clutchInactive(); })
        .then([this]() {
            nav_long_press_used_ = true;
            structure_workflow_.enterSelectionModeForCurrentFocus();
        });

    buttons_.button(Config::ButtonID::NAV)
        .release()
        .scope(scope_id_)
        .when([this]() { return selectionActive(); })
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
            if (performance_workflow_.clutchActive() &&
                !performance_workflow_.quickControlsSelecting()) {
                performance_workflow_.deactivateClutch();
            }
        });

    buttons_.button(Config::ButtonID::NAV)
        .release()
        .scope(scope_id_)
        .when([this]() { return performance_workflow_.clutchInactive(); })
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
        .when([this]() { return performance_workflow_.quickControlsSelecting(); })
        .then([this](float delta) { performance_workflow_.navigateQuickControls(delta); });

    encoders_.encoder(Config::EncoderID::NAV)
        .turn()
        .scope(scope_id_)
        .when([this]() { return performance_workflow_.clutchActive(); })
        .then([this](float delta) { performance_workflow_.navigateProperty(delta); });

    encoders_.encoder(Config::EncoderID::NAV)
        .turn()
        .scope(scope_id_)
        .when([this]() { return performance_workflow_.clutchInactive(); })
        .then([this](float delta) {
            structure_workflow_.moveByFocus(delta);
            performance_workflow_.refreshEncoders();
        });

    encoders_.encoder(Config::EncoderID::OPT)
        .turn()
        .scope(scope_id_)
        .when([this]() { return performance_workflow_.quickControlsSelecting(); })
        .then([this](float normalized) {
            performance_workflow_.setFocusedQuickControlValue(normalized);
        });

    buttons_.button(Config::ButtonID::LEFT_TOP)
        .release()
        .scope(scope_id_)
        .when([this]() { return selectionActive(); })
        .then([this]() { structure_workflow_.cancelSelectionMode(); });

    buttons_.button(Config::ButtonID::BOTTOM_LEFT)
        .press()
        .scope(scope_id_)
        .when([this]() { return performance_workflow_.clutchInactive(); })
        .then([this]() {
            if (structure_workflow_.canRemoveCurrentStructure()) {
                structure_workflow_.beginHoldAction(core::state::StructureHoldAction::REMOVE);
            }
        });

    buttons_.button(Config::ButtonID::BOTTOM_LEFT)
        .release()
        .scope(scope_id_)
        .when([this]() { return selectionActive(); })
        .then([this]() {
            structure_workflow_.deleteSelection();
            performance_workflow_.refreshEncoders();
        });

    buttons_.button(Config::ButtonID::BOTTOM_LEFT)
        .release()
        .scope(scope_id_)
        .when([this]() { return performance_workflow_.clutchInactive(); })
        .then([this]() {
            structure_workflow_.clearHoldAction();
            if (ignore_next_bottom_left_release_) {
                ignore_next_bottom_left_release_ = false;
                return;
            }
            structure_workflow_.eraseCurrentStructure();
            performance_workflow_.refreshEncoders();
        });

    buttons_.button(Config::ButtonID::BOTTOM_LEFT)
        .longPress(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS)
        .scope(scope_id_)
        .when([this]() { return performance_workflow_.clutchInactive(); })
        .then([this]() {
            structure_workflow_.clearHoldAction();
            ignore_next_bottom_left_release_ = true;
            structure_workflow_.removeCurrentStructure();
            performance_workflow_.refreshEncoders();
        });

    buttons_.button(Config::ButtonID::BOTTOM_RIGHT)
        .press()
        .scope(scope_id_)
        .when([this]() { return performance_workflow_.clutchInactive(); })
        .then([this]() {
            if (structure_workflow_.canPasteCurrentStructure()) {
                structure_workflow_.beginHoldAction(core::state::StructureHoldAction::PASTE);
            }
        });

    buttons_.button(Config::ButtonID::BOTTOM_RIGHT)
        .release()
        .scope(scope_id_)
        .when([this]() { return selectionActive(); })
        .then([this]() {
            structure_workflow_.duplicateSelection();
            performance_workflow_.refreshEncoders();
        });

    buttons_.button(Config::ButtonID::BOTTOM_RIGHT)
        .release()
        .scope(scope_id_)
        .when([this]() { return performance_workflow_.clutchInactive(); })
        .then([this]() {
            structure_workflow_.clearHoldAction();
            if (ignore_next_bottom_right_release_) {
                ignore_next_bottom_right_release_ = false;
                return;
            }
            structure_workflow_.copyCurrentStructure();
        });

    buttons_.button(Config::ButtonID::BOTTOM_RIGHT)
        .longPress(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS)
        .scope(scope_id_)
        .when([this]() { return performance_workflow_.clutchInactive(); })
        .then([this]() {
            structure_workflow_.clearHoldAction();
            ignore_next_bottom_right_release_ = true;
            structure_workflow_.pasteCurrentStructure();
            performance_workflow_.refreshEncoders();
        });

    buttons_.button(Config::ButtonID::LEFT_TOP)
        .release()
        .scope(scope_id_)
        .when([this]() { return performance_workflow_.quickControlsSelecting(); })
        .then([this]() { performance_workflow_.closeQuickControlsCancel(); });
}

bool MacroPerformanceHandler::selectionActive() const {
    return structure_workflow_.selectionActive() && !overlays_.hasVisible();
}

}  // namespace core::handler
