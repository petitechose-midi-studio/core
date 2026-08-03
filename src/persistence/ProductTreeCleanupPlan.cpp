#include "persistence/ProductTreeCleanupPlan.hpp"

#include <cstring>
#include <utility>

#include <config/PlatformCompat.hpp>

namespace core::persistence {
namespace {

using oc::type::Error;
using oc::type::ErrorCode;

const char kCleanupAlreadyActive[] PROGMEM =
    "product tree cleanup already active";
const char kCleanupSourceInvalid[] PROGMEM =
    "invalid recursive cleanup source";
const char kCleanupPrefixTooLong[] PROGMEM =
    "recursive cleanup quarantine would exceed path bound";
const char kCleanupLeaseLost[] PROGMEM =
    "product tree cleanup lease lost";
const char kCleanupMarkerOccupied[] PROGMEM =
    "pending hidden tree requires recovery";
const char kCleanupChildInvalid[] PROGMEM =
    "hidden tree child name unavailable";
const char kCleanupDepthExceeded[] PROGMEM =
    "hidden tree depth exceeds eight";
const char kCleanupPathExceeded[] PROGMEM =
    "hidden tree child path too long";
const char kCleanupCancelled[] PROGMEM =
    "hidden tree cleanup cancelled";

FLASHMEM char asciiLower(char value) {
    return value >= 'A' && value <= 'Z'
        ? static_cast<char>(value - 'A' + 'a')
        : value;
}

}  // namespace

FLASHMEM oc::type::Result<void> ProductTreeCleanupPlan::beginDelete(
    ProductFileService& files,
    const char* productPath
) {
    if (active()) {
        return oc::type::Result<void>::err({ErrorCode::HARDWARE_BUSY,
                                            kCleanupAlreadyActive});
    }
    reset_();
    auto resolved = files.resolvePath(productPath, source_path_, sizeof(source_path_));
    if (!resolved || samePathCaseFolded_(source_path_, ProductFileService::PRODUCT_ROOT) ||
        samePathCaseFolded_(source_path_, PRODUCT_TREE_CLEANUP_PARENT_PATH) ||
        samePathCaseFolded_(source_path_, PRODUCT_TREE_CLEANUP_PATH)) {
        reset_();
        return oc::type::Result<void>::err(
            resolved ? Error{ErrorCode::INVALID_ARGUMENT, kCleanupSourceInvalid}
                     : resolved.error()
        );
    }
    // Replacing the canonical prefix with the quarantine prefix must never
    // lengthen a descendant. Every path which was addressable before hide then
    // remains within the same 192-character bound during cleanup.
    if (std::strlen(source_path_) < std::strlen(PRODUCT_TREE_CLEANUP_PATH)) {
        reset_();
        return oc::type::Result<void>::err(
            {ErrorCode::RESOURCE_EXHAUSTED, kCleanupPrefixTooLong}
        );
    }

    auto acquired = files.acquireMutation(ProductMutationOwner::FILESYSTEM_RPC);
    if (!acquired) {
        reset_();
        return oc::type::Result<void>::err(acquired.error());
    }
    lease_ = std::move(acquired.value());
    mode_ = Mode::DELETE;
    step_ = Step::CHECK_DELETE;
    return oc::type::Result<void>::ok();
}

FLASHMEM void ProductTreeCleanupPlan::beginRecovery() {
    reset_();
    mode_ = Mode::RECOVERY;
    step_ = Step::CHECK_RECOVERY;
}

FLASHMEM bool ProductTreeCleanupPlan::advanceDelete(
    ProductFileService& files,
    ProductPersistenceWorkMeasurement* measurement
) {
    if (terminal()) return true;
    if (mode_ != Mode::DELETE || !files.owns(lease_, ProductMutationOwner::FILESYSTEM_RPC)) {
        return fail_(files, {ErrorCode::INVALID_STATE, kCleanupLeaseLost});
    }
    return advance_(files, lease_, measurement);
}

FLASHMEM bool ProductTreeCleanupPlan::advanceRecovery(
    ProductFileService& files,
    const ProductMutationLease& recoveryLease,
    ProductPersistenceWorkMeasurement* measurement
) {
    if (terminal()) return true;
    if (mode_ != Mode::RECOVERY ||
        !files.owns(recoveryLease, ProductMutationOwner::RECOVERY)) {
        error_ = {ErrorCode::INVALID_STATE, kCleanupLeaseLost};
        step_ = Step::FAILED;
        return true;
    }
    return advance_(files, recoveryLease, measurement);
}

FLASHMEM void ProductTreeCleanupPlan::cancelDelete(
    ProductFileService& files,
    ErrorCode errorCode
) {
    if (mode_ != Mode::DELETE || terminal()) return;
    error_ = {
        errorCode == ErrorCode::OK ? ErrorCode::HARDWARE_BUSY : errorCode,
        kCleanupCancelled,
    };
    (void)releaseOwned_(files, cleanup_required_, error_.code);
    step_ = Step::FAILED;
}

FLASHMEM bool ProductTreeCleanupPlan::active() const {
    return step_ != Step::IDLE && step_ != Step::COMPLETE &&
           step_ != Step::FAILED;
}

FLASHMEM bool ProductTreeCleanupPlan::terminal() const {
    return step_ == Step::COMPLETE || step_ == Step::FAILED;
}

FLASHMEM bool ProductTreeCleanupPlan::completed() const {
    return step_ == Step::COMPLETE;
}

FLASHMEM bool ProductTreeCleanupPlan::firstEntryVisitor_(
    const oc::interface::DirectoryEntry& entry,
    void* context
) {
    auto* self = static_cast<ProductTreeCleanupPlan*>(context);
    if (!self) return false;
    self->first_entry_ = entry;
    self->first_entry_present_ = true;
    return false;
}

FLASHMEM bool ProductTreeCleanupPlan::samePathCaseFolded_(
    const char* lhs,
    const char* rhs
) {
    if (!lhs || !rhs) return false;
    while (*lhs != '\0' && *rhs != '\0') {
        if (asciiLower(*lhs) != asciiLower(*rhs)) return false;
        ++lhs;
        ++rhs;
    }
    return *lhs == '\0' && *rhs == '\0';
}

FLASHMEM void ProductTreeCleanupPlan::reset_() {
    first_entry_ = {};
    source_path_[0] = '\0';
    for (auto& path : path_stack_) path[0] = '\0';
    candidate_path_[0] = '\0';
    lease_ = ProductMutationLease{};
    error_ = {ErrorCode::OK, nullptr};
    depth_ = 0U;
    mode_ = Mode::NONE;
    step_ = Step::IDLE;
    first_entry_present_ = false;
    cleanup_required_ = false;
    root_is_directory_ = false;
}

FLASHMEM bool ProductTreeCleanupPlan::advance_(
    ProductFileService& files,
    const ProductMutationLease& lease,
    ProductPersistenceWorkMeasurement* measurement
) {
    switch (step_) {
        case Step::CHECK_DELETE:
            return checkDelete_(files, lease, measurement);
        case Step::HIDE:
            return hide_(files, lease, measurement);
        case Step::CHECK_RECOVERY:
            return checkRecovery_(files, lease, measurement);
        case Step::CLEAN_FILE:
            return cleanFile_(files, lease, measurement);
        case Step::CLEAN_DIRECTORY:
            return cleanDirectory_(files, lease, measurement);
        case Step::COMPLETE:
        case Step::FAILED:
            return true;
        case Step::IDLE:
        default:
            return fail_(files, {ErrorCode::INVALID_STATE, kCleanupLeaseLost});
    }
}

FLASHMEM bool ProductTreeCleanupPlan::checkDelete_(
    ProductFileService& files,
    const ProductMutationLease& lease,
    ProductPersistenceWorkMeasurement* measurement
) {
    recordPathBytes_(measurement, source_path_, PRODUCT_TREE_CLEANUP_PATH);
    auto hidden = files.stat(lease, PRODUCT_TREE_CLEANUP_PATH);
    if (hidden) {
        cleanup_required_ = true;
        return fail_(files, {ErrorCode::INVALID_STATE, kCleanupMarkerOccupied});
    }
    if (hidden.error().code != ErrorCode::RESOURCE_NOT_FOUND) {
        return fail_(files, hidden.error());
    }

    auto source = files.stat(lease, source_path_);
    if (!source) return fail_(files, source.error());
    root_is_directory_ = source.value().type == oc::interface::FileType::DIRECTORY;
    step_ = Step::HIDE;
    return false;
}

FLASHMEM bool ProductTreeCleanupPlan::hide_(
    ProductFileService& files,
    const ProductMutationLease& lease,
    ProductPersistenceWorkMeasurement* measurement
) {
    recordPathBytes_(measurement, source_path_, PRODUCT_TREE_CLEANUP_PATH);
    // A failed rename has an uncertain durable outcome. Recovery always checks
    // the fixed marker before ordinary I/O is allowed again.
    cleanup_required_ = true;
    auto hidden = files.rename(lease, source_path_, PRODUCT_TREE_CLEANUP_PATH);
    if (!hidden) return fail_(files, hidden.error());

    if (!root_is_directory_) {
        step_ = Step::CLEAN_FILE;
        return false;
    }
    std::memcpy(
        path_stack_[0],
        PRODUCT_TREE_CLEANUP_PATH,
        sizeof(PRODUCT_TREE_CLEANUP_PATH)
    );
    depth_ = 1U;
    step_ = Step::CLEAN_DIRECTORY;
    return false;
}

FLASHMEM bool ProductTreeCleanupPlan::checkRecovery_(
    ProductFileService& files,
    const ProductMutationLease& lease,
    ProductPersistenceWorkMeasurement* measurement
) {
    recordPathBytes_(measurement, PRODUCT_TREE_CLEANUP_PATH);
    auto hidden = files.stat(lease, PRODUCT_TREE_CLEANUP_PATH);
    if (!hidden) {
        if (hidden.error().code == ErrorCode::RESOURCE_NOT_FOUND) {
            cleanup_required_ = false;
            return complete_(files);
        }
        return fail_(files, hidden.error());
    }

    cleanup_required_ = true;
    root_is_directory_ = hidden.value().type == oc::interface::FileType::DIRECTORY;
    if (!root_is_directory_) {
        step_ = Step::CLEAN_FILE;
        return false;
    }
    std::memcpy(
        path_stack_[0],
        PRODUCT_TREE_CLEANUP_PATH,
        sizeof(PRODUCT_TREE_CLEANUP_PATH)
    );
    depth_ = 1U;
    step_ = Step::CLEAN_DIRECTORY;
    return false;
}

FLASHMEM bool ProductTreeCleanupPlan::cleanFile_(
    ProductFileService& files,
    const ProductMutationLease& lease,
    ProductPersistenceWorkMeasurement* measurement
) {
    recordPathBytes_(measurement, PRODUCT_TREE_CLEANUP_PATH);
    auto removed = files.remove(
        lease,
        PRODUCT_TREE_CLEANUP_PATH,
        oc::interface::RemoveMode::FILE_OR_EMPTY_DIRECTORY
    );
    if (!removed && removed.error().code != ErrorCode::RESOURCE_NOT_FOUND) {
        return fail_(files, removed.error());
    }
    if (measurement) measurement->addNodes(1U);
    cleanup_required_ = false;
    return complete_(files);
}

FLASHMEM bool ProductTreeCleanupPlan::cleanDirectory_(
    ProductFileService& files,
    const ProductMutationLease& lease,
    ProductPersistenceWorkMeasurement* measurement
) {
    if (depth_ == 0U || depth_ > MAX_DEPTH) {
        return fail_(files, {ErrorCode::INVALID_STATE, kCleanupDepthExceeded});
    }
    const char* current = path_stack_[depth_ - 1U];
    first_entry_ = {};
    first_entry_present_ = false;
    auto listed = files.list(lease, current, firstEntryVisitor_, this);
    if (!listed) return fail_(files, listed.error());

    if (!first_entry_present_) {
        recordPathBytes_(measurement, current);
        auto removed = files.remove(
            lease,
            current,
            oc::interface::RemoveMode::FILE_OR_EMPTY_DIRECTORY
        );
        if (!removed && removed.error().code != ErrorCode::RESOURCE_NOT_FOUND) {
            return fail_(files, removed.error());
        }
        if (measurement) measurement->addNodes(1U);
        --depth_;
        if (depth_ == 0U) {
            cleanup_required_ = false;
            return complete_(files);
        }
        return false;
    }

    if (first_entry_.nameTruncated || first_entry_.name[0] == '\0' ||
        std::strchr(first_entry_.name, '/') != nullptr ||
        std::strchr(first_entry_.name, '\\') != nullptr) {
        return fail_(files, {ErrorCode::RESOURCE_EXHAUSTED, kCleanupChildInvalid});
    }
    if (!joinChild_(current, first_entry_.name)) {
        return fail_(files, {ErrorCode::RESOURCE_EXHAUSTED, kCleanupPathExceeded});
    }
    recordPathBytes_(measurement, candidate_path_);

    if (first_entry_.type == oc::interface::FileType::DIRECTORY) {
        if (depth_ >= MAX_DEPTH) {
            return fail_(files, {ErrorCode::RESOURCE_EXHAUSTED,
                                 kCleanupDepthExceeded});
        }
        std::memcpy(
            path_stack_[depth_],
            candidate_path_,
            std::strlen(candidate_path_) + 1U
        );
        ++depth_;
        if (measurement) measurement->addNodes(1U);
        return false;
    }

    auto removed = files.remove(
        lease,
        candidate_path_,
        oc::interface::RemoveMode::FILE_OR_EMPTY_DIRECTORY
    );
    if (!removed && removed.error().code != ErrorCode::RESOURCE_NOT_FOUND) {
        return fail_(files, removed.error());
    }
    if (measurement) measurement->addNodes(1U);
    return false;
}

FLASHMEM bool ProductTreeCleanupPlan::complete_(ProductFileService& files) {
    if (mode_ == Mode::DELETE && !releaseOwned_(files, false, ErrorCode::OK)) {
        step_ = Step::FAILED;
        return true;
    }
    error_ = {ErrorCode::OK, nullptr};
    step_ = Step::COMPLETE;
    return true;
}

FLASHMEM bool ProductTreeCleanupPlan::fail_(
    ProductFileService& files,
    Error error
) {
    error_ = error;
    if (mode_ == Mode::DELETE) {
        (void)releaseOwned_(files, cleanup_required_, error.code);
    }
    step_ = Step::FAILED;
    return true;
}

FLASHMEM bool ProductTreeCleanupPlan::releaseOwned_(
    ProductFileService& files,
    bool requireRecovery,
    ErrorCode errorCode
) {
    if (!lease_.valid()) return true;
    if (!files.owns(lease_, ProductMutationOwner::FILESYSTEM_RPC)) {
        lease_ = ProductMutationLease{};
        if (error_.code == ErrorCode::OK) {
            error_ = {ErrorCode::INVALID_STATE, kCleanupLeaseLost};
        }
        return false;
    }
    if (requireRecovery) {
        const auto required = files.requireRecovery(
            lease_,
            errorCode == ErrorCode::OK ? ErrorCode::STORAGE_WRITE_FAILED : errorCode
        );
        if (!required && error_.code == ErrorCode::OK) error_ = required.error();
    }
    const auto released = files.releaseMutation(lease_);
    if (!released) {
        if (error_.code == ErrorCode::OK) error_ = released.error();
        return false;
    }
    return true;
}

FLASHMEM bool ProductTreeCleanupPlan::joinChild_(
    const char* parent,
    const char* name
) {
    if (!parent || !name || name[0] == '\0') return false;
    const size_t parentLength = std::strlen(parent);
    const size_t nameLength = std::strlen(name);
    const bool hasSeparator = parentLength > 0U && parent[parentLength - 1U] == '/';
    const size_t totalLength = parentLength + (hasSeparator ? 0U : 1U) + nameLength;
    if (totalLength + 1U > sizeof(candidate_path_)) return false;
    std::memcpy(candidate_path_, parent, parentLength);
    size_t offset = parentLength;
    if (!hasSeparator) candidate_path_[offset++] = '/';
    std::memcpy(candidate_path_ + offset, name, nameLength);
    candidate_path_[offset + nameLength] = '\0';
    return true;
}

FLASHMEM void ProductTreeCleanupPlan::recordPathBytes_(
    ProductPersistenceWorkMeasurement* measurement,
    const char* first,
    const char* second
) {
    if (!measurement) return;
    const size_t firstBytes = first ? std::strlen(first) + 1U : 0U;
    const size_t secondBytes = second ? std::strlen(second) + 1U : 0U;
    measurement->addBytes(firstBytes > secondBytes ? firstBytes : secondBytes);
}

}  // namespace core::persistence
