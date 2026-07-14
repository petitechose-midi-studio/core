#include "persistence/StepPresetFileStore.hpp"

#include <algorithm>
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
    StepPresetFileListEntry anchor{};
    bool hasAnchor = false;
    StepPresetFilePageDirection direction = StepPresetFilePageDirection::FORWARD;
    uint16_t eligibleCount = 0;
    StepPresetFileListResult result{};
};

FLASHMEM int compareTextCaseFolded(const char* lhs, const char* rhs) {
    size_t i = 0;
    while (lhs[i] != '\0' && rhs[i] != '\0') {
        auto left = static_cast<unsigned char>(lhs[i]);
        auto right = static_cast<unsigned char>(rhs[i]);
        if (left >= 'A' && left <= 'Z') left = static_cast<unsigned char>(left + 32U);
        if (right >= 'A' && right <= 'Z') right = static_cast<unsigned char>(right + 32U);
        if (left != right) return left < right ? -1 : 1;
        ++i;
    }
    if (lhs[i] == rhs[i]) return 0;
    return lhs[i] == '\0' ? -1 : 1;
}

FLASHMEM int compareEntries(
    const StepPresetFileListEntry& lhs,
    const StepPresetFileListEntry& rhs
) {
    const int byName = compareTextCaseFolded(lhs.semanticName, rhs.semanticName);
    return byName != 0 ? byName : std::strcmp(lhs.id, rhs.id);
}

FLASHMEM void deriveSemanticName(
    const char* presetId,
    char* out,
    size_t outSize
) {
    if (out == nullptr || outSize == 0) return;
    out[0] = '\0';
    size_t written = 0;
    bool capitalize = true;
    for (size_t i = 0; presetId != nullptr && presetId[i] != '\0' &&
         written + 1U < outSize; ++i) {
        char ch = presetId[i];
        if (ch == '-' || ch == '_' || ch == '.') {
            if (written > 0 && out[written - 1U] != ' ') out[written++] = ' ';
            capitalize = true;
            continue;
        }
        if (capitalize && ch >= 'a' && ch <= 'z') {
            ch = static_cast<char>(ch - 'a' + 'A');
        }
        out[written++] = ch;
        capitalize = false;
    }
    while (written > 0 && out[written - 1U] == ' ') --written;
    out[written] = '\0';
}

FLASHMEM oc::type::Result<void> invalid(const char* context) {
    return oc::type::Result<void>::err({ErrorCode::INVALID_ARGUMENT, context});
}

FLASHMEM void insertSorted(
    StepPresetFileListEntry* entries,
    uint8_t count,
    StepPresetFileListEntry entry
) {
    uint8_t insert = count;
    while (insert > 0 && compareEntries(entries[insert - 1U], entry) > 0) {
        entries[insert] = entries[insert - 1U];
        --insert;
    }
    entries[insert] = entry;
}

FLASHMEM void sortEntries(StepPresetFileListEntry* entries, uint8_t count) {
    for (uint8_t i = 1; i < count; ++i) {
        const auto current = entries[i];
        uint8_t insert = i;
        while (insert > 0 && compareEntries(entries[insert - 1U], current) > 0) {
            entries[insert] = entries[insert - 1U];
            --insert;
        }
        entries[insert] = current;
    }
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

FLASHMEM bool StepPresetFileStore::buildListEntry_(
    const char* presetId,
    uint32_t sizeBytes,
    StepPresetFileListEntry& out
) {
    out = {};
    if (!validPresetId(presetId) || sizeBytes == 0 || sizeBytes > MAX_FILE_SIZE) {
        return false;
    }
    std::strncpy(out.id, presetId, sizeof(out.id) - 1U);
    out.sizeBytes = sizeBytes;
    deriveSemanticName(presetId, out.semanticName, sizeof(out.semanticName));

    PresetPaths paths{};
    if (!buildPaths_(presetId, paths)) return false;
    uint8_t header[state::sequencer::STEP_GRAPH_PRESET_HEADER_SIZE]{};
    const size_t readSize = std::min<size_t>(sizeBytes, sizeof(header));
    const auto read = files_.read(paths.current, 0, header, readSize);
    if (!read || read.value() != readSize) return true;

    state::sequencer::SequencerStepGraphPresetMetadataView metadata{};
    state::sequencer::SequencerGraphAssetReport report{};
    if (!state::sequencer::decodeStepGraphPresetMetadata(
            header,
            static_cast<uint16_t>(readSize),
            metadata,
            &report
        )) {
        return true;
    }
    out.metadataDefaulted = metadata.metadataDefaulted;
    if (metadata.metadataDefaulted) {
        out.metadataReadable = true;
        return true;
    }
    if (std::strcmp(metadata.technicalId, presetId) != 0 ||
        !state::sequencer::validStepGraphPresetSemanticName(
            metadata.semanticName
        )) {
        return true;
    }
    std::strncpy(
        out.semanticName,
        metadata.semanticName,
        sizeof(out.semanticName) - 1U
    );
    out.metadataReadable = true;
    return true;
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

    if (list->result.totalCount < UINT16_MAX) {
        ++list->result.totalCount;
    }

    StepPresetFileListEntry candidate{};
    if (!list->store->buildListEntry_(
            presetId,
            info.value().sizeBytes,
            candidate
        )) {
        return true;
    }

    const int anchorOrder = list->hasAnchor
        ? compareEntries(candidate, list->anchor)
        : 1;
    const bool eligible = list->direction == StepPresetFilePageDirection::FORWARD
        ? (!list->hasAnchor || anchorOrder > 0)
        : (list->hasAnchor && anchorOrder < 0);
    if (!eligible) return true;

    if (list->eligibleCount < UINT16_MAX) ++list->eligibleCount;

    if (list->result.count < list->capacity) {
        insertSorted(list->entries, list->result.count, candidate);
        ++list->result.count;
        return true;
    }

    if (list->direction == StepPresetFilePageDirection::FORWARD) {
        if (compareEntries(candidate, list->entries[list->capacity - 1U]) < 0) {
            list->entries[list->capacity - 1U] = candidate;
            sortEntries(list->entries, list->capacity);
        }
    } else if (compareEntries(candidate, list->entries[0]) > 0) {
        list->entries[0] = candidate;
        sortEntries(list->entries, list->capacity);
    }
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

FLASHMEM oc::type::Result<void> StepPresetFileStore::remove(
    const char* presetId
) {
    PresetPaths paths{};
    if (!buildPaths_(presetId, paths)) {
        return invalid("invalid step preset id");
    }
    if (files_.writeSessionActive()) {
        return oc::type::Result<void>::err(
            {ErrorCode::INVALID_STATE, "step preset write session active"}
        );
    }

    // Establish that the exact public asset exists before touching private
    // sidecars. A directory or another unexpected object is never treated as
    // a deletable preset.
    const auto currentInfo = files_.stat(paths.current);
    if (!currentInfo) {
        return oc::type::Result<void>::err(currentInfo.error());
    }
    if (currentInfo.value().type != oc::interface::FileType::FILE) {
        return invalid("step preset path is not a file");
    }

    // Remove recovery material first. Thus every failure before the final
    // remove leaves the current preset addressable, while success cannot be
    // undone by backup recovery on the next load.
    auto removedTmp = removeProductFileIfExists(files_, paths.tmp);
    if (!removedTmp) return removedTmp;
    auto removedBackup = removeProductFileIfExists(files_, paths.backup);
    if (!removedBackup) return removedBackup;
    return files_.remove(paths.current);
}

FLASHMEM oc::type::Result<StepPresetFileListResult> StepPresetFileStore::list(
    StepPresetFileListEntry* entries,
    uint8_t capacity
) {
    return listPage(
        entries,
        capacity,
        nullptr,
        StepPresetFilePageDirection::FORWARD
    );
}

FLASHMEM oc::type::Result<StepPresetFileListResult> StepPresetFileStore::listPage(
    StepPresetFileListEntry* entries,
    uint8_t capacity,
    const char* anchorExclusive,
    StepPresetFilePageDirection direction
) {
    if (entries == nullptr && capacity > 0) {
        return oc::type::Result<StepPresetFileListResult>::err(
            {ErrorCode::INVALID_ARGUMENT, "invalid step preset list buffer"}
        );
    }
    if (capacity == 0) {
        return oc::type::Result<StepPresetFileListResult>::ok(StepPresetFileListResult{});
    }
    if (anchorExclusive != nullptr && anchorExclusive[0] != '\0' &&
        !validPresetId(anchorExclusive)) {
        return oc::type::Result<StepPresetFileListResult>::err(
            {ErrorCode::INVALID_ARGUMENT, "invalid step preset page anchor"}
        );
    }
    if (direction == StepPresetFilePageDirection::BACKWARD &&
        (anchorExclusive == nullptr || anchorExclusive[0] == '\0')) {
        return oc::type::Result<StepPresetFileListResult>::err(
            {ErrorCode::INVALID_ARGUMENT, "missing backward page anchor"}
        );
    }

    auto ensureDir = files_.createDirectory(DIRECTORY);
    if (!ensureDir) {
        return oc::type::Result<StepPresetFileListResult>::err(ensureDir.error());
    }

    for (uint8_t i = 0; i < capacity; ++i) {
        entries[i] = StepPresetFileListEntry{};
    }

    StepPresetFileListEntry anchor{};
    const bool hasAnchor = anchorExclusive != nullptr && anchorExclusive[0] != '\0';
    if (hasAnchor) {
        PresetPaths paths{};
        if (!buildPaths_(anchorExclusive, paths)) {
            return oc::type::Result<StepPresetFileListResult>::err(
                {ErrorCode::INVALID_ARGUMENT, "invalid step preset page anchor"}
            );
        }
        const auto info = files_.stat(paths.current);
        if (!info) {
            return oc::type::Result<StepPresetFileListResult>::err(info.error());
        }
        if (info.value().type != oc::interface::FileType::FILE ||
            !buildListEntry_(anchorExclusive, info.value().sizeBytes, anchor)) {
            return oc::type::Result<StepPresetFileListResult>::err(
                {ErrorCode::INVALID_ARGUMENT, "missing page anchor"}
            );
        }
    }

    StepPresetListContext context{
        this,
        entries,
        capacity,
        anchor,
        hasAnchor,
        direction,
        0,
        StepPresetFileListResult{},
    };
    auto listed = files_.list(DIRECTORY, listVisitor_, &context);
    if (!listed) {
        return oc::type::Result<StepPresetFileListResult>::err(listed.error());
    }
    if (direction == StepPresetFilePageDirection::FORWARD) {
        context.result.hasPrevious = hasAnchor &&
            context.result.totalCount > context.eligibleCount;
        context.result.hasNext = context.eligibleCount > context.result.count;
    } else {
        context.result.hasPrevious = context.eligibleCount > context.result.count;
        context.result.hasNext = hasAnchor &&
            context.result.totalCount > context.eligibleCount;
    }
    context.result.truncated = context.result.hasPrevious || context.result.hasNext;
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

FLASHMEM oc::type::Result<bool> StepPresetFileStore::exists(const char* presetId) {
    PresetPaths paths{};
    if (!buildPaths_(presetId, paths)) {
        return oc::type::Result<bool>::err(
            {ErrorCode::INVALID_ARGUMENT, "invalid step preset id"}
        );
    }

    auto info = files_.stat(paths.current);
    if (!info && info.error().code == ErrorCode::RESOURCE_NOT_FOUND) {
        return oc::type::Result<bool>::ok(false);
    }
    if (!info) return oc::type::Result<bool>::err(info.error());
    return oc::type::Result<bool>::ok(
        info.value().type == oc::interface::FileType::FILE
    );
}

}  // namespace core::persistence
