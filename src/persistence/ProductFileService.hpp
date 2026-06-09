#pragma once

#include <cstddef>
#include <cstdint>

#include <oc/interface/IFileSystem.hpp>
#include <oc/type/Result.hpp>

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
    static constexpr const char* TMP_DIR = "/midi-studio/tmp";

    explicit ProductFileService(oc::interface::IFileSystem& filesystem);

    oc::type::Result<void> init();
    bool available() const;
    oc::type::Result<void> ensureLayout();

    oc::type::Result<void> resolvePath(const char* productPath,
                                       char* outPath,
                                       size_t outPathSize) const;

    oc::type::Result<oc::interface::FileInfo> stat(const char* productPath);
    oc::type::Result<void> list(const char* productPath,
                                oc::interface::DirectoryEntryVisitor visitor,
                                void* context);
    oc::type::Result<void> createDirectory(const char* productPath);
    oc::type::Result<void> remove(
        const char* productPath,
        oc::interface::RemoveMode mode = oc::interface::RemoveMode::FILE_OR_EMPTY_DIRECTORY
    );
    oc::type::Result<void> rename(const char* fromProductPath, const char* toProductPath);
    oc::type::Result<size_t> read(const char* productPath,
                                  uint32_t offset,
                                  uint8_t* buffer,
                                  size_t size);
    oc::type::Result<size_t> write(const char* productPath,
                                   uint32_t offset,
                                   const uint8_t* data,
                                   size_t size);
    oc::type::Result<void> flush(const char* productPath);
    oc::type::Result<void> beginWrite(const char* productPath, uint32_t expectedSize);
    oc::type::Result<size_t> appendWrite(const uint8_t* data, size_t size);
    oc::type::Result<void> finishWrite();
    void abortWrite();
    bool writeSessionActive() const { return writeSessionActive_; }

private:
    static constexpr size_t PATH_BUFFER_SIZE = oc::interface::FILESYSTEM_MAX_PATH_LENGTH + 1;

    static bool isProductRootPath_(const char* resolvedPath);
    static bool isProductRootSegment_(const char* path, size_t offset);

    oc::interface::IFileSystem& filesystem_;
    bool writeSessionActive_ = false;
};

}  // namespace core::persistence
