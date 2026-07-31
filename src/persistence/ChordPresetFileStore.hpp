#pragma once

#include <cstdint>

#include <oc/note/sequencer/StepSequencerChordPreset.hpp>
#include <oc/type/Result.hpp>

#include "persistence/ProductAssetFileStore.hpp"

namespace core::persistence {

struct ChordPresetFileSaveResult {
    uint32_t bytesWritten = 0;
    char presetPath[oc::interface::FILESYSTEM_MAX_PATH_LENGTH + 1] = {};
    char presetId[core::state::project::ProjectMetadata::ID_SIZE] = {};
};

struct ChordPresetFileLoadResult {
    uint32_t bytesRead = 0;
    char presetPath[oc::interface::FILESYSTEM_MAX_PATH_LENGTH + 1] = {};
    char presetId[core::state::project::ProjectMetadata::ID_SIZE] = {};
};

using ChordPresetFileListEntry = ProductAssetFileListEntry;
using ChordPresetFileListResult = ProductAssetFileListResult;
using ChordPresetFilePageDirection = ProductAssetFilePageDirection;

/**
 * Chord-specific codec adapter over the common product asset filesystem.
 *
 * Only fully valid v1 `.mscp` assets are written or returned. The filename id
 * must match the id embedded in the codec payload.
 */
class ChordPresetFileStore {
public:
    static constexpr const char* DIRECTORY = "library/chord-presets";
    static constexpr const char* EXTENSION = ".mscp";
    static constexpr uint32_t MAX_FILE_SIZE =
        oc::note::sequencer::STEP_SEQUENCER_CHORD_PRESET_ENCODED_SIZE;
    static constexpr uint32_t WRITE_CHUNK_SIZE = MAX_FILE_SIZE;

    explicit ChordPresetFileStore(ProductFileService& files);

    oc::type::Result<ChordPresetFileSaveResult> save(
        const oc::note::sequencer::StepSequencerChordPreset& preset
    );

    oc::type::Result<ChordPresetFileLoadResult> load(
        const char* presetId,
        oc::note::sequencer::StepSequencerChordPreset& out
    );

    oc::type::Result<void> remove(const char* presetId);

    oc::type::Result<ChordPresetFileListResult> list(
        ChordPresetFileListEntry* entries,
        uint8_t capacity
    );

    oc::type::Result<ChordPresetFileListResult> listPage(
        ChordPresetFileListEntry* entries,
        uint8_t capacity,
        const char* anchorExclusive,
        ChordPresetFilePageDirection direction
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
