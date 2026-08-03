#include "persistence/StepPresetFileStore.hpp"

#include <algorithm>
#include <cstring>

#include <config/PlatformCompat.hpp>

namespace core::persistence {

FLASHMEM StepPresetFileStore::StepPresetFileStore(
    ProductFileService& files,
    ProductDirectoryCatalog& catalog
)
    : store_(
          files,
          catalog,
          {
              .directory = DIRECTORY,
              .extension = EXTENSION,
              .generatedIdPrefix = "step-preset-",
              .maxFileSize = MAX_FILE_SIZE,
              .writeChunkSize = WRITE_CHUNK_SIZE,
          },
          readMetadata_,
          ProductPersistenceJobOwner::STEP_PRESET_CATALOG
      ) {}

FLASHMEM bool StepPresetFileStore::validPresetId(const char* presetId) {
    return ProductAssetFileStore::validAssetId(presetId);
}

FLASHMEM bool StepPresetFileStore::readMetadata_(
    ProductFileService& files,
    const char* currentPath,
    const char* expectedPresetId,
    uint32_t fileSize,
    char* outSemanticName,
    size_t outSemanticNameSize
) {
    if (currentPath == nullptr || expectedPresetId == nullptr ||
        outSemanticName == nullptr || outSemanticNameSize == 0U ||
        fileSize == 0U || fileSize > MAX_FILE_SIZE) {
        return false;
    }
    uint8_t header[sequencer_graph_asset_codec::HEADER_SIZE] = {};
    const size_t readSize = std::min<size_t>(fileSize, sizeof(header));
    const auto read = files.read(
        currentPath,
        0,
        header,
        readSize
    );
    if (!read || read.value() != readSize) return false;

    sequencer_graph_asset_codec::MetadataView metadata{};
    state::sequencer::SequencerGraphAssetReport report{};
    if (!sequencer_graph_asset_codec::decodeMetadata(
            header,
            static_cast<uint16_t>(readSize),
            metadata,
            &report
        ) ||
        std::strcmp(metadata.technicalId, expectedPresetId) != 0 ||
        !state::sequencer::validStepGraphPresetSemanticName(
            metadata.semanticName
        ) ||
        std::strlen(metadata.semanticName) >= outSemanticNameSize) {
        return false;
    }
    std::strncpy(
        outSemanticName,
        metadata.semanticName,
        outSemanticNameSize - 1U
    );
    outSemanticName[outSemanticNameSize - 1U] = '\0';
    return true;
}

FLASHMEM oc::type::Result<StepPresetFileSaveResult>
StepPresetFileStore::save(
    const char* presetId,
    const uint8_t* payload,
    uint16_t payloadSize
) {
    const auto saved = store_.save(presetId, payload, payloadSize);
    if (!saved) {
        return oc::type::Result<StepPresetFileSaveResult>::err(
            saved.error()
        );
    }
    StepPresetFileSaveResult result{};
    result.bytesWritten = saved.value().bytes;
    std::strncpy(
        result.presetPath,
        saved.value().path,
        sizeof(result.presetPath) - 1U
    );
    std::strncpy(
        result.presetId,
        saved.value().id,
        sizeof(result.presetId) - 1U
    );
    return oc::type::Result<StepPresetFileSaveResult>::ok(result);
}

FLASHMEM oc::type::Result<StepPresetFileLoadResult>
StepPresetFileStore::load(
    const char* presetId,
    uint8_t* outPayload,
    uint16_t outCapacity,
    uint16_t& outSize
) {
    outSize = 0U;
    uint32_t loadedSize = 0U;
    const auto loaded = store_.load(
        presetId,
        outPayload,
        outCapacity,
        loadedSize
    );
    if (!loaded) {
        return oc::type::Result<StepPresetFileLoadResult>::err(
            loaded.error()
        );
    }
    if (loadedSize > UINT16_MAX) {
        return oc::type::Result<StepPresetFileLoadResult>::err(
            {
                oc::type::ErrorCode::RESOURCE_EXHAUSTED,
                "step preset exceeds codec size",
            }
        );
    }
    outSize = static_cast<uint16_t>(loadedSize);
    StepPresetFileLoadResult result{};
    result.bytesRead = loaded.value().bytes;
    std::strncpy(
        result.presetPath,
        loaded.value().path,
        sizeof(result.presetPath) - 1U
    );
    std::strncpy(
        result.presetId,
        loaded.value().id,
        sizeof(result.presetId) - 1U
    );
    return oc::type::Result<StepPresetFileLoadResult>::ok(result);
}

FLASHMEM oc::type::Result<void> StepPresetFileStore::remove(
    const char* presetId
) {
    return store_.remove(presetId);
}

FLASHMEM oc::type::Result<StepPresetFileListResult>
StepPresetFileStore::list(
    StepPresetFileListEntry* entries,
    uint8_t capacity
) {
    return store_.list(entries, capacity);
}

FLASHMEM oc::type::Result<StepPresetFileListResult>
StepPresetFileStore::listPage(
    StepPresetFileListEntry* entries,
    uint8_t capacity,
    const char* anchorExclusive,
    StepPresetFilePageDirection direction
) {
    return store_.listPage(
        entries,
        capacity,
        anchorExclusive,
        direction
    );
}

FLASHMEM oc::type::Result<void> StepPresetFileStore::nextPresetId(
    char* out,
    size_t outSize
) {
    return store_.nextAssetId(out, outSize);
}

FLASHMEM oc::type::Result<bool> StepPresetFileStore::exists(
    const char* presetId
) {
    return store_.exists(presetId);
}

}  // namespace core::persistence
