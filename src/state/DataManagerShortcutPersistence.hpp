#pragma once

#include "DataManagerCatalog.hpp"

namespace core::state {

struct CoreState;

namespace data_manager {

void setShortcut(CoreState& state,
                 DataManagerContext context,
                 bool leftButton,
                 DataManagerCommand command);
void loadShortcutsFromSettings(CoreState& state);

}  // namespace data_manager

}  // namespace core::state
