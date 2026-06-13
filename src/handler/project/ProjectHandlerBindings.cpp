#include "handler/project/ProjectHandlerInternals.hpp"

namespace core::handler {

using namespace project_handler_internal;

FLASHMEM void ProjectHandler::setupBindings() {
    encoders_.encoder(EncoderID::NAV)
        .turn()
        .scope(project_view_scope_)
        .when([this]() { return regularProjectInputActive(); })
        .then([this](float delta) { navigate(delta); });

    buttons_.button(ButtonID::NAV)
        .release()
        .scope(project_view_scope_)
        .when([this]() { return regularProjectInputActive(); })
        .then([this]() { enterFocused(); });

    buttons_.button(ButtonID::LEFT_TOP)
        .release()
        .scope(project_view_scope_)
        .when([this]() {
            return regularProjectInputActive() &&
                   !core::state::project::projectNavigationAtRoot(navigation_);
        })
        .then([this]() { back(); });

    buttons_.button(ButtonID::LEFT_CENTER)
        .press()
        .scope(project_view_scope_)
        .when([this]() {
            return canHandleProjectInput() &&
                   !projectConfirmationActive() &&
                   !isProjectNameEditorNode(navigation_.currentNode.get());
        })
        .then([this]() { enterPhysicalHoldLayer(); });

    buttons_.button(ButtonID::LEFT_CENTER)
        .press()
        .scope(project_view_scope_)
        .when([this]() {
            return regularProjectInputActive() &&
                   isProjectNameEditorNode(navigation_.currentNode.get());
        })
        .then([this]() { enterProjectNameShift(); });

    buttons_.button(ButtonID::LEFT_CENTER)
        .release()
        .scope(project_view_scope_)
        .when([this]() {
            return canHandleProjectInput() && navigation_.projectNameShiftActive;
        })
        .then([this]() { leaveProjectNameShift(); });

    buttons_.button(ButtonID::LEFT_CENTER)
        .release()
        .scope(project_view_scope_)
        .when([this]() { return physicalHoldActive(); })
        .then([this]() { leavePhysicalHoldLayer(); });

    encoders_.encoder(EncoderID::NAV)
        .turn()
        .scope(project_view_scope_)
        .when([this]() { return physicalHoldActive(); })
        .then([this](float delta) { switchTab(delta); });

    encoders_.encoder(EncoderID::OPT)
        .turn()
        .scope(project_view_scope_)
        .when([this]() { return regularProjectInputActive(); })
        .then([this](float normalized) { setFocusedValue(normalized); });

    buttons_.button(ButtonID::LEFT_TOP)
        .release()
        .scope(project_view_scope_)
        .when([this]() { return physicalHoldActive(); })
        .then([this]() { consumeUndo(); });

    buttons_.button(ButtonID::LEFT_BOTTOM)
        .release()
        .scope(project_view_scope_)
        .when([this]() { return physicalHoldActive(); })
        .then([this]() { consumeRedo(); });

    buttons_.button(ButtonID::BOTTOM_LEFT)
        .release()
        .scope(project_view_scope_)
        .when([this]() {
            return regularProjectInputActive() &&
                   isProjectNameEditorNode(navigation_.currentNode.get());
        })
        .then([this]() { backspaceProjectName(navigation_); });

    buttons_.button(ButtonID::LEFT_BOTTOM)
        .release()
        .scope(project_view_scope_)
        .when([this]() {
            return regularProjectInputActive() &&
                   isProjectNameEditorNode(navigation_.currentNode.get());
        })
        .then([this]() { clearProjectName(navigation_); });

    buttons_.button(ButtonID::BOTTOM_CENTER)
        .release()
        .scope(project_view_scope_)
        .when([this]() {
            return regularProjectInputActive() &&
                   isProjectNameEditorNode(navigation_.currentNode.get());
        })
        .then([this]() {
            if (!appendProjectNameSpace(navigation_)) {
                navigation_.setLifecycleFeedback("Name too long");
            }
        });

    buttons_.button(ButtonID::BOTTOM_RIGHT)
        .release()
        .scope(project_view_scope_)
        .when([this]() {
            return regularProjectInputActive() &&
                   isProjectNameEditorNode(navigation_.currentNode.get());
        })
        .then([this]() { commitProjectNameEditor(); });
}


}  // namespace core::handler
