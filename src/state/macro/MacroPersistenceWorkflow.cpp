#include "state/macro/MacroPersistenceWorkflow.hpp"

#include <config/PlatformCompat.hpp>
#include <oc/log/Log.hpp>

#include "state/CoreState.hpp"
#include "state/macro/MacroWorkflow.hpp"

namespace core::state::macro {

FLASHMEM bool MacroPersistenceWorkflow::saveLibrarySlot(CoreState& state, uint8_t slotIndex) {
    if (!state.isMacroPersistenceReady()) return false;

    const auto status = state.macroPersistence.saveLibrarySlotStatus(slotIndex, state.pages);
    if (status != persistence::PersistenceWriteStatus::OK) {
        OC_LOG_WARN("[MacroPersistence] Save library slot {} failed: {}",
                    slotIndex,
                    persistence::persistenceWriteStatusLabel(status));
    }
    return status == persistence::PersistenceWriteStatus::OK;
}

FLASHMEM persistence::SlotLoadStatus MacroPersistenceWorkflow::loadLibrarySlot(CoreState& state,
                                                                      uint8_t slotIndex) {
    if (!state.isMacroPersistenceReady()) return persistence::SlotLoadStatus::STORAGE_UNAVAILABLE;

    const persistence::SlotLoadStatus status =
        state.macroPersistence.loadLibrarySlot(slotIndex, state.pages);
    if (status == persistence::SlotLoadStatus::OK) {
        state.macroHistory.clear();
        state.refreshSharedTrackStateFromMacroPages();
        state.statusBar.pageName.set(state.pages.activePageData().name);
        MacroWorkflow::syncRuntimeFromActivePage(state.macros, state.pages);
        state.markProjectMutated();
        state.configRevision.set(core::state::macro::nextMacroConfigRevision(state.configRevision.get()));
    }

    return status;
}

FLASHMEM bool MacroPersistenceWorkflow::eraseLibrarySlot(CoreState& state, uint8_t slotIndex) {
    if (!state.isMacroPersistenceReady()) return false;
    const auto status = state.macroPersistence.eraseLibrarySlotStatus(slotIndex);
    if (status != persistence::PersistenceWriteStatus::OK) {
        OC_LOG_WARN("[MacroPersistence] Erase library slot {} failed: {}",
                    slotIndex,
                    persistence::persistenceWriteStatusLabel(status));
    }
    return status == persistence::PersistenceWriteStatus::OK;
}

}  // namespace core::state::macro
