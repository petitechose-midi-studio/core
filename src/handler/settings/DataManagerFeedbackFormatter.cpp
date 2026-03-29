#include "handler/settings/DataManagerFeedbackFormatter.hpp"

#include <oc/type/TextFormat.hpp>

namespace core::handler {

namespace {

const char* slotLoadStatusLabel(core::persistence::SlotLoadStatus status) {
    using core::persistence::SlotLoadStatus;
    switch (status) {
        case SlotLoadStatus::OK: return "OK";
        case SlotLoadStatus::EMPTY: return "EMPTY";
        case SlotLoadStatus::OUT_OF_RANGE: return "OUT_OF_RANGE";
        case SlotLoadStatus::CRC_MISMATCH: return "CRC";
        case SlotLoadStatus::HEADER_MISMATCH: return "HEADER";
        case SlotLoadStatus::IO_ERROR: return "IO";
        case SlotLoadStatus::STORAGE_UNAVAILABLE: return "NO_STORAGE";
        case SlotLoadStatus::CAPACITY_TOO_SMALL: return "CAPACITY";
        default:
            return "ERROR";
    }
}

const char* dataManagerActionFailureLabel(core::state::DataManagerCommandAction action) {
    switch (action) {
        case core::state::DataManagerCommandAction::SAVE:
            return "Save failed";
        case core::state::DataManagerCommandAction::ERASE:
            return "Erase failed";
        case core::state::DataManagerCommandAction::LOAD:
        case core::state::DataManagerCommandAction::NONE:
        default:
            return "Command failed";
    }
}

const char* dataManagerActionSuccessVerb(core::state::DataManagerCommandAction action) {
    switch (action) {
        case core::state::DataManagerCommandAction::SAVE:
            return "Saved";
        case core::state::DataManagerCommandAction::LOAD:
            return "Loaded";
        case core::state::DataManagerCommandAction::ERASE:
            return "Erased";
        case core::state::DataManagerCommandAction::NONE:
        default:
            return "Done";
    }
}

}  // namespace

void formatDataManagerCommandExecutionFeedback(
    char* message,
    size_t messageSize,
    core::state::DataManagerCommand command,
    uint8_t slot,
    core::state::DataManagerSetLoadMode setLoadMode,
    const core::state::DataManagerCommandExecutionResult& result
) {
    if (!message || messageSize == 0U) return;

    message[0] = '\0';

    if (!result.handled || command == core::state::DataManagerCommand::NONE) {
        oc::type::text::copy(message, messageSize, "No command");
        return;
    }

    if (result.isLoadOperation) {
        if (result.loadStatus != core::persistence::SlotLoadStatus::OK) {
            size_t pos = oc::type::text::appendString(message, messageSize, 0, "Load ");
            pos = oc::type::text::appendString(message, messageSize, pos, slotLoadStatusLabel(result.loadStatus));
            oc::type::text::terminate(message, messageSize, pos);
            return;
        }

        const char slotTag = core::state::dataManagerCommandSlotTag(command);
        const char safeSlotTag = (slotTag == '\0') ? 'S' : slotTag;
        const char* verb = result.deferredApply ? "Queued" : "Loaded";

        if (core::state::dataManagerCommandSupportsSetLoadMode(command)) {
            const char modeTag =
                (setLoadMode == core::state::DataManagerSetLoadMode::MERGE) ? 'M' : 'R';
            size_t pos = oc::type::text::appendString(message, messageSize, 0, verb);
            pos = oc::type::text::appendChar(message, messageSize, pos, ' ');
            pos = oc::type::text::appendChar(message, messageSize, pos, safeSlotTag);
            pos = oc::type::text::appendUnsigned(message, messageSize, pos, slot + 1U, 2);
            pos = oc::type::text::appendChar(message, messageSize, pos, ' ');
            pos = oc::type::text::appendChar(message, messageSize, pos, modeTag);
            oc::type::text::terminate(message, messageSize, pos);
            return;
        }

        size_t pos = oc::type::text::appendString(message, messageSize, 0, verb);
        pos = oc::type::text::appendChar(message, messageSize, pos, ' ');
        pos = oc::type::text::appendChar(message, messageSize, pos, safeSlotTag);
        pos = oc::type::text::appendUnsigned(message, messageSize, pos, slot + 1U, 2);
        oc::type::text::terminate(message, messageSize, pos);
        return;
    }

    const auto action = core::state::dataManagerCommandAction(command);
    if (!result.success) {
        oc::type::text::copy(message, messageSize, dataManagerActionFailureLabel(action));
        return;
    }

    const char slotTag = core::state::dataManagerCommandSlotTag(command);
    const char safeSlotTag = (slotTag == '\0') ? 'S' : slotTag;
    size_t pos = oc::type::text::appendString(
        message,
        messageSize,
        0,
        dataManagerActionSuccessVerb(action)
    );
    pos = oc::type::text::appendChar(message, messageSize, pos, ' ');
    pos = oc::type::text::appendChar(message, messageSize, pos, safeSlotTag);
    pos = oc::type::text::appendUnsigned(message, messageSize, pos, slot + 1U, 2);
    oc::type::text::terminate(message, messageSize, pos);
}

}  // namespace core::handler
