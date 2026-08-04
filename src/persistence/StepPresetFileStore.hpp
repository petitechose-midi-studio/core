#pragma once

#include <cstdint>

#include <oc/interface/IFileSystem.hpp>
#include <oc/type/Result.hpp>

#include "persistence/ProductAssetFileStore.hpp"
#include "persistence/SequencerGraphAssetCodec.hpp"

namespace core::persistence {

struct StepPresetFileSaveResult {
    uint32_t bytesWritten = 0;
    char presetPath[oc::interface::FILESYSTEM_MAX_PATH_LENGTH + 1] = {};
    char presetId[core::state::project::ProjectMetadata::ID_SIZE] = {};
};

struct StepPresetFileLoadResult {
    uint32_t bytesRead = 0;
    char presetPath[oc::interface::FILESYSTEM_MAX_PATH_LENGTH + 1] = {};
    char presetId[core::state::project::ProjectMetadata::ID_SIZE] = {};
};

using StepPresetFileListEntry = ProductAssetFileListEntry;
using StepPresetFileListResult = ProductAssetFileListResult;
using StepPresetFilePageDirection = ProductAssetFilePageDirection;

static_assert(
    ProductAssetFileListEntry::SEMANTIC_NAME_SIZE ==
        core::state::sequencer::SequencerStepGraphPreset::SEMANTIC_NAME_SIZE
);

/**
 * Thin Step-specific adapter over the common product asset store.
 *
 * Step codec metadata remains owned by the Sequencer graph asset codec;
 * filesystem
 * paths, pagination, atomic replacement and recovery are shared with other
 * user libraries.
 */
class StepPresetFileStore {
public:
    static constexpr const char* DIRECTORY = "library/step-presets";
    static constexpr const char* EXTENSION = ".mssp";
    static constexpr uint8_t EXTENSION_LENGTH = sizeof(".mssp") - 1U;
    static constexpr uint32_t MAX_FILE_SIZE =
        sequencer_graph_asset_codec::MAX_ENCODED_SIZE;
    static constexpr uint32_t WRITE_CHUNK_SIZE = 1024;

    StepPresetFileStore(
        ProductFileService& files,
        ProductDirectoryCatalog& catalog
    );

    oc::type::Result<StepPresetFileSaveResult> save(
        const char* presetId,
        const uint8_t* payload,
        uint16_t payloadSize
    );

    oc::type::Result<StepPresetFileLoadResult> load(
        const char* presetId,
        uint8_t* outPayload,
        uint16_t outCapacity,
        uint16_t& outSize
    );

    oc::type::Result<void> remove(const char* presetId);

    oc::type::Result<StepPresetFileListResult> list(
        StepPresetFileListEntry* entries,
        uint8_t capacity
    );

    oc::type::Result<StepPresetFileListResult> listPage(
        StepPresetFileListEntry* entries,
        uint8_t capacity,
        const char* anchorExclusive,
        StepPresetFilePageDirection direction
    );

    oc::type::Result<void> nextPresetId(char* out, size_t outSize);
    oc::type::Result<bool> exists(const char* presetId);

    static bool validPresetId(const char* presetId);

private:
    static bool readMetadata_(
        ProductFileService& files,
        const char* currentPath,
        const char* expectedPresetId,
        uint32_t fileSize,
        char* outSemanticName,
        size_t outSemanticNameSize
    );

    ProductAssetFileStore store_;
};

}  // namespace core::persistence
