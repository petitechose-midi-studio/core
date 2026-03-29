#pragma once

#include <cstdint>

#include "DataManagerCatalog.hpp"
#include "DataManagerWorkflow.hpp"

namespace core::state {

struct CoreState;

namespace data_manager {

uint8_t slotCount(DataManagerCommand command);
bool slotOccupied(CoreState& state, DataManagerCommand command, uint8_t slotIndex);
DataManagerCommandExecutionResult execute(CoreState& state,
                                          DataManagerCommand command,
                                          uint8_t slotIndex,
                                          DataManagerSetLoadMode setLoadMode);

}  // namespace data_manager

}  // namespace core::state
