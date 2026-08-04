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
    ProductDirectoryCatalog& catalog,
    ProductAssetFileLayout layout,
    ProductAssetMetadataReader metadataReader,
    ProductPersistenceJobOwner catalogOwner
) : files_(files),
    catalog_(catalog),
    layout_(layout),
    metadataReader_(metadataReader),
    catalog_owner_(catalogOwner) {}

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

FLASHMEM ProductDirectoryAssetQuery ProductAssetFileStore::catalogQuery_() const {
    return {
        .directory = layout_.directory,
        .extension = layout_.extension,
        .generatedIdPrefix = layout_.generatedIdPrefix,
        .maxFileSize = layout_.maxFileSize,
        .metadataReader = metadataReader_,
    };
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

    const bool hasAnchor =
        anchorExclusive != nullptr && anchorExclusive[0] != '\0';
    const auto query = catalogQuery_();
    const auto ready = catalog_.requestAssets(query, catalog_owner_);
    if (!ready) {
        return oc::type::Result<ProductAssetFileListResult>::err(ready.error());
    }

    uint16_t totalCount = 0U;
    const auto* catalogEntries = catalog_.assetEntries(query, totalCount);
    if (catalogEntries == nullptr) {
        return oc::type::Result<ProductAssetFileListResult>::err(
            {ErrorCode::HARDWARE_BUSY, "asset catalog not ready"}
        );
    }

    uint16_t anchorIndex = 0U;
    if (hasAnchor) {
        bool found = false;
        for (; anchorIndex < totalCount; ++anchorIndex) {
            if (std::strcmp(catalogEntries[anchorIndex].id, anchorExclusive) == 0) {
                found = true;
                break;
            }
        }
        if (!found) {
            return oc::type::Result<ProductAssetFileListResult>::err(
                {ErrorCode::INVALID_ARGUMENT, "missing asset page anchor"}
            );
        }
    }

    uint16_t begin = 0U;
    uint16_t end = totalCount;
    if (direction == ProductAssetFilePageDirection::FORWARD) {
        begin = hasAnchor ? static_cast<uint16_t>(anchorIndex + 1U) : 0U;
        const uint16_t remaining = static_cast<uint16_t>(totalCount - begin);
        end = static_cast<uint16_t>(
            begin + (remaining < capacity ? remaining : capacity)
        );
    } else {
        end = anchorIndex;
        begin = end > capacity ? static_cast<uint16_t>(end - capacity) : 0U;
    }

    ProductAssetFileListResult result{};
    result.totalCount = totalCount;
    for (uint16_t index = begin; index < end; ++index) {
        entries[result.count++] = catalogEntries[index];
    }
    result.hasPrevious = begin > 0U;
    result.hasNext = end < totalCount;
    result.truncated = result.hasPrevious || result.hasNext;
    OC_PERF_UNITS(
        perfList,
        result.totalCount,
        result.count
    );
    return oc::type::Result<ProductAssetFileListResult>::ok(result);
}

FLASHMEM oc::type::Result<void> ProductAssetFileStore::nextAssetId(
    char* out,
    size_t outSize
) {
    if (out == nullptr || outSize == 0U || !layoutValid_()) {
        return invalid("invalid next asset id buffer");
    }
    const auto query = catalogQuery_();
    const auto ready = catalog_.requestAssets(query, catalog_owner_);
    if (!ready) return ready;
    return catalog_.nextGeneratedId(query, out, outSize);
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
