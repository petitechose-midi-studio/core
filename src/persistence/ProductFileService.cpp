#include "persistence/ProductFileService.hpp"

#include <cstring>
#include <utility>

#include <config/PlatformCompat.hpp>

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
    ProductFileService::TMP_DIR,
};

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

}  // namespace

FLASHMEM ProductFileService::ProductFileService(oc::interface::IFileSystem& filesystem)
    : filesystem_(filesystem) {}

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
    auto initialized = filesystem_.init();
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
        filesystem_.abortWrite();
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

FLASHMEM void ProductFileService::markMediaUnavailable() {
    if (write_lease_id_ != 0) {
        filesystem_.abortWrite();
        write_lease_id_ = 0;
    }
    coordinator_.markMediaUnavailable();
}

FLASHMEM oc::type::Result<void> ProductFileService::ensureLayout(
    const ProductMutationLease& lease
) {
    for (const char* directory : kLayoutDirectories) {
        auto result = createDirectory(lease, directory);
        if (!result) {
            return result;
        }
    }
    return oc::type::Result<void>::ok();
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
    auto result = filesystem_.stat(path);
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
    auto result = filesystem_.list(path, visitor, context);
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

    auto existing = filesystem_.stat(path);
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

    auto result = filesystem_.createDirectory(path);
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
    auto result = filesystem_.remove(path, mode);
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

    auto result = filesystem_.rename(fromPath, toPath);
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
    auto result = filesystem_.read(path, offset, buffer, size);
    if (!result) {
        observeBackendFailure_(result.error());
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
    auto result = filesystem_.write(path, offset, data, size);
    if (!result) {
        observeBackendFailure_(result.error());
        return result;
    }
    auto touched = coordinator_.noteMutation(lease);
    if (!touched) {
        return oc::type::Result<size_t>::err(touched.error());
    }
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
    auto result = filesystem_.flush(path);
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
    auto result = filesystem_.beginWrite(path, expectedSize);
    if (!result) {
        observeBackendFailure_(result.error());
        return result;
    }
    write_lease_id_ = lease.id_;
    auto touched = coordinator_.noteMutation(lease);
    if (!touched) {
        filesystem_.abortWrite();
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
    auto result = filesystem_.appendWrite(data, size);
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
    auto result = filesystem_.finishWrite();
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
    filesystem_.abortWrite();
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
