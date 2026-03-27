#pragma once

#include <array>
#include <cstdint>

namespace core::state {

enum class DataManagerContext : uint8_t {
    MACRO = 0,
    SEQUENCER = 1,
};

enum class DataManagerCommand : uint8_t {
    NONE = 0,
    MACRO_SAVE_SLOT = 1,
    MACRO_LOAD_SLOT = 2,
    MACRO_ERASE_SLOT = 3,
    SEQ_SAVE_PATTERN_SLOT = 4,
    SEQ_LOAD_PATTERN_SLOT = 5,
    SEQ_ERASE_PATTERN_SLOT = 6,
    SEQ_SAVE_SET_SLOT = 7,
    SEQ_LOAD_SET_SLOT = 8,
    SEQ_ERASE_SET_SLOT = 9,
};

enum class DataManagerSetLoadMode : uint8_t {
    REPLACE = 0,
    MERGE = 1,
};

enum class DataManagerDialogMode : uint8_t {
    ASSIGN_SHORTCUT = 0,
    COMMAND_PALETTE = 1,
    SLOT_PICKER = 2,
    SET_LOAD_MODE = 3,
    CONFIRM = 4,
};

enum class DataManagerShortcutSide : uint8_t {
    LEFT = 0,
    RIGHT = 1,
};

enum class DataManagerSlotDomain : uint8_t {
    NONE = 0,
    MACRO_LIBRARY = 1,
    SEQ_PATTERN_LIBRARY = 2,
    SEQ_SET_LIBRARY = 3,
};

enum class DataManagerCommandAction : uint8_t {
    NONE = 0,
    SAVE = 1,
    LOAD = 2,
    ERASE = 3,
};

inline constexpr DataManagerCommand DEFAULT_MACRO_SHORTCUT_LEFT = DataManagerCommand::MACRO_SAVE_SLOT;
inline constexpr DataManagerCommand DEFAULT_MACRO_SHORTCUT_RIGHT = DataManagerCommand::MACRO_LOAD_SLOT;
inline constexpr DataManagerCommand DEFAULT_SEQ_SHORTCUT_LEFT = DataManagerCommand::SEQ_SAVE_PATTERN_SLOT;
inline constexpr DataManagerCommand DEFAULT_SEQ_SHORTCUT_RIGHT = DataManagerCommand::SEQ_LOAD_PATTERN_SLOT;
inline constexpr int DATA_MANAGER_MAX_COMMANDS_PER_CONTEXT = 6;

struct DataManagerCommandSpec {
    DataManagerCommand command = DataManagerCommand::NONE;
    DataManagerContext context = DataManagerContext::MACRO;
    DataManagerSlotDomain slotDomain = DataManagerSlotDomain::NONE;
    DataManagerCommandAction action = DataManagerCommandAction::NONE;
    const char* label = "None";
    char slotTag = '\0';
    bool supportsSetLoadMode = false;
};

inline constexpr std::array<DataManagerCommandSpec, 9> DATA_MANAGER_COMMAND_SPECS = {{
    {DataManagerCommand::MACRO_SAVE_SLOT, DataManagerContext::MACRO, DataManagerSlotDomain::MACRO_LIBRARY, DataManagerCommandAction::SAVE, "Save Macro", 'M', false},
    {DataManagerCommand::MACRO_LOAD_SLOT, DataManagerContext::MACRO, DataManagerSlotDomain::MACRO_LIBRARY, DataManagerCommandAction::LOAD, "Load Macro", 'M', false},
    {DataManagerCommand::MACRO_ERASE_SLOT, DataManagerContext::MACRO, DataManagerSlotDomain::MACRO_LIBRARY, DataManagerCommandAction::ERASE, "Erase Macro", 'M', false},
    {DataManagerCommand::SEQ_SAVE_PATTERN_SLOT, DataManagerContext::SEQUENCER, DataManagerSlotDomain::SEQ_PATTERN_LIBRARY, DataManagerCommandAction::SAVE, "Save Pattern", 'P', false},
    {DataManagerCommand::SEQ_LOAD_PATTERN_SLOT, DataManagerContext::SEQUENCER, DataManagerSlotDomain::SEQ_PATTERN_LIBRARY, DataManagerCommandAction::LOAD, "Load Pattern", 'P', false},
    {DataManagerCommand::SEQ_ERASE_PATTERN_SLOT, DataManagerContext::SEQUENCER, DataManagerSlotDomain::SEQ_PATTERN_LIBRARY, DataManagerCommandAction::ERASE, "Erase Pattern", 'P', false},
    {DataManagerCommand::SEQ_SAVE_SET_SLOT, DataManagerContext::SEQUENCER, DataManagerSlotDomain::SEQ_SET_LIBRARY, DataManagerCommandAction::SAVE, "Save Set", 'S', false},
    {DataManagerCommand::SEQ_LOAD_SET_SLOT, DataManagerContext::SEQUENCER, DataManagerSlotDomain::SEQ_SET_LIBRARY, DataManagerCommandAction::LOAD, "Load Set", 'S', true},
    {DataManagerCommand::SEQ_ERASE_SET_SLOT, DataManagerContext::SEQUENCER, DataManagerSlotDomain::SEQ_SET_LIBRARY, DataManagerCommandAction::ERASE, "Erase Set", 'S', false},
}};

inline constexpr uint8_t dataManagerSpecCountForContext(DataManagerContext context) {
    uint8_t count = 0;
    for (const auto& spec : DATA_MANAGER_COMMAND_SPECS) {
        if (spec.context == context) ++count;
    }
    return count;
}

static_assert(dataManagerSpecCountForContext(DataManagerContext::MACRO) <= static_cast<uint8_t>(DATA_MANAGER_MAX_COMMANDS_PER_CONTEXT));
static_assert(dataManagerSpecCountForContext(DataManagerContext::SEQUENCER) <= static_cast<uint8_t>(DATA_MANAGER_MAX_COMMANDS_PER_CONTEXT));

inline constexpr DataManagerCommand defaultDataManagerShortcut(DataManagerContext context,
                                                               DataManagerShortcutSide side) {
    if (context == DataManagerContext::MACRO) {
        return (side == DataManagerShortcutSide::LEFT) ? DEFAULT_MACRO_SHORTCUT_LEFT : DEFAULT_MACRO_SHORTCUT_RIGHT;
    }
    return (side == DataManagerShortcutSide::LEFT) ? DEFAULT_SEQ_SHORTCUT_LEFT : DEFAULT_SEQ_SHORTCUT_RIGHT;
}

inline constexpr const DataManagerCommandSpec* dataManagerCommandSpec(DataManagerCommand cmd) {
    for (const auto& spec : DATA_MANAGER_COMMAND_SPECS) {
        if (spec.command == cmd) return &spec;
    }
    return nullptr;
}

inline constexpr const char* dataManagerCommandLabel(DataManagerCommand cmd) {
    if (const auto* spec = dataManagerCommandSpec(cmd)) return spec->label;
    return "None";
}

inline constexpr bool dataManagerCommandMatchesContext(DataManagerContext context, DataManagerCommand cmd) {
    if (cmd == DataManagerCommand::NONE) return true;
    if (const auto* spec = dataManagerCommandSpec(cmd)) return spec->context == context;
    return false;
}

inline constexpr uint8_t dataManagerCommandCount(DataManagerContext context) {
    return dataManagerSpecCountForContext(context);
}

inline constexpr DataManagerCommand dataManagerCommandAt(DataManagerContext context, int index) {
    const uint8_t count = dataManagerSpecCountForContext(context);
    if (count == 0U) return DataManagerCommand::NONE;
    if (index < 0) index = 0;
    if (index >= static_cast<int>(count)) index = static_cast<int>(count) - 1;

    int remaining = index;
    for (const auto& spec : DATA_MANAGER_COMMAND_SPECS) {
        if (spec.context != context) continue;
        if (remaining == 0) return spec.command;
        --remaining;
    }
    return DataManagerCommand::NONE;
}

inline constexpr int dataManagerCommandIndex(DataManagerContext context, DataManagerCommand command) {
    int index = 0;
    for (const auto& spec : DATA_MANAGER_COMMAND_SPECS) {
        if (spec.context != context) continue;
        if (spec.command == command) return index;
        ++index;
    }
    return 0;
}

inline constexpr DataManagerSlotDomain dataManagerSlotDomain(DataManagerCommand cmd) {
    if (const auto* spec = dataManagerCommandSpec(cmd)) return spec->slotDomain;
    return DataManagerSlotDomain::NONE;
}

inline constexpr DataManagerCommandAction dataManagerCommandAction(DataManagerCommand cmd) {
    if (const auto* spec = dataManagerCommandSpec(cmd)) return spec->action;
    return DataManagerCommandAction::NONE;
}

inline constexpr char dataManagerCommandSlotTag(DataManagerCommand cmd) {
    if (const auto* spec = dataManagerCommandSpec(cmd)) return spec->slotTag;
    return '\0';
}

inline constexpr bool dataManagerCommandIsSave(DataManagerCommand cmd) {
    return dataManagerCommandAction(cmd) == DataManagerCommandAction::SAVE;
}

inline constexpr bool dataManagerCommandIsErase(DataManagerCommand cmd) {
    return dataManagerCommandAction(cmd) == DataManagerCommandAction::ERASE;
}

inline constexpr bool dataManagerCommandSupportsSetLoadMode(DataManagerCommand cmd) {
    if (const auto* spec = dataManagerCommandSpec(cmd)) return spec->supportsSetLoadMode;
    return false;
}

inline constexpr DataManagerCommand sanitizeDataManagerShortcut(DataManagerContext context,
                                                                DataManagerCommand candidate,
                                                                DataManagerCommand fallback) {
    if (dataManagerCommandMatchesContext(context, candidate) && candidate != DataManagerCommand::NONE) {
        return candidate;
    }
    return fallback;
}

}  // namespace core::state
