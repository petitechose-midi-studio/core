#include "persistence/ProductDirectoryCatalog.hpp"

#include <cstdio>
#include <cstring>
#include <utility>

#include <config/PlatformCompat.hpp>

#include "config/TimeCompat.hpp"
#include "state/project/ProjectSlug.hpp"

namespace core::persistence {
namespace {

using oc::type::Error;
using oc::type::ErrorCode;

const char kInvalidDirectory[] PROGMEM = "invalid product catalog directory";
const char kInvalidQuery[] PROGMEM = "invalid product asset catalog query";
const char kInvalidOwner[] PROGMEM = "invalid product catalog job owner";
const char kCatalogPending[] PROGMEM = "product directory catalog pending";
const char kCatalogBusy[] PROGMEM = "product directory catalog busy";
const char kCatalogOverflow[] PROGMEM = "product directory exceeds 256 entries";
const char kCatalogMeasurementUnavailable[] PROGMEM =
    "product catalog measurement unavailable";
const char kCatalogAdvanceFailed[] PROGMEM = "product catalog advance failed";
const char kCatalogNotReady[] PROGMEM = "product directory catalog not ready";
const char kGeneratedIdBufferInvalid[] PROGMEM =
    "invalid generated asset id buffer";
const char kGeneratedIdExhausted[] PROGMEM = "asset id space exhausted";

FLASHMEM int compareTextCaseFolded(const char* lhs, const char* rhs) {
    size_t index = 0U;
    while (lhs[index] != '\0' && rhs[index] != '\0') {
        auto left = static_cast<unsigned char>(lhs[index]);
        auto right = static_cast<unsigned char>(rhs[index]);
        if (left >= 'A' && left <= 'Z') left = static_cast<unsigned char>(left + 32U);
        if (right >= 'A' && right <= 'Z') right = static_cast<unsigned char>(right + 32U);
        if (left != right) return left < right ? -1 : 1;
        ++index;
    }
    if (lhs[index] == rhs[index]) return 0;
    return lhs[index] == '\0' ? -1 : 1;
}

}  // namespace

FLASHMEM ProductDirectoryCatalog::ProductDirectoryCatalog(
    ProductFileService& files,
    NowProvider nowProvider,
    MicrosProvider microsProvider
) : files_(files)
  , now_provider_(nowProvider ? nowProvider : &core::time_compat::millis)
  , micros_provider_(microsProvider ? microsProvider : &core::time_compat::micros) {}

FLASHMEM ProductDirectoryCatalog::~ProductDirectoryCatalog() {
    invalidate();
}

FLASHMEM bool ProductDirectoryCatalog::validCatalogOwner_(
    ProductPersistenceJobOwner owner
) {
    return owner == ProductPersistenceJobOwner::PROJECT_CATALOG ||
           owner == ProductPersistenceJobOwner::STEP_PRESET_CATALOG ||
           owner == ProductPersistenceJobOwner::CHORD_PRESET_CATALOG;
}

FLASHMEM bool ProductDirectoryCatalog::sameText_(
    const char* lhs,
    const char* rhs
) {
    return lhs != nullptr && rhs != nullptr && std::strcmp(lhs, rhs) == 0;
}

FLASHMEM bool ProductDirectoryCatalog::queryValid_(
    const ProductDirectoryAssetQuery& query
) {
    return query.directory != nullptr && query.directory[0] != '\0' &&
           std::strlen(query.directory) <= oc::interface::FILESYSTEM_MAX_PATH_LENGTH &&
           query.extension != nullptr && query.extension[0] == '.' &&
           query.generatedIdPrefix != nullptr && query.generatedIdPrefix[0] != '\0' &&
           query.maxFileSize > 0U;
}

FLASHMEM bool ProductDirectoryCatalog::identityMatches_() const {
    return identity_ == files_.storageIdentity();
}

FLASHMEM bool ProductDirectoryCatalog::pathMatches_(const char* directory) const {
    return sameText_(directory_, directory);
}

FLASHMEM bool ProductDirectoryCatalog::queryMatches_(
    const ProductDirectoryAssetQuery& query
) const {
    return pathMatches_(query.directory) &&
           sameText_(query_.extension, query.extension) &&
           sameText_(query_.generatedIdPrefix, query.generatedIdPrefix) &&
           query_.maxFileSize == query.maxFileSize &&
           query_.metadataReader == query.metadataReader;
}

FLASHMEM bool ProductDirectoryCatalog::rawReadyFor_(
    const char* directory
) const {
    return identityMatches_() && pathMatches_(directory) &&
           (stage_ == Stage::READY_RAW || stage_ == Stage::READY_ASSETS);
}

FLASHMEM bool ProductDirectoryCatalog::assetsReadyFor_(
    const ProductDirectoryAssetQuery& query
) const {
    return identityMatches_() && stage_ == Stage::READY_ASSETS &&
           queryMatches_(query);
}

FLASHMEM bool ProductDirectoryCatalog::copyDirectory_(const char* directory) {
    if (directory == nullptr || directory[0] == '\0') return false;
    const size_t length = std::strlen(directory);
    if (length > oc::interface::FILESYSTEM_MAX_PATH_LENGTH) return false;
    std::memcpy(directory_, directory, length + 1U);
    return true;
}

FLASHMEM void ProductDirectoryCatalog::resetState_(bool keepRaw) {
    if (!keepRaw) {
        raw_count_ = 0U;
        directory_[0] = '\0';
    }
    enrich_index_ = 0U;
    asset_count_ = 0U;
    query_ = {};
    failure_ = {ErrorCode::OK, nullptr};
    raw_overflow_ = false;
    stage_ = keepRaw ? Stage::READY_RAW : Stage::IDLE;
}

FLASHMEM void ProductDirectoryCatalog::setFailure_(Error error) {
    failure_ = error;
    stage_ = Stage::FAILED;
}

FLASHMEM oc::type::Result<void> ProductDirectoryCatalog::admit_(
    ProductPersistenceJobOwner owner
) {
    if (!validCatalogOwner_(owner)) {
        return oc::type::Result<void>::err({ErrorCode::INVALID_ARGUMENT, kInvalidOwner});
    }
    auto admitted = files_.persistenceJobs().admit({
        .owner = owner,
        .nowMs = now_provider_ ? now_provider_() : 0U,
        .deadlineAfterMs = 0U,
        .quota = stage_ == Stage::ASSET_ENRICH
            ? PRODUCT_PERSISTENCE_QUOTA_ASSET_METADATA
            : PRODUCT_PERSISTENCE_QUOTA_RAW_CATALOG,
    });
    if (!admitted) return oc::type::Result<void>::err(admitted.error());
    job_ = std::move(admitted.value());
    return oc::type::Result<void>::ok();
}

FLASHMEM oc::type::Result<void> ProductDirectoryCatalog::requestRaw(
    const char* directory,
    ProductPersistenceJobOwner owner
) {
    if (directory == nullptr || directory[0] == '\0' ||
        std::strlen(directory) > oc::interface::FILESYSTEM_MAX_PATH_LENGTH) {
        return oc::type::Result<void>::err({ErrorCode::INVALID_ARGUMENT, kInvalidDirectory});
    }
    if (rawReadyFor_(directory)) return oc::type::Result<void>::ok();
    if (!identityMatches_() && stage_ != Stage::IDLE) invalidate();

    if (pending()) {
        return oc::type::Result<void>::err({
            ErrorCode::HARDWARE_BUSY,
            pathMatches_(directory) ? kCatalogPending : kCatalogBusy,
        });
    }
    if (stage_ == Stage::FAILED && pathMatches_(directory)) {
        return oc::type::Result<void>::err(failure_);
    }

    resetState_();
    if (!copyDirectory_(directory)) {
        return oc::type::Result<void>::err({ErrorCode::INVALID_ARGUMENT, kInvalidDirectory});
    }
    identity_ = files_.storageIdentity();
    stage_ = Stage::RAW_SCAN;
    const auto admitted = admit_(owner);
    if (!admitted) {
        resetState_();
        return admitted;
    }
    return oc::type::Result<void>::err({ErrorCode::HARDWARE_BUSY, kCatalogPending});
}

FLASHMEM oc::type::Result<void> ProductDirectoryCatalog::requestAssets(
    const ProductDirectoryAssetQuery& query,
    ProductPersistenceJobOwner owner
) {
    if (!queryValid_(query)) {
        return oc::type::Result<void>::err({ErrorCode::INVALID_ARGUMENT, kInvalidQuery});
    }
    if (assetsReadyFor_(query)) return oc::type::Result<void>::ok();
    if (!identityMatches_() && stage_ != Stage::IDLE) invalidate();

    if (pending()) {
        return oc::type::Result<void>::err({
            ErrorCode::HARDWARE_BUSY,
            queryMatches_(query) || pathMatches_(query.directory)
                ? kCatalogPending
                : kCatalogBusy,
        });
    }
    if (stage_ == Stage::FAILED && queryMatches_(query)) {
        return oc::type::Result<void>::err(failure_);
    }

    const bool keepRaw = rawReadyFor_(query.directory);
    resetState_(keepRaw);
    if (!keepRaw && !copyDirectory_(query.directory)) {
        return oc::type::Result<void>::err({ErrorCode::INVALID_ARGUMENT, kInvalidDirectory});
    }
    identity_ = files_.storageIdentity();
    query_ = query;
    stage_ = keepRaw ? Stage::ASSET_ENRICH : Stage::RAW_SCAN;
    const auto admitted = admit_(owner);
    if (!admitted) {
        resetState_(keepRaw);
        return admitted;
    }
    return oc::type::Result<void>::err({ErrorCode::HARDWARE_BUSY, kCatalogPending});
}

FLASHMEM bool ProductDirectoryCatalog::rawVisitor_(
    const oc::interface::DirectoryEntry& entry,
    void* context
) {
    auto* self = static_cast<ProductDirectoryCatalog*>(context);
    if (self == nullptr) return false;
    if (self->raw_count_ >= MAX_ENTRIES) {
        self->raw_overflow_ = true;
        return false;
    }
    self->raw_entries_[self->raw_count_++] = entry;
    return true;
}

FLASHMEM oc::type::Result<void> ProductDirectoryCatalog::scanRaw_(
    ProductPersistenceWorkMeasurement& measurement
) {
    raw_count_ = 0U;
    raw_overflow_ = false;
    const auto listed = files_.list(directory_, rawVisitor_, this);
    measurement.addBytes(
        static_cast<size_t>(raw_count_) * sizeof(oc::interface::DirectoryEntry)
    );
    if (!listed) return listed;
    if (raw_overflow_) {
        return oc::type::Result<void>::err({ErrorCode::RESOURCE_EXHAUSTED, kCatalogOverflow});
    }
    if (query_.directory != nullptr) {
        enrich_index_ = 0U;
        asset_count_ = 0U;
        stage_ = Stage::ASSET_ENRICH;
    } else {
        stage_ = Stage::READY_RAW;
    }
    return oc::type::Result<void>::ok();
}

FLASHMEM bool ProductDirectoryCatalog::rawEntryIsAsset_(
    const oc::interface::DirectoryEntry& entry,
    const ProductDirectoryAssetQuery& query,
    char* outId,
    size_t outIdSize
) {
    if (entry.type != oc::interface::FileType::FILE || entry.nameTruncated ||
        entry.sizeBytes == 0U || entry.sizeBytes > query.maxFileSize ||
        outId == nullptr || outIdSize == 0U) {
        return false;
    }
    const size_t nameLength = std::strlen(entry.name);
    const size_t extensionLength = std::strlen(query.extension);
    if (nameLength <= extensionLength ||
        std::strcmp(entry.name + nameLength - extensionLength, query.extension) != 0) {
        return false;
    }
    const size_t idLength = nameLength - extensionLength;
    if (idLength >= outIdSize) return false;
    std::memcpy(outId, entry.name, idLength);
    outId[idLength] = '\0';
    return core::state::project::validProjectSlug(outId);
}

FLASHMEM void ProductDirectoryCatalog::deriveSemanticName_(
    const char* assetId,
    char* out,
    size_t outSize
) {
    if (out == nullptr || outSize == 0U) return;
    out[0] = '\0';
    size_t written = 0U;
    bool capitalize = true;
    for (size_t index = 0U;
         assetId != nullptr && assetId[index] != '\0' && written + 1U < outSize;
         ++index) {
        char character = assetId[index];
        if (character == '-' || character == '_' || character == '.') {
            if (written > 0U && out[written - 1U] != ' ') out[written++] = ' ';
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

FLASHMEM int ProductDirectoryCatalog::compareAssets_(
    const ProductDirectoryAssetEntry& lhs,
    const ProductDirectoryAssetEntry& rhs
) {
    const int byName = compareTextCaseFolded(lhs.semanticName, rhs.semanticName);
    return byName != 0 ? byName : std::strcmp(lhs.id, rhs.id);
}

FLASHMEM void ProductDirectoryCatalog::insertAssetSorted_(
    const ProductDirectoryAssetEntry& entry
) {
    uint16_t insert = asset_count_;
    while (insert > 0U && compareAssets_(asset_entries_[insert - 1U], entry) > 0) {
        asset_entries_[insert] = asset_entries_[insert - 1U];
        --insert;
    }
    asset_entries_[insert] = entry;
    ++asset_count_;
}

FLASHMEM void ProductDirectoryCatalog::enrichOne_(
    ProductPersistenceWorkMeasurement& measurement
) {
    if (enrich_index_ >= raw_count_) {
        stage_ = Stage::READY_ASSETS;
        return;
    }

    const auto& raw = raw_entries_[enrich_index_++];
    measurement.addEntries(1U);
    char assetId[ProductDirectoryAssetEntry::ID_SIZE] = {};
    if (rawEntryIsAsset_(raw, query_, assetId, sizeof(assetId))) {
        ProductDirectoryAssetEntry candidate{};
        std::strncpy(candidate.id, assetId, sizeof(candidate.id) - 1U);
        candidate.sizeBytes = raw.sizeBytes;
        deriveSemanticName_(assetId, candidate.semanticName, sizeof(candidate.semanticName));

        if (query_.metadataReader != nullptr) {
            char path[oc::interface::FILESYSTEM_MAX_PATH_LENGTH + 1] = {};
            const int written = std::snprintf(
                path,
                sizeof(path),
                "%s/%s%s",
                query_.directory,
                assetId,
                query_.extension
            );
            char semanticName[ProductDirectoryAssetEntry::SEMANTIC_NAME_SIZE] = {};
            if (written > 0 && static_cast<size_t>(written) < sizeof(path) &&
                query_.metadataReader(
                    files_,
                    path,
                    assetId,
                    raw.sizeBytes,
                    semanticName,
                    sizeof(semanticName)
                )) {
                std::strncpy(
                    candidate.semanticName,
                    semanticName,
                    sizeof(candidate.semanticName) - 1U
                );
                candidate.metadataReadable = true;
            }
        }
        insertAssetSorted_(candidate);
    }

    if (enrich_index_ >= raw_count_) stage_ = Stage::READY_ASSETS;
}

void ProductDirectoryCatalog::advance(uint32_t nowMs, bool playbackActive) {
    if (!job_.valid() || playbackActive) return;
    advancePending_(nowMs);
}

FLASHMEM void ProductDirectoryCatalog::advancePending_(uint32_t nowMs) {
    auto& jobs = files_.persistenceJobs();
    if (!jobs.owns(job_)) {
        job_ = {};
        resetState_();
        return;
    }
    if (!identityMatches_()) {
        (void)jobs.cancel(job_);
        resetState_();
        return;
    }
    if (!jobs.isActive(job_)) return;

    const auto quota = stage_ == Stage::ASSET_ENRICH
        ? PRODUCT_PERSISTENCE_QUOTA_ASSET_METADATA
        : PRODUCT_PERSISTENCE_QUOTA_RAW_CATALOG;
    if (!jobs.prepareAdvance(job_, quota) || !jobs.claimAdvance(job_, nowMs)) return;

    ProductPersistenceWorkUsage usage{};
    const uint32_t startedMicros = micros_provider_ ? micros_provider_() : 0U;
    bool failed = false;
    auto measured = files_.measurePersistenceWork(usage);
    if (!measured) {
        setFailure_({ErrorCode::HARDWARE_BUSY, kCatalogMeasurementUnavailable});
        failed = true;
    } else {
        auto measurement = std::move(measured.value());
        if (stage_ == Stage::RAW_SCAN) {
            const auto scanned = scanRaw_(measurement);
            if (!scanned) {
                setFailure_(scanned.error());
                failed = true;
            }
        } else if (stage_ == Stage::ASSET_ENRICH) {
            enrichOne_(measurement);
        } else {
            setFailure_({ErrorCode::INVALID_STATE, kCatalogAdvanceFailed});
            failed = true;
        }
    }
    const uint32_t finishedMicros = micros_provider_ ? micros_provider_() : startedMicros;
    usage.wallMicros = static_cast<uint32_t>(finishedMicros - startedMicros);
    const auto finished = jobs.finishAdvance(job_, usage, true);
    if (!finished) {
        setFailure_(finished.error());
        failed = true;
    }

    const bool terminal = failed || stage_ == Stage::READY_RAW ||
                          stage_ == Stage::READY_ASSETS;
    if (terminal) (void)jobs.complete(job_);
}

FLASHMEM oc::type::Result<void> ProductDirectoryCatalog::prepareRawExternal(
    const char* directory,
    ProductPersistenceWorkMeasurement& measurement
) {
    if (directory == nullptr || directory[0] == '\0' ||
        std::strlen(directory) > oc::interface::FILESYSTEM_MAX_PATH_LENGTH) {
        return oc::type::Result<void>::err({ErrorCode::INVALID_ARGUMENT, kInvalidDirectory});
    }
    if (rawReadyFor_(directory)) return oc::type::Result<void>::ok();

    auto& jobs = files_.persistenceJobs();
    if (job_.valid() && jobs.owns(job_)) {
        const auto cancelled = jobs.cancel(job_);
        if (!cancelled) {
            return oc::type::Result<void>::err({ErrorCode::HARDWARE_BUSY, kCatalogBusy});
        }
    } else {
        job_ = {};
    }
    resetState_();
    if (!copyDirectory_(directory)) {
        return oc::type::Result<void>::err({ErrorCode::INVALID_ARGUMENT, kInvalidDirectory});
    }
    identity_ = files_.storageIdentity();
    stage_ = Stage::RAW_SCAN;
    const auto scanned = scanRaw_(measurement);
    if (!scanned) {
        setFailure_(scanned.error());
        return scanned;
    }
    return oc::type::Result<void>::ok();
}

FLASHMEM const oc::interface::DirectoryEntry*
ProductDirectoryCatalog::rawEntries(
    const char* directory,
    uint16_t& outCount
) const {
    outCount = 0U;
    if (!rawReadyFor_(directory)) return nullptr;
    outCount = raw_count_;
    return raw_entries_;
}

FLASHMEM const ProductDirectoryAssetEntry*
ProductDirectoryCatalog::assetEntries(
    const ProductDirectoryAssetQuery& query,
    uint16_t& outCount
) const {
    outCount = 0U;
    if (!assetsReadyFor_(query)) return nullptr;
    outCount = asset_count_;
    return asset_entries_;
}

FLASHMEM bool ProductDirectoryCatalog::rawContainsName_(const char* name) const {
    if (name == nullptr) return false;
    for (uint16_t index = 0U; index < raw_count_; ++index) {
        if (!raw_entries_[index].nameTruncated &&
            compareTextCaseFolded(raw_entries_[index].name, name) == 0) {
            return true;
        }
    }
    return false;
}

FLASHMEM oc::type::Result<void> ProductDirectoryCatalog::nextGeneratedId(
    const ProductDirectoryAssetQuery& query,
    char* out,
    size_t outSize
) const {
    if (out == nullptr || outSize == 0U || !queryValid_(query)) {
        return oc::type::Result<void>::err({
            ErrorCode::INVALID_ARGUMENT,
            kGeneratedIdBufferInvalid,
        });
    }
    if (!assetsReadyFor_(query)) {
        return oc::type::Result<void>::err({ErrorCode::HARDWARE_BUSY, kCatalogNotReady});
    }
    return nextGeneratedId(
        query.directory,
        query.generatedIdPrefix,
        query.extension,
        out,
        outSize
    );
}

FLASHMEM oc::type::Result<void> ProductDirectoryCatalog::nextGeneratedId(
    const char* directory,
    const char* generatedIdPrefix,
    const char* extension,
    char* out,
    size_t outSize
) const {
    if (out == nullptr || outSize == 0U || directory == nullptr ||
        generatedIdPrefix == nullptr || generatedIdPrefix[0] == '\0' ||
        extension == nullptr || extension[0] == '\0') {
        return oc::type::Result<void>::err({
            ErrorCode::INVALID_ARGUMENT,
            kGeneratedIdBufferInvalid,
        });
    }
    if (!rawReadyFor_(directory)) {
        return oc::type::Result<void>::err({ErrorCode::HARDWARE_BUSY, kCatalogNotReady});
    }
    for (uint16_t index = 1U; index <= 999U; ++index) {
        char id[ProductDirectoryAssetEntry::ID_SIZE] = {};
        const int idWritten = std::snprintf(
            id,
            sizeof(id),
            "%s%03u",
            generatedIdPrefix,
            static_cast<unsigned>(index)
        );
        char name[oc::interface::FILESYSTEM_MAX_NAME_LENGTH] = {};
        const int nameWritten = idWritten > 0
            ? std::snprintf(name, sizeof(name), "%s%s", id, extension)
            : -1;
        if (idWritten <= 0 || static_cast<size_t>(idWritten) >= sizeof(id) ||
            !core::state::project::validProjectSlug(id) ||
            nameWritten <= 0 || static_cast<size_t>(nameWritten) >= sizeof(name)) {
            return oc::type::Result<void>::err({
                ErrorCode::INVALID_ARGUMENT,
                kGeneratedIdBufferInvalid,
            });
        }
        if (rawContainsName_(name)) continue;
        if (static_cast<size_t>(idWritten) >= outSize) {
            return oc::type::Result<void>::err({
                ErrorCode::INVALID_ARGUMENT,
                kGeneratedIdBufferInvalid,
            });
        }
        std::memcpy(out, id, static_cast<size_t>(idWritten) + 1U);
        return oc::type::Result<void>::ok();
    }
    return oc::type::Result<void>::err({
        ErrorCode::RESOURCE_EXHAUSTED,
        kGeneratedIdExhausted,
    });
}

FLASHMEM void ProductDirectoryCatalog::invalidate() {
    if (job_.valid()) {
        auto& jobs = files_.persistenceJobs();
        if (jobs.owns(job_)) (void)jobs.cancel(job_);
        job_ = {};
    }
    resetState_();
}

FLASHMEM bool ProductDirectoryCatalog::pending() const {
    return stage_ == Stage::RAW_SCAN || stage_ == Stage::ASSET_ENRICH;
}

}  // namespace core::persistence
