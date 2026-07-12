#include "persistence/StepPresetFileStore.hpp"

#include <cstdio>
#include <cstring>

#include <config/PlatformCompat.hpp>

#include "persistence/AtomicProductFile.hpp"
#include "persistence/ProductFilePath.hpp"
#include "state/project/ProjectSlug.hpp"

namespace core::persistence {

namespace {

using oc::type::ErrorCode;

struct StepPresetListContext {
    StepPresetFileStore* store = nullptr;
    StepPresetFileListEntry* entries = nullptr;
    uint8_t capacity = 0;
    StepPresetFileListResult result{};
};

FLASHMEM oc::type::Result<void> invalid(const char* context) {
    return oc::type::Result<void>::err({ErrorCode::INVALID_ARGUMENT, context});
}

}  // namespace

FLASHMEM StepPresetFileStore::StepPresetFileStore(ProductFileService& files)
    : files_(files) {}

FLASHMEM bool StepPresetFileStore::validPresetId(const char* presetId) {
    return core::state::project::validProjectSlug(presetId);
}

FLASHMEM bool StepPresetFileStore::buildPaths_(const char* presetId, PresetPaths& out) {
    if (!validPresetId(presetId)) return false;
    return copyProductRelativePath(out.directory, sizeof(out.directory), DIRECTORY) &&
           formatProductRelativePath(
               out.current,
               sizeof(out.current),
               "library/step-presets/%s.mssp",
               presetId
           ) &&
           formatProductRelativePath(
               out.backup,
               sizeof(out.backup),
               "library/step-presets/%s.mssp.bak",
               presetId
           ) &&
           formatProductRelativePath(
               out.tmp,
               sizeof(out.tmp),
               "tmp/%s.mssp.tmp",
               presetId
           );
}

FLASHMEM bool StepPresetFileStore::listVisitor_(
    const oc::interface::DirectoryEntry& entry,
    void* context
) {
    auto* list = static_cast<StepPresetListContext*>(context);
    if (!list || !list->store || !list->entries) return false;
    if (entry.type != oc::interface::FileType::FILE || entry.nameTruncated) return true;

    const size_t nameLength = std::strlen(entry.name);
    if (nameLength <= EXTENSION_LENGTH) return true;
    if (std::strcmp(entry.name + nameLength - EXTENSION_LENGTH, EXTENSION) != 0) {
        return true;
    }

    char presetId[core::state::project::ProjectMetadata::ID_SIZE] = {};
    const size_t slugLength = nameLength - EXTENSION_LENGTH;
    if (slugLength >= sizeof(presetId)) return true;
    std::memcpy(presetId, entry.name, slugLength);
    presetId[slugLength] = '\0';
    if (!validPresetId(presetId)) return true;

    PresetPaths paths{};
    if (!buildPaths_(presetId, paths)) return true;

    auto info = list->store->files_.stat(paths.current);
    if (!info || info.value().type != oc::interface::FileType::FILE) return true;
    if (info.value().sizeBytes == 0 || info.value().sizeBytes > MAX_FILE_SIZE) return true;

    if (list->result.count >= list->capacity) {
        list->result.truncated = true;
        return true;
    }

    auto& target = list->entries[list->result.count++];
    std::strncpy(target.id, presetId, sizeof(target.id) - 1U);
    target.id[sizeof(target.id) - 1U] = '\0';
    target.sizeBytes = info.value().sizeBytes;
    return true;
}

FLASHMEM oc::type::Result<StepPresetFileSaveResult> StepPresetFileStore::save(
    const char* presetId,
    const uint8_t* payload,
    uint16_t payloadSize
) {
    if (payload == nullptr || payloadSize == 0 || payloadSize > MAX_FILE_SIZE) {
        return oc::type::Result<StepPresetFileSaveResult>::err(
            {ErrorCode::INVALID_ARGUMENT, "invalid step preset payload"}
        );
    }

    PresetPaths paths{};
    if (!buildPaths_(presetId, paths)) {
        return oc::type::Result<StepPresetFileSaveResult>::err(
            {ErrorCode::INVALID_ARGUMENT, "invalid step preset id"}
        );
    }

    auto saved = replaceProductFileAtomically(
        files_,
        {
            .directory = paths.directory,
            .current = paths.current,
            .backup = paths.backup,
            .tmp = paths.tmp,
        },
        payload,
        payloadSize,
        WRITE_CHUNK_SIZE
    );
    if (!saved) {
        return oc::type::Result<StepPresetFileSaveResult>::err(saved.error());
    }

    StepPresetFileSaveResult result{};
    result.bytesWritten = payloadSize;
    std::strncpy(result.presetPath, paths.current, sizeof(result.presetPath) - 1U);
    std::strncpy(result.presetId, presetId, sizeof(result.presetId) - 1U);
    return oc::type::Result<StepPresetFileSaveResult>::ok(result);
}

FLASHMEM oc::type::Result<StepPresetFileLoadResult> StepPresetFileStore::load(
    const char* presetId,
    uint8_t* outPayload,
    uint16_t outCapacity,
    uint16_t& outSize
) {
    outSize = 0;
    if (outPayload == nullptr || outCapacity == 0) {
        return oc::type::Result<StepPresetFileLoadResult>::err(
            {ErrorCode::INVALID_ARGUMENT, "invalid step preset buffer"}
        );
    }

    PresetPaths paths{};
    if (!buildPaths_(presetId, paths)) {
        return oc::type::Result<StepPresetFileLoadResult>::err(
            {ErrorCode::INVALID_ARGUMENT, "invalid step preset id"}
        );
    }

    auto recovered = recoverProductFileBackupIfCurrentMissing(
        files_,
        paths.current,
        paths.backup
    );
    if (!recovered) {
        return oc::type::Result<StepPresetFileLoadResult>::err(recovered.error());
    }

    auto info = files_.stat(paths.current);
    if (!info) {
        return oc::type::Result<StepPresetFileLoadResult>::err(info.error());
    }
    if (info.value().type != oc::interface::FileType::FILE ||
        info.value().sizeBytes == 0 ||
        info.value().sizeBytes > outCapacity ||
        info.value().sizeBytes > MAX_FILE_SIZE) {
        return oc::type::Result<StepPresetFileLoadResult>::err(
            {ErrorCode::RESOURCE_EXHAUSTED, "step preset file too large"}
        );
    }

    auto read = files_.read(paths.current, 0, outPayload, info.value().sizeBytes);
    if (!read) {
        return oc::type::Result<StepPresetFileLoadResult>::err(read.error());
    }
    if (read.value() != info.value().sizeBytes) {
        return oc::type::Result<StepPresetFileLoadResult>::err(
            {ErrorCode::STORAGE_READ_FAILED, "short step preset read"}
        );
    }

    StepPresetFileLoadResult result{};
    result.bytesRead = static_cast<uint32_t>(read.value());
    outSize = static_cast<uint16_t>(read.value());
    std::strncpy(result.presetPath, paths.current, sizeof(result.presetPath) - 1U);
    std::strncpy(result.presetId, presetId, sizeof(result.presetId) - 1U);
    return oc::type::Result<StepPresetFileLoadResult>::ok(result);
}

FLASHMEM oc::type::Result<StepPresetFileListResult> StepPresetFileStore::list(
    StepPresetFileListEntry* entries,
    uint8_t capacity
) {
    if (entries == nullptr && capacity > 0) {
        return oc::type::Result<StepPresetFileListResult>::err(
            {ErrorCode::INVALID_ARGUMENT, "invalid step preset list buffer"}
        );
    }
    if (capacity == 0) {
        return oc::type::Result<StepPresetFileListResult>::ok(StepPresetFileListResult{});
    }

    auto ensureDir = files_.createDirectory(DIRECTORY);
    if (!ensureDir) {
        return oc::type::Result<StepPresetFileListResult>::err(ensureDir.error());
    }

    for (uint8_t i = 0; i < capacity; ++i) {
        entries[i] = StepPresetFileListEntry{};
    }

    StepPresetListContext context{this, entries, capacity, StepPresetFileListResult{}};
    auto listed = files_.list(DIRECTORY, listVisitor_, &context);
    if (!listed) {
        return oc::type::Result<StepPresetFileListResult>::err(listed.error());
    }
    for (uint8_t i = 1; i < context.result.count; ++i) {
        StepPresetFileListEntry current = entries[i];
        uint8_t insert = i;
        while (insert > 0 && std::strcmp(entries[insert - 1U].id, current.id) > 0) {
            entries[insert] = entries[insert - 1U];
            --insert;
        }
        entries[insert] = current;
    }
    return oc::type::Result<StepPresetFileListResult>::ok(context.result);
}

FLASHMEM oc::type::Result<void> StepPresetFileStore::nextPresetId(
    char* out,
    size_t outSize
) {
    if (out == nullptr || outSize == 0) {
        return invalid("invalid next step preset id buffer");
    }

    auto ensureDir = files_.createDirectory(DIRECTORY);
    if (!ensureDir) return ensureDir;

    for (uint16_t index = 1; index <= 999; ++index) {
        char candidate[core::state::project::ProjectMetadata::ID_SIZE] = {};
        const int written = std::snprintf(
            candidate,
            sizeof(candidate),
            "step-preset-%03u",
            static_cast<unsigned>(index)
        );
        if (written <= 0 || static_cast<size_t>(written) >= sizeof(candidate)) {
            return invalid("step preset id buffer too small");
        }

        PresetPaths paths{};
        if (!buildPaths_(candidate, paths)) {
            return invalid("generated invalid step preset id");
        }

        auto info = files_.stat(paths.current);
        if (!info && info.error().code == ErrorCode::RESOURCE_NOT_FOUND) {
            if (std::strlen(candidate) >= outSize) {
                return invalid("next step preset id output too small");
            }
            std::strncpy(out, candidate, outSize - 1U);
            out[outSize - 1U] = '\0';
            return oc::type::Result<void>::ok();
        }
        if (!info) {
            return oc::type::Result<void>::err(info.error());
        }
    }

    return oc::type::Result<void>::err(
        {ErrorCode::RESOURCE_EXHAUSTED, "step preset id space exhausted"}
    );
}

}  // namespace core::persistence
