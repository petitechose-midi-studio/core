#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <type_traits>

#include <oc/interface/IStorage.hpp>
#include <oc/log/Log.hpp>
#include <oc/time/Time.hpp>

#include "persistence/PersistenceSlotFileStore.hpp"
#include "state/macro/MacroPagesState.hpp"

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
        if (!workspace_store_.init(true)) return PersistenceWriteStatus::INVALID_CONFIG;
        if (!library_store_.init(true)) return PersistenceWriteStatus::INVALID_CONFIG;

        std::array<uint8_t, PAYLOAD_SIZE> raw{};
        const auto latest = workspace_store_.loadLatest(raw.data(), raw.size());
        if (latest.status == SlotLoadStatus::OK) {
            next_workspace_counter_ = latest.metadata.saveCounter + 1;
            next_workspace_slot_ =
                static_cast<uint16_t>((latest.slotIndex + 1) % WORKSPACE_SLOT_COUNT);
        } else {
            next_workspace_counter_ = 1;
            next_workspace_slot_ = 0;
        }

        return PersistenceWriteStatus::OK;
    }

    bool loadWorkspace(state::macro::MacroPagesState& pages) {
        Payload snapshot{};
        std::array<uint8_t, PAYLOAD_SIZE> raw{};
        const auto latest = workspace_store_.loadLatest(raw.data(), raw.size());
        if (latest.status != SlotLoadStatus::OK) {
            return false;
        }

        std::memcpy(&snapshot, raw.data(), sizeof(snapshot));
        applyPayload_(snapshot, pages);
        next_workspace_counter_ = latest.metadata.saveCounter + 1;
        next_workspace_slot_ =
            static_cast<uint16_t>((latest.slotIndex + 1) % WORKSPACE_SLOT_COUNT);
        return true;
    }

    bool saveWorkspace(const state::macro::MacroPagesState& pages) {
        return saveWorkspaceStatus(pages) == PersistenceWriteStatus::OK;
    }

    PersistenceWriteStatus saveWorkspaceStatus(const state::macro::MacroPagesState& pages) {
        const uint32_t start_ms = oc::time::millis();
        Payload snapshot{};
        fillPayload_(pages, snapshot);
        std::array<uint8_t, PAYLOAD_SIZE> raw{};
        std::memcpy(raw.data(), &snapshot, sizeof(snapshot));

        const auto status = workspace_store_.saveSlotStatus(
            next_workspace_slot_,
            raw.data(),
            raw.size(),
            next_workspace_counter_
        );
        if (status != PersistenceWriteStatus::OK) return status;

        next_workspace_counter_ += 1;
        next_workspace_slot_ =
            static_cast<uint16_t>((next_workspace_slot_ + 1) % WORKSPACE_SLOT_COUNT);

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

        Payload snapshot{};
        fillPayload_(pages, snapshot);
        std::array<uint8_t, PAYLOAD_SIZE> raw{};
        std::memcpy(raw.data(), &snapshot, sizeof(snapshot));

        const uint32_t counter = static_cast<uint32_t>(slotIndex) + 1;
        return library_store_.saveSlotStatus(
            slotIndex,
            raw.data(),
            raw.size(),
            counter
        );
    }

    SlotLoadStatus loadLibrarySlot(uint8_t slotIndex, state::macro::MacroPagesState& pages) {
        if (slotIndex >= LIBRARY_SLOT_COUNT) return SlotLoadStatus::OUT_OF_RANGE;

        Payload snapshot{};
        std::array<uint8_t, PAYLOAD_SIZE> raw{};
        SlotMetadata metadata{};
        const SlotLoadStatus status =
            library_store_.loadSlot(slotIndex, raw.data(), raw.size(), &metadata);
        if (status != SlotLoadStatus::OK) {
            return status;
        }

        if (metadata.payloadSize != raw.size()) {
            return SlotLoadStatus::HEADER_MISMATCH;
        }

        std::memcpy(&snapshot, raw.data(), sizeof(snapshot));
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
    struct Payload {
        uint8_t activeTrack = 0;
        uint8_t reserved0 = 0;
        uint16_t trackEnabledMask = 0x01;
        std::array<state::macro::MacroTrackData, state::macro::TRACK_COUNT> tracks{};
    };
#pragma pack(pop)

    static_assert(std::is_trivially_copyable_v<state::macro::MacroTrackData>,
                  "MacroTrackData must remain trivially copyable");
    static_assert(std::is_trivially_copyable_v<Payload>,
                  "Macro persistence payload must remain trivially copyable");
    static_assert(sizeof(Payload) == 3620, "Unexpected macro persistence payload size");

    static constexpr uint16_t PAYLOAD_SIZE = static_cast<uint16_t>(sizeof(Payload));

    static void fillPayload_(const state::macro::MacroPagesState& source, Payload& out) {
        out.activeTrack = source.activeTrack;
        out.trackEnabledMask = source.trackEnabledMask.get();
        out.tracks = source.tracks;
    }

    static void applyPayload_(const Payload& payload, state::macro::MacroPagesState& target) {
        target.initDefaults();
        target.tracks = payload.tracks;
        target.trackEnabledMask.set(payload.trackEnabledMask == 0 ? 0x01 : payload.trackEnabledMask);
        target.activeTrack = payload.activeTrack < state::macro::TRACK_COUNT ? payload.activeTrack : 0;
        target.syncActiveTrackCache();
        target.updateActiveConfigs();
    }

    PersistenceSlotFileStore workspace_store_;
    PersistenceSlotFileStore library_store_;
    uint32_t next_workspace_counter_ = 1;
    uint16_t next_workspace_slot_ = 0;
};

}  // namespace core::persistence
