#include "persistence/ChordPresetFileStore.hpp"

#include <array>
#include <cstring>

#include <config/PlatformCompat.hpp>

namespace core::persistence {
namespace {

using ChordPreset =
    oc::note::sequencer::StepSequencerChordPreset;
using CodecReport =
    oc::note::sequencer::StepSequencerChordPresetCodecReport;

}  // namespace

FLASHMEM ChordPresetFileStore::ChordPresetFileStore(
    ProductFileService& files
) : store_(
        files,
        {
            .directory = DIRECTORY,
            .extension = EXTENSION,
            .generatedIdPrefix = "chord-preset-",
            .maxFileSize = MAX_FILE_SIZE,
            .writeChunkSize = WRITE_CHUNK_SIZE,
        },
        readMetadata_
    ) {}

FLASHMEM bool ChordPresetFileStore::validPresetId(const char* presetId) {
    return ProductAssetFileStore::validAssetId(presetId);
}

FLASHMEM bool ChordPresetFileStore::readMetadata_(
    ProductFileService& files,
    const char* currentPath,
    const char* expectedPresetId,
    uint32_t fileSize,
    char* outSemanticName,
    size_t outSemanticNameSize
) {
    if (currentPath == nullptr || expectedPresetId == nullptr ||
        outSemanticName == nullptr || outSemanticNameSize == 0U ||
        fileSize != MAX_FILE_SIZE) {
        return false;
    }
    std::array<
        uint8_t,
        oc::note::sequencer::STEP_SEQUENCER_CHORD_PRESET_HEADER_SIZE
    > header{};
    const auto read = files.read(
        currentPath,
        0,
        header.data(),
        header.size()
    );
    if (!read || read.value() != header.size()) return false;

    oc::note::sequencer::StepSequencerChordPresetMetadataView metadata{};
    CodecReport report{};
    if (!oc::note::sequencer::decodeChordPresetMetadata(
            header.data(),
            header.size(),
            metadata,
            &report
        ) ||
        std::strcmp(metadata.technicalId, expectedPresetId) != 0 ||
        !validPresetId(metadata.technicalId) ||
        !oc::note::sequencer::validChordPresetSemanticName(
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

FLASHMEM oc::type::Result<ChordPresetFileSaveResult>
ChordPresetFileStore::save(const ChordPreset& preset) {
    if (!validPresetId(preset.technicalId)) {
        return oc::type::Result<ChordPresetFileSaveResult>::err(
            {
                oc::type::ErrorCode::INVALID_ARGUMENT,
                "invalid chord preset id",
            }
        );
    }
    std::array<uint8_t, MAX_FILE_SIZE> payload{};
    const auto encoded = oc::note::sequencer::encodeChordPreset(
        preset,
        payload.data(),
        payload.size()
    );
    if (!encoded.ok()) {
        return oc::type::Result<ChordPresetFileSaveResult>::err(
            {
                oc::type::ErrorCode::INVALID_ARGUMENT,
                "invalid chord preset payload",
            }
        );
    }
    const auto saved = store_.save(
        preset.technicalId,
        payload.data(),
        encoded.bytesWritten
    );
    if (!saved) {
        return oc::type::Result<ChordPresetFileSaveResult>::err(
            saved.error()
        );
    }

    ChordPresetFileSaveResult result{};
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
    return oc::type::Result<ChordPresetFileSaveResult>::ok(result);
}

FLASHMEM oc::type::Result<ChordPresetFileLoadResult>
ChordPresetFileStore::load(const char* presetId, ChordPreset& out) {
    out.reset();
    std::array<uint8_t, MAX_FILE_SIZE> payload{};
    uint32_t payloadSize = 0U;
    const auto loaded = store_.load(
        presetId,
        payload.data(),
        payload.size(),
        payloadSize
    );
    if (!loaded) {
        return oc::type::Result<ChordPresetFileLoadResult>::err(
            loaded.error()
        );
    }
    CodecReport report{};
    ChordPreset decoded{};
    if (payloadSize != payload.size() ||
        !oc::note::sequencer::decodeChordPreset(
            payload.data(),
            static_cast<uint16_t>(payloadSize),
            decoded,
            &report
        ) ||
        std::strcmp(decoded.technicalId, presetId) != 0 ||
        !validPresetId(decoded.technicalId)) {
        return oc::type::Result<ChordPresetFileLoadResult>::err(
            {
                oc::type::ErrorCode::STORAGE_CORRUPT,
                "invalid chord preset file",
            }
        );
    }

    out = decoded;
    ChordPresetFileLoadResult result{};
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
    return oc::type::Result<ChordPresetFileLoadResult>::ok(result);
}

FLASHMEM oc::type::Result<void> ChordPresetFileStore::remove(
    const char* presetId
) {
    return store_.remove(presetId);
}

FLASHMEM oc::type::Result<ChordPresetFileListResult>
ChordPresetFileStore::list(
    ChordPresetFileListEntry* entries,
    uint8_t capacity
) {
    return store_.list(entries, capacity);
}

FLASHMEM oc::type::Result<ChordPresetFileListResult>
ChordPresetFileStore::listPage(
    ChordPresetFileListEntry* entries,
    uint8_t capacity,
    const char* anchorExclusive,
    ChordPresetFilePageDirection direction
) {
    return store_.listPage(
        entries,
        capacity,
        anchorExclusive,
        direction
    );
}

FLASHMEM oc::type::Result<void> ChordPresetFileStore::nextPresetId(
    char* out,
    size_t outSize
) {
    return store_.nextAssetId(out, outSize);
}

FLASHMEM oc::type::Result<bool> ChordPresetFileStore::exists(
    const char* presetId
) {
    return store_.exists(presetId);
}

}  // namespace core::persistence
