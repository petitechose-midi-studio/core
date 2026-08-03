#pragma once

#include <cstddef>
#include <cstdint>

#include <oc/interface/IFileSystem.hpp>
#include <oc/type/Result.hpp>

#include "persistence/ProductPersistenceCoordinator.hpp"

namespace core::persistence {

/**
 * Product-scoped filesystem facade for MIDI Studio user data.
 *
 * The service intentionally exposes paths relative to the product root and
 * resolves them under /midi-studio before delegating to the platform backend.
 * This keeps project/library/transfer code sandboxed from the physical SD root.
 */
class ProductFileService {
public:
    static constexpr const char* PRODUCT_ROOT = "/midi-studio";
    static constexpr const char* SESSION_DIR = "/midi-studio/session";
    static constexpr const char* PROJECTS_DIR = "/midi-studio/projects";
    static constexpr const char* LIBRARY_DIR = "/midi-studio/library";
    static constexpr const char* STEP_PRESETS_DIR = "/midi-studio/library/step-presets";
    static constexpr const char* CHORD_PRESETS_DIR = "/midi-studio/library/chord-presets";
    static constexpr const char* TMP_DIR = "/midi-studio/tmp";

    explicit ProductFileService(oc::interface::IFileSystem& filesystem);

    oc::type::Result<void> init();
    /**
     * Initialize or retry the backend while keeping product I/O blocked for
     * reconciliation. A retry from ABSENT leaves generation admission to
     * beginRecovery().
     */
    oc::type::Result<void> initForRecovery();
    bool available() const;
    bool mediaPresent() const;

    oc::type::Result<ProductMutationLease> acquireMutation(ProductMutationOwner owner);
    oc::type::Result<ProductMutationLease> beginRecovery();
    oc::type::Result<void> releaseMutation(ProductMutationLease& lease);
    oc::type::Result<void> completeRecovery(
        ProductMutationLease& lease,
        bool success,
        oc::type::ErrorCode error = oc::type::ErrorCode::OK
    );
    oc::type::Result<void> requireRecovery(oc::type::ErrorCode error);
    oc::type::Result<void> requireRecovery(
        const ProductMutationLease& lease,
        oc::type::ErrorCode error
    );
    bool recoveryRequired(const ProductMutationLease& lease) const {
        return coordinator_.recoveryRequired(lease);
    }
    void markMediaUnavailable();

    ProductStorageIdentity storageIdentity() const { return coordinator_.identity(); }
    ProductStorageState storageState() const { return coordinator_.storageState(); }
    ProductMutationOwner activeMutationOwner() const { return coordinator_.activeOwner(); }
    oc::type::ErrorCode recoveryError() const { return coordinator_.recoveryError(); }
    bool owns(const ProductMutationLease& lease) const { return coordinator_.owns(lease); }
    bool owns(const ProductMutationLease& lease, ProductMutationOwner owner) const {
        return coordinator_.owns(lease, owner);
    }

    oc::type::Result<void> ensureLayout(const ProductMutationLease& lease);

    oc::type::Result<void> resolvePath(const char* productPath,
                                       char* outPath,
                                       size_t outPathSize) const;

    oc::type::Result<oc::interface::FileInfo> stat(const char* productPath);
    oc::type::Result<oc::interface::FileInfo> stat(
        const ProductMutationLease& lease,
        const char* productPath
    );
    oc::type::Result<void> list(const char* productPath,
                                oc::interface::DirectoryEntryVisitor visitor,
                                void* context);
    oc::type::Result<void> list(const ProductMutationLease& lease,
                                const char* productPath,
                                oc::interface::DirectoryEntryVisitor visitor,
                                void* context);
    oc::type::Result<void> createDirectory(
        const ProductMutationLease& lease,
        const char* productPath
    );
    oc::type::Result<void> remove(
        const ProductMutationLease& lease,
        const char* productPath,
        oc::interface::RemoveMode mode = oc::interface::RemoveMode::FILE_OR_EMPTY_DIRECTORY
    );
    oc::type::Result<void> rename(const ProductMutationLease& lease,
                                  const char* fromProductPath,
                                  const char* toProductPath);
    oc::type::Result<size_t> read(const char* productPath,
                                  uint32_t offset,
                                  uint8_t* buffer,
                                  size_t size);
    oc::type::Result<size_t> read(const ProductMutationLease& lease,
                                  const char* productPath,
                                  uint32_t offset,
                                  uint8_t* buffer,
                                  size_t size);
    oc::type::Result<size_t> write(const ProductMutationLease& lease,
                                   const char* productPath,
                                   uint32_t offset,
                                   const uint8_t* data,
                                   size_t size);
    oc::type::Result<void> flush(const ProductMutationLease& lease,
                                 const char* productPath);
    oc::type::Result<void> beginWrite(const ProductMutationLease& lease,
                                      const char* productPath,
                                      uint32_t expectedSize);
    oc::type::Result<size_t> appendWrite(const ProductMutationLease& lease,
                                         const uint8_t* data,
                                         size_t size);
    oc::type::Result<void> finishWrite(const ProductMutationLease& lease);
    oc::type::Result<void> abortWrite(const ProductMutationLease& lease);
    bool writeSessionActive() const { return write_lease_id_ != 0; }

private:
    static constexpr size_t PATH_BUFFER_SIZE = oc::interface::FILESYSTEM_MAX_PATH_LENGTH + 1;

    static bool isProductRootPath_(const char* resolvedPath);
    static bool isProductRootSegment_(const char* path, size_t offset);

    oc::type::Result<void> initBackend_();
    oc::type::Result<void> ensureReadable_(const ProductMutationLease* lease);
    oc::type::Result<void> ensureMutationLease_(const ProductMutationLease& lease);
    oc::type::Result<oc::interface::FileInfo> stat_(
        const ProductMutationLease* lease,
        const char* productPath
    );
    oc::type::Result<void> list_(const ProductMutationLease* lease,
                                 const char* productPath,
                                 oc::interface::DirectoryEntryVisitor visitor,
                                 void* context);
    oc::type::Result<size_t> read_(const ProductMutationLease* lease,
                                   const char* productPath,
                                   uint32_t offset,
                                   uint8_t* buffer,
                                   size_t size);
    void observeBackendFailure_(oc::type::Error error);

    oc::interface::IFileSystem& filesystem_;
    ProductPersistenceCoordinator coordinator_{};
    uint32_t write_lease_id_ = 0;
};

#if defined(ARDUINO_TEENSY41) && !defined(OC_DESKTOP)
static_assert(sizeof(ProductFileService) == 28U, "product file service exceeds LOCK-S");
static_assert(alignof(ProductFileService) == 4U, "product file service alignment drift");
#endif

}  // namespace core::persistence
