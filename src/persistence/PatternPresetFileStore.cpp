#include "persistence/PatternPresetFileStore.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <utility>

#include <config/PlatformCompat.hpp>

namespace core::persistence {
namespace {

using Entry = PatternPresetFileListEntry;
using EntryKind = ProductDirectoryAssetEntryKind;
using Direction = PatternPresetFilePageDirection;

FLASHMEM int compareTextCaseFolded(const char* lhs, const char* rhs) {
    size_t index = 0U;
    while (lhs[index] != '\0' && rhs[index] != '\0') {
        auto left = static_cast<unsigned char>(lhs[index]);
        auto right = static_cast<unsigned char>(rhs[index]);
        if (left >= 'A' && left <= 'Z') left += 32U;
        if (right >= 'A' && right <= 'Z') right += 32U;
        if (left != right) return left < right ? -1 : 1;
        ++index;
    }
    if (lhs[index] == rhs[index]) return 0;
    return lhs[index] == '\0' ? -1 : 1;
}

FLASHMEM bool visibleFolder(
    const oc::interface::DirectoryEntry& entry
) {
    return entry.type == oc::interface::FileType::DIRECTORY &&
        !entry.nameTruncated &&
        core::state::sequencer::sequencerPatternPresetFolderNameIsValid(
            entry.name
        );
}

FLASHMEM uint16_t folderCount(
    const oc::interface::DirectoryEntry* entries,
    uint16_t count
) {
    uint16_t result = 0U;
    for (uint16_t index = 0U; index < count; ++index) {
        if (visibleFolder(entries[index])) ++result;
    }
    return result;
}

FLASHMEM const oc::interface::DirectoryEntry* folderAtRank(
    const oc::interface::DirectoryEntry* entries,
    uint16_t count,
    uint16_t wantedRank
) {
    for (uint16_t index = 0U; index < count; ++index) {
        const auto& candidate = entries[index];
        if (!visibleFolder(candidate)) continue;
        uint16_t rank = 0U;
        for (uint16_t otherIndex = 0U; otherIndex < count; ++otherIndex) {
            const auto& other = entries[otherIndex];
            if (!visibleFolder(other)) continue;
            const int comparison = compareTextCaseFolded(
                other.name,
                candidate.name
            );
            if (comparison < 0 ||
                (comparison == 0 && otherIndex < index)) {
                ++rank;
            }
        }
        if (rank == wantedRank) return &candidate;
    }
    return nullptr;
}

FLASHMEM bool copyFolderEntry(
    const oc::interface::DirectoryEntry& folder,
    Entry& out
) {
    out = {};
    const int written = std::snprintf(
        out.id,
        sizeof(out.id),
        "@%s",
        folder.name
    );
    if (written <= 1 || static_cast<size_t>(written) >= sizeof(out.id)) {
        return false;
    }
    std::strncpy(
        out.semanticName,
        folder.name,
        sizeof(out.semanticName) - 1U
    );
    out.kind = EntryKind::FOLDER;
    out.metadataReadable = true;
    return true;
}

FLASHMEM bool formatChildDirectory(
    const char* parent,
    const char* child,
    char* out,
    size_t outSize
) {
    if (parent == nullptr || child == nullptr || out == nullptr ||
        outSize == 0U ||
        !core::state::sequencer::sequencerPatternPresetFolderNameIsValid(
            child
        )) {
        return false;
    }
    const int written = std::snprintf(
        out,
        outSize,
        "%s/%s",
        parent,
        child
    );
    return written > 0 && static_cast<size_t>(written) < outSize;
}

}  // namespace

FLASHMEM PatternPresetFileStore::PatternPresetFileStore(
    ProductFileService& files,
    ProductDirectoryCatalog& catalog,
    const core::state::sequencer::SequencerPatternPresetLocation& location
) : files_(files),
    catalog_(catalog),
    directory_{},
    store_(
        files,
        catalog,
        {
            .directory = directory_,
            .extension = EXTENSION,
            .generatedIdPrefix = "pattern-preset-",
            .maxFileSize = MAX_FILE_SIZE,
            .writeChunkSize = WRITE_CHUNK_SIZE,
        },
        readMetadata_,
        ProductPersistenceJobOwner::PATTERN_PRESET_CATALOG
    ) {
    (void)core::state::sequencer::formatSequencerPatternPresetDirectory(
        location,
        directory_,
        sizeof(directory_)
    );
}

FLASHMEM bool PatternPresetFileStore::validPresetId(const char* presetId) {
    return ProductAssetFileStore::validAssetId(presetId);
}

FLASHMEM bool PatternPresetFileStore::readMetadata_(
    ProductFileService& files,
    const char* currentPath,
    const char* expectedPresetId,
    uint32_t fileSize,
    char* outSemanticName,
    size_t outSemanticNameSize
) {
    namespace codec = sequencer_pattern_preset_codec;
    if (currentPath == nullptr || expectedPresetId == nullptr ||
        outSemanticName == nullptr || outSemanticNameSize == 0U ||
        fileSize < codec::HEADER_SIZE || fileSize > MAX_FILE_SIZE) {
        return false;
    }

    uint8_t header[codec::HEADER_SIZE]{};
    const auto read = files.read(currentPath, 0U, header, sizeof(header));
    if (!read || read.value() != sizeof(header)) return false;

    codec::MetadataView metadata{};
    if (!codec::decodeMetadata(header, sizeof(header), metadata) ||
        static_cast<uint32_t>(codec::HEADER_SIZE) +
                metadata.patternEnvelopeSize + metadata.drumRecordSize !=
            fileSize ||
        std::strcmp(metadata.metadata.technicalId, expectedPresetId) != 0 ||
        std::strlen(metadata.metadata.semanticName) >= outSemanticNameSize) {
        return false;
    }
    std::strncpy(
        outSemanticName,
        metadata.metadata.semanticName,
        outSemanticNameSize - 1U
    );
    outSemanticName[outSemanticNameSize - 1U] = '\0';
    return true;
}

FLASHMEM oc::type::Result<PatternPresetFileTransferResult>
PatternPresetFileStore::save(
    const char* presetId,
    const uint8_t* payload,
    uint16_t payloadSize
) {
    return store_.save(presetId, payload, payloadSize);
}

FLASHMEM oc::type::Result<PatternPresetFileTransferResult>
PatternPresetFileStore::load(
    const char* presetId,
    uint8_t* outPayload,
    uint16_t outCapacity,
    uint16_t& outSize
) {
    outSize = 0U;
    uint32_t loadedSize = 0U;
    auto loaded = store_.load(
        presetId,
        outPayload,
        outCapacity,
        loadedSize
    );
    if (!loaded) {
        return oc::type::Result<PatternPresetFileTransferResult>::err(
            loaded.error()
        );
    }
    if (loadedSize > UINT16_MAX) {
        return oc::type::Result<PatternPresetFileTransferResult>::err({
            oc::type::ErrorCode::RESOURCE_EXHAUSTED,
            "pattern preset exceeds codec size",
        });
    }
    outSize = static_cast<uint16_t>(loadedSize);
    return loaded;
}

FLASHMEM oc::type::Result<void> PatternPresetFileStore::remove(
    const char* presetId
) {
    return store_.remove(presetId);
}

FLASHMEM oc::type::Result<PatternPresetFileListResult>
PatternPresetFileStore::listPage(
    PatternPresetFileListEntry* entries,
    uint8_t capacity,
    const char* anchorExclusive,
    PatternPresetFilePageDirection direction
) {
    if (entries == nullptr && capacity > 0U) {
        return oc::type::Result<PatternPresetFileListResult>::err({
            oc::type::ErrorCode::INVALID_ARGUMENT,
            "invalid pattern preset list buffer",
        });
    }
    if (capacity == 0U) {
        return oc::type::Result<PatternPresetFileListResult>::ok({});
    }

    const ProductDirectoryAssetQuery query{
        .directory = directory_,
        .extension = EXTENSION,
        .generatedIdPrefix = "pattern-preset-",
        .maxFileSize = MAX_FILE_SIZE,
        .metadataReader = readMetadata_,
    };
    const auto ready = catalog_.requestAssets(
        query,
        ProductPersistenceJobOwner::PATTERN_PRESET_CATALOG
    );
    if (!ready) {
        return oc::type::Result<PatternPresetFileListResult>::err(
            ready.error()
        );
    }

    uint16_t rawCount = 0U;
    const auto* raw = catalog_.rawEntries(directory_, rawCount);
    uint16_t assetCount = 0U;
    const auto* assets = catalog_.assetEntries(query, assetCount);
    if (raw == nullptr || assets == nullptr) {
        return oc::type::Result<PatternPresetFileListResult>::err({
            oc::type::ErrorCode::HARDWARE_BUSY,
            "pattern preset catalog not ready",
        });
    }

    const uint16_t folders = folderCount(raw, rawCount);
    const uint16_t total = static_cast<uint16_t>(folders + assetCount);
    uint16_t anchorIndex = 0U;
    const bool hasAnchor = anchorExclusive != nullptr &&
        anchorExclusive[0] != '\0';
    if (hasAnchor) {
        bool found = false;
        if (anchorExclusive[0] == FOLDER_ENTRY_PREFIX) {
            for (uint16_t rank = 0U; rank < folders; ++rank) {
                const auto* folder = folderAtRank(raw, rawCount, rank);
                if (folder != nullptr &&
                    std::strcmp(folder->name, anchorExclusive + 1U) == 0) {
                    anchorIndex = rank;
                    found = true;
                    break;
                }
            }
        } else {
            for (uint16_t index = 0U; index < assetCount; ++index) {
                if (std::strcmp(assets[index].id, anchorExclusive) == 0) {
                    anchorIndex = static_cast<uint16_t>(folders + index);
                    found = true;
                    break;
                }
            }
        }
        if (!found) {
            return oc::type::Result<PatternPresetFileListResult>::err({
                oc::type::ErrorCode::INVALID_ARGUMENT,
                "missing pattern preset page anchor",
            });
        }
    }
    if (direction == Direction::BACKWARD && !hasAnchor) {
        return oc::type::Result<PatternPresetFileListResult>::err({
            oc::type::ErrorCode::INVALID_ARGUMENT,
            "missing backward page anchor",
        });
    }

    const uint16_t begin = direction == Direction::FORWARD
        ? (hasAnchor ? static_cast<uint16_t>(anchorIndex + 1U) : 0U)
        : (anchorIndex > capacity
               ? static_cast<uint16_t>(anchorIndex - capacity)
               : 0U);
    const uint16_t end = direction == Direction::FORWARD
        ? static_cast<uint16_t>(std::min<unsigned>(
              total,
              static_cast<unsigned>(begin) + capacity
          ))
        : anchorIndex;

    PatternPresetFileListResult result{};
    result.totalCount = total;
    for (uint16_t index = begin; index < end; ++index) {
        bool copied = false;
        if (index < folders) {
            const auto* folder = folderAtRank(raw, rawCount, index);
            copied = folder != nullptr &&
                copyFolderEntry(*folder, entries[result.count]);
        } else {
            entries[result.count] = assets[index - folders];
            copied = true;
        }
        if (copied) ++result.count;
    }
    result.hasPrevious = begin > 0U;
    result.hasNext = end < total;
    result.truncated = result.hasPrevious || result.hasNext;
    return oc::type::Result<PatternPresetFileListResult>::ok(result);
}

FLASHMEM oc::type::Result<PatternPresetFileListResult>
PatternPresetFileStore::listFoldersPage(
    PatternPresetFileListEntry* entries,
    uint8_t capacity,
    const char* anchorExclusive,
    PatternPresetFilePageDirection direction
) {
    if (entries == nullptr && capacity > 0U) {
        return oc::type::Result<PatternPresetFileListResult>::err({
            oc::type::ErrorCode::INVALID_ARGUMENT,
            "invalid pattern folder list buffer",
        });
    }
    if (capacity == 0U) {
        return oc::type::Result<PatternPresetFileListResult>::ok({});
    }

    const ProductDirectoryAssetQuery query{
        .directory = directory_,
        .extension = EXTENSION,
        .generatedIdPrefix = "pattern-preset-",
        .maxFileSize = MAX_FILE_SIZE,
        .metadataReader = readMetadata_,
    };
    const auto ready = catalog_.requestAssets(
        query,
        ProductPersistenceJobOwner::PATTERN_PRESET_CATALOG
    );
    if (!ready) {
        return oc::type::Result<PatternPresetFileListResult>::err(
            ready.error()
        );
    }

    uint16_t rawCount = 0U;
    const auto* raw = catalog_.rawEntries(directory_, rawCount);
    if (raw == nullptr) {
        return oc::type::Result<PatternPresetFileListResult>::err({
            oc::type::ErrorCode::HARDWARE_BUSY,
            "pattern folder catalog not ready",
        });
    }
    const uint16_t total = folderCount(raw, rawCount);
    const bool hasAnchor = anchorExclusive != nullptr &&
        anchorExclusive[0] != '\0';
    uint16_t anchorIndex = 0U;
    if (hasAnchor) {
        if (anchorExclusive[0] != FOLDER_ENTRY_PREFIX) {
            return oc::type::Result<PatternPresetFileListResult>::err({
                oc::type::ErrorCode::INVALID_ARGUMENT,
                "invalid pattern folder page anchor",
            });
        }
        bool found = false;
        for (uint16_t rank = 0U; rank < total; ++rank) {
            const auto* folder = folderAtRank(raw, rawCount, rank);
            if (folder != nullptr &&
                std::strcmp(folder->name, anchorExclusive + 1U) == 0) {
                anchorIndex = rank;
                found = true;
                break;
            }
        }
        if (!found) {
            return oc::type::Result<PatternPresetFileListResult>::err({
                oc::type::ErrorCode::INVALID_ARGUMENT,
                "missing pattern folder page anchor",
            });
        }
    }
    if (direction == Direction::BACKWARD && !hasAnchor) {
        return oc::type::Result<PatternPresetFileListResult>::err({
            oc::type::ErrorCode::INVALID_ARGUMENT,
            "missing backward pattern folder anchor",
        });
    }

    const uint16_t begin = direction == Direction::FORWARD
        ? (hasAnchor ? static_cast<uint16_t>(anchorIndex + 1U) : 0U)
        : (anchorIndex > capacity
               ? static_cast<uint16_t>(anchorIndex - capacity)
               : 0U);
    const uint16_t end = direction == Direction::FORWARD
        ? static_cast<uint16_t>(std::min<unsigned>(
              total,
              static_cast<unsigned>(begin) + capacity
          ))
        : anchorIndex;

    PatternPresetFileListResult result{};
    result.totalCount = total;
    for (uint16_t rank = begin; rank < end; ++rank) {
        const auto* folder = folderAtRank(raw, rawCount, rank);
        if (folder != nullptr &&
            copyFolderEntry(*folder, entries[result.count])) {
            ++result.count;
        }
    }
    result.hasPrevious = begin > 0U;
    result.hasNext = end < total;
    result.truncated = result.hasPrevious || result.hasNext;
    return oc::type::Result<PatternPresetFileListResult>::ok(result);
}

FLASHMEM oc::type::Result<void> PatternPresetFileStore::nextPresetId(
    char* out,
    size_t outSize
) {
    return store_.nextAssetId(out, outSize);
}

FLASHMEM oc::type::Result<bool> PatternPresetFileStore::exists(
    const char* presetId
) {
    return store_.exists(presetId);
}

FLASHMEM bool PatternPresetFileStore::folderNameFromEntryId(
    const char* entryId,
    char* out,
    size_t outSize
) {
    if (entryId == nullptr || entryId[0] != FOLDER_ENTRY_PREFIX ||
        out == nullptr || outSize == 0U ||
        !core::state::sequencer::sequencerPatternPresetFolderNameIsValid(
            entryId + 1U
        ) || std::strlen(entryId + 1U) >= outSize) {
        return false;
    }
    std::strcpy(out, entryId + 1U);
    return true;
}

FLASHMEM oc::type::Result<void> PatternPresetFileStore::createFolder(
    const char* folderName
) {
    char child[sizeof(directory_)]{};
    if (!formatChildDirectory(
            directory_,
            folderName,
            child,
            sizeof(child)
        )) {
        return oc::type::Result<void>::err({
            oc::type::ErrorCode::INVALID_ARGUMENT,
            "invalid pattern preset folder",
        });
    }
    auto acquired = files_.acquireMutation(ProductMutationOwner::ASSET);
    if (!acquired) return oc::type::Result<void>::err(acquired.error());
    auto lease = std::move(acquired.value());
    const auto created = files_.createDirectory(lease, child);
    const auto error = created
        ? oc::type::Error{oc::type::ErrorCode::OK, nullptr}
        : created.error();
    const auto released = files_.releaseMutation(lease);
    if (!created) return oc::type::Result<void>::err(error);
    if (!released) return released;
    catalog_.invalidate();
    return oc::type::Result<void>::ok();
}

FLASHMEM oc::type::Result<void> PatternPresetFileStore::removeEmptyFolder(
    const char* folderName
) {
    char child[sizeof(directory_)]{};
    if (!formatChildDirectory(
            directory_,
            folderName,
            child,
            sizeof(child)
        )) {
        return oc::type::Result<void>::err({
            oc::type::ErrorCode::INVALID_ARGUMENT,
            "invalid pattern preset folder",
        });
    }
    auto acquired = files_.acquireMutation(ProductMutationOwner::ASSET);
    if (!acquired) return oc::type::Result<void>::err(acquired.error());
    auto lease = std::move(acquired.value());
    const auto removed = files_.remove(lease, child);
    const auto error = removed
        ? oc::type::Error{oc::type::ErrorCode::OK, nullptr}
        : removed.error();
    const auto released = files_.releaseMutation(lease);
    if (!removed) return oc::type::Result<void>::err(error);
    if (!released) return released;
    catalog_.invalidate();
    return oc::type::Result<void>::ok();
}

FLASHMEM oc::type::Result<void> PatternPresetFileStore::renameFolder(
    const char* folderName,
    const char* newFolderName
) {
    char source[sizeof(directory_)]{};
    char destination[sizeof(directory_)]{};
    if (!formatChildDirectory(
            directory_, folderName, source, sizeof(source)
        ) ||
        !formatChildDirectory(
            directory_, newFolderName, destination, sizeof(destination)
        ) ||
        std::strcmp(source, destination) == 0) {
        return oc::type::Result<void>::err({
            oc::type::ErrorCode::INVALID_ARGUMENT,
            "invalid pattern folder rename",
        });
    }
    auto acquired = files_.acquireMutation(ProductMutationOwner::ASSET);
    if (!acquired) return oc::type::Result<void>::err(acquired.error());
    auto lease = std::move(acquired.value());
    const auto renamed = files_.rename(lease, source, destination);
    const auto error = renamed
        ? oc::type::Error{oc::type::ErrorCode::OK, nullptr}
        : renamed.error();
    const auto released = files_.releaseMutation(lease);
    if (!renamed) return oc::type::Result<void>::err(error);
    if (!released) return released;
    catalog_.invalidate();
    return oc::type::Result<void>::ok();
}

FLASHMEM oc::type::Result<void> PatternPresetFileStore::movePreset(
    const char* presetId,
    const core::state::sequencer::SequencerPatternPresetLocation& destination
) {
    char destinationDirectory[sizeof(directory_)]{};
    if (!validPresetId(presetId) ||
        !core::state::sequencer::formatSequencerPatternPresetDirectory(
            destination,
            destinationDirectory,
            sizeof(destinationDirectory)
        )) {
        return oc::type::Result<void>::err({
            oc::type::ErrorCode::INVALID_ARGUMENT,
            "invalid pattern preset move",
        });
    }
    const ProductAssetFileLayout sourceLayout{
        .directory = directory_,
        .extension = EXTENSION,
        .generatedIdPrefix = "pattern-preset-",
        .maxFileSize = MAX_FILE_SIZE,
        .writeChunkSize = WRITE_CHUNK_SIZE,
    };
    auto destinationLayout = sourceLayout;
    destinationLayout.directory = destinationDirectory;
    ProductAssetFilePaths sourcePaths{};
    ProductAssetFilePaths destinationPaths{};
    if (!buildProductAssetFilePaths(sourceLayout, presetId, sourcePaths) ||
        !buildProductAssetFilePaths(
            destinationLayout, presetId, destinationPaths
        ) ||
        std::strcmp(sourcePaths.current, destinationPaths.current) == 0) {
        return oc::type::Result<void>::err({
            oc::type::ErrorCode::INVALID_ARGUMENT,
            "invalid pattern preset destination",
        });
    }
    auto acquired = files_.acquireMutation(ProductMutationOwner::ASSET);
    if (!acquired) return oc::type::Result<void>::err(acquired.error());
    auto lease = std::move(acquired.value());
    const auto moved = files_.rename(
        lease, sourcePaths.current, destinationPaths.current
    );
    const auto error = moved
        ? oc::type::Error{oc::type::ErrorCode::OK, nullptr}
        : moved.error();
    const auto released = files_.releaseMutation(lease);
    if (!moved) return oc::type::Result<void>::err(error);
    if (!released) return released;
    catalog_.invalidate();
    return oc::type::Result<void>::ok();
}

FLASHMEM oc::type::Result<void> PatternPresetFileStore::moveFolder(
    const char* folderName,
    const core::state::sequencer::SequencerPatternPresetLocation& destination
) {
    char destinationDirectory[sizeof(directory_)]{};
    char source[sizeof(directory_)]{};
    char target[sizeof(directory_)]{};
    if (!formatChildDirectory(
            directory_, folderName, source, sizeof(source)
        ) ||
        !core::state::sequencer::formatSequencerPatternPresetDirectory(
            destination,
            destinationDirectory,
            sizeof(destinationDirectory)
        ) ||
        !formatChildDirectory(
            destinationDirectory, folderName, target, sizeof(target)
        )) {
        return oc::type::Result<void>::err({
            oc::type::ErrorCode::INVALID_ARGUMENT,
            "invalid pattern folder move",
        });
    }
    const size_t sourceLength = std::strlen(source);
    if (std::strcmp(source, target) == 0 ||
        (std::strncmp(destinationDirectory, source, sourceLength) == 0 &&
         destinationDirectory[sourceLength] == '/')) {
        return oc::type::Result<void>::err({
            oc::type::ErrorCode::INVALID_ARGUMENT,
            "pattern folder cannot move into itself",
        });
    }
    auto acquired = files_.acquireMutation(ProductMutationOwner::ASSET);
    if (!acquired) return oc::type::Result<void>::err(acquired.error());
    auto lease = std::move(acquired.value());
    const auto moved = files_.rename(lease, source, target);
    const auto error = moved
        ? oc::type::Error{oc::type::ErrorCode::OK, nullptr}
        : moved.error();
    const auto released = files_.releaseMutation(lease);
    if (!moved) return oc::type::Result<void>::err(error);
    if (!released) return released;
    catalog_.invalidate();
    return oc::type::Result<void>::ok();
}

}  // namespace core::persistence
