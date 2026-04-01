#include "context/standalone/DataManagerPresenterFormatters.hpp"

#include <config/PlatformCompat.hpp>
#include <algorithm>

#include <oc/type/TextFormat.hpp>

#include "state/DataManagerWorkflow.hpp"

namespace core::context::standalone::data_manager_presenter {

namespace {

constexpr const char* DEFAULT_OVERLAY_META = "NAV=MAP  L/R=RUN  C=ALL";
constexpr const char* const SET_MODE_ITEMS[] = {"REPLACE", "MERGE"};
constexpr const char* const CONFIRM_ITEMS[] = {"CANCEL", "CONFIRM"};

uint32_t hashFeedback(const char* feedback) {
    uint32_t hash = 0;
    if (!feedback) return hash;

    for (const char* p = feedback; *p; ++p) {
        hash = (hash * 131U) + static_cast<uint8_t>(*p);
    }
    return hash;
}

void fillCommandItems(core::state::DataManagerContext context,
                      std::array<const char*, core::state::DATA_MANAGER_MAX_COMMANDS_PER_CONTEXT>& items,
                      int commandCount) {
    for (int i = 0; i < commandCount; ++i) {
        items[i] = core::state::dataManagerCommandLabel(
            core::state::dataManagerCommandAt(context, i)
        );
    }
}

void fillSlotItems(core::state::DataManagerCommand command,
                   uint8_t slotCount,
                   std::array<std::array<char, 8>, 32>& slotLabels,
                   std::array<const char*, 32>& slotItems) {
    const char slotTag = core::state::dataManagerCommandSlotTag(command);
    const char safeSlotTag = (slotTag == '\0') ? 'S' : slotTag;

    for (uint8_t i = 0; i < slotCount; ++i) {
        size_t pos = oc::type::text::appendChar(
            slotLabels[i].data(),
            slotLabels[i].size(),
            0,
            safeSlotTag
        );
        pos = oc::type::text::appendUnsigned(
            slotLabels[i].data(),
            slotLabels[i].size(),
            pos,
            static_cast<unsigned>(i + 1),
            2
        );
        oc::type::text::terminate(slotLabels[i].data(), slotLabels[i].size(), pos);
        slotItems[i] = slotLabels[i].data();
    }
}

const char* buildConfirmMeta(core::state::DataManagerCommand command,
                             uint8_t slot,
                             char* buffer,
                             size_t bufferSize) {
    const char slotTag = core::state::dataManagerCommandSlotTag(command);
    const char safeSlotTag = (slotTag == '\0') ? 'S' : slotTag;
    const char* prefix =
        core::state::dataManagerCommandIsErase(command) ? "ERASE " : "OVERWRITE ";

    size_t pos = oc::type::text::appendString(buffer, bufferSize, 0, prefix);
    pos = oc::type::text::appendChar(buffer, bufferSize, pos, safeSlotTag);
    pos = oc::type::text::appendUnsigned(buffer, bufferSize, pos, static_cast<unsigned>(slot + 1U), 2);
    pos = oc::type::text::appendString(buffer, bufferSize, pos, " ?");
    oc::type::text::terminate(buffer, bufferSize, pos);
    return buffer;
}

}  // namespace

FLASHMEM OverlayRenderData buildOverlayRenderData(const Source& source) {
    OverlayRenderData data{};
    const auto& dm = source.dataManager;

    const bool macroContext = dm.context.get() == core::state::DataManagerContext::MACRO;
    const auto leftCommand = dm.shortcutForRow(0U);
    const auto rightCommand = dm.shortcutForRow(1U);
    const char* feedback = dm.feedback.get();

    data.title = macroContext ? "MACRO TOOLS" : "SEQUENCER TOOLS";
    data.meta = (feedback && feedback[0] != '\0') ? feedback : DEFAULT_OVERLAY_META;
    data.selectedIndex = std::min<uint8_t>(dm.focusedRow.get(), 1U);
    data.rows[0] = {
        .key = "Bottom Left",
        .value = core::state::dataManagerCommandLabel(leftCommand),
    };
    data.rows[1] = {
        .key = "Bottom Right",
        .value = core::state::dataManagerCommandLabel(rightCommand),
    };
    data.dataRevision =
        (static_cast<uint32_t>(dm.context.get()) << 24) |
        (static_cast<uint32_t>(leftCommand) << 16) |
        (static_cast<uint32_t>(rightCommand) << 8) |
        (hashFeedback(feedback) & 0xFFU);

    return data;
}

FLASHMEM DialogRenderData buildDialogRenderData(const Source& source) {
    DialogRenderData data{};
    const auto& dm = source.dataManager;
    const auto& dialog = dm.dialog;
    const auto phase = dm.flowPhase.get();

    if (!core::state::dataManagerFlowShowsDialog(phase) || !dialog.visible.get()) {
        return data;
    }

    data.visible = true;

    const auto context = dm.context.get();
    const int commandCount = static_cast<int>(core::state::dataManagerCommandCount(context));
    fillCommandItems(context, data.commandItems, commandCount);

    if (phase == core::state::DataManagerFlowPhase::ASSIGN_SHORTCUT) {
        data.title = (dialog.editingShortcutRow.get() == 0) ? "MAP LEFT" : "MAP RIGHT";
        data.meta = "SELECT COMMAND";
        data.items = data.commandItems.data();
        data.itemCount = commandCount;
        data.selectedIndex = std::clamp(dialog.selectedIndex.get(), 0, commandCount - 1);
    } else if (phase == core::state::DataManagerFlowPhase::COMMAND_PALETTE) {
        data.title = "COMMANDS";
        data.meta = "RUN COMMAND";
        data.items = data.commandItems.data();
        data.itemCount = commandCount;
        data.selectedIndex = std::clamp(dialog.selectedIndex.get(), 0, commandCount - 1);
    } else if (phase == core::state::DataManagerFlowPhase::SLOT_PICKER) {
        const auto command = dm.pendingCommand.get();
        const uint8_t slotCount = core::state::DataManagerWorkflow::slotCount(command);
        if (slotCount == 0U) {
            data.visible = false;
            return data;
        }

        data.title = core::state::dataManagerCommandLabel(command);
        data.meta = "SELECT SLOT";
        fillSlotItems(command, slotCount, data.slotLabels, data.slotItems);
        data.items = data.slotItems.data();
        data.itemCount = static_cast<int>(slotCount);
        data.selectedIndex = std::clamp(dialog.selectedIndex.get(), 0, data.itemCount - 1);
    } else if (phase == core::state::DataManagerFlowPhase::SET_LOAD_MODE) {
        data.title = "LOAD SET";
        data.meta = "MODE";
        data.items = SET_MODE_ITEMS;
        data.itemCount = 2;
        data.selectedIndex = std::clamp(dialog.selectedIndex.get(), 0, 1);
    } else if (phase == core::state::DataManagerFlowPhase::CONFIRM) {
        static constexpr size_t META_SIZE = 24;
        static std::array<char, META_SIZE> confirmMeta{};
        data.title = "CONFIRM";
        data.meta = buildConfirmMeta(dm.pendingCommand.get(), dm.pendingSlot.get(), confirmMeta.data(), confirmMeta.size());
        data.items = CONFIRM_ITEMS;
        data.itemCount = 2;
        data.selectedIndex = std::clamp(dialog.selectedIndex.get(), 0, 1);
    }

    if (!data.items || data.itemCount <= 0) {
        data.visible = false;
        return data;
    }

    data.dataRevision =
        (static_cast<uint32_t>(phase) << 24) |
        (static_cast<uint32_t>(data.selectedIndex) << 16) |
        (static_cast<uint32_t>(dm.pendingCommand.get()) << 8) |
        static_cast<uint32_t>(dm.pendingSlot.get());

    return data;
}

FLASHMEM SoftkeyRenderData buildSoftkeyRenderData(const Source& source) {
    SoftkeyRenderData data{};
    const auto& dm = source.dataManager;
    if (!dm.visible.get()) {
        return data;
    }

    const auto left = dm.shortcutForRow(0U);
    const auto right = dm.shortcutForRow(1U);

    size_t leftPos = oc::type::text::appendString(data.leftLabel.data(), data.leftLabel.size(), 0, "L:");
    leftPos = oc::type::text::appendString(
        data.leftLabel.data(),
        data.leftLabel.size(),
        leftPos,
        core::state::dataManagerCommandLabel(left)
    );
    oc::type::text::terminate(data.leftLabel.data(), data.leftLabel.size(), leftPos);

    size_t rightPos = oc::type::text::appendString(data.rightLabel.data(), data.rightLabel.size(), 0, "R:");
    rightPos = oc::type::text::appendString(
        data.rightLabel.data(),
        data.rightLabel.size(),
        rightPos,
        core::state::dataManagerCommandLabel(right)
    );
    oc::type::text::terminate(data.rightLabel.data(), data.rightLabel.size(), rightPos);

    data.visible = true;
    return data;
}

}  // namespace core::context::standalone::data_manager_presenter
