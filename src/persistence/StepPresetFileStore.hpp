#pragma once

#include <cstdint>

#include <oc/interface/IFileSystem.hpp>
#include <oc/type/Result.hpp>

#include "persistence/ProductFileService.hpp"
#include "state/project/ProjectState.hpp"
#include "state/sequencer/SequencerGraphAssetCodec.hpp"

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

struct StepPresetFileListEntry {
    char id[core::state::project::ProjectMetadata::ID_SIZE] = {};
    char semanticName[
        core::state::sequencer::SequencerStepGraphPreset::SEMANTIC_NAME_SIZE
    ] = {};
    uint32_t sizeBytes = 0;
    bool metadataReadable = false;
    bool metadataDefaulted = false;
};

struct StepPresetFileListResult {
    uint8_t count = 0;
    bool truncated = false;
    bool hasPrevious = false;
    bool hasNext = false;
    uint16_t totalCount = 0;
};

enum class StepPresetFilePageDirection : uint8_t {
    FORWARD = 0,
    BACKWARD,
};

/**
 * Product-file store for transferable step presets.
 *
 * Step presets are user assets, so they live under the product filesystem
 * rather than in fixed internal storage slots. Each file contains a complete
 * encoded step payload, including any graph carried by the edited step.
 */
class StepPresetFileStore {
public:
    static constexpr const char* DIRECTORY = "library/step-presets";
    static constexpr const char* EXTENSION = ".mssp";
    static constexpr uint8_t EXTENSION_LENGTH = sizeof(".mssp") - 1U;
    static constexpr uint32_t MAX_FILE_SIZE =
        state::sequencer::STEP_GRAPH_PRESET_MAX_ENCODED_SIZE;
    static constexpr uint32_t WRITE_CHUNK_SIZE = 1024;

    explicit StepPresetFileStore(ProductFileService& files);

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

    /**
     * Removes one preset and every private atomic-write sidecar associated
     * with its exact technical id. The current file is removed last so a
     * failed cleanup cannot make the preset disappear or later resurrect it.
     */
    oc::type::Result<void> remove(const char* presetId);

    oc::type::Result<StepPresetFileListResult> list(
        StepPresetFileListEntry* entries,
        uint8_t capacity
    );

    /**
     * Returns a globally alphabetic cursor page without retaining the whole
     * directory. FORWARD keeps the first `capacity` ids after anchorExclusive;
     * BACKWARD keeps the last `capacity` ids before it.
     */
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
    struct PresetPaths {
        char directory[oc::interface::FILESYSTEM_MAX_PATH_LENGTH + 1] = {};
        char current[oc::interface::FILESYSTEM_MAX_PATH_LENGTH + 1] = {};
        char backup[oc::interface::FILESYSTEM_MAX_PATH_LENGTH + 1] = {};
        char tmp[oc::interface::FILESYSTEM_MAX_PATH_LENGTH + 1] = {};
    };

    static bool buildPaths_(const char* presetId, PresetPaths& out);
    static bool listVisitor_(const oc::interface::DirectoryEntry& entry, void* context);
    bool buildListEntry_(
        const char* presetId,
        uint32_t sizeBytes,
        StepPresetFileListEntry& out
    );

    ProductFileService& files_;
};

}  // namespace core::persistence
