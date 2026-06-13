#include "handler/project/ProjectHandlerInternals.hpp"

namespace core::handler {

using namespace project_handler_internal;

FLASHMEM void ProjectHandler::enterProjectNameShift() {
    if (navigation_.projectNameShiftActive) return;
    navigation_.projectNameShiftActive = true;
    navigation_.notifyContentChanged();
}

FLASHMEM void ProjectHandler::leaveProjectNameShift() {
    if (!navigation_.projectNameShiftActive) return;
    navigation_.projectNameShiftActive = false;
    navigation_.notifyContentChanged();
}


}  // namespace core::handler
