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
                   (isProjectNameEditorNode(navigation_.currentNode.get()) ||
                    !projectConfirmationActive());
        })
        .then([this]() {
            if (isProjectNameEditorNode(navigation_.currentNode.get())) {
                enterProjectNameShift();
            } else {
                enterPhysicalHoldLayer();
            }
        });

    buttons_.button(ButtonID::LEFT_CENTER)
        .release()
        .scope(project_view_scope_)
        .when([this]() {
            return canHandleProjectInput() &&
                   (navigation_.projectNameShiftActive || physicalHoldActive());
        })
        .then([this]() {
            if (navigation_.projectNameShiftActive) {
                leaveProjectNameShift();
            } else {
                leavePhysicalHoldLayer();
            }
        });

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
        .press()
        .scope(project_view_scope_)
        .when([this]() {
            const auto node = navigation_.currentNode.get();
            return regularProjectInputActive() &&
                   (node == core::state::project::ProjectNodeId::MODULATORS_ROOT ||
                    node == core::state::project::ProjectNodeId::MODULATOR_SOURCE_DETAIL ||
                    node == core::state::project::ProjectNodeId::MODULATOR_DESTINATIONS);
        })
        .then([this]() { beginModulatorBottomLeft(); });

    buttons_.button(ButtonID::BOTTOM_LEFT)
        .release()
        .scope(project_view_scope_)
        .when([this]() {
            if (!regularProjectInputActive()) return false;
            const auto node = navigation_.currentNode.get();
            return isProjectNameEditorNode(node) ||
                   node == core::state::project::ProjectNodeId::MODULATORS_ROOT ||
                   node == core::state::project::ProjectNodeId::MODULATOR_SOURCE_DETAIL ||
                   node == core::state::project::ProjectNodeId::MODULATOR_DESTINATIONS;
        })
        .then([this]() {
            if (isProjectNameEditorNode(navigation_.currentNode.get())) {
                backspaceProjectName(navigation_);
            } else {
                releaseModulatorBottomLeft();
            }
        });

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
        .press()
        .scope(project_view_scope_)
        .when([this]() {
            const auto node = navigation_.currentNode.get();
            return regularProjectInputActive() &&
                   (node == core::state::project::ProjectNodeId::MODULATORS_ROOT ||
                    node == core::state::project::ProjectNodeId::MODULATOR_SOURCE_DETAIL);
        })
        .then([this]() { beginModulatorBottomRight(); });

    buttons_.button(ButtonID::BOTTOM_RIGHT)
        .release()
        .scope(project_view_scope_)
        .when([this]() {
            if (!regularProjectInputActive()) return false;
            const auto node = navigation_.currentNode.get();
            return isProjectNameEditorNode(node) ||
                   node == core::state::project::ProjectNodeId::MODULATORS_ROOT ||
                   node == core::state::project::ProjectNodeId::MODULATOR_SOURCE_DETAIL;
        })
        .then([this]() {
            if (isProjectNameEditorNode(navigation_.currentNode.get())) {
                commitProjectNameEditor();
            } else {
                releaseModulatorBottomRight();
            }
        });
}


}  // namespace core::handler
