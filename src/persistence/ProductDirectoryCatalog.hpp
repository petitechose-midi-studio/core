#pragma once

#include <cstddef>
#include <cstdint>

#include <oc/interface/IFileSystem.hpp>
#include <oc/type/Result.hpp>

#include "persistence/ProductFileService.hpp"

namespace core::persistence {

enum class ProductDirectoryAssetEntryKind : uint8_t {
    ASSET = 0,
    FOLDER,
};

struct ProductDirectoryAssetEntry {
    static constexpr size_t ID_SIZE = 64U;
    static constexpr size_t SEMANTIC_NAME_SIZE = 32U;

    char id[ID_SIZE] = {};
    char semanticName[SEMANTIC_NAME_SIZE] = {};
    char displayValue[12] = {};
    uint32_t sizeBytes = 0U;
    bool metadataReadable = false;
    ProductDirectoryAssetEntryKind kind =
        ProductDirectoryAssetEntryKind::ASSET;
};

using ProductDirectoryMetadataReader = bool (*)(
    ProductFileService& files,
    const char* currentPath,
    const char* expectedAssetId,
    uint32_t fileSize,
    char* outSemanticName,
    size_t outSemanticNameSize
);

struct ProductDirectoryAssetQuery {
    const char* directory = nullptr;
    const char* extension = nullptr;
    const char* generatedIdPrefix = nullptr;
    uint32_t maxFileSize = 0U;
    ProductDirectoryMetadataReader metadataReader = nullptr;
};

/**
 * One product-wide, stopped-only directory snapshot.
 *
 * The complete owner is allocated once in PSRAM. A backend directory is
 * enumerated at most once per storage identity and path, with entry 257
 * producing an explicit capacity error. Asset metadata is then enriched one
 * raw entry per admitted catalog turn. Pages and generated-ID selection are
 * allocation-free reads of the retained snapshot.
 */
class ProductDirectoryCatalog {
public:
    using NowProvider = uint32_t (*)();
    using MicrosProvider = uint32_t (*)();

    static constexpr uint16_t MAX_ENTRIES = 256U;
    static constexpr size_t RAW_SNAPSHOT_BYTES =
        sizeof(oc::interface::DirectoryEntry) * MAX_ENTRIES;

    explicit ProductDirectoryCatalog(
        ProductFileService& files,
        NowProvider nowProvider = nullptr,
        MicrosProvider microsProvider = nullptr
    );
    ~ProductDirectoryCatalog();

    ProductDirectoryCatalog(const ProductDirectoryCatalog&) = delete;
    ProductDirectoryCatalog& operator=(const ProductDirectoryCatalog&) = delete;
    ProductDirectoryCatalog(ProductDirectoryCatalog&&) = delete;
    ProductDirectoryCatalog& operator=(ProductDirectoryCatalog&&) = delete;

    oc::type::Result<void> requestRaw(
        const char* directory,
        ProductPersistenceJobOwner owner
    );
    oc::type::Result<void> requestAssets(
        const ProductDirectoryAssetQuery& query,
        ProductPersistenceJobOwner owner
    );

    /** Advance the internally admitted Project/Step/Chord catalog job. */
    void advance(uint32_t nowMs, bool playbackActive);

    /**
     * Populate/reuse the raw snapshot under a caller-owned admitted job.
     * Filesystem RPC uses this path so its explicit-priority token remains the
     * only scheduled record while the shared snapshot is filled.
     */
    oc::type::Result<void> prepareRawExternal(
        const char* directory,
        ProductPersistenceWorkMeasurement& measurement
    );

    const oc::interface::DirectoryEntry* rawEntries(
        const char* directory,
        uint16_t& outCount
    ) const;
    const ProductDirectoryAssetEntry* assetEntries(
        const ProductDirectoryAssetQuery& query,
        uint16_t& outCount
    ) const;

    oc::type::Result<void> nextGeneratedId(
        const ProductDirectoryAssetQuery& query,
        char* out,
        size_t outSize
    ) const;
    oc::type::Result<void> nextGeneratedId(
        const char* directory,
        const char* generatedIdPrefix,
        const char* extension,
        char* out,
        size_t outSize
    ) const;

    void invalidate();
    bool pending() const;

private:
    enum class Stage : uint8_t {
        IDLE = 0,
        RAW_SCAN,
        ASSET_ENRICH,
        READY_RAW,
        READY_ASSETS,
        FAILED,
    };

    static bool rawVisitor_(
        const oc::interface::DirectoryEntry& entry,
        void* context
    );
    static bool validCatalogOwner_(ProductPersistenceJobOwner owner);
    static bool queryValid_(const ProductDirectoryAssetQuery& query);
    static bool sameText_(const char* lhs, const char* rhs);
    static bool rawEntryIsAsset_(
        const oc::interface::DirectoryEntry& entry,
        const ProductDirectoryAssetQuery& query,
        char* outId,
        size_t outIdSize
    );
    static void deriveSemanticName_(
        const char* assetId,
        char* out,
        size_t outSize
    );
    static int compareAssets_(
        const ProductDirectoryAssetEntry& lhs,
        const ProductDirectoryAssetEntry& rhs
    );

    bool identityMatches_() const;
    bool pathMatches_(const char* directory) const;
    bool queryMatches_(const ProductDirectoryAssetQuery& query) const;
    bool rawReadyFor_(const char* directory) const;
    bool assetsReadyFor_(const ProductDirectoryAssetQuery& query) const;
    bool copyDirectory_(const char* directory);
    void resetState_(bool keepRaw = false);
    void setFailure_(oc::type::Error error);
    oc::type::Result<void> admit_(ProductPersistenceJobOwner owner);
    oc::type::Result<void> scanRaw_(
        ProductPersistenceWorkMeasurement& measurement
    );
    void enrichOne_(ProductPersistenceWorkMeasurement& measurement);
    void insertAssetSorted_(const ProductDirectoryAssetEntry& entry);
    bool rawContainsName_(const char* name) const;
    void advancePending_(uint32_t nowMs);

    ProductFileService& files_;
    NowProvider now_provider_ = nullptr;
    MicrosProvider micros_provider_ = nullptr;
    ProductPersistenceJobToken job_{};
    ProductStorageIdentity identity_{};
    ProductDirectoryAssetQuery query_{};
    oc::type::Error failure_{oc::type::ErrorCode::OK, nullptr};
    char directory_[oc::interface::FILESYSTEM_MAX_PATH_LENGTH + 1] = {};
    oc::interface::DirectoryEntry raw_entries_[MAX_ENTRIES] = {};
    ProductDirectoryAssetEntry asset_entries_[MAX_ENTRIES] = {};
    uint16_t raw_count_ = 0U;
    uint16_t enrich_index_ = 0U;
    uint16_t asset_count_ = 0U;
    Stage stage_ = Stage::IDLE;
    bool raw_overflow_ = false;
};

static_assert(
    ProductDirectoryCatalog::RAW_SNAPSHOT_BYTES == 19'456U,
    "raw directory snapshot contract drift"
);
static_assert(
    sizeof(ProductDirectoryCatalog) <= 64U * 1024U,
    "catalog/traversal PSRAM owner exceeds 64 KiB"
);

}  // namespace core::persistence
