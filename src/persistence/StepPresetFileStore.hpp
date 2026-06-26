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
    uint32_t sizeBytes = 0;
};

struct StepPresetFileListResult {
    uint8_t count = 0;
    bool truncated = false;
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

    oc::type::Result<StepPresetFileListResult> list(
        StepPresetFileListEntry* entries,
        uint8_t capacity
    );

    oc::type::Result<void> nextPresetId(char* out, size_t outSize);

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

    ProductFileService& files_;
};

}  // namespace core::persistence
