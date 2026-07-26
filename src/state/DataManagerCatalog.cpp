#include "state/DataManagerCatalog.hpp"

#include <config/PlatformCompat.hpp>

namespace core::state {

FLASHMEM DataManagerCommand dataManagerCommandAt(
    DataManagerContext context,
    int index
) {
    const uint8_t count = dataManagerSpecCountForContext(context);
    if (count == 0U) return DataManagerCommand::NONE;
    if (index < 0) index = 0;
    if (index >= static_cast<int>(count)) {
        index = static_cast<int>(count) - 1;
    }

    int remaining = index;
    for (const auto& spec : DATA_MANAGER_COMMAND_SPECS) {
        if (spec.context != context) continue;
        if (remaining == 0) return spec.command;
        --remaining;
    }
    return DataManagerCommand::NONE;
}

}  // namespace core::state
