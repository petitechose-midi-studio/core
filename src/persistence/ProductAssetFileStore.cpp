#include "persistence/ProductAssetFileStore.hpp"

#include <cstdio>
#include <cstring>
#include <utility>

#include <config/PlatformCompat.hpp>
#include <oc/diagnostics/Performance.hpp>

#include "persistence/AtomicProductFile.hpp"
#include "persistence/ProductFilePath.hpp"
#include "state/project/ProjectSlug.hpp"

namespace core::persistence {
namespace {

using oc::type::ErrorCode;

FLASHMEM oc::type::Result<void> invalid(const char* context) {
    return oc::type::Result<void>::err(
        {ErrorCode::INVALID_ARGUMENT, context}
    );
}

FLASHMEM int compareTextCaseFolded(const char* lhs, const char* rhs) {
    size_t index = 0;
    while (lhs[index] != '\0' && rhs[index] != '\0') {
        auto left = static_cast<unsigned char>(lhs[index]);
        auto right = static_cast<unsigned char>(rhs[index]);
        if (left >= 'A' && left <= 'Z') {
            left = static_cast<unsigned char>(left + 32U);
        }
        if (right >= 'A' && right <= 'Z') {
            right = static_cast<unsigned char>(right + 32U);
        }
        if (left != right) return left < right ? -1 : 1;
        ++index;
    }
    if (lhs[index] == rhs[index]) return 0;
    return lhs[index] == '\0' ? -1 : 1;
}

FLASHMEM int compareEntries(
    const ProductAssetFileListEntry& lhs,
    const ProductAssetFileListEntry& rhs
) {
    const int byName = compareTextCaseFolded(
        lhs.semanticName,
        rhs.semanticName
    );
    return byName != 0 ? byName : std::strcmp(lhs.id, rhs.id);
}

FLASHMEM void deriveSemanticName(
    const char* assetId,
    char* out,
    size_t outSize
) {
    if (out == nullptr || outSize == 0) return;
    out[0] = '\0';
    size_t written = 0;
    bool capitalize = true;
    for (size_t index = 0;
         assetId != nullptr && assetId[index] != '\0' &&
         written + 1U < outSize;
         ++index) {
        char character = assetId[index];
        if (character == '-' || character == '_' || character == '.') {
            if (written > 0U && out[written - 1U] != ' ') {
                out[written++] = ' ';
            }
            capitalize = true;
            continue;
        }
        if (capitalize && character >= 'a' && character <= 'z') {
            character = static_cast<char>(character - 'a' + 'A');
        }
        out[written++] = character;
        capitalize = false;
    }
    while (written > 0U && out[written - 1U] == ' ') --written;
    out[written] = '\0';
}

FLASHMEM void insertSorted(
    ProductAssetFileListEntry* entries,
    uint8_t count,
    ProductAssetFileListEntry entry
) {
    uint8_t insert = count;
    while (insert > 0U &&
           compareEntries(entries[insert - 1U], entry) > 0) {
        entries[insert] = entries[insert - 1U];
        --insert;
    }
    entries[insert] = entry;
}

FLASHMEM void sortEntries(
    ProductAssetFileListEntry* entries,
    uint8_t count
) {
    for (uint8_t index = 1; index < count; ++index) {
        const auto current = entries[index];
        uint8_t insert = index;
        while (insert > 0U &&
               compareEntries(entries[insert - 1U], current) > 0) {
            entries[insert] = entries[insert - 1U];
            --insert;
        }
        entries[insert] = current;
    }
}

FLASHMEM bool formatPath(
    char* out,
    size_t outSize,
    const char* format,
    const char* first,
    const char* second,
    const char* third
) {
    if (out == nullptr || outSize == 0U || format == nullptr ||
        first == nullptr || second == nullptr || third == nullptr) {
        return false;
    }
    const int written = std::snprintf(
        out,
        outSize,
        format,
        first,
        second,
        third
    );
    return written > 0 && static_cast<size_t>(written) < outSize;
}

}  // namespace

struct ProductAssetFileStore::ListContext {
    ProductAssetFileStore* store = nullptr;
    ProductAssetFileListEntry* entries = nullptr;
    uint8_t capacity = 0;
    ProductAssetFileListEntry anchor{};
    bool hasAnchor = false;
    ProductAssetFilePageDirection direction =
        ProductAssetFilePageDirection::FORWARD;
    uint16_t eligibleCount = 0;
    ProductAssetFileListResult result{};
};

FLASHMEM bool buildProductAssetFilePaths(
    ProductAssetFileLayout layout,
    const char* assetId,
    ProductAssetFilePaths& out
) {
    out = {};
    if (!ProductAssetFileStore::validAssetId(assetId) ||
        layout.directory == nullptr ||
        layout.directory[0] == '\0' ||
        layout.extension == nullptr ||
        layout.extension[0] != '.') {
        return false;
    }
    return copyProductRelativePath(
               out.directory,
               sizeof(out.directory),
               layout.directory
           ) &&
           formatPath(
               out.current,
               sizeof(out.current),
               "%s/%s%s",
               layout.directory,
               assetId,
               layout.extension
           ) &&
           formatPath(
               out.backup,
               sizeof(out.backup),
               "%s/%s%s.bak",
               layout.directory,
               assetId,
               layout.extension
           ) &&
           formatPath(
               out.tmp,
               sizeof(out.tmp),
               "tmp/%s%s%s",
               assetId,
               layout.extension,
               ".tmp"
           );
}

FLASHMEM ProductAssetFileStore::ProductAssetFileStore(
    ProductFileService& files,
    ProductAssetFileLayout layout,
    ProductAssetMetadataReader metadataReader
) : files_(files),
    layout_(layout),
    metadataReader_(metadataReader) {}

FLASHMEM bool ProductAssetFileStore::validAssetId(const char* assetId) {
    return core::state::project::validProjectSlug(assetId);
}

FLASHMEM bool ProductAssetFileStore::layoutValid_() const {
    return layout_.directory != nullptr &&
           layout_.directory[0] != '\0' &&
           layout_.extension != nullptr &&
           layout_.extension[0] == '.' &&
           layout_.generatedIdPrefix != nullptr &&
           layout_.generatedIdPrefix[0] != '\0' &&
           layout_.maxFileSize > 0U &&
           layout_.writeChunkSize > 0U;
}

FLASHMEM bool ProductAssetFileStore::paths_(
    const char* assetId,
    ProductAssetFilePaths& out
) const {
    return layoutValid_() &&
           buildProductAssetFilePaths(layout_, assetId, out);
}

FLASHMEM bool ProductAssetFileStore::buildListEntry_(
    const char* assetId,
    uint32_t sizeBytes,
    ProductAssetFileListEntry& out
) {
    out = {};
    if (!validAssetId(assetId) || sizeBytes == 0U ||
        sizeBytes > layout_.maxFileSize) {
        return false;
    }
    std::strncpy(out.id, assetId, sizeof(out.id) - 1U);
    out.sizeBytes = sizeBytes;
    deriveSemanticName(
        assetId,
        out.semanticName,
        sizeof(out.semanticName)
    );

    ProductAssetFilePaths paths{};
    if (!paths_(assetId, paths)) return false;
    if (metadataReader_ != nullptr) {
        char semanticName[ProductAssetFileListEntry::SEMANTIC_NAME_SIZE] = {};
        if (metadataReader_(
                files_,
                paths.current,
                assetId,
                sizeBytes,
                semanticName,
                sizeof(semanticName)
            )) {
            std::strncpy(
                out.semanticName,
                semanticName,
                sizeof(out.semanticName) - 1U
            );
            out.metadataReadable = true;
        }
    }
    return true;
}

FLASHMEM bool ProductAssetFileStore::listVisitor_(
    const oc::interface::DirectoryEntry& entry,
    void* context
) {
    auto* list = static_cast<ListContext*>(context);
    if (list == nullptr || list->store == nullptr ||
        list->entries == nullptr) {
        return false;
    }
    if (entry.type != oc::interface::FileType::FILE ||
        entry.nameTruncated) {
        return true;
    }

    const char* extension = list->store->layout_.extension;
    const size_t extensionLength = std::strlen(extension);
    const size_t nameLength = std::strlen(entry.name);
    if (nameLength <= extensionLength ||
        std::strcmp(
            entry.name + nameLength - extensionLength,
            extension
        ) != 0) {
        return true;
    }

    char assetId[core::state::project::ProjectMetadata::ID_SIZE] = {};
    const size_t idLength = nameLength - extensionLength;
    if (idLength >= sizeof(assetId)) return true;
    std::memcpy(assetId, entry.name, idLength);
    assetId[idLength] = '\0';
    if (!validAssetId(assetId)) return true;

    ProductAssetFilePaths paths{};
    if (!list->store->paths_(assetId, paths)) return true;
    const auto info = list->store->files_.stat(paths.current);
    if (!info || info.value().type != oc::interface::FileType::FILE ||
        info.value().sizeBytes == 0U ||
        info.value().sizeBytes > list->store->layout_.maxFileSize) {
        return true;
    }

    if (list->result.totalCount < UINT16_MAX) {
        ++list->result.totalCount;
    }

    ProductAssetFileListEntry candidate{};
    if (!list->store->buildListEntry_(
            assetId,
            info.value().sizeBytes,
            candidate
        )) {
        return true;
    }

    const int anchorOrder = list->hasAnchor
        ? compareEntries(candidate, list->anchor)
        : 1;
    const bool eligible =
        list->direction == ProductAssetFilePageDirection::FORWARD
            ? (!list->hasAnchor || anchorOrder > 0)
            : (list->hasAnchor && anchorOrder < 0);
    if (!eligible) return true;
    if (list->eligibleCount < UINT16_MAX) ++list->eligibleCount;

    if (list->result.count < list->capacity) {
        insertSorted(
            list->entries,
            list->result.count,
            candidate
        );
        ++list->result.count;
        return true;
    }

    if (list->direction == ProductAssetFilePageDirection::FORWARD) {
        if (compareEntries(
                candidate,
                list->entries[list->capacity - 1U]
            ) < 0) {
            list->entries[list->capacity - 1U] = candidate;
            sortEntries(list->entries, list->capacity);
        }
    } else if (compareEntries(candidate, list->entries[0]) > 0) {
        list->entries[0] = candidate;
        sortEntries(list->entries, list->capacity);
    }
    return true;
}

FLASHMEM oc::type::Result<ProductAssetFileTransferResult>
ProductAssetFileStore::save(
    const char* assetId,
    const uint8_t* payload,
    uint32_t payloadSize
) {
    if (payload == nullptr || payloadSize == 0U ||
        payloadSize > layout_.maxFileSize) {
        return oc::type::Result<ProductAssetFileTransferResult>::err(
            {ErrorCode::INVALID_ARGUMENT, "invalid asset payload"}
        );
    }
    ProductAssetFilePaths paths{};
    if (!paths_(assetId, paths)) {
        return oc::type::Result<ProductAssetFileTransferResult>::err(
            {ErrorCode::INVALID_ARGUMENT, "invalid asset id"}
        );
    }

    auto acquired = files_.acquireMutation(ProductMutationOwner::ASSET);
    if (!acquired) {
        return oc::type::Result<ProductAssetFileTransferResult>::err(acquired.error());
    }
    auto lease = std::move(acquired.value());

    auto saved = replaceProductFileAtomically(
        files_,
        lease,
        {
            .directory = paths.directory,
            .current = paths.current,
            .backup = paths.backup,
            .tmp = paths.tmp,
        },
        payload,
        payloadSize,
        layout_.writeChunkSize
    );
    if (!saved) {
        const auto error = saved.error();
        (void)files_.releaseMutation(lease);
        return oc::type::Result<ProductAssetFileTransferResult>::err(
            error
        );
    }
    auto released = files_.releaseMutation(lease);
    if (!released) {
        return oc::type::Result<ProductAssetFileTransferResult>::err(released.error());
    }

    ProductAssetFileTransferResult result{};
    result.bytes = payloadSize;
    std::strncpy(result.path, paths.current, sizeof(result.path) - 1U);
    std::strncpy(result.id, assetId, sizeof(result.id) - 1U);
    return oc::type::Result<ProductAssetFileTransferResult>::ok(result);
}

FLASHMEM oc::type::Result<ProductAssetFileTransferResult>
ProductAssetFileStore::load(
    const char* assetId,
    uint8_t* outPayload,
    uint32_t outCapacity,
    uint32_t& outSize
) {
    outSize = 0U;
    if (outPayload == nullptr || outCapacity == 0U) {
        return oc::type::Result<ProductAssetFileTransferResult>::err(
            {ErrorCode::INVALID_ARGUMENT, "invalid asset buffer"}
        );
    }
    ProductAssetFilePaths paths{};
    if (!paths_(assetId, paths)) {
        return oc::type::Result<ProductAssetFileTransferResult>::err(
            {ErrorCode::INVALID_ARGUMENT, "invalid asset id"}
        );
    }

    auto acquired = files_.acquireMutation(ProductMutationOwner::ASSET);
    if (!acquired) {
        return oc::type::Result<ProductAssetFileTransferResult>::err(acquired.error());
    }
    auto lease = std::move(acquired.value());

    const auto recovered = recoverProductFileBackupIfCurrentMissing(
        files_,
        lease,
        paths.current,
        paths.backup
    );
    if (!recovered) {
        const auto error = recovered.error();
        (void)files_.releaseMutation(lease);
        return oc::type::Result<ProductAssetFileTransferResult>::err(
            error
        );
    }

    const auto info = files_.stat(lease, paths.current);
    if (!info) {
        const auto error = info.error();
        (void)files_.releaseMutation(lease);
        return oc::type::Result<ProductAssetFileTransferResult>::err(
            error
        );
    }
    if (info.value().type != oc::interface::FileType::FILE ||
        info.value().sizeBytes == 0U ||
        info.value().sizeBytes > outCapacity ||
        info.value().sizeBytes > layout_.maxFileSize) {
        (void)files_.releaseMutation(lease);
        return oc::type::Result<ProductAssetFileTransferResult>::err(
            {ErrorCode::RESOURCE_EXHAUSTED, "asset file too large"}
        );
    }

    const auto read = files_.read(
        lease,
        paths.current,
        0,
        outPayload,
        info.value().sizeBytes
    );
    if (!read) {
        const auto error = read.error();
        (void)files_.releaseMutation(lease);
        return oc::type::Result<ProductAssetFileTransferResult>::err(
            error
        );
    }
    if (read.value() != info.value().sizeBytes) {
        (void)files_.releaseMutation(lease);
        return oc::type::Result<ProductAssetFileTransferResult>::err(
            {ErrorCode::STORAGE_READ_FAILED, "short asset read"}
        );
    }

    ProductAssetFileTransferResult result{};
    result.bytes = static_cast<uint32_t>(read.value());
    outSize = result.bytes;
    std::strncpy(result.path, paths.current, sizeof(result.path) - 1U);
    std::strncpy(result.id, assetId, sizeof(result.id) - 1U);
    auto released = files_.releaseMutation(lease);
    if (!released) {
        outSize = 0U;
        return oc::type::Result<ProductAssetFileTransferResult>::err(released.error());
    }
    return oc::type::Result<ProductAssetFileTransferResult>::ok(result);
}

FLASHMEM oc::type::Result<void> ProductAssetFileStore::remove(
    const char* assetId
) {
    ProductAssetFilePaths paths{};
    if (!paths_(assetId, paths)) return invalid("invalid asset id");
    auto acquired = files_.acquireMutation(ProductMutationOwner::ASSET);
    if (!acquired) {
        return oc::type::Result<void>::err(acquired.error());
    }
    auto lease = std::move(acquired.value());

    const auto currentInfo = files_.stat(lease, paths.current);
    if (!currentInfo) {
        const auto error = currentInfo.error();
        (void)files_.releaseMutation(lease);
        return oc::type::Result<void>::err(error);
    }
    if (currentInfo.value().type != oc::interface::FileType::FILE) {
        (void)files_.releaseMutation(lease);
        return invalid("asset path is not a file");
    }

    auto deletedTmp = deleteProductFileIfExists(files_, lease, paths.tmp);
    if (!deletedTmp) {
        const auto error = deletedTmp.error();
        (void)files_.releaseMutation(lease);
        return oc::type::Result<void>::err(error);
    }
    auto deletedBackup = deleteProductFileIfExists(files_, lease, paths.backup);
    if (!deletedBackup) {
        const auto error = deletedBackup.error();
        (void)files_.releaseMutation(lease);
        return oc::type::Result<void>::err(error);
    }
    auto removed = files_.remove(lease, paths.current);
    if (!removed) {
        const auto error = removed.error();
        (void)files_.releaseMutation(lease);
        return oc::type::Result<void>::err(error);
    }
    return files_.releaseMutation(lease);
}

FLASHMEM oc::type::Result<ProductAssetFileListResult>
ProductAssetFileStore::list(
    ProductAssetFileListEntry* entries,
    uint8_t capacity
) {
    return listPage(
        entries,
        capacity,
        nullptr,
        ProductAssetFilePageDirection::FORWARD
    );
}

FLASHMEM oc::type::Result<ProductAssetFileListResult>
ProductAssetFileStore::listPage(
    ProductAssetFileListEntry* entries,
    uint8_t capacity,
    const char* anchorExclusive,
    ProductAssetFilePageDirection direction
) {
    OC_PERF_SCOPE(perfList, "persistence.asset-catalog.list-page");
    if (!layoutValid_()) {
        return oc::type::Result<ProductAssetFileListResult>::err(
            {ErrorCode::INVALID_ARGUMENT, "invalid asset layout"}
        );
    }
    if (entries == nullptr && capacity > 0U) {
        return oc::type::Result<ProductAssetFileListResult>::err(
            {ErrorCode::INVALID_ARGUMENT, "invalid asset list buffer"}
        );
    }
    if (capacity == 0U) {
        return oc::type::Result<ProductAssetFileListResult>::ok({});
    }
    if (anchorExclusive != nullptr && anchorExclusive[0] != '\0' &&
        !validAssetId(anchorExclusive)) {
        return oc::type::Result<ProductAssetFileListResult>::err(
            {ErrorCode::INVALID_ARGUMENT, "invalid asset page anchor"}
        );
    }
    if (direction == ProductAssetFilePageDirection::BACKWARD &&
        (anchorExclusive == nullptr || anchorExclusive[0] == '\0')) {
        return oc::type::Result<ProductAssetFileListResult>::err(
            {ErrorCode::INVALID_ARGUMENT, "missing backward page anchor"}
        );
    }

    for (uint8_t index = 0; index < capacity; ++index) {
        entries[index] = {};
    }

    ProductAssetFileListEntry anchor{};
    const bool hasAnchor =
        anchorExclusive != nullptr && anchorExclusive[0] != '\0';
    if (hasAnchor) {
        ProductAssetFilePaths paths{};
        if (!paths_(anchorExclusive, paths)) {
            return oc::type::Result<ProductAssetFileListResult>::err(
                {ErrorCode::INVALID_ARGUMENT, "invalid asset page anchor"}
            );
        }
        const auto info = files_.stat(paths.current);
        if (!info) {
            return oc::type::Result<ProductAssetFileListResult>::err(
                info.error()
            );
        }
        if (info.value().type != oc::interface::FileType::FILE ||
            !buildListEntry_(
                anchorExclusive,
                info.value().sizeBytes,
                anchor
            )) {
            return oc::type::Result<ProductAssetFileListResult>::err(
                {ErrorCode::INVALID_ARGUMENT, "missing asset page anchor"}
            );
        }
    }

    ListContext context{
        this,
        entries,
        capacity,
        anchor,
        hasAnchor,
        direction,
        0,
        {},
    };
    const auto listed = files_.list(
        layout_.directory,
        listVisitor_,
        &context
    );
    if (!listed) {
        return oc::type::Result<ProductAssetFileListResult>::err(
            listed.error()
        );
    }
    if (direction == ProductAssetFilePageDirection::FORWARD) {
        context.result.hasPrevious =
            hasAnchor &&
            context.result.totalCount > context.eligibleCount;
        context.result.hasNext =
            context.eligibleCount > context.result.count;
    } else {
        context.result.hasPrevious =
            context.eligibleCount > context.result.count;
        context.result.hasNext =
            hasAnchor &&
            context.result.totalCount > context.eligibleCount;
    }
    context.result.truncated =
        context.result.hasPrevious || context.result.hasNext;
    OC_PERF_UNITS(
        perfList,
        context.result.totalCount,
        context.result.count
    );
    return oc::type::Result<ProductAssetFileListResult>::ok(
        context.result
    );
}

FLASHMEM oc::type::Result<void> ProductAssetFileStore::nextAssetId(
    char* out,
    size_t outSize
) {
    if (out == nullptr || outSize == 0U || !layoutValid_()) {
        return invalid("invalid next asset id buffer");
    }
    for (uint16_t index = 1; index <= 999U; ++index) {
        char candidate[core::state::project::ProjectMetadata::ID_SIZE] = {};
        const int written = std::snprintf(
            candidate,
            sizeof(candidate),
            "%s%03u",
            layout_.generatedIdPrefix,
            static_cast<unsigned>(index)
        );
        if (written <= 0 ||
            static_cast<size_t>(written) >= sizeof(candidate) ||
            !validAssetId(candidate)) {
            return invalid("generated invalid asset id");
        }

        ProductAssetFilePaths paths{};
        if (!paths_(candidate, paths)) {
            return invalid("generated invalid asset path");
        }
        const auto info = files_.stat(paths.current);
        if (!info && info.error().code == ErrorCode::RESOURCE_NOT_FOUND) {
            if (std::strlen(candidate) >= outSize) {
                return invalid("next asset id output too small");
            }
            std::strncpy(out, candidate, outSize - 1U);
            out[outSize - 1U] = '\0';
            return oc::type::Result<void>::ok();
        }
        if (!info) return oc::type::Result<void>::err(info.error());
    }
    return oc::type::Result<void>::err(
        {ErrorCode::RESOURCE_EXHAUSTED, "asset id space exhausted"}
    );
}

FLASHMEM oc::type::Result<bool> ProductAssetFileStore::exists(
    const char* assetId
) {
    ProductAssetFilePaths paths{};
    if (!paths_(assetId, paths)) {
        return oc::type::Result<bool>::err(
            {ErrorCode::INVALID_ARGUMENT, "invalid asset id"}
        );
    }
    const auto info = files_.stat(paths.current);
    if (!info && info.error().code == ErrorCode::RESOURCE_NOT_FOUND) {
        return oc::type::Result<bool>::ok(false);
    }
    if (!info) return oc::type::Result<bool>::err(info.error());
    return oc::type::Result<bool>::ok(
        info.value().type == oc::interface::FileType::FILE
    );
}

}  // namespace core::persistence
