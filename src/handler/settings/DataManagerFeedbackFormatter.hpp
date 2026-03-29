#pragma once

#include <cstddef>
#include <cstdint>

#include "state/DataManagerWorkflow.hpp"

namespace core::handler {

void formatDataManagerCommandExecutionFeedback(
    char* message,
    size_t messageSize,
    core::state::DataManagerCommand command,
    uint8_t slot,
    core::state::DataManagerSetLoadMode setLoadMode,
    const core::state::DataManagerCommandExecutionResult& result
);

}  // namespace core::handler
