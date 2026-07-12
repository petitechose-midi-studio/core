#include "persistence/ProductFileService.hpp"

#include <cstring>

#include <config/PlatformCompat.hpp>

namespace core::persistence {

namespace {

constexpr const char* kLayoutDirectories[] = {
    ProductFileService::PRODUCT_ROOT,
    ProductFileService::SESSION_DIR,
    ProductFileService::PROJECTS_DIR,
    ProductFileService::LIBRARY_DIR,
    ProductFileService::STEP_PRESETS_DIR,
    ProductFileService::TMP_DIR,
};

oc::type::Result<void> invalidPath_(const char* context) {
    return oc::type::Result<void>::err({oc::type::ErrorCode::INVALID_ARGUMENT, context});
}

}  // namespace

FLASHMEM ProductFileService::ProductFileService(oc::interface::IFileSystem& filesystem)
    : filesystem_(filesystem) {}

FLASHMEM oc::type::Result<void> ProductFileService::init() {
    auto initResult = filesystem_.init();
    if (!initResult) {
        return initResult;
    }
    return ensureLayout();
}

FLASHMEM bool ProductFileService::available() const {
    return filesystem_.available();
}

FLASHMEM oc::type::Result<void> ProductFileService::ensureLayout() {
    for (const char* directory : kLayoutDirectories) {
        auto result = filesystem_.createDirectory(directory);
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
        read += rootLength - 1;  // Skip "midi-studio"; PRODUCT_ROOT includes the slash.
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
    char path[PATH_BUFFER_SIZE] = {};
    auto pathResult = resolvePath(productPath, path, sizeof(path));
    if (!pathResult) {
        return oc::type::Result<oc::interface::FileInfo>::err(pathResult.error());
    }
    return filesystem_.stat(path);
}

FLASHMEM oc::type::Result<void> ProductFileService::list(
    const char* productPath,
    oc::interface::DirectoryEntryVisitor visitor,
    void* context
) {
    char path[PATH_BUFFER_SIZE] = {};
    auto pathResult = resolvePath(productPath, path, sizeof(path));
    if (!pathResult) {
        return pathResult;
    }
    return filesystem_.list(path, visitor, context);
}

FLASHMEM oc::type::Result<void> ProductFileService::createDirectory(const char* productPath) {
    char path[PATH_BUFFER_SIZE] = {};
    auto pathResult = resolvePath(productPath, path, sizeof(path));
    if (!pathResult) {
        return pathResult;
    }
    return filesystem_.createDirectory(path);
}

FLASHMEM oc::type::Result<void> ProductFileService::remove(
    const char* productPath,
    oc::interface::RemoveMode mode
) {
    char path[PATH_BUFFER_SIZE] = {};
    auto pathResult = resolvePath(productPath, path, sizeof(path));
    if (!pathResult) {
        return pathResult;
    }
    if (isProductRootPath_(path)) {
        return invalidPath_("cannot remove product root");
    }
    return filesystem_.remove(path, mode);
}

FLASHMEM oc::type::Result<void> ProductFileService::rename(
    const char* fromProductPath,
    const char* toProductPath
) {
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

    return filesystem_.rename(fromPath, toPath);
}

FLASHMEM oc::type::Result<size_t> ProductFileService::read(
    const char* productPath,
    uint32_t offset,
    uint8_t* buffer,
    size_t size
) {
    char path[PATH_BUFFER_SIZE] = {};
    auto pathResult = resolvePath(productPath, path, sizeof(path));
    if (!pathResult) {
        return oc::type::Result<size_t>::err(pathResult.error());
    }
    return filesystem_.read(path, offset, buffer, size);
}

FLASHMEM oc::type::Result<size_t> ProductFileService::write(
    const char* productPath,
    uint32_t offset,
    const uint8_t* data,
    size_t size
) {
    char path[PATH_BUFFER_SIZE] = {};
    auto pathResult = resolvePath(productPath, path, sizeof(path));
    if (!pathResult) {
        return oc::type::Result<size_t>::err(pathResult.error());
    }
    if (isProductRootPath_(path)) {
        return oc::type::Result<size_t>::err(
            {oc::type::ErrorCode::INVALID_ARGUMENT, "cannot write product root"}
        );
    }
    return filesystem_.write(path, offset, data, size);
}

FLASHMEM oc::type::Result<void> ProductFileService::flush(const char* productPath) {
    char path[PATH_BUFFER_SIZE] = {};
    auto pathResult = resolvePath(productPath, path, sizeof(path));
    if (!pathResult) {
        return pathResult;
    }
    return filesystem_.flush(path);
}

FLASHMEM oc::type::Result<void> ProductFileService::beginWrite(
    const char* productPath,
    uint32_t expectedSize
) {
    if (writeSessionActive_) {
        return oc::type::Result<void>::err(
            {oc::type::ErrorCode::INVALID_STATE, "write session already active"}
        );
    }
    char path[PATH_BUFFER_SIZE] = {};
    auto pathResult = resolvePath(productPath, path, sizeof(path));
    if (!pathResult) {
        return pathResult;
    }
    if (isProductRootPath_(path)) {
        return oc::type::Result<void>::err(
            {oc::type::ErrorCode::INVALID_ARGUMENT, "cannot write product root"}
        );
    }
    auto result = filesystem_.beginWrite(path, expectedSize);
    if (result) {
        writeSessionActive_ = true;
    }
    return result;
}

FLASHMEM oc::type::Result<size_t> ProductFileService::appendWrite(
    const uint8_t* data,
    size_t size
) {
    if (!writeSessionActive_) {
        return oc::type::Result<size_t>::err(
            {oc::type::ErrorCode::INVALID_STATE, "write session is not active"}
        );
    }
    return filesystem_.appendWrite(data, size);
}

FLASHMEM oc::type::Result<void> ProductFileService::finishWrite() {
    if (!writeSessionActive_) {
        return oc::type::Result<void>::err(
            {oc::type::ErrorCode::INVALID_STATE, "write session is not active"}
        );
    }
    auto result = filesystem_.finishWrite();
    writeSessionActive_ = false;
    return result;
}

FLASHMEM void ProductFileService::abortWrite() {
    filesystem_.abortWrite();
    writeSessionActive_ = false;
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
