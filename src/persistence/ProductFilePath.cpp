#include "persistence/ProductFilePath.hpp"

#include <cstdio>
#include <cstring>

#include <config/PlatformCompat.hpp>

namespace core::persistence {

FLASHMEM bool copyProductRelativePath(char* out, size_t outSize, const char* path) {
    if (out == nullptr || outSize == 0 || path == nullptr) return false;
    const size_t length = std::strlen(path);
    if (length >= outSize) return false;
    std::memcpy(out, path, length + 1U);
    return true;
}

FLASHMEM bool formatProductRelativePath(char* out,
                                        size_t outSize,
                                        const char* pattern,
                                        const char* assetId) {
    if (out == nullptr || outSize == 0 || pattern == nullptr || assetId == nullptr) {
        return false;
    }
    const int written = std::snprintf(out, outSize, pattern, assetId);
    return written > 0 && static_cast<size_t>(written) < outSize;
}

}  // namespace core::persistence
