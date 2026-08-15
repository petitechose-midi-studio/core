#pragma once

#include <cstddef>
#include <cstdint>

#include <oc/type/Result.hpp>

#include "persistence/ProductFileService.hpp"

namespace core::persistence::conditional_mutation {

inline constexpr size_t SHA256_SIZE = 32U;
inline constexpr const char* JOURNAL_STAGING_PATH =
    "tmp/rpc-conditional.journal.tmp";
inline constexpr const char* JOURNAL_PATH = "tmp/rpc-conditional.journal";
inline constexpr const char* JOURNAL_QUARANTINE_PATH =
    "tmp/rpc-conditional.journal.corrupt";
inline constexpr const char* BACKUP_PATH = "tmp/rpc-conditional.backup";

enum class Status : uint8_t {
    OK = 0,
    INVALID_ARGUMENT,
    NOT_FOUND,
    BUSY,
    TOO_LARGE,
    STORAGE_ERROR,
    INVALID_STATE,
    UNSUPPORTED,
    PRECONDITION_FAILED,
};

enum class Kind : uint8_t {
    REPLACE = 1,
    DELETE = 2,
};

enum class Outcome : uint8_t {
    NONE = 0,
    APPLIED,
    ALREADY_APPLIED,
};

enum class Subject : uint8_t {
    NONE = 0,
    SOURCE,
    STAGING,
};

struct Journal {
    Kind kind = Kind::REPLACE;
    uint32_t operationId = 0;
    uint8_t expectedSourceSha256[SHA256_SIZE] = {};
    uint8_t replacementSha256[SHA256_SIZE] = {};
    char currentPath[oc::interface::FILESYSTEM_MAX_PATH_LENGTH + 1] = {};
    char stagingPath[oc::interface::FILESYSTEM_MAX_PATH_LENGTH + 1] = {};
};

struct ExecutionResult {
    Status status = Status::STORAGE_ERROR;
    bool applied = false;
};

Status statusFromError(oc::type::Error error);
oc::type::ErrorCode recoveryError(Status status);

Status removeIfExists(
    ProductFileService& files,
    const ProductMutationLease& lease,
    const char* path
);
Status writeJournal(
    ProductFileService& files,
    const ProductMutationLease& lease,
    const Journal& journal
);
Status readJournal(
    ProductFileService& files,
    const ProductMutationLease& lease,
    Journal& journal,
    bool& present,
    bool& corrupt
);
Status quarantineCorruptJournal(
    ProductFileService& files,
    const ProductMutationLease& lease
);
ExecutionResult executeJournal(
    ProductFileService& files,
    const ProductMutationLease& lease,
    const Journal& journal
);
Status recoverPendingMutation(
    ProductFileService& files,
    const ProductMutationLease& lease,
    bool& quarantined
);

}  // namespace core::persistence::conditional_mutation
