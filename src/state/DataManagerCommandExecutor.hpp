#pragma once

#include <cstdint>

#include "DataManagerCatalog.hpp"
#include "DataManagerWorkflow.hpp"

namespace core::state {

struct CoreState;

namespace data_manager {

/**
 * Executes Data Manager commands against CoreState persistence workflows.
 *
 * The catalog defines what a command means; this boundary is where slot probing
 * and save/load/erase dispatch reach macro or sequencer storage.
 */
bool slotOccupied(CoreState& state, DataManagerCommand command, uint8_t slotIndex);
DataManagerCommandExecutionResult execute(CoreState& state,
                                          DataManagerCommand command,
                                          uint8_t slotIndex,
                                          DataManagerSetLoadMode setLoadMode);

}  // namespace data_manager

}  // namespace core::state
