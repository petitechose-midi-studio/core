#pragma once

#include <cstddef>
#include <cstdint>

#include "protocol/filesystem/FileSystemRpc.hpp"

namespace core::protocol::filesystem::conditional_mutation {

struct DigestReadResult {
    FileSystemRpcStatus status = FileSystemRpcStatus::STORAGE_ERROR;
    uint8_t sha256[FILESYSTEM_RPC_SHA256_SIZE] = {};
};

bool digestEquals(const uint8_t* lhs, const uint8_t* rhs);
void copyDigest(uint8_t* destination, const uint8_t* source);

DigestReadResult readDigest(
    core::persistence::ProductFileService& files,
    const core::persistence::ProductMutationLease& lease,
    const char* path
);

}  // namespace core::protocol::filesystem::conditional_mutation
