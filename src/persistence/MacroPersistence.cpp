#include "persistence/MacroPersistence.hpp"

#include <array>
#include <type_traits>

#include <config/PlatformCompat.hpp>
#include <oc/log/Log.hpp>
#include <oc/time/Time.hpp>

#include "app/ExtmemAllocator.hpp"

namespace core::persistence {

namespace {

#pragma pack(push, 1)
struct WorkspacePayload {
    std::array<state::macro::MacroTrackData, state::macro::TRACK_COUNT> tracks{};
};

struct LibraryPayload {
    uint8_t activeTrack = 0;
    uint8_t reserved0 = 0;
    uint16_t trackEnabledMask = 0x0001;
    std::array<state::macro::MacroTrackData, state::macro::TRACK_COUNT> tracks{};
};
#pragma pack(pop)

static_assert(std::is_trivially_copyable_v<state::macro::MacroTrackData>,
              "MacroTrackData must remain trivially copyable");
static_assert(std::is_trivially_copyable_v<WorkspacePayload>,
              "Macro workspace payload must remain trivially copyable");
static_assert(std::is_trivially_copyable_v<LibraryPayload>,
              "Macro library payload must remain trivially copyable");
static_assert(sizeof(LibraryPayload) == 14404, "Unexpected macro library payload size");
static_assert(sizeof(LibraryPayload) == sizeof(WorkspacePayload) + 4,
              "Unexpected macro library header size");

constexpr uint16_t kWorkspacePayloadSize = static_cast<uint16_t>(sizeof(WorkspacePayload));
constexpr uint16_t kLibraryPayloadSize = static_cast<uint16_t>(sizeof(LibraryPayload));

template <typename Payload>
uint8_t* payloadBytes(Payload& payload) {
    return reinterpret_cast<uint8_t*>(&payload);
}

template <typename Payload>
const uint8_t* payloadBytes(const Payload& payload) {
    return reinterpret_cast<const uint8_t*>(&payload);
}

FLASHMEM core::app::ExtmemUniquePtr<WorkspacePayload> makeWorkspacePayloadScratch() {
    return core::app::makeExtmemUnique<WorkspacePayload>();
}

FLASHMEM core::app::ExtmemUniquePtr<LibraryPayload> makeLibraryPayloadScratch() {
    return core::app::makeExtmemUnique<LibraryPayload>();
}

FLASHMEM void fillWorkspacePayload(const state::macro::MacroPagesState& source,
                                   WorkspacePayload& out) {
    out.tracks = source.tracks;
}

FLASHMEM void fillLibraryPayload(const state::macro::MacroPagesState& source,
                                 LibraryPayload& out) {
    source.captureSharedTrackState(out.trackEnabledMask, out.activeTrack);
    out.tracks = source.tracks;
}

FLASHMEM void applyWorkspacePayload(const WorkspacePayload& payload,
                                    state::macro::MacroPagesState& target) {
    target.restoreTracksPreservingSharedState(payload.tracks);
}

FLASHMEM void applyLibraryPayload(const LibraryPayload& payload,
                                  state::macro::MacroPagesState& target) {
    target.restoreTracksWithSharedState(
        payload.tracks,
        payload.trackEnabledMask,
        payload.activeTrack
    );
}

}  // namespace

FLASHMEM MacroPersistence::MacroPersistence(oc::interface::IStorage& workspaceStorage,
                                            oc::interface::IStorage& libraryStorage)
    : workspace_store_(workspaceStorage,
                       {.fileMagic = WORKSPACE_MAGIC,
                        .domainVersion = WORKSPACE_DATA_VERSION,
                        .slotCount = WORKSPACE_SLOT_COUNT,
                        .slotPayloadSize = kWorkspacePayloadSize})
    , library_store_(libraryStorage,
                     {.fileMagic = LIBRARY_MAGIC,
                      .domainVersion = LIBRARY_DATA_VERSION,
                      .slotCount = LIBRARY_SLOT_COUNT,
                      .slotPayloadSize = kLibraryPayloadSize}) {}

FLASHMEM bool MacroPersistence::init() {
    return initStatus() == PersistenceWriteStatus::OK;
}

FLASHMEM PersistenceWriteStatus MacroPersistence::initStatus() {
    if (!workspace_store_.init(true)) return PersistenceWriteStatus::INVALID_CONFIG;
    if (!library_store_.init(true)) return PersistenceWriteStatus::INVALID_CONFIG;

    auto payload = makeWorkspacePayloadScratch();
    if (!payload) return PersistenceWriteStatus::STORAGE_UNAVAILABLE;

    const auto latest = workspace_store_.loadLatest(payloadBytes(*payload), kWorkspacePayloadSize);
    if (latest.status == SlotLoadStatus::OK && latest.metadata.payloadSize == kWorkspacePayloadSize) {
        next_workspace_counter_ = latest.metadata.saveCounter + 1;
        next_workspace_slot_ =
            static_cast<uint16_t>((latest.slotIndex + 1) % WORKSPACE_SLOT_COUNT);
    } else {
        next_workspace_counter_ = 1;
        next_workspace_slot_ = 0;
    }

    return PersistenceWriteStatus::OK;
}

FLASHMEM bool MacroPersistence::loadWorkspace(state::macro::MacroPagesState& pages) {
    auto payload = makeWorkspacePayloadScratch();
    if (!payload) return false;

    const auto latest = workspace_store_.loadLatest(payloadBytes(*payload), kWorkspacePayloadSize);
    if (latest.status != SlotLoadStatus::OK ||
        latest.metadata.payloadSize != kWorkspacePayloadSize) {
        return false;
    }

    applyWorkspacePayload(*payload, pages);
    next_workspace_counter_ = latest.metadata.saveCounter + 1;
    next_workspace_slot_ =
        static_cast<uint16_t>((latest.slotIndex + 1) % WORKSPACE_SLOT_COUNT);
    return true;
}

FLASHMEM bool MacroPersistence::saveWorkspace(const state::macro::MacroPagesState& pages) {
    return saveWorkspaceStatus(pages) == PersistenceWriteStatus::OK;
}

FLASHMEM PersistenceWriteStatus MacroPersistence::saveWorkspaceStatus(
    const state::macro::MacroPagesState& pages
) {
    const uint32_t start_ms = oc::time::millis();
    auto payload = makeWorkspacePayloadScratch();
    if (!payload) return PersistenceWriteStatus::STORAGE_UNAVAILABLE;

    fillWorkspacePayload(pages, *payload);

    const auto status = workspace_store_.saveSlotStatus(
        next_workspace_slot_,
        payloadBytes(*payload),
        kWorkspacePayloadSize,
        next_workspace_counter_
    );
    if (status != PersistenceWriteStatus::OK) return status;

    next_workspace_counter_ += 1;
    next_workspace_slot_ =
        static_cast<uint16_t>((next_workspace_slot_ + 1) % WORKSPACE_SLOT_COUNT);

    const uint32_t elapsed_ms = oc::time::millis() - start_ms;
#if defined(PERF_LOG)
    if (elapsed_ms >= 5) {
        OC_LOG_INFO("[Perf][MacroPersist] workspace save took {}ms", elapsed_ms);
    }
#else
    (void)elapsed_ms;
#endif
    return PersistenceWriteStatus::OK;
}

FLASHMEM bool MacroPersistence::saveLibrarySlot(uint8_t slotIndex,
                                                const state::macro::MacroPagesState& pages) {
    return saveLibrarySlotStatus(slotIndex, pages) == PersistenceWriteStatus::OK;
}

FLASHMEM PersistenceWriteStatus MacroPersistence::saveLibrarySlotStatus(
    uint8_t slotIndex,
    const state::macro::MacroPagesState& pages
) {
    if (slotIndex >= LIBRARY_SLOT_COUNT) return PersistenceWriteStatus::OUT_OF_RANGE;

    auto payload = makeLibraryPayloadScratch();
    if (!payload) return PersistenceWriteStatus::STORAGE_UNAVAILABLE;

    fillLibraryPayload(pages, *payload);

    const uint32_t counter = static_cast<uint32_t>(slotIndex) + 1;
    return library_store_.saveSlotStatus(
        slotIndex,
        payloadBytes(*payload),
        kLibraryPayloadSize,
        counter
    );
}

FLASHMEM SlotLoadStatus MacroPersistence::loadLibrarySlot(
    uint8_t slotIndex,
    state::macro::MacroPagesState& pages
) {
    if (slotIndex >= LIBRARY_SLOT_COUNT) return SlotLoadStatus::OUT_OF_RANGE;

    auto payload = makeLibraryPayloadScratch();
    if (!payload) return SlotLoadStatus::STORAGE_UNAVAILABLE;

    SlotMetadata metadata{};
    const SlotLoadStatus status =
        library_store_.loadSlot(slotIndex, payloadBytes(*payload), kLibraryPayloadSize, &metadata);
    if (status != SlotLoadStatus::OK) {
        return status;
    }

    if (metadata.payloadSize != kLibraryPayloadSize) {
        return SlotLoadStatus::HEADER_MISMATCH;
    }

    applyLibraryPayload(*payload, pages);
    return SlotLoadStatus::OK;
}

FLASHMEM bool MacroPersistence::eraseLibrarySlot(uint8_t slotIndex) {
    return eraseLibrarySlotStatus(slotIndex) == PersistenceWriteStatus::OK;
}

FLASHMEM PersistenceWriteStatus MacroPersistence::eraseLibrarySlotStatus(uint8_t slotIndex) {
    if (slotIndex >= LIBRARY_SLOT_COUNT) return PersistenceWriteStatus::OUT_OF_RANGE;
    return library_store_.eraseSlotStatus(slotIndex);
}

}  // namespace core::persistence
