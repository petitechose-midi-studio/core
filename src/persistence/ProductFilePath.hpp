#pragma once

#include <cstddef>

namespace core::persistence {

bool copyProductRelativePath(char* out, size_t outSize, const char* path);
bool formatProductRelativePath(char* out,
                               size_t outSize,
                               const char* pattern,
                               const char* assetId);

}  // namespace core::persistence
