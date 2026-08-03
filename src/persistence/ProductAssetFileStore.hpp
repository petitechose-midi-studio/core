#pragma once

#include <cstddef>
#include <cstdint>

#include <oc/interface/IFileSystem.hpp>
#include <oc/type/Result.hpp>

#include "persistence/ProductFileService.hpp"
#include "state/project/ProjectState.hpp"

namespace core::persistence {

struct ProductAssetFileLayout {
    const char* directory = nullptr;
    const char* extension = nullptr;
    const char* generatedIdPrefix = nullptr;
    uint32_t maxFileSize = 0;
    uint32_t writeChunkSize = 0;
};

struct ProductAssetFilePaths {
    // Product asset IDs are bounded to 63 bytes and the registered library
    // roots are shorter than 32 bytes. Keep private sidecars independent from
    // the wider RPC path maximum so four cold paths do not consume 772 bytes
    // of scarce DTCM stack.
    static constexpr size_t PATH_SIZE = 128;

    char directory[PATH_SIZE] = {};
    char current[PATH_SIZE] = {};
    char backup[PATH_SIZE] = {};
    char tmp[PATH_SIZE] = {};
};

static_assert(
    ProductAssetFilePaths::PATH_SIZE <=
    oc::interface::FILESYSTEM_MAX_PATH_LENGTH + 1
);

struct ProductAssetFileTransferResult {
    uint32_t bytes = 0;
    char path[oc::interface::FILESYSTEM_MAX_PATH_LENGTH + 1] = {};
    char id[core::state::project::ProjectMetadata::ID_SIZE] = {};
};

struct ProductAssetFileListEntry {
    static constexpr size_t SEMANTIC_NAME_SIZE = 32;

    char id[core::state::project::ProjectMetadata::ID_SIZE] = {};
    char semanticName[SEMANTIC_NAME_SIZE] = {};
    uint32_t sizeBytes = 0;
    bool metadataReadable = false;
};

struct ProductAssetFileListResult {
    uint8_t count = 0;
    bool truncated = false;
    bool hasPrevious = false;
    bool hasNext = false;
    uint16_t totalCount = 0;
};

enum class ProductAssetFilePageDirection : uint8_t {
    FORWARD = 0,
    BACKWARD,
};

using ProductAssetMetadataReader = bool (*)(
    ProductFileService& files,
    const char* currentPath,
    const char* expectedAssetId,
    uint32_t fileSize,
    char* outSemanticName,
    size_t outSemanticNameSize
);

/**
 * Builds the public file and private atomic-write sidecar paths for one asset.
 * Asset-specific stores share this function so path and recovery semantics
 * cannot drift between libraries.
 */
bool buildProductAssetFilePaths(
    ProductAssetFileLayout layout,
    const char* assetId,
    ProductAssetFilePaths& out
);

/**
 * Allocation-free product asset store shared by Step and Chord libraries.
 *
 * The store retains no catalog: listPage scans the directory and keeps only
 * the requested sorted page. Asset-specific codecs are injected solely for
 * bounded metadata reads.
 */
class ProductAssetFileStore {
public:
    ProductAssetFileStore(
        ProductFileService& files,
        ProductAssetFileLayout layout,
        ProductAssetMetadataReader metadataReader
    );

    oc::type::Result<ProductAssetFileTransferResult> save(
        const char* assetId,
        const uint8_t* payload,
        uint32_t payloadSize
    );

    oc::type::Result<ProductAssetFileTransferResult> load(
        const char* assetId,
        uint8_t* outPayload,
        uint32_t outCapacity,
        uint32_t& outSize
    );

    oc::type::Result<void> remove(const char* assetId);

    oc::type::Result<ProductAssetFileListResult> list(
        ProductAssetFileListEntry* entries,
        uint8_t capacity
    );

    oc::type::Result<ProductAssetFileListResult> listPage(
        ProductAssetFileListEntry* entries,
        uint8_t capacity,
        const char* anchorExclusive,
        ProductAssetFilePageDirection direction
    );

    oc::type::Result<void> nextAssetId(char* out, size_t outSize);
    oc::type::Result<bool> exists(const char* assetId);

    static bool validAssetId(const char* assetId);

private:
    struct ListContext;

    static bool listVisitor_(
        const oc::interface::DirectoryEntry& entry,
        void* context
    );
    bool buildListEntry_(
        const char* assetId,
        uint32_t sizeBytes,
        ProductAssetFileListEntry& out
    );
    bool paths_(const char* assetId, ProductAssetFilePaths& out) const;
    bool layoutValid_() const;

    ProductFileService& files_;
    ProductAssetFileLayout layout_{};
    ProductAssetMetadataReader metadataReader_ = nullptr;
};

}  // namespace core::persistence
