#pragma once

#include <cstddef>
#include <cstdint>

#include "protocol/filesystem/FileSystemRpc.hpp"

namespace core::protocol::filesystem::conditional_mutation {

inline constexpr const char* JOURNAL_STAGING_PATH =
    "tmp/rpc-conditional.journal.tmp";
inline constexpr const char* JOURNAL_PATH = "tmp/rpc-conditional.journal";
inline constexpr const char* BACKUP_PATH = "tmp/rpc-conditional.backup";

enum class Kind : uint8_t {
    REPLACE = 1,
    DELETE = 2,
};

struct Journal {
    Kind kind = Kind::REPLACE;
    uint32_t operationId = 0;
    uint8_t expectedSourceSha256[FILESYSTEM_RPC_SHA256_SIZE] = {};
    uint8_t replacementSha256[FILESYSTEM_RPC_SHA256_SIZE] = {};
    char currentPath[oc::interface::FILESYSTEM_MAX_PATH_LENGTH + 1] = {};
    char stagingPath[oc::interface::FILESYSTEM_MAX_PATH_LENGTH + 1] = {};
};

struct ExecutionResult {
    FileSystemRpcStatus status = FileSystemRpcStatus::STORAGE_ERROR;
    bool applied = false;
};

bool pathEquals(const char* lhs, const char* rhs);
bool isReservedPath(const char* normalized);
bool isStagingPath(const char* normalized);
bool containsFatShortNameAliasSyntax(const char* normalized);
oc::type::ErrorCode recoveryError(FileSystemRpcStatus status);

FileSystemRpcStatus normalizeMutationPath(
    core::persistence::ProductFileService& files,
    const char* path,
    char* normalized,
    size_t normalizedSize
);
FileSystemRpcStatus removeIfExists(
    core::persistence::ProductFileService& files,
    const core::persistence::ProductMutationLease& lease,
    const char* path
);
FileSystemRpcStatus writeJournal(
    core::persistence::ProductFileService& files,
    const core::persistence::ProductMutationLease& lease,
    const Journal& journal
);
FileSystemRpcStatus readJournal(
    core::persistence::ProductFileService& files,
    const core::persistence::ProductMutationLease& lease,
    Journal& journal,
    bool& present,
    bool& corrupt
);
FileSystemRpcStatus quarantineCorruptJournal(
    core::persistence::ProductFileService& files,
    const core::persistence::ProductMutationLease& lease
);
ExecutionResult executeJournal(
    core::persistence::ProductFileService& files,
    const core::persistence::ProductMutationLease& lease,
    const Journal& journal
);
FileSystemRpcStatus recoverPendingMutation(
    core::persistence::ProductFileService& files,
    const core::persistence::ProductMutationLease& lease,
    bool& quarantined
);

}  // namespace core::protocol::filesystem::conditional_mutation
