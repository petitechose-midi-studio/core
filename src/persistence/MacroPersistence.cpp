#include "persistence/MacroPersistence.hpp"

#include <array>

#include <config/PlatformCompat.hpp>

#include "app/ExtmemAllocator.hpp"
#include "persistence/MacroTrackBankPersistenceCodec.hpp"

namespace core::persistence {

namespace {

using MacroLibraryBuffer = std::array<uint8_t, MacroPersistence::LIBRARY_PAYLOAD_SIZE>;

FLASHMEM core::app::ExtmemUniquePtr<MacroLibraryBuffer> makeLibraryPayloadScratch() {
    return core::app::makeExtmemUnique<MacroLibraryBuffer>();
}

}  // namespace

FLASHMEM MacroPersistence::MacroPersistence(oc::interface::IStorage& libraryStorage)
    : library_store_(libraryStorage,
                     {.fileMagic = LIBRARY_MAGIC,
                      .domainVersion = LIBRARY_DATA_VERSION,
                      .slotCount = LIBRARY_SLOT_COUNT,
                      .slotPayloadSize = LIBRARY_PAYLOAD_SIZE}) {}

FLASHMEM bool MacroPersistence::init() {
    return initStatus() == PersistenceWriteStatus::OK;
}

FLASHMEM PersistenceWriteStatus MacroPersistence::initStatus() {
    if (!library_store_.init(true)) return PersistenceWriteStatus::INVALID_CONFIG;
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

    if (!macro_track_codec::encodePagesPayload(
            pages,
            payload->data(),
            static_cast<uint32_t>(payload->size())
        )) {
        return PersistenceWriteStatus::INVALID_CONFIG;
    }

    const uint32_t counter = static_cast<uint32_t>(slotIndex) + 1;
    return library_store_.saveSlotStatus(
        slotIndex,
        payload->data(),
        LIBRARY_PAYLOAD_SIZE,
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
        library_store_.loadSlot(slotIndex, payload->data(), LIBRARY_PAYLOAD_SIZE, &metadata);
    if (status != SlotLoadStatus::OK) {
        return status;
    }

    if (metadata.payloadSize != LIBRARY_PAYLOAD_SIZE) {
        return SlotLoadStatus::HEADER_MISMATCH;
    }

    if (!macro_track_codec::applyPagesPayload(
            payload->data(),
            static_cast<uint32_t>(payload->size()),
            pages
        )) {
        return SlotLoadStatus::HEADER_MISMATCH;
    }
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
