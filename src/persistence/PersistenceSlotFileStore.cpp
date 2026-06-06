#include "persistence/PersistenceSlotFileStore.hpp"

#include <array>

#include <config/PlatformCompat.hpp>

namespace core::persistence {

FLASHMEM PersistenceSlotFileStore::PersistenceSlotFileStore(
    oc::interface::IStorage& storage,
    const SlotFileStoreConfig& config
)
    : storage_(storage)
    , config_(config) {}

FLASHMEM bool PersistenceSlotFileStore::init(bool formatIfInvalid) {
    if (!isConfigValid_()) return false;
    if (!storage_.available()) return false;

    FileHeader header{};
    const bool ok = readBytes_(0, reinterpret_cast<uint8_t*>(&header), sizeof(header));
    if (!ok || !isHeaderValid_(header)) {
        return formatIfInvalid ? format() : false;
    }

    return true;
}

FLASHMEM bool PersistenceSlotFileStore::format() {
    return formatStatus() == PersistenceWriteStatus::OK;
}

FLASHMEM PersistenceWriteStatus PersistenceSlotFileStore::formatStatus() {
    if (!isConfigValid_()) return PersistenceWriteStatus::INVALID_CONFIG;
    if (!storage_.available()) return PersistenceWriteStatus::STORAGE_UNAVAILABLE;

    const FileHeader header = buildHeader_();
    if (!writeBytes_(0, reinterpret_cast<const uint8_t*>(&header), sizeof(header))) {
        return PersistenceWriteStatus::IO_ERROR;
    }

    const uint32_t slots_region_address = sizeof(FileHeader);
    const size_t slots_region_size = static_cast<size_t>(config_.slotCount) * slotSize_();
    if (!storage_.erase(slots_region_address, slots_region_size)) {
        return PersistenceWriteStatus::ERASE_FAILED;
    }

    return storage_.commit() ? PersistenceWriteStatus::OK : PersistenceWriteStatus::COMMIT_FAILED;
}

FLASHMEM bool PersistenceSlotFileStore::saveSlot(uint16_t slotIndex,
                                                 const uint8_t* payload,
                                                 uint16_t payloadSize,
                                                 uint32_t saveCounter) {
    return saveSlotStatus(slotIndex, payload, payloadSize, saveCounter) ==
           PersistenceWriteStatus::OK;
}

FLASHMEM PersistenceWriteStatus PersistenceSlotFileStore::saveSlotStatus(
    uint16_t slotIndex,
    const uint8_t* payload,
    uint16_t payloadSize,
    uint32_t saveCounter
) {
    if (!isConfigValid_()) return PersistenceWriteStatus::INVALID_CONFIG;
    if (!storage_.available()) return PersistenceWriteStatus::STORAGE_UNAVAILABLE;
    if (!isSlotIndexValid_(slotIndex)) return PersistenceWriteStatus::OUT_OF_RANGE;
    if (!payload && payloadSize > 0) return PersistenceWriteStatus::PAYLOAD_TOO_LARGE;
    if (payloadSize > config_.slotPayloadSize) return PersistenceWriteStatus::PAYLOAD_TOO_LARGE;

    SlotHeader header{};
    header.magic = SLOT_HEADER_MAGIC;
    header.formatVersion = FILE_FORMAT_VERSION;
    header.state = SLOT_STATE_WRITING;
    header.payloadSize = payloadSize;
    header.saveCounter = saveCounter;
    header.payloadCrc32 = crc32_(payload, payloadSize);

    const uint32_t header_address = slotHeaderAddress(slotIndex);
    if (!writeBytes_(header_address, reinterpret_cast<const uint8_t*>(&header), sizeof(header))) {
        return PersistenceWriteStatus::IO_ERROR;
    }

    if (payloadSize > 0) {
        const uint32_t payload_address = slotPayloadAddress(slotIndex);
        if (!writeBytes_(payload_address, payload, payloadSize)) {
            return PersistenceWriteStatus::IO_ERROR;
        }
    }

    header.state = SLOT_STATE_VALID;
    if (!writeBytes_(header_address, reinterpret_cast<const uint8_t*>(&header), sizeof(header))) {
        return PersistenceWriteStatus::IO_ERROR;
    }

    return storage_.commit() ? PersistenceWriteStatus::OK : PersistenceWriteStatus::COMMIT_FAILED;
}

FLASHMEM SlotLoadStatus PersistenceSlotFileStore::loadSlot(uint16_t slotIndex,
                                                           uint8_t* outPayload,
                                                           uint16_t outCapacity,
                                                           SlotMetadata* outMeta) const {
    if (!isConfigValid_()) return SlotLoadStatus::IO_ERROR;
    if (!storage_.available()) return SlotLoadStatus::STORAGE_UNAVAILABLE;
    if (!isSlotIndexValid_(slotIndex)) return SlotLoadStatus::OUT_OF_RANGE;

    SlotHeader header{};
    const SlotLoadStatus header_status = readSlotHeader_(slotIndex, header);
    if (header_status != SlotLoadStatus::OK) {
        return header_status;
    }

    if (header.payloadSize > outCapacity) {
        return SlotLoadStatus::CAPACITY_TOO_SMALL;
    }

    if (!outPayload && header.payloadSize > 0) {
        return SlotLoadStatus::CAPACITY_TOO_SMALL;
    }

    if (header.payloadSize > 0) {
        if (!readBytes_(slotPayloadAddress(slotIndex), outPayload, header.payloadSize)) {
            return SlotLoadStatus::IO_ERROR;
        }
    }

    const uint32_t actual_crc = crc32_(outPayload, header.payloadSize);
    if (actual_crc != header.payloadCrc32) {
        return SlotLoadStatus::CRC_MISMATCH;
    }

    if (outMeta) {
        outMeta->payloadSize = header.payloadSize;
        outMeta->saveCounter = header.saveCounter;
    }

    return SlotLoadStatus::OK;
}

FLASHMEM LatestSlotLoadResult PersistenceSlotFileStore::loadLatest(
    uint8_t* outPayload,
    uint16_t outCapacity
) const {
    LatestSlotLoadResult result{};
    if (!isConfigValid_()) {
        result.status = SlotLoadStatus::IO_ERROR;
        return result;
    }
    if (!storage_.available()) {
        result.status = SlotLoadStatus::STORAGE_UNAVAILABLE;
        return result;
    }

    std::array<bool, MAX_SLOT_COUNT> excluded{};
    excluded.fill(false);

    for (uint16_t attempt = 0; attempt < config_.slotCount; ++attempt) {
        bool found = false;
        uint16_t candidate_index = 0;
        uint32_t candidate_counter = 0;

        for (uint16_t i = 0; i < config_.slotCount; ++i) {
            if (excluded[i]) continue;

            SlotHeader header{};
            const SlotLoadStatus status = readSlotHeader_(i, header);
            if (status != SlotLoadStatus::OK) continue;

            if (!found || isCounterNewer_(header.saveCounter, candidate_counter)) {
                found = true;
                candidate_index = i;
                candidate_counter = header.saveCounter;
            }
        }

        if (!found) {
            result.status = SlotLoadStatus::EMPTY;
            return result;
        }

        SlotMetadata meta{};
        const SlotLoadStatus load_status = loadSlot(
            candidate_index,
            outPayload,
            outCapacity,
            &meta
        );
        if (load_status == SlotLoadStatus::OK) {
            result.status = SlotLoadStatus::OK;
            result.slotIndex = candidate_index;
            result.metadata = meta;
            return result;
        }

        excluded[candidate_index] = true;
    }

    result.status = SlotLoadStatus::EMPTY;
    return result;
}

FLASHMEM bool PersistenceSlotFileStore::eraseSlot(uint16_t slotIndex) {
    return eraseSlotStatus(slotIndex) == PersistenceWriteStatus::OK;
}

FLASHMEM PersistenceWriteStatus PersistenceSlotFileStore::eraseSlotStatus(uint16_t slotIndex) {
    if (!isConfigValid_()) return PersistenceWriteStatus::INVALID_CONFIG;
    if (!storage_.available()) return PersistenceWriteStatus::STORAGE_UNAVAILABLE;
    if (!isSlotIndexValid_(slotIndex)) return PersistenceWriteStatus::OUT_OF_RANGE;

    if (!storage_.erase(slotHeaderAddress(slotIndex), slotSize_())) {
        return PersistenceWriteStatus::ERASE_FAILED;
    }

    return storage_.commit() ? PersistenceWriteStatus::OK : PersistenceWriteStatus::COMMIT_FAILED;
}

FLASHMEM uint32_t PersistenceSlotFileStore::slotHeaderAddress(uint16_t slotIndex) const {
    return static_cast<uint32_t>(
        sizeof(FileHeader) + static_cast<size_t>(slotIndex) * slotSize_()
    );
}

FLASHMEM uint32_t PersistenceSlotFileStore::slotPayloadAddress(uint16_t slotIndex) const {
    return slotHeaderAddress(slotIndex) + static_cast<uint32_t>(sizeof(SlotHeader));
}

FLASHMEM uint16_t PersistenceSlotFileStore::slotPayloadSize() const {
    return config_.slotPayloadSize;
}

FLASHMEM uint16_t PersistenceSlotFileStore::slotCount() const {
    return config_.slotCount;
}

FLASHMEM bool PersistenceSlotFileStore::isConfigValid_() const {
    if (config_.fileMagic == 0) return false;
    if (config_.slotCount == 0 || config_.slotCount > MAX_SLOT_COUNT) return false;
    if (config_.slotPayloadSize == 0) return false;

    const size_t total_size = sizeof(FileHeader) + static_cast<size_t>(config_.slotCount) * slotSize_();
    return total_size <= storage_.capacity();
}

FLASHMEM bool PersistenceSlotFileStore::isSlotIndexValid_(uint16_t slotIndex) const {
    return slotIndex < config_.slotCount;
}

FLASHMEM size_t PersistenceSlotFileStore::slotSize_() const {
    return sizeof(SlotHeader) + static_cast<size_t>(config_.slotPayloadSize);
}

FLASHMEM bool PersistenceSlotFileStore::isHeaderValid_(const FileHeader& header) const {
    if (header.magic != config_.fileMagic) return false;
    if (header.formatVersion != FILE_FORMAT_VERSION) return false;
    if (header.domainVersion != config_.domainVersion) return false;
    if (header.slotCount != config_.slotCount) return false;
    if (header.slotPayloadSize != config_.slotPayloadSize) return false;

    FileHeader expected = header;
    expected.layoutCrc32 = 0;
    const uint32_t computed = crc32_(reinterpret_cast<const uint8_t*>(&expected), sizeof(expected));
    return computed == header.layoutCrc32;
}

FLASHMEM PersistenceSlotFileStore::FileHeader PersistenceSlotFileStore::buildHeader_() const {
    FileHeader header{};
    header.magic = config_.fileMagic;
    header.formatVersion = FILE_FORMAT_VERSION;
    header.domainVersion = config_.domainVersion;
    header.slotCount = config_.slotCount;
    header.slotPayloadSize = config_.slotPayloadSize;
    header.layoutCrc32 = 0;
    header.layoutCrc32 = crc32_(reinterpret_cast<const uint8_t*>(&header), sizeof(header));
    return header;
}

FLASHMEM SlotLoadStatus PersistenceSlotFileStore::readSlotHeader_(
    uint16_t slotIndex,
    SlotHeader& header
) const {
    if (!readBytes_(slotHeaderAddress(slotIndex), reinterpret_cast<uint8_t*>(&header), sizeof(header))) {
        return SlotLoadStatus::IO_ERROR;
    }

    if (isAllFF_(reinterpret_cast<const uint8_t*>(&header), sizeof(header))) {
        return SlotLoadStatus::EMPTY;
    }

    if (header.state != SLOT_STATE_VALID) {
        return SlotLoadStatus::EMPTY;
    }

    if (header.magic != SLOT_HEADER_MAGIC) {
        return SlotLoadStatus::HEADER_MISMATCH;
    }
    if (header.formatVersion != FILE_FORMAT_VERSION) {
        return SlotLoadStatus::HEADER_MISMATCH;
    }
    if (header.payloadSize > config_.slotPayloadSize) {
        return SlotLoadStatus::HEADER_MISMATCH;
    }

    return SlotLoadStatus::OK;
}

FLASHMEM bool PersistenceSlotFileStore::readBytes_(uint32_t address,
                                                   uint8_t* data,
                                                   size_t size) const {
    return storage_.read(address, data, size) == size;
}

FLASHMEM bool PersistenceSlotFileStore::writeBytes_(uint32_t address,
                                                    const uint8_t* data,
                                                    size_t size) {
    return storage_.write(address, data, size) == size;
}

FLASHMEM bool PersistenceSlotFileStore::isAllFF_(const uint8_t* data, size_t size) {
    for (size_t i = 0; i < size; ++i) {
        if (data[i] != 0xFF) return false;
    }
    return true;
}

FLASHMEM bool PersistenceSlotFileStore::isCounterNewer_(uint32_t a, uint32_t b) {
    return static_cast<int32_t>(a - b) > 0;
}

FLASHMEM uint32_t PersistenceSlotFileStore::crc32_(const uint8_t* data, size_t size) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < size; ++i) {
        crc ^= static_cast<uint32_t>(data[i]);
        for (uint8_t bit = 0; bit < 8; ++bit) {
            const uint32_t mask = static_cast<uint32_t>(-(static_cast<int32_t>(crc & 1u)));
            crc = (crc >> 1u) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

}  // namespace core::persistence
