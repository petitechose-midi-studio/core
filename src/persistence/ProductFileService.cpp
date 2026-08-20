#include "persistence/ProductFileService.hpp"

#include <cstring>
#include <limits>
#include <utility>

#include <config/PlatformCompat.hpp>

#include "diagnostics/StorageQualificationProbe.hpp"
#include "persistence/AtomicProductFile.hpp"

namespace core::persistence {

namespace {

using oc::type::ErrorCode;

constexpr const char* const kLayoutDirectories[] PROGMEM = {
    ProductFileService::PRODUCT_ROOT,
    ProductFileService::SESSION_DIR,
    ProductFileService::PROJECTS_DIR,
    ProductFileService::LIBRARY_DIR,
    ProductFileService::STEP_PRESETS_DIR,
    ProductFileService::CHORD_PRESETS_DIR,
    ProductFileService::PATTERN_PRESETS_DIR,
    ProductFileService::TMP_DIR,
};
static_assert(
    sizeof(kLayoutDirectories) / sizeof(kLayoutDirectories[0]) ==
        ProductFileService::LAYOUT_DIRECTORY_COUNT
);

const char kWorkMeasurementBusy[] PROGMEM =
    "product persistence work measurement already active";
const char kProjectWorkspaceBusy[] PROGMEM =
    "project workspace mutation active";
const char kProjectWorkspaceUnavailable[] PROGMEM =
    "project workspace allocation failed";
const char kProjectWorkspaceLeaseRequired[] PROGMEM =
    "project workspace requires project or recovery lease";

template <typename T, typename U>
void saturatingAccumulate(T& value, U increment) {
    const auto maximum = std::numeric_limits<T>::max();
    const uint64_t room = static_cast<uint64_t>(maximum) -
                          static_cast<uint64_t>(value);
    value = static_cast<uint64_t>(increment) > room
        ? maximum
        : static_cast<T>(value + static_cast<T>(increment));
}

oc::type::Result<void> invalidPath_(const char* context) {
    return oc::type::Result<void>::err({ErrorCode::INVALID_ARGUMENT, context});
}

oc::type::Result<void> staleLease_() {
    return oc::type::Result<void>::err(
        {ErrorCode::INVALID_STATE, "stale product mutation lease"}
    );
}

oc::type::Result<void> mediaUnavailable_() {
    return oc::type::Result<void>::err(
        {ErrorCode::HARDWARE_NOT_FOUND, "product storage unavailable"}
    );
}

template <typename Result>
uint8_t qualificationResultCode(const Result& result) {
    return static_cast<uint8_t>(
        result ? ErrorCode::OK : result.error().code
    );
}

uint32_t qualificationByteCount(size_t bytes) {
    return bytes > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(bytes);
}

#if defined(MS_STORAGE_QUALIFICATION) && OC_ENABLE_STATS
struct QualificationListVisitorContext {
    oc::interface::DirectoryEntryVisitor visitor = nullptr;
    void* context = nullptr;
    uint16_t entries = 0U;
};

bool qualificationListVisitor(
    const oc::interface::DirectoryEntry& entry,
    void* opaque
) {
    auto* measured = static_cast<QualificationListVisitorContext*>(opaque);
    if (!measured || !measured->visitor) return false;
    if (measured->entries != UINT16_MAX) ++measured->entries;
    return measured->visitor(entry, measured->context);
}
#endif

}  // namespace

FLASHMEM ProductPersistenceWorkMeasurement::~ProductPersistenceWorkMeasurement() {
    release_();
}

FLASHMEM ProductPersistenceWorkMeasurement::ProductPersistenceWorkMeasurement(
    ProductPersistenceWorkMeasurement&& other
) noexcept : service_(other.service_), usage_(other.usage_) {
    other.service_ = nullptr;
    other.usage_ = nullptr;
}

FLASHMEM ProductPersistenceWorkMeasurement&
ProductPersistenceWorkMeasurement::operator=(
    ProductPersistenceWorkMeasurement&& other
) noexcept {
    if (this != &other) {
        release_();
        service_ = other.service_;
        usage_ = other.usage_;
        other.service_ = nullptr;
        other.usage_ = nullptr;
    }
    return *this;
}

FLASHMEM void ProductPersistenceWorkMeasurement::addEntries(uint16_t count) {
    if (service_ && usage_) service_->noteEntries_(count);
}

FLASHMEM void ProductPersistenceWorkMeasurement::addBytes(size_t count) {
    if (service_ && usage_) service_->noteBytes_(count);
}

FLASHMEM void ProductPersistenceWorkMeasurement::addNodes(uint8_t count) {
    if (service_ && usage_) service_->noteNodes_(count);
}

FLASHMEM void ProductPersistenceWorkMeasurement::addAllocations(uint8_t count) {
    if (service_ && usage_) service_->noteAllocations_(count);
}

FLASHMEM void ProductPersistenceWorkMeasurement::release_() {
    if (service_ && usage_) service_->endWorkMeasurement_(usage_);
    service_ = nullptr;
    usage_ = nullptr;
}

FLASHMEM ProductFileService::ProductFileService(oc::interface::IFileSystem& filesystem)
    : filesystem_(filesystem) {}

FLASHMEM oc::type::Result<ProductPersistenceWorkMeasurement>
ProductFileService::measurePersistenceWork(ProductPersistenceWorkUsage& usage) {
    if (work_usage_) {
        return oc::type::Result<ProductPersistenceWorkMeasurement>::err(
            {ErrorCode::INVALID_STATE, kWorkMeasurementBusy}
        );
    }
    usage = {};
    work_usage_ = &usage;
    return oc::type::Result<ProductPersistenceWorkMeasurement>::ok(
        ProductPersistenceWorkMeasurement(*this, usage)
    );
}

FLASHMEM oc::type::Result<void> ProductFileService::init() {
    auto initialized = initBackend_();
    if (!initialized) return initialized;

    auto recovery = beginRecovery();
    if (!recovery) {
        return oc::type::Result<void>::err(recovery.error());
    }
    auto lease = std::move(recovery.value());

    auto layout = ensureLayout(lease);
    if (!layout) {
        if (coordinator_.owns(lease, ProductMutationOwner::RECOVERY)) {
            (void)completeRecovery(lease, false, layout.error().code);
        } else {
            lease.invalidate_();
        }
        return layout;
    }

    auto ordinary = recoverPendingProductFileTransaction(*this, lease);
    if (!ordinary) {
        const auto error = ordinary.error();
        if (coordinator_.owns(lease, ProductMutationOwner::RECOVERY)) {
            (void)completeRecovery(lease, false, error.code);
        } else {
            lease.invalidate_();
        }
        return oc::type::Result<void>::err(error);
    }

    auto completed = completeRecovery(lease, true);
    if (!completed) {
        return completed;
    }
    return oc::type::Result<void>::ok();
}

FLASHMEM oc::type::Result<void> ProductFileService::initForRecovery() {
    auto initialized = initBackend_();
    if (!initialized) return initialized;

    // A successful retry after an unavailable medium must preserve ABSENT
    // until beginRecovery() owns the transition. That transition advances the
    // media generation exactly once and acquires the recovery lease atomically.
    if (coordinator_.storageState() == ProductStorageState::ABSENT) {
        return oc::type::Result<void>::ok();
    }
    return coordinator_.requireRecovery(ErrorCode::OK);
}

FLASHMEM oc::type::Result<void> ProductFileService::initBackend_() {
    noteFilesystemCall_();
    const auto identity = coordinator_.identity();
    const uint32_t qualificationStarted =
        core::diagnostics::storage_qualification::beginStoragePrimitive(
            core::diagnostics::storage_qualification::OperationKind::StorageInit,
            identity.mediaGeneration,
            identity.storageEpoch
        );
    auto initialized = filesystem_.init();
    core::diagnostics::storage_qualification::endStoragePrimitive(
        qualificationStarted,
        core::diagnostics::storage_qualification::OperationKind::StorageInit,
        qualificationResultCode(initialized),
        identity.mediaGeneration,
        identity.storageEpoch
    );
    if (!initialized) {
        observeBackendFailure_(initialized.error());
        if (!filesystem_.available()) {
            markMediaUnavailable();
        }
        return initialized;
    }
    return oc::type::Result<void>::ok();
}

FLASHMEM bool ProductFileService::available() const {
    return filesystem_.available() &&
           coordinator_.storageState() == ProductStorageState::READY;
}

FLASHMEM bool ProductFileService::mediaPresent() const {
    return filesystem_.available();
}

FLASHMEM oc::type::Result<void> ProductFileService::prepareProjectWorkspace() {
    if (coordinator_.mutationActive()) {
        return oc::type::Result<void>::err(
            {ErrorCode::HARDWARE_BUSY, kProjectWorkspaceBusy}
        );
    }
    if (!project_workspace_.prepare()) {
        return oc::type::Result<void>::err(
            {ErrorCode::RESOURCE_EXHAUSTED, kProjectWorkspaceUnavailable}
        );
    }
    return oc::type::Result<void>::ok();
}

FLASHMEM oc::type::Result<ProjectFileReadWorkspace*>
ProductFileService::projectReadWorkspace(const ProductMutationLease& lease) {
    if (!ownsProjectWorkspaceLease_(lease)) {
        return oc::type::Result<ProjectFileReadWorkspace*>::err(
            {ErrorCode::INVALID_STATE, kProjectWorkspaceLeaseRequired}
        );
    }
    return oc::type::Result<ProjectFileReadWorkspace*>::ok(
        &project_workspace_.read()
    );
}

FLASHMEM oc::type::Result<ProjectFileWriteWorkspace*>
ProductFileService::projectWriteWorkspace(const ProductMutationLease& lease) {
    if (!ownsProjectWorkspaceLease_(lease)) {
        return oc::type::Result<ProjectFileWriteWorkspace*>::err(
            {ErrorCode::INVALID_STATE, kProjectWorkspaceLeaseRequired}
        );
    }
    return oc::type::Result<ProjectFileWriteWorkspace*>::ok(
        &project_workspace_.write()
    );
}

FLASHMEM bool ProductFileService::ownsProjectWorkspaceLease_(
    const ProductMutationLease& lease
) const {
    return coordinator_.owns(lease, ProductMutationOwner::PROJECT) ||
           coordinator_.owns(lease, ProductMutationOwner::RECOVERY);
}

FLASHMEM oc::type::Result<ProductMutationLease> ProductFileService::acquireMutation(
    ProductMutationOwner owner
) {
    if (coordinator_.storageState() == ProductStorageState::EXHAUSTED) {
        return coordinator_.acquireMutation(owner);
    }
    if (!filesystem_.available()) {
        markMediaUnavailable();
        return oc::type::Result<ProductMutationLease>::err(
            {ErrorCode::HARDWARE_NOT_FOUND, "product storage unavailable"}
        );
    }
    return coordinator_.acquireMutation(owner);
}

FLASHMEM oc::type::Result<ProductMutationLease> ProductFileService::beginRecovery() {
    if (coordinator_.storageState() == ProductStorageState::EXHAUSTED) {
        return coordinator_.beginRecovery();
    }
    if (!filesystem_.available()) {
        markMediaUnavailable();
        return oc::type::Result<ProductMutationLease>::err(
            {ErrorCode::HARDWARE_NOT_FOUND, "product storage unavailable"}
        );
    }
    return coordinator_.beginRecovery();
}

FLASHMEM oc::type::Result<void> ProductFileService::releaseMutation(
    ProductMutationLease& lease
) {
    if (write_lease_id_ != 0 && coordinator_.owns(lease) &&
        write_lease_id_ == lease.id_) {
        noteFilesystemCall_();
        const auto identity = coordinator_.identity();
        const uint32_t qualificationStarted =
            core::diagnostics::storage_qualification::beginStoragePrimitive(
                core::diagnostics::storage_qualification::OperationKind::AbortWrite,
                identity.mediaGeneration,
                identity.storageEpoch
            );
        filesystem_.abortWrite();
        core::diagnostics::storage_qualification::endStoragePrimitive(
            qualificationStarted,
            core::diagnostics::storage_qualification::OperationKind::AbortWrite,
            static_cast<uint8_t>(ErrorCode::OK),
            identity.mediaGeneration,
            identity.storageEpoch
        );
        write_lease_id_ = 0;
    }
    return coordinator_.releaseMutation(lease);
}

FLASHMEM oc::type::Result<void> ProductFileService::completeRecovery(
    ProductMutationLease& lease,
    bool success,
    ErrorCode errorCode
) {
    if (!filesystem_.available() || errorCode == ErrorCode::HARDWARE_NOT_FOUND) {
        markMediaUnavailable();
        lease.invalidate_();
        return mediaUnavailable_();
    }
    return coordinator_.completeRecovery(lease, success, errorCode);
}

FLASHMEM oc::type::Result<void> ProductFileService::requireRecovery(ErrorCode errorCode) {
    return coordinator_.requireRecovery(errorCode);
}

FLASHMEM oc::type::Result<void> ProductFileService::requireRecovery(
    const ProductMutationLease& lease,
    ErrorCode errorCode
) {
    return coordinator_.requireRecovery(lease, errorCode);
}

FLASHMEM void ProductFileService::markMediaUnavailable() {
    job_coordinator_.invalidateAll();
    if (write_lease_id_ != 0) {
        noteFilesystemCall_();
        const auto identity = coordinator_.identity();
        const uint32_t qualificationStarted =
            core::diagnostics::storage_qualification::beginStoragePrimitive(
                core::diagnostics::storage_qualification::OperationKind::AbortWrite,
                identity.mediaGeneration,
                identity.storageEpoch
            );
        filesystem_.abortWrite();
        core::diagnostics::storage_qualification::endStoragePrimitive(
            qualificationStarted,
            core::diagnostics::storage_qualification::OperationKind::AbortWrite,
            static_cast<uint8_t>(ErrorCode::OK),
            identity.mediaGeneration,
            identity.storageEpoch
        );
        write_lease_id_ = 0;
    }
    coordinator_.markMediaUnavailable();
}

FLASHMEM oc::type::Result<void> ProductFileService::ensureLayout(
    const ProductMutationLease& lease
) {
    for (uint8_t index = 0U; index < LAYOUT_DIRECTORY_COUNT; ++index) {
        auto result = ensureLayoutDirectory(lease, index);
        if (!result) {
            return result;
        }
    }
    return oc::type::Result<void>::ok();
}

FLASHMEM oc::type::Result<void> ProductFileService::ensureLayoutDirectory(
    const ProductMutationLease& lease,
    uint8_t index
) {
    if (index >= LAYOUT_DIRECTORY_COUNT) {
        return oc::type::Result<void>::err(
            {ErrorCode::INVALID_ARGUMENT, "product layout directory index"}
        );
    }
    return createDirectory(lease, kLayoutDirectories[index]);
}

FLASHMEM oc::type::Result<void> ProductFileService::resolvePath(
    const char* productPath,
    char* outPath,
    size_t outPathSize
) const {
    if (!productPath || !outPath || outPathSize == 0) {
        return invalidPath_("invalid product path buffer");
    }

    const size_t rootLength = std::strlen(PRODUCT_ROOT);
    if (rootLength + 1 > outPathSize) {
        return invalidPath_("product path buffer too small");
    }

    std::memcpy(outPath, PRODUCT_ROOT, rootLength);
    size_t write = rootLength;
    outPath[write] = '\0';

    size_t read = 0;
    while (productPath[read] == '/') {
        ++read;
    }

    if (isProductRootSegment_(productPath, read)) {
        read += rootLength - 1;
        while (productPath[read] == '/') {
            ++read;
        }
    }

    while (productPath[read] != '\0') {
        size_t segmentStart = read;
        size_t segmentLength = 0;
        while (productPath[read] != '\0' && productPath[read] != '/') {
            const char c = productPath[read];
            const auto byte = static_cast<unsigned char>(c);
            if (c == '\\' || c == ':' || byte < 32U || byte == 127U) {
                return invalidPath_("invalid product path character");
            }
            ++segmentLength;
            ++read;
        }

        if (segmentLength > oc::interface::FILESYSTEM_MAX_NAME_LENGTH) {
            return invalidPath_("product path segment too long");
        }
        if ((segmentLength == 1 && productPath[segmentStart] == '.') ||
            (segmentLength == 2 && productPath[segmentStart] == '.' &&
             productPath[segmentStart + 1] == '.')) {
            return invalidPath_("dot product path segment not allowed");
        }

        if (segmentLength > 0) {
            if (write + 1 + segmentLength + 1 > outPathSize) {
                return invalidPath_("product path too long");
            }
            outPath[write++] = '/';
            std::memcpy(outPath + write, productPath + segmentStart, segmentLength);
            write += segmentLength;
            outPath[write] = '\0';
        }

        while (productPath[read] == '/') {
            ++read;
        }
    }

    return oc::type::Result<void>::ok();
}

FLASHMEM oc::type::Result<oc::interface::FileInfo> ProductFileService::stat(
    const char* productPath
) {
    return stat_(nullptr, productPath);
}

FLASHMEM oc::type::Result<oc::interface::FileInfo> ProductFileService::stat(
    const ProductMutationLease& lease,
    const char* productPath
) {
    return stat_(&lease, productPath);
}

FLASHMEM oc::type::Result<oc::interface::FileInfo> ProductFileService::stat_(
    const ProductMutationLease* lease,
    const char* productPath
) {
    auto readable = ensureReadable_(lease);
    if (!readable) {
        return oc::type::Result<oc::interface::FileInfo>::err(readable.error());
    }
    char path[PATH_BUFFER_SIZE] = {};
    auto pathResult = resolvePath(productPath, path, sizeof(path));
    if (!pathResult) {
        return oc::type::Result<oc::interface::FileInfo>::err(pathResult.error());
    }
    noteFilesystemCall_();
    const auto identity = coordinator_.identity();
    const uint32_t qualificationStarted =
        core::diagnostics::storage_qualification::beginStoragePrimitive(
            core::diagnostics::storage_qualification::OperationKind::Stat,
            identity.mediaGeneration,
            identity.storageEpoch
        );
    auto result = filesystem_.stat(path);
    core::diagnostics::storage_qualification::endStoragePrimitive(
        qualificationStarted,
        core::diagnostics::storage_qualification::OperationKind::Stat,
        qualificationResultCode(result),
        identity.mediaGeneration,
        identity.storageEpoch
    );
    if (!result) {
        observeBackendFailure_(result.error());
    }
    return result;
}

FLASHMEM oc::type::Result<void> ProductFileService::list(
    const char* productPath,
    oc::interface::DirectoryEntryVisitor visitor,
    void* context
) {
    return list_(nullptr, productPath, visitor, context);
}

FLASHMEM oc::type::Result<void> ProductFileService::list(
    const ProductMutationLease& lease,
    const char* productPath,
    oc::interface::DirectoryEntryVisitor visitor,
    void* context
) {
    return list_(&lease, productPath, visitor, context);
}

FLASHMEM oc::type::Result<void> ProductFileService::list_(
    const ProductMutationLease* lease,
    const char* productPath,
    oc::interface::DirectoryEntryVisitor visitor,
    void* context
) {
    auto readable = ensureReadable_(lease);
    if (!readable) {
        return readable;
    }
    char path[PATH_BUFFER_SIZE] = {};
    auto pathResult = resolvePath(productPath, path, sizeof(path));
    if (!pathResult) {
        return pathResult;
    }
    noteFilesystemCall_();
    MeasuredListVisitorContext measuredContext{this, visitor, context};
    const auto identity = coordinator_.identity();
    const uint32_t qualificationStarted =
        core::diagnostics::storage_qualification::beginStoragePrimitive(
            core::diagnostics::storage_qualification::OperationKind::List,
            identity.mediaGeneration,
            identity.storageEpoch
        );
    auto effectiveVisitor = work_usage_
        ? &ProductFileService::measuredListVisitor_
        : visitor;
    void* effectiveContext = work_usage_
        ? static_cast<void*>(&measuredContext)
        : context;
    uint16_t qualificationEntries = 0U;
#if defined(MS_STORAGE_QUALIFICATION) && OC_ENABLE_STATS
    QualificationListVisitorContext qualificationContext{
        effectiveVisitor,
        effectiveContext,
        0U,
    };
    auto result = filesystem_.list(
        path,
        &qualificationListVisitor,
        &qualificationContext
    );
    qualificationEntries = qualificationContext.entries;
#else
    auto result = filesystem_.list(path, effectiveVisitor, effectiveContext);
#endif
    core::diagnostics::storage_qualification::endStoragePrimitive(
        qualificationStarted,
        core::diagnostics::storage_qualification::OperationKind::List,
        qualificationResultCode(result),
        identity.mediaGeneration,
        identity.storageEpoch,
        0U,
        qualificationEntries
    );
    if (!result) {
        observeBackendFailure_(result.error());
    }
    return result;
}

FLASHMEM oc::type::Result<void> ProductFileService::createDirectory(
    const ProductMutationLease& lease,
    const char* productPath
) {
    auto allowed = ensureMutationLease_(lease);
    if (!allowed) {
        return allowed;
    }
    char path[PATH_BUFFER_SIZE] = {};
    auto pathResult = resolvePath(productPath, path, sizeof(path));
    if (!pathResult) {
        return pathResult;
    }

    noteFilesystemCall_();
    const auto identity = coordinator_.identity();
    const uint32_t statQualificationStarted =
        core::diagnostics::storage_qualification::beginStoragePrimitive(
            core::diagnostics::storage_qualification::OperationKind::Stat,
            identity.mediaGeneration,
            identity.storageEpoch
        );
    auto existing = filesystem_.stat(path);
    core::diagnostics::storage_qualification::endStoragePrimitive(
        statQualificationStarted,
        core::diagnostics::storage_qualification::OperationKind::Stat,
        qualificationResultCode(existing),
        identity.mediaGeneration,
        identity.storageEpoch
    );
    if (existing) {
        if (existing.value().type == oc::interface::FileType::DIRECTORY) {
            return oc::type::Result<void>::ok();
        }
        return oc::type::Result<void>::err(
            {ErrorCode::INVALID_STATE, "product directory path is not a directory"}
        );
    }
    if (existing.error().code != ErrorCode::RESOURCE_NOT_FOUND) {
        observeBackendFailure_(existing.error());
        return oc::type::Result<void>::err(existing.error());
    }

    noteFilesystemCall_();
    const uint32_t createQualificationStarted =
        core::diagnostics::storage_qualification::beginStoragePrimitive(
            core::diagnostics::storage_qualification::OperationKind::CreateDirectory,
            identity.mediaGeneration,
            identity.storageEpoch
        );
    auto result = filesystem_.createDirectory(path);
    core::diagnostics::storage_qualification::endStoragePrimitive(
        createQualificationStarted,
        core::diagnostics::storage_qualification::OperationKind::CreateDirectory,
        qualificationResultCode(result),
        identity.mediaGeneration,
        identity.storageEpoch
    );
    if (!result) {
        observeBackendFailure_(result.error());
        return result;
    }
    auto touched = coordinator_.noteMutation(lease);
    return touched ? oc::type::Result<void>::ok() : touched;
}

FLASHMEM oc::type::Result<void> ProductFileService::remove(
    const ProductMutationLease& lease,
    const char* productPath,
    oc::interface::RemoveMode mode
) {
    auto allowed = ensureMutationLease_(lease);
    if (!allowed) {
        return allowed;
    }
    char path[PATH_BUFFER_SIZE] = {};
    auto pathResult = resolvePath(productPath, path, sizeof(path));
    if (!pathResult) {
        return pathResult;
    }
    if (isProductRootPath_(path)) {
        return invalidPath_("cannot remove product root");
    }
    if (mode == oc::interface::RemoveMode::RECURSIVE) {
        return invalidPath_("recursive remove requires cooperative tree cleanup");
    }
    noteFilesystemCall_();
    const auto identity = coordinator_.identity();
    const uint32_t qualificationStarted =
        core::diagnostics::storage_qualification::beginStoragePrimitive(
            core::diagnostics::storage_qualification::OperationKind::Remove,
            identity.mediaGeneration,
            identity.storageEpoch
        );
    auto result = filesystem_.remove(
        path,
        oc::interface::RemoveMode::FILE_OR_EMPTY_DIRECTORY
    );
    core::diagnostics::storage_qualification::endStoragePrimitive(
        qualificationStarted,
        core::diagnostics::storage_qualification::OperationKind::Remove,
        qualificationResultCode(result),
        identity.mediaGeneration,
        identity.storageEpoch
    );
    if (!result) {
        observeBackendFailure_(result.error());
        return result;
    }
    auto touched = coordinator_.noteMutation(lease);
    return touched ? oc::type::Result<void>::ok() : touched;
}

FLASHMEM oc::type::Result<void> ProductFileService::rename(
    const ProductMutationLease& lease,
    const char* fromProductPath,
    const char* toProductPath
) {
    auto allowed = ensureMutationLease_(lease);
    if (!allowed) {
        return allowed;
    }
    char fromPath[PATH_BUFFER_SIZE] = {};
    auto fromResult = resolvePath(fromProductPath, fromPath, sizeof(fromPath));
    if (!fromResult) {
        return fromResult;
    }
    if (isProductRootPath_(fromPath)) {
        return invalidPath_("cannot rename product root");
    }

    char toPath[PATH_BUFFER_SIZE] = {};
    auto toResult = resolvePath(toProductPath, toPath, sizeof(toPath));
    if (!toResult) {
        return toResult;
    }
    if (isProductRootPath_(toPath)) {
        return invalidPath_("cannot rename to product root");
    }

    noteFilesystemCall_();
    const auto identity = coordinator_.identity();
    const uint32_t qualificationStarted =
        core::diagnostics::storage_qualification::beginStoragePrimitive(
            core::diagnostics::storage_qualification::OperationKind::Rename,
            identity.mediaGeneration,
            identity.storageEpoch
        );
    auto result = filesystem_.rename(fromPath, toPath);
    core::diagnostics::storage_qualification::endStoragePrimitive(
        qualificationStarted,
        core::diagnostics::storage_qualification::OperationKind::Rename,
        qualificationResultCode(result),
        identity.mediaGeneration,
        identity.storageEpoch
    );
    if (!result) {
        observeBackendFailure_(result.error());
        return result;
    }
    auto touched = coordinator_.noteMutation(lease);
    return touched ? oc::type::Result<void>::ok() : touched;
}

FLASHMEM oc::type::Result<size_t> ProductFileService::read(
    const char* productPath,
    uint32_t offset,
    uint8_t* buffer,
    size_t size
) {
    return read_(nullptr, productPath, offset, buffer, size);
}

FLASHMEM oc::type::Result<size_t> ProductFileService::read(
    const ProductMutationLease& lease,
    const char* productPath,
    uint32_t offset,
    uint8_t* buffer,
    size_t size
) {
    return read_(&lease, productPath, offset, buffer, size);
}

FLASHMEM oc::type::Result<size_t> ProductFileService::read_(
    const ProductMutationLease* lease,
    const char* productPath,
    uint32_t offset,
    uint8_t* buffer,
    size_t size
) {
    auto readable = ensureReadable_(lease);
    if (!readable) {
        return oc::type::Result<size_t>::err(readable.error());
    }
    char path[PATH_BUFFER_SIZE] = {};
    auto pathResult = resolvePath(productPath, path, sizeof(path));
    if (!pathResult) {
        return oc::type::Result<size_t>::err(pathResult.error());
    }
    noteFilesystemCall_();
    const auto identity = coordinator_.identity();
    const uint32_t qualificationStarted =
        core::diagnostics::storage_qualification::beginStoragePrimitive(
            core::diagnostics::storage_qualification::OperationKind::Read,
            identity.mediaGeneration,
            identity.storageEpoch
        );
    auto result = filesystem_.read(path, offset, buffer, size);
    core::diagnostics::storage_qualification::endStoragePrimitive(
        qualificationStarted,
        core::diagnostics::storage_qualification::OperationKind::Read,
        qualificationResultCode(result),
        identity.mediaGeneration,
        identity.storageEpoch,
        result ? qualificationByteCount(result.value()) : 0U
    );
    if (!result) {
        observeBackendFailure_(result.error());
    } else {
        noteBytes_(result.value());
    }
    return result;
}

FLASHMEM oc::type::Result<size_t> ProductFileService::write(
    const ProductMutationLease& lease,
    const char* productPath,
    uint32_t offset,
    const uint8_t* data,
    size_t size
) {
    auto allowed = ensureMutationLease_(lease);
    if (!allowed) {
        return oc::type::Result<size_t>::err(allowed.error());
    }
    char path[PATH_BUFFER_SIZE] = {};
    auto pathResult = resolvePath(productPath, path, sizeof(path));
    if (!pathResult) {
        return oc::type::Result<size_t>::err(pathResult.error());
    }
    if (isProductRootPath_(path)) {
        return oc::type::Result<size_t>::err(
            {ErrorCode::INVALID_ARGUMENT, "cannot write product root"}
        );
    }
    if (size == 0) {
        return oc::type::Result<size_t>::ok(0);
    }
    noteFilesystemCall_();
    const auto identity = coordinator_.identity();
    const uint32_t qualificationStarted =
        core::diagnostics::storage_qualification::beginStoragePrimitive(
            core::diagnostics::storage_qualification::OperationKind::Write,
            identity.mediaGeneration,
            identity.storageEpoch
        );
    auto result = filesystem_.write(path, offset, data, size);
    core::diagnostics::storage_qualification::endStoragePrimitive(
        qualificationStarted,
        core::diagnostics::storage_qualification::OperationKind::Write,
        qualificationResultCode(result),
        identity.mediaGeneration,
        identity.storageEpoch,
        result ? qualificationByteCount(result.value()) : 0U
    );
    if (!result) {
        observeBackendFailure_(result.error());
        return result;
    }
    auto touched = coordinator_.noteMutation(lease);
    if (!touched) {
        return oc::type::Result<size_t>::err(touched.error());
    }
    noteBytes_(result.value());
    return result;
}

FLASHMEM oc::type::Result<void> ProductFileService::flush(
    const ProductMutationLease& lease,
    const char* productPath
) {
    auto allowed = ensureMutationLease_(lease);
    if (!allowed) {
        return allowed;
    }
    char path[PATH_BUFFER_SIZE] = {};
    auto pathResult = resolvePath(productPath, path, sizeof(path));
    if (!pathResult) {
        return pathResult;
    }
    noteFilesystemCall_();
    const auto identity = coordinator_.identity();
    const uint32_t qualificationStarted =
        core::diagnostics::storage_qualification::beginStoragePrimitive(
            core::diagnostics::storage_qualification::OperationKind::Flush,
            identity.mediaGeneration,
            identity.storageEpoch
        );
    auto result = filesystem_.flush(path);
    core::diagnostics::storage_qualification::endStoragePrimitive(
        qualificationStarted,
        core::diagnostics::storage_qualification::OperationKind::Flush,
        qualificationResultCode(result),
        identity.mediaGeneration,
        identity.storageEpoch
    );
    if (!result) {
        observeBackendFailure_(result.error());
        return result;
    }
    auto touched = coordinator_.noteMutation(lease);
    return touched ? oc::type::Result<void>::ok() : touched;
}

FLASHMEM oc::type::Result<void> ProductFileService::beginWrite(
    const ProductMutationLease& lease,
    const char* productPath,
    uint32_t expectedSize
) {
    auto allowed = ensureMutationLease_(lease);
    if (!allowed) {
        return allowed;
    }
    if (write_lease_id_ != 0) {
        return oc::type::Result<void>::err(
            {ErrorCode::INVALID_STATE, "write session already active"}
        );
    }
    char path[PATH_BUFFER_SIZE] = {};
    auto pathResult = resolvePath(productPath, path, sizeof(path));
    if (!pathResult) {
        return pathResult;
    }
    if (isProductRootPath_(path)) {
        return oc::type::Result<void>::err(
            {ErrorCode::INVALID_ARGUMENT, "cannot write product root"}
        );
    }
    noteFilesystemCall_();
    const auto identity = coordinator_.identity();
    const uint32_t qualificationStarted =
        core::diagnostics::storage_qualification::beginStoragePrimitive(
            core::diagnostics::storage_qualification::OperationKind::BeginWrite,
            identity.mediaGeneration,
            identity.storageEpoch
        );
    auto result = filesystem_.beginWrite(path, expectedSize);
    core::diagnostics::storage_qualification::endStoragePrimitive(
        qualificationStarted,
        core::diagnostics::storage_qualification::OperationKind::BeginWrite,
        qualificationResultCode(result),
        identity.mediaGeneration,
        identity.storageEpoch,
        expectedSize
    );
    if (!result) {
        observeBackendFailure_(result.error());
        return result;
    }
    write_lease_id_ = lease.id_;
    auto touched = coordinator_.noteMutation(lease);
    if (!touched) {
        noteFilesystemCall_();
        const uint32_t abortQualificationStarted =
            core::diagnostics::storage_qualification::beginStoragePrimitive(
                core::diagnostics::storage_qualification::OperationKind::AbortWrite,
                identity.mediaGeneration,
                identity.storageEpoch
            );
        filesystem_.abortWrite();
        core::diagnostics::storage_qualification::endStoragePrimitive(
            abortQualificationStarted,
            core::diagnostics::storage_qualification::OperationKind::AbortWrite,
            static_cast<uint8_t>(ErrorCode::OK),
            identity.mediaGeneration,
            identity.storageEpoch
        );
        write_lease_id_ = 0;
        return touched;
    }
    return oc::type::Result<void>::ok();
}

FLASHMEM oc::type::Result<size_t> ProductFileService::appendWrite(
    const ProductMutationLease& lease,
    const uint8_t* data,
    size_t size
) {
    auto allowed = ensureMutationLease_(lease);
    if (!allowed) {
        return oc::type::Result<size_t>::err(allowed.error());
    }
    if (write_lease_id_ == 0 || write_lease_id_ != lease.id_) {
        return oc::type::Result<size_t>::err(
            {ErrorCode::INVALID_STATE, "write session is not owned by lease"}
        );
    }
    noteFilesystemCall_();
    const auto identity = coordinator_.identity();
    const uint32_t qualificationStarted =
        core::diagnostics::storage_qualification::beginStoragePrimitive(
            core::diagnostics::storage_qualification::OperationKind::AppendWrite,
            identity.mediaGeneration,
            identity.storageEpoch
        );
    auto result = filesystem_.appendWrite(data, size);
    core::diagnostics::storage_qualification::endStoragePrimitive(
        qualificationStarted,
        core::diagnostics::storage_qualification::OperationKind::AppendWrite,
        qualificationResultCode(result),
        identity.mediaGeneration,
        identity.storageEpoch,
        result ? qualificationByteCount(result.value()) : 0U
    );
    if (!result) {
        observeBackendFailure_(result.error());
        return result;
    }
    if (size > 0) {
        auto touched = coordinator_.noteMutation(lease);
        if (!touched) {
            return oc::type::Result<size_t>::err(touched.error());
        }
    }
    noteBytes_(result.value());
    return result;
}

FLASHMEM oc::type::Result<void> ProductFileService::finishWrite(
    const ProductMutationLease& lease
) {
    auto allowed = ensureMutationLease_(lease);
    if (!allowed) {
        return allowed;
    }
    if (write_lease_id_ == 0 || write_lease_id_ != lease.id_) {
        return oc::type::Result<void>::err(
            {ErrorCode::INVALID_STATE, "write session is not owned by lease"}
        );
    }
    noteFilesystemCall_();
    const auto identity = coordinator_.identity();
    const uint32_t qualificationStarted =
        core::diagnostics::storage_qualification::beginStoragePrimitive(
            core::diagnostics::storage_qualification::OperationKind::FinishWrite,
            identity.mediaGeneration,
            identity.storageEpoch
        );
    auto result = filesystem_.finishWrite();
    core::diagnostics::storage_qualification::endStoragePrimitive(
        qualificationStarted,
        core::diagnostics::storage_qualification::OperationKind::FinishWrite,
        qualificationResultCode(result),
        identity.mediaGeneration,
        identity.storageEpoch
    );
    write_lease_id_ = 0;
    if (!result) {
        observeBackendFailure_(result.error());
        return result;
    }
    auto touched = coordinator_.noteMutation(lease);
    return touched ? oc::type::Result<void>::ok() : touched;
}

FLASHMEM oc::type::Result<void> ProductFileService::abortWrite(
    const ProductMutationLease& lease
) {
    auto allowed = ensureMutationLease_(lease);
    if (!allowed) {
        return allowed;
    }
    if (write_lease_id_ == 0) {
        return oc::type::Result<void>::ok();
    }
    if (write_lease_id_ != lease.id_) {
        return staleLease_();
    }
    noteFilesystemCall_();
    const auto identity = coordinator_.identity();
    const uint32_t qualificationStarted =
        core::diagnostics::storage_qualification::beginStoragePrimitive(
            core::diagnostics::storage_qualification::OperationKind::AbortWrite,
            identity.mediaGeneration,
            identity.storageEpoch
        );
    filesystem_.abortWrite();
    core::diagnostics::storage_qualification::endStoragePrimitive(
        qualificationStarted,
        core::diagnostics::storage_qualification::OperationKind::AbortWrite,
        static_cast<uint8_t>(ErrorCode::OK),
        identity.mediaGeneration,
        identity.storageEpoch
    );
    write_lease_id_ = 0;
    return oc::type::Result<void>::ok();
}

FLASHMEM oc::type::Result<void> ProductFileService::ensureReadable_(
    const ProductMutationLease* lease
) {
    if (lease && !coordinator_.owns(*lease)) {
        return staleLease_();
    }
    if (!filesystem_.available()) {
        markMediaUnavailable();
        return mediaUnavailable_();
    }

    const auto state = coordinator_.storageState();
    if (state == ProductStorageState::READY) {
        return oc::type::Result<void>::ok();
    }
    if (state == ProductStorageState::RECOVERING && lease &&
        coordinator_.owns(*lease, ProductMutationOwner::RECOVERY)) {
        return oc::type::Result<void>::ok();
    }
    if (state == ProductStorageState::EXHAUSTED) {
        return oc::type::Result<void>::err(
            {ErrorCode::RESOURCE_EXHAUSTED, "product storage identity exhausted"}
        );
    }
    if (state == ProductStorageState::ABSENT) {
        return mediaUnavailable_();
    }
    return oc::type::Result<void>::err(
        {ErrorCode::HARDWARE_BUSY, "product storage reconciliation pending"}
    );
}

FLASHMEM oc::type::Result<void> ProductFileService::ensureMutationLease_(
    const ProductMutationLease& lease
) {
    if (!coordinator_.owns(lease)) {
        return staleLease_();
    }
    if (!filesystem_.available()) {
        markMediaUnavailable();
        return mediaUnavailable_();
    }
    const auto state = coordinator_.storageState();
    if (state == ProductStorageState::READY ||
        (state == ProductStorageState::RECOVERING &&
         coordinator_.owns(lease, ProductMutationOwner::RECOVERY))) {
        return oc::type::Result<void>::ok();
    }
    return oc::type::Result<void>::err(
        {ErrorCode::INVALID_STATE, "mutation lease is not valid for storage state"}
    );
}

FLASHMEM void ProductFileService::observeBackendFailure_(oc::type::Error error) {
    if (!filesystem_.available() || error.code == ErrorCode::HARDWARE_NOT_FOUND) {
        markMediaUnavailable();
    }
}

FLASHMEM bool ProductFileService::measuredListVisitor_(
    const oc::interface::DirectoryEntry& entry,
    void* context
) {
    auto* measured = static_cast<MeasuredListVisitorContext*>(context);
    if (!measured || !measured->service || !measured->visitor) return false;
    measured->service->noteEntries_(1U);
    return measured->visitor(entry, measured->context);
}

FLASHMEM void ProductFileService::noteFilesystemCall_() {
    if (work_usage_) saturatingAccumulate(work_usage_->filesystemCalls, 1U);
}

FLASHMEM void ProductFileService::noteBytes_(size_t bytes) {
    if (work_usage_) saturatingAccumulate(work_usage_->bytes, bytes);
}

FLASHMEM void ProductFileService::noteEntries_(uint16_t entries) {
    if (work_usage_) saturatingAccumulate(work_usage_->entries, entries);
}

FLASHMEM void ProductFileService::noteNodes_(uint8_t nodes) {
    if (work_usage_) saturatingAccumulate(work_usage_->nodes, nodes);
}

FLASHMEM void ProductFileService::noteAllocations_(uint8_t allocations) {
    if (work_usage_) saturatingAccumulate(work_usage_->allocations, allocations);
}

FLASHMEM void ProductFileService::endWorkMeasurement_(
    ProductPersistenceWorkUsage* usage
) {
    if (work_usage_ == usage) work_usage_ = nullptr;
}

FLASHMEM bool ProductFileService::isProductRootPath_(const char* resolvedPath) {
    return resolvedPath && std::strcmp(resolvedPath, PRODUCT_ROOT) == 0;
}

FLASHMEM bool ProductFileService::isProductRootSegment_(const char* path, size_t offset) {
    constexpr const char* rootSegment = "midi-studio";
    constexpr size_t rootSegmentLength = sizeof("midi-studio") - 1;

    if (!path) {
        return false;
    }

    if (std::strncmp(path + offset, rootSegment, rootSegmentLength) != 0) {
        return false;
    }

    const char next = path[offset + rootSegmentLength];
    return next == '\0' || next == '/';
}

}  // namespace core::persistence
