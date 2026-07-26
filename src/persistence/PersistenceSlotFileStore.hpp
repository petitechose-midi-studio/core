#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

#include <oc/interface/IStorage.hpp>

namespace core::persistence {

/**
 * Generic fixed-slot storage journal.
 *
 * Each domain supplies its magic, version, slot count, and payload size. The
 * store validates headers and CRCs, writes slots through a WRITING->VALID state
 * transition, and can recover the latest valid slot by save counter.
 */
enum class SlotLoadStatus : uint8_t {
    OK = 0,
    EMPTY,
    OUT_OF_RANGE,
    STORAGE_UNAVAILABLE,
    HEADER_MISMATCH,
    CAPACITY_TOO_SMALL,
    CRC_MISMATCH,
    IO_ERROR,
};

enum class PersistenceWriteStatus : uint8_t {
    OK = 0,
    INVALID_CONFIG,
    STORAGE_UNAVAILABLE,
    OUT_OF_RANGE,
    PAYLOAD_TOO_LARGE,
    IO_ERROR,
    ERASE_FAILED,
    COMMIT_FAILED,
};

inline const char* slotLoadStatusLabel(SlotLoadStatus status) {
    switch (status) {
        case SlotLoadStatus::OK: return "OK";
        case SlotLoadStatus::EMPTY: return "EMPTY";
        case SlotLoadStatus::OUT_OF_RANGE: return "OUT_OF_RANGE";
        case SlotLoadStatus::STORAGE_UNAVAILABLE: return "STORAGE_UNAVAILABLE";
        case SlotLoadStatus::HEADER_MISMATCH: return "HEADER_MISMATCH";
        case SlotLoadStatus::CAPACITY_TOO_SMALL: return "CAPACITY_TOO_SMALL";
        case SlotLoadStatus::CRC_MISMATCH: return "CRC_MISMATCH";
        case SlotLoadStatus::IO_ERROR: return "IO_ERROR";
        default: return "UNKNOWN";
    }
}

inline const char* persistenceWriteStatusLabel(PersistenceWriteStatus status) {
    switch (status) {
        case PersistenceWriteStatus::OK: return "OK";
        case PersistenceWriteStatus::INVALID_CONFIG: return "INVALID_CONFIG";
        case PersistenceWriteStatus::STORAGE_UNAVAILABLE: return "STORAGE_UNAVAILABLE";
        case PersistenceWriteStatus::OUT_OF_RANGE: return "OUT_OF_RANGE";
        case PersistenceWriteStatus::PAYLOAD_TOO_LARGE: return "PAYLOAD_TOO_LARGE";
        case PersistenceWriteStatus::IO_ERROR: return "IO_ERROR";
        case PersistenceWriteStatus::ERASE_FAILED: return "ERASE_FAILED";
        case PersistenceWriteStatus::COMMIT_FAILED: return "COMMIT_FAILED";
        default: return "UNKNOWN";
    }
}

struct SlotMetadata {
    uint32_t payloadSize = 0;
    uint32_t saveCounter = 0;
};

struct SlotFileStoreConfig {
    uint32_t fileMagic = 0;
    uint8_t domainVersion = 1;
    uint16_t slotCount = 0;
    uint32_t slotPayloadSize = 0;
    uint32_t baseAddress = 0;
};

enum class SlotFileLayoutProbeStatus : uint8_t {
    EMPTY = 0,
    VALID,
    MISMATCH,
    IO,
    CAPACITY,
    IO_ERROR = IO,
    CAPACITY_TOO_SMALL = CAPACITY,
};

/**
 * A non-default config is returned only after the serialized layout CRC has
 * been verified. Its base address is the address passed to probeLayout().
 */
struct SlotFileLayoutProbeResult {
    SlotFileLayoutProbeStatus status = SlotFileLayoutProbeStatus::MISMATCH;
    SlotFileStoreConfig config{};
};

struct LatestSlotLoadResult {
    SlotLoadStatus status = SlotLoadStatus::EMPTY;
    uint16_t slotIndex = 0;
    SlotMetadata metadata{};
};

/**
 * Fixed-size slot file abstraction over IStorage.
 *
 * Payload serialization belongs to domain-specific persistence classes; this
 * class owns file layout validation, erase/commit sequencing, CRC checking, and
 * latest-slot fallback.
 */
class PersistenceSlotFileStore {
public:
    static constexpr uint8_t FILE_FORMAT_VERSION = 2;
    static constexpr size_t FILE_HEADER_SIZE = 24;
    static constexpr size_t SLOT_HEADER_SIZE = 20;
    static constexpr uint16_t MAX_SLOT_COUNT = 64;

    static constexpr size_t requiredCapacity(uint16_t slotCount, uint32_t slotPayloadSize) {
        const size_t payload_size = static_cast<size_t>(slotPayloadSize);
        if (payload_size > std::numeric_limits<size_t>::max() - SLOT_HEADER_SIZE) {
            return std::numeric_limits<size_t>::max();
        }
        const size_t slot_size = SLOT_HEADER_SIZE + payload_size;
        if (slotCount > 0 &&
            slot_size >
                (std::numeric_limits<size_t>::max() - FILE_HEADER_SIZE) /
                    static_cast<size_t>(slotCount)) {
            return std::numeric_limits<size_t>::max();
        }
        return FILE_HEADER_SIZE + static_cast<size_t>(slotCount) * slot_size;
    }

    explicit PersistenceSlotFileStore(oc::interface::IStorage& storage,
                                      const SlotFileStoreConfig& config);

    bool init(bool formatIfInvalid = true);
    static SlotFileLayoutProbeResult probeLayout(oc::interface::IStorage& storage,
                                                 uint32_t baseAddress);
    SlotFileLayoutProbeResult probe() const;
    bool format();
    PersistenceWriteStatus formatStatus();
    PersistenceWriteStatus eraseUnpublishedBankStatus();
    PersistenceWriteStatus publishHeaderStatus();

    bool saveSlot(uint16_t slotIndex,
                  const uint8_t* payload,
                  uint32_t payloadSize,
                  uint32_t saveCounter);
    PersistenceWriteStatus saveSlotStatus(uint16_t slotIndex,
                                          const uint8_t* payload,
                                          uint32_t payloadSize,
                                          uint32_t saveCounter);

    SlotLoadStatus loadSlot(uint16_t slotIndex,
                            uint8_t* outPayload,
                            uint32_t outCapacity,
                            SlotMetadata* outMeta = nullptr) const;
    SlotLoadStatus inspectSlot(uint16_t slotIndex, SlotMetadata* outMeta = nullptr) const;
    LatestSlotLoadResult loadLatest(uint8_t* outPayload, uint32_t outCapacity) const;

    bool eraseSlot(uint16_t slotIndex);
    PersistenceWriteStatus eraseSlotStatus(uint16_t slotIndex);

    uint32_t slotHeaderAddress(uint16_t slotIndex) const;
    uint32_t slotPayloadAddress(uint16_t slotIndex) const;
    uint32_t slotPayloadSize() const;
    uint16_t slotCount() const;
    uint32_t baseAddress() const;
    size_t bankCapacity() const;

private:
    struct FileHeader {
        uint32_t magic = 0;
        uint8_t formatVersion = 0;
        uint8_t domainVersion = 0;
        uint16_t slotCount = 0;
        uint32_t slotPayloadSize = 0;
        uint32_t layoutCrc32 = 0;
    };

    struct SlotHeader {
        uint32_t magic = 0;
        uint8_t formatVersion = 0;
        uint8_t state = 0;
        uint32_t payloadSize = 0;
        uint32_t saveCounter = 0;
        uint32_t payloadCrc32 = 0;
    };

    static constexpr uint32_t SLOT_HEADER_MAGIC = 0x53534C54;  // "SSLT"
    static constexpr uint8_t SLOT_STATE_VALID = 0x3C;
    static constexpr uint8_t SLOT_STATE_WRITING = 0x7F;

    bool isConfigValid_() const;
    bool isConfigLayoutValid_() const;
    bool hasStorageCapacity_() const;
    bool isSlotIndexValid_(uint16_t slotIndex) const;
    size_t slotSize_() const;
    bool isHeaderValid_(const FileHeader& header) const;
    FileHeader buildHeader_() const;
    SlotLoadStatus readSlotHeader_(uint16_t slotIndex, SlotHeader& header) const;
    bool readBytes_(uint32_t address, uint8_t* data, size_t size) const;
    bool writeBytes_(uint32_t address, const uint8_t* data, size_t size);

    static bool isAllFF_(const uint8_t* data, size_t size);
    static bool isCounterNewer_(uint32_t a, uint32_t b);
    static uint32_t crc32_(const uint8_t* data, size_t size);
    static bool encodeFileHeader_(const FileHeader& header, uint8_t* out, size_t size);
    static bool decodeFileHeader_(const uint8_t* data, size_t size, FileHeader& out);
    static bool encodeSlotHeader_(const SlotHeader& header, uint8_t* out, size_t size);
    static bool decodeSlotHeader_(const uint8_t* data, size_t size, SlotHeader& out);
    static uint32_t fileHeaderLayoutCrc_(const FileHeader& header);

    oc::interface::IStorage& storage_;
    SlotFileStoreConfig config_;
};

}  // namespace core::persistence
