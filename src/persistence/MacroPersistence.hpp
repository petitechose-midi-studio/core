#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <type_traits>

#include <oc/interface/IStorage.hpp>
#include <oc/log/Log.hpp>
#include <oc/time/Time.hpp>

#include "state/macro/MacroPagesState.hpp"
#include "persistence/PersistenceSlotFileStore.hpp"

namespace core::persistence {

class MacroPersistence {
public:
    static constexpr uint16_t WORKSPACE_SLOT_COUNT = 2;
    static constexpr uint16_t LIBRARY_SLOT_COUNT = 16;

    static constexpr uint32_t WORKSPACE_MAGIC = 0x4D57534B;  // "MWSK"
    static constexpr uint32_t LIBRARY_MAGIC = 0x4D4C4942;    // "MLIB"
    static constexpr uint8_t DATA_VERSION = 1;

    explicit MacroPersistence(oc::interface::IStorage& workspaceStorage,
                              oc::interface::IStorage& libraryStorage)
        : workspace_store_(workspaceStorage,
                           {.fileMagic = WORKSPACE_MAGIC,
                            .domainVersion = DATA_VERSION,
                            .slotCount = WORKSPACE_SLOT_COUNT,
                            .slotPayloadSize = PAYLOAD_SIZE})
        , library_store_(libraryStorage,
                         {.fileMagic = LIBRARY_MAGIC,
                          .domainVersion = DATA_VERSION,
                          .slotCount = LIBRARY_SLOT_COUNT,
                          .slotPayloadSize = PAYLOAD_SIZE}) {}

    bool init() {
        return initStatus() == PersistenceWriteStatus::OK;
    }

    PersistenceWriteStatus initStatus() {
        if (!workspace_store_.init()) return PersistenceWriteStatus::IO_ERROR;
        if (!library_store_.init()) return PersistenceWriteStatus::IO_ERROR;

        uint8_t payload[PAYLOAD_SIZE] = {};
        const auto latest = workspace_store_.loadLatest(payload, sizeof(payload));
        if (latest.status == SlotLoadStatus::OK) {
            next_workspace_counter_ = latest.metadata.saveCounter + 1;
            next_workspace_slot_ = static_cast<uint16_t>((latest.slotIndex + 1) % WORKSPACE_SLOT_COUNT);
        } else {
            next_workspace_counter_ = 1;
            next_workspace_slot_ = 0;
        }

        return PersistenceWriteStatus::OK;
    }

    bool loadWorkspace(state::macro::MacroPagesState& pages) {
        uint8_t payload[PAYLOAD_SIZE] = {};
        const auto latest = workspace_store_.loadLatest(payload, sizeof(payload));
        if (latest.status != SlotLoadStatus::OK) {
            return false;
        }

        PayloadV1 snapshot{};
        std::memcpy(&snapshot, payload, sizeof(snapshot));
        applyPayload_(snapshot, pages);

        next_workspace_counter_ = latest.metadata.saveCounter + 1;
        next_workspace_slot_ = static_cast<uint16_t>((latest.slotIndex + 1) % WORKSPACE_SLOT_COUNT);
        return true;
    }

    bool saveWorkspace(const state::macro::MacroPagesState& pages) {
        return saveWorkspaceStatus(pages) == PersistenceWriteStatus::OK;
    }

    PersistenceWriteStatus saveWorkspaceStatus(const state::macro::MacroPagesState& pages) {
        const uint32_t start_ms = oc::time::millis();
        PayloadV1 snapshot{};
        fillPayload_(pages, snapshot);

        uint8_t payload[PAYLOAD_SIZE] = {};
        std::memcpy(payload, &snapshot, sizeof(snapshot));

        const auto status = workspace_store_.saveSlotStatus(
                next_workspace_slot_,
                payload,
                sizeof(snapshot),
                next_workspace_counter_);
        if (status != PersistenceWriteStatus::OK) return status;

        next_workspace_counter_ += 1;
        next_workspace_slot_ = static_cast<uint16_t>((next_workspace_slot_ + 1) % WORKSPACE_SLOT_COUNT);

        const uint32_t elapsed_ms = oc::time::millis() - start_ms;
        if (elapsed_ms >= 5) {
            OC_LOG_INFO("[Perf][MacroPersist] workspace save took {}ms", elapsed_ms);
        }
        return PersistenceWriteStatus::OK;
    }

    bool saveLibrarySlot(uint8_t slotIndex, const state::macro::MacroPagesState& pages) {
        return saveLibrarySlotStatus(slotIndex, pages) == PersistenceWriteStatus::OK;
    }

    PersistenceWriteStatus saveLibrarySlotStatus(uint8_t slotIndex,
                                                 const state::macro::MacroPagesState& pages) {
        if (slotIndex >= LIBRARY_SLOT_COUNT) return PersistenceWriteStatus::OUT_OF_RANGE;

        PayloadV1 snapshot{};
        fillPayload_(pages, snapshot);

        uint8_t payload[PAYLOAD_SIZE] = {};
        std::memcpy(payload, &snapshot, sizeof(snapshot));

        const uint32_t counter = static_cast<uint32_t>(slotIndex) + 1;
        return library_store_.saveSlotStatus(slotIndex, payload, sizeof(snapshot), counter);
    }

    SlotLoadStatus loadLibrarySlot(uint8_t slotIndex, state::macro::MacroPagesState& pages) {
        if (slotIndex >= LIBRARY_SLOT_COUNT) return SlotLoadStatus::OUT_OF_RANGE;

        uint8_t payload[PAYLOAD_SIZE] = {};
        SlotMetadata meta{};
        const SlotLoadStatus status = library_store_.loadSlot(slotIndex, payload, sizeof(payload), &meta);
        if (status != SlotLoadStatus::OK) {
            return status;
        }

        if (meta.payloadSize != sizeof(PayloadV1)) {
            return SlotLoadStatus::HEADER_MISMATCH;
        }

        PayloadV1 snapshot{};
        std::memcpy(&snapshot, payload, sizeof(snapshot));
        applyPayload_(snapshot, pages);
        return SlotLoadStatus::OK;
    }

    bool eraseLibrarySlot(uint8_t slotIndex) {
        return eraseLibrarySlotStatus(slotIndex) == PersistenceWriteStatus::OK;
    }

    PersistenceWriteStatus eraseLibrarySlotStatus(uint8_t slotIndex) {
        if (slotIndex >= LIBRARY_SLOT_COUNT) return PersistenceWriteStatus::OUT_OF_RANGE;
        return library_store_.eraseSlotStatus(slotIndex);
    }

private:
#pragma pack(push, 1)
    struct PayloadV1 {
        uint8_t activePage = 0;
        std::array<state::macro::MacroPageData, state::macro::PAGE_COUNT> pages{};
    };
#pragma pack(pop)

    static_assert(std::is_trivially_copyable<state::macro::MacroPageData>::value,
                  "MacroPageData must remain trivially copyable");
    static_assert(sizeof(PayloadV1) == 513, "Unexpected macro payload size");

    static constexpr uint16_t PAYLOAD_SIZE = static_cast<uint16_t>(sizeof(PayloadV1));

    static void fillPayload_(const state::macro::MacroPagesState& source, PayloadV1& out) {
        out.activePage = source.activePage;
        for (uint8_t i = 0; i < state::macro::PAGE_COUNT; ++i) {
            out.pages[i] = source.pages[i];
        }
    }

    static void applyPayload_(const PayloadV1& payload, state::macro::MacroPagesState& target) {
        for (uint8_t i = 0; i < state::macro::PAGE_COUNT; ++i) {
            target.pages[i] = payload.pages[i];
        }

        const uint8_t clamped_active_page =
            (payload.activePage < state::macro::PAGE_COUNT) ? payload.activePage : 0;
        target.activePage = clamped_active_page;
        target.updateActiveConfigs();
    }

    PersistenceSlotFileStore workspace_store_;
    PersistenceSlotFileStore library_store_;
    uint32_t next_workspace_counter_ = 1;
    uint16_t next_workspace_slot_ = 0;
};

}  // namespace core::persistence
