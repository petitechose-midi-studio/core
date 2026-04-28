#include "state/DataManagerShortcutPersistence.hpp"

#include <config/PlatformCompat.hpp>
#include <oc/log/Log.hpp>

namespace core::state::data_manager {

namespace {

FLASHMEM DataManagerShortcutSide shortcutSide(bool leftButton) {
    return leftButton ? DataManagerShortcutSide::LEFT : DataManagerShortcutSide::RIGHT;
}

FLASHMEM DataManagerCommand sanitizeShortcut(DataManagerContext context,
                                             DataManagerShortcutSide side,
                                             DataManagerCommand command) {
    return sanitizeDataManagerShortcut(context, command, defaultDataManagerShortcut(context, side));
}

FLASHMEM void setShortcutSignal(ShortcutStateRefs state,
                                DataManagerContext context,
                                DataManagerShortcutSide side,
                                DataManagerCommand command) {
    const bool left = side == DataManagerShortcutSide::LEFT;
    if (context == DataManagerContext::MACRO) {
        (left ? state.dataManager.macroShortcutLeft : state.dataManager.macroShortcutRight).set(
            command
        );
        return;
    }

    (left ? state.dataManager.seqShortcutLeft : state.dataManager.seqShortcutRight).set(command);
}

FLASHMEM persistence::PersistenceWriteStatus persistShortcut(ShortcutStateRefs state,
                                                             DataManagerContext context,
                                                             DataManagerShortcutSide side,
                                                             DataManagerCommand command) {
    const auto raw = static_cast<uint8_t>(command);
    if (context == DataManagerContext::MACRO) {
        return side == DataManagerShortcutSide::LEFT
                   ? state.settings.saveDataManagerMacroShortcutLeftStatus(raw)
                   : state.settings.saveDataManagerMacroShortcutRightStatus(raw);
    }

    return side == DataManagerShortcutSide::LEFT
               ? state.settings.saveDataManagerSeqShortcutLeftStatus(raw)
               : state.settings.saveDataManagerSeqShortcutRightStatus(raw);
}

FLASHMEM void logShortcutPersistFailure(const char* label,
                                        persistence::PersistenceWriteStatus status) {
    if (status == persistence::PersistenceWriteStatus::OK) return;
    OC_LOG_WARN("[DataManager] Failed to persist {} shortcut: {}",
                label,
                persistence::persistenceWriteStatusLabel(status));
}

FLASHMEM void loadShortcut(ShortcutStateRefs state,
                           DataManagerContext context,
                           DataManagerShortcutSide side,
                           uint8_t rawValue) {
    setShortcutSignal(
        state,
        context,
        side,
        sanitizeShortcut(context, side, static_cast<DataManagerCommand>(rawValue))
    );
}

}  // namespace

FLASHMEM void setShortcut(ShortcutStateRefs state,
                          DataManagerContext context,
                          bool leftButton,
                          DataManagerCommand command) {
    const auto side = shortcutSide(leftButton);
    const auto sanitized = sanitizeShortcut(context, side, command);

    setShortcutSignal(state, context, side, sanitized);
    logShortcutPersistFailure(
        (context == DataManagerContext::MACRO)
            ? (side == DataManagerShortcutSide::LEFT ? "macro left" : "macro right")
            : (side == DataManagerShortcutSide::LEFT ? "sequencer left" : "sequencer right"),
        persistShortcut(state, context, side, sanitized)
    );

    const auto commitStatus = state.settings.commitStatus();
    if (commitStatus != persistence::PersistenceWriteStatus::OK) {
        OC_LOG_WARN("[DataManager] Failed to commit shortcut update: {}",
                    persistence::persistenceWriteStatusLabel(commitStatus));
    }
}

FLASHMEM void loadShortcutsFromSettings(ShortcutStateRefs state) {
    uint8_t macroLeft = 0;
    uint8_t macroRight = 0;
    uint8_t seqLeft = 0;
    uint8_t seqRight = 0;
    if (!state.settings.loadDataManagerShortcuts(macroLeft, macroRight, seqLeft, seqRight)) {
        OC_LOG_WARN("[DataManager] Failed to load shortcut settings, using defaults");
    }

    loadShortcut(state, DataManagerContext::MACRO, DataManagerShortcutSide::LEFT, macroLeft);
    loadShortcut(state, DataManagerContext::MACRO, DataManagerShortcutSide::RIGHT, macroRight);
    loadShortcut(state, DataManagerContext::SEQUENCER, DataManagerShortcutSide::LEFT, seqLeft);
    loadShortcut(state, DataManagerContext::SEQUENCER, DataManagerShortcutSide::RIGHT, seqRight);
}

}  // namespace core::state::data_manager
