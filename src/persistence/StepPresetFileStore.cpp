#include "persistence/StepPresetFileStore.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include <config/PlatformCompat.hpp>

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

FLASHMEM bool pathWrite(char* out,
                        size_t outSize,
                        const char* pattern,
                        const char* presetId) {
    if (out == nullptr || outSize == 0 || pattern == nullptr || presetId == nullptr) {
        return false;
    }
    const int written = std::snprintf(out, outSize, pattern, presetId);
    return written > 0 && static_cast<size_t>(written) < outSize;
}

FLASHMEM bool pathWriteLiteral(char* out, size_t outSize, const char* path) {
    if (out == nullptr || outSize == 0 || path == nullptr) return false;
    const size_t length = std::strlen(path);
    if (length >= outSize) return false;
    std::memcpy(out, path, length);
    out[length] = '\0';
    return true;
}

FLASHMEM bool isNotFound(const oc::type::Result<void>& result) {
    return !result && result.error().code == ErrorCode::RESOURCE_NOT_FOUND;
}

FLASHMEM oc::type::Result<void> invalid(const char* context) {
    return oc::type::Result<void>::err({ErrorCode::INVALID_ARGUMENT, context});
}

FLASHMEM oc::type::Result<void> storageWriteFailed(const char* context) {
    return oc::type::Result<void>::err({ErrorCode::STORAGE_WRITE_FAILED, context});
}

FLASHMEM oc::type::Result<void> removeIfExists(ProductFileService& files, const char* path) {
    auto removed = files.remove(path);
    if (removed || isNotFound(removed)) {
        return oc::type::Result<void>::ok();
    }
    return removed;
}

FLASHMEM oc::type::Result<void> writeTmp(ProductFileService& files,
                                         const char* tmpPath,
                                         const uint8_t* data,
                                         uint32_t size) {
    if (data == nullptr || size == 0 || size > StepPresetFileStore::MAX_FILE_SIZE) {
        return invalid("invalid step preset payload");
    }
    if (files.writeSessionActive()) {
        return oc::type::Result<void>::err({ErrorCode::INVALID_STATE, "write session active"});
    }

    auto begin = files.beginWrite(tmpPath, size);
    if (!begin) return begin;

    uint32_t offset = 0;
    while (offset < size) {
        const uint32_t next = std::min<uint32_t>(
            StepPresetFileStore::WRITE_CHUNK_SIZE,
            size - offset
        );
        auto written = files.appendWrite(data + offset, next);
        if (!written || written.value() != next) {
            files.abortWrite();
            return storageWriteFailed("step preset tmp write failed");
        }
        offset += next;
    }

    auto finish = files.finishWrite();
    if (!finish) {
        files.abortWrite();
        return finish;
    }
    return oc::type::Result<void>::ok();
}

FLASHMEM oc::type::Result<void> commitTmp(ProductFileService& files,
                                          const char* current,
                                          const char* backup,
                                          const char* tmp) {
    auto removeBackup = removeIfExists(files, backup);
    if (!removeBackup) return removeBackup;

    auto currentInfo = files.stat(current);
    if (!currentInfo && currentInfo.error().code != ErrorCode::RESOURCE_NOT_FOUND) {
        return oc::type::Result<void>::err(currentInfo.error());
    }
    const bool hadCurrent = static_cast<bool>(currentInfo);
    if (hadCurrent) {
        auto backupResult = files.rename(current, backup);
        if (!backupResult) return backupResult;
    }

    auto promote = files.rename(tmp, current);
    if (!promote) {
        if (hadCurrent) {
            (void)files.rename(backup, current);
        }
        return promote;
    }

    if (hadCurrent) {
        (void)removeIfExists(files, backup);
    }
    return oc::type::Result<void>::ok();
}

}  // namespace

FLASHMEM StepPresetFileStore::StepPresetFileStore(ProductFileService& files)
    : files_(files) {}

FLASHMEM bool StepPresetFileStore::validPresetId(const char* presetId) {
    return core::state::project::validProjectSlug(presetId);
}

FLASHMEM bool StepPresetFileStore::buildPaths_(const char* presetId, PresetPaths& out) {
    if (!validPresetId(presetId)) return false;
    return pathWriteLiteral(out.directory, sizeof(out.directory), DIRECTORY) &&
           pathWrite(out.current, sizeof(out.current), "library/step-presets/%s.mssp", presetId) &&
           pathWrite(out.backup,
                     sizeof(out.backup),
                     "library/step-presets/%s.mssp.bak",
                     presetId) &&
           pathWrite(out.tmp, sizeof(out.tmp), "tmp/%s.mssp.tmp", presetId);
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
    PresetPaths paths{};
    if (!buildPaths_(presetId, paths)) {
        return oc::type::Result<StepPresetFileSaveResult>::err(
            {ErrorCode::INVALID_ARGUMENT, "invalid step preset id"}
        );
    }

    auto ensureDir = files_.createDirectory(paths.directory);
    if (!ensureDir) {
        return oc::type::Result<StepPresetFileSaveResult>::err(ensureDir.error());
    }

    auto removeTmp = removeIfExists(files_, paths.tmp);
    if (!removeTmp) {
        return oc::type::Result<StepPresetFileSaveResult>::err(removeTmp.error());
    }

    auto write = writeTmp(files_, paths.tmp, payload, payloadSize);
    if (!write) {
        (void)removeIfExists(files_, paths.tmp);
        return oc::type::Result<StepPresetFileSaveResult>::err(write.error());
    }

    auto commit = commitTmp(files_, paths.current, paths.backup, paths.tmp);
    if (!commit) {
        (void)removeIfExists(files_, paths.tmp);
        return oc::type::Result<StepPresetFileSaveResult>::err(commit.error());
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
