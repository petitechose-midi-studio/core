#pragma once

#include "CoreSettings.hpp"
#include "DataManagerCatalog.hpp"
#include "DataManagerState.hpp"

namespace core::state {

struct CoreState;

namespace data_manager {

/**
 * Persists and restores Data Manager shortcuts from CoreSettings.
 *
 * Inputs are sanitized through the command catalog so stored bytes cannot select
 * commands outside the active macro/sequencer context.
 */
struct ShortcutStateRefs {
    DataManagerState& dataManager;
    CoreSettings& settings;
};

void setShortcut(ShortcutStateRefs state,
                 DataManagerContext context,
                 bool leftButton,
                 DataManagerCommand command);
void setShortcut(CoreState& state,
                 DataManagerContext context,
                 bool leftButton,
                 DataManagerCommand command);
void loadShortcutsFromSettings(ShortcutStateRefs state);
void loadShortcutsFromSettings(CoreState& state);

}  // namespace data_manager

}  // namespace core::state
