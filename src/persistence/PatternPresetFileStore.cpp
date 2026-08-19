#include "persistence/PatternPresetFileStore.hpp"

#include <cstring>

#include <config/PlatformCompat.hpp>

namespace core::persistence {

FLASHMEM PatternPresetFileStore::PatternPresetFileStore(
    ProductFileService& files,
    ProductDirectoryCatalog& catalog
) : store_(
        files,
        catalog,
        {
            .directory = DIRECTORY,
            .extension = EXTENSION,
            .generatedIdPrefix = "pattern-preset-",
            .maxFileSize = MAX_FILE_SIZE,
            .writeChunkSize = WRITE_CHUNK_SIZE,
        },
        readMetadata_,
        ProductPersistenceJobOwner::PATTERN_PRESET_CATALOG
    ) {}

FLASHMEM bool PatternPresetFileStore::validPresetId(const char* presetId) {
    return ProductAssetFileStore::validAssetId(presetId);
}

FLASHMEM bool PatternPresetFileStore::readMetadata_(
    ProductFileService& files,
    const char* currentPath,
    const char* expectedPresetId,
    uint32_t fileSize,
    char* outSemanticName,
    size_t outSemanticNameSize
) {
    namespace codec = sequencer_pattern_preset_codec;
    if (currentPath == nullptr || expectedPresetId == nullptr ||
        outSemanticName == nullptr || outSemanticNameSize == 0U ||
        fileSize < codec::HEADER_SIZE || fileSize > MAX_FILE_SIZE) {
        return false;
    }

    uint8_t header[codec::HEADER_SIZE]{};
    const auto read = files.read(currentPath, 0U, header, sizeof(header));
    if (!read || read.value() != sizeof(header)) return false;

    codec::MetadataView metadata{};
    if (!codec::decodeMetadata(header, sizeof(header), metadata) ||
        static_cast<uint32_t>(codec::HEADER_SIZE) +
                metadata.patternEnvelopeSize + metadata.drumRecordSize !=
            fileSize ||
        std::strcmp(metadata.metadata.technicalId, expectedPresetId) != 0 ||
        std::strlen(metadata.metadata.semanticName) >= outSemanticNameSize) {
        return false;
    }
    std::strncpy(
        outSemanticName,
        metadata.metadata.semanticName,
        outSemanticNameSize - 1U
    );
    outSemanticName[outSemanticNameSize - 1U] = '\0';
    return true;
}

FLASHMEM oc::type::Result<PatternPresetFileTransferResult>
PatternPresetFileStore::save(
    const char* presetId,
    const uint8_t* payload,
    uint16_t payloadSize
) {
    return store_.save(presetId, payload, payloadSize);
}

FLASHMEM oc::type::Result<PatternPresetFileTransferResult>
PatternPresetFileStore::load(
    const char* presetId,
    uint8_t* outPayload,
    uint16_t outCapacity,
    uint16_t& outSize
) {
    outSize = 0U;
    uint32_t loadedSize = 0U;
    auto loaded = store_.load(
        presetId,
        outPayload,
        outCapacity,
        loadedSize
    );
    if (!loaded) {
        return oc::type::Result<PatternPresetFileTransferResult>::err(
            loaded.error()
        );
    }
    if (loadedSize > UINT16_MAX) {
        return oc::type::Result<PatternPresetFileTransferResult>::err({
            oc::type::ErrorCode::RESOURCE_EXHAUSTED,
            "pattern preset exceeds codec size",
        });
    }
    outSize = static_cast<uint16_t>(loadedSize);
    return loaded;
}

FLASHMEM oc::type::Result<void> PatternPresetFileStore::remove(
    const char* presetId
) {
    return store_.remove(presetId);
}

FLASHMEM oc::type::Result<PatternPresetFileListResult>
PatternPresetFileStore::listPage(
    PatternPresetFileListEntry* entries,
    uint8_t capacity,
    const char* anchorExclusive,
    PatternPresetFilePageDirection direction
) {
    return store_.listPage(entries, capacity, anchorExclusive, direction);
}

FLASHMEM oc::type::Result<void> PatternPresetFileStore::nextPresetId(
    char* out,
    size_t outSize
) {
    return store_.nextAssetId(out, outSize);
}

FLASHMEM oc::type::Result<bool> PatternPresetFileStore::exists(
    const char* presetId
) {
    return store_.exists(presetId);
}

}  // namespace core::persistence
