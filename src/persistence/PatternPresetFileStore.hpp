#pragma once

#include <cstdint>

#include <oc/type/Result.hpp>

#include "persistence/ProductAssetFileStore.hpp"
#include "persistence/SequencerPatternPresetCodec.hpp"

namespace core::persistence {

using PatternPresetFileTransferResult = ProductAssetFileTransferResult;
using PatternPresetFileListEntry = ProductAssetFileListEntry;
using PatternPresetFileListResult = ProductAssetFileListResult;
using PatternPresetFilePageDirection = ProductAssetFilePageDirection;

static_assert(
    core::state::project::ProjectMetadata::ID_SIZE ==
        core::state::sequencer::SEQUENCER_PRESET_TECHNICAL_ID_SIZE
);
static_assert(
    ProductAssetFileListEntry::SEMANTIC_NAME_SIZE ==
        core::state::sequencer::SEQUENCER_PRESET_SEMANTIC_NAME_SIZE
);

/** Thin Pattern-specific adapter over the common atomic asset store. */
class PatternPresetFileStore {
public:
    static constexpr const char* DIRECTORY = "library/pattern-presets";
    static constexpr const char* EXTENSION = ".mspp";
    static constexpr uint32_t MAX_FILE_SIZE =
        sequencer_pattern_preset_codec::MAX_ENCODED_SIZE;
    static constexpr uint32_t WRITE_CHUNK_SIZE = 4096U;

    PatternPresetFileStore(
        ProductFileService& files,
        ProductDirectoryCatalog& catalog,
        const core::state::sequencer::SequencerPatternPresetLocation&
            location = {}
    );

    oc::type::Result<PatternPresetFileTransferResult> save(
        const char* presetId,
        const uint8_t* payload,
        uint16_t payloadSize
    );

    oc::type::Result<PatternPresetFileTransferResult> load(
        const char* presetId,
        uint8_t* outPayload,
        uint16_t outCapacity,
        uint16_t& outSize
    );

    oc::type::Result<void> remove(const char* presetId);

    oc::type::Result<PatternPresetFileListResult> listPage(
        PatternPresetFileListEntry* entries,
        uint8_t capacity,
        const char* anchorExclusive,
        PatternPresetFilePageDirection direction
    );

    oc::type::Result<PatternPresetFileListResult> listFoldersPage(
        PatternPresetFileListEntry* entries,
        uint8_t capacity,
        const char* anchorExclusive,
        PatternPresetFilePageDirection direction
    );

    oc::type::Result<void> nextPresetId(char* out, size_t outSize);
    oc::type::Result<bool> exists(const char* presetId);

    oc::type::Result<void> createFolder(const char* folderName);
    oc::type::Result<void> removeEmptyFolder(const char* folderName);
    oc::type::Result<void> renameFolder(
        const char* folderName,
        const char* newFolderName
    );
    oc::type::Result<void> movePreset(
        const char* presetId,
        const core::state::sequencer::SequencerPatternPresetLocation&
            destination
    );
    oc::type::Result<void> moveFolder(
        const char* folderName,
        const core::state::sequencer::SequencerPatternPresetLocation&
            destination
    );

    static bool folderNameFromEntryId(
        const char* entryId,
        char* out,
        size_t outSize
    );

    static bool validPresetId(const char* presetId);

private:
    static constexpr char FOLDER_ENTRY_PREFIX = '@';

    static bool readMetadata_(
        ProductFileService& files,
        const char* currentPath,
        const char* expectedPresetId,
        uint32_t fileSize,
        char* outSemanticName,
        size_t outSemanticNameSize
    );

    ProductFileService& files_;
    ProductDirectoryCatalog& catalog_;
    char directory_[ProductAssetFilePaths::PATH_SIZE] = {};
    ProductAssetFileStore store_;
};

}  // namespace core::persistence
