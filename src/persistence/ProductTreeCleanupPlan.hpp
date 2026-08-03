#pragma once

#include <cstddef>
#include <cstdint>

#include <oc/interface/IFileSystem.hpp>
#include <oc/type/Result.hpp>

#include "persistence/ProductFileService.hpp"

namespace core::persistence {

/**
 * Fixed private identity used as both the recursive-delete quarantine and its
 * durable recovery marker. It lives under the pre-existing reserved
 * `tmp/rpc-*` namespace; recursive deletion of that parent is rejected.
 */
inline constexpr const char PRODUCT_TREE_CLEANUP_PATH[] =
    "/midi-studio/tmp/rpc-d";
inline constexpr const char PRODUCT_TREE_CLEANUP_PARENT_PATH[] =
    "/midi-studio/tmp";

/**
 * Allocation-free hide-first recursive cleanup continuation.
 *
 * A live delete owns its mutation lease. Recovery borrows the caller's exact
 * RECOVERY lease. In both modes the fixed hidden root is traversed by listing
 * only its first remaining child: one successful advance removes or descends
 * one node, so reopening the directory never creates a quadratic prefix scan.
 */
class ProductTreeCleanupPlan {
public:
    static constexpr uint8_t MAX_DEPTH = 8U;
    static constexpr size_t PATH_SIZE =
        oc::interface::FILESYSTEM_MAX_PATH_LENGTH + 1U;

    oc::type::Result<void> beginDelete(
        ProductFileService& files,
        const char* productPath
    );
    void beginRecovery();

    /** Return true when the continuation is terminal. */
    bool advanceDelete(
        ProductFileService& files,
        ProductPersistenceWorkMeasurement* measurement = nullptr
    );
    /** Return true when the continuation is terminal. */
    bool advanceRecovery(
        ProductFileService& files,
        const ProductMutationLease& recoveryLease,
        ProductPersistenceWorkMeasurement* measurement = nullptr
    );

    void cancelDelete(
        ProductFileService& files,
        oc::type::ErrorCode error = oc::type::ErrorCode::HARDWARE_BUSY
    );

    bool active() const;
    bool terminal() const;
    bool completed() const;
    bool canonicalHidden() const { return cleanup_required_; }
    oc::type::Error error() const { return error_; }

private:
    enum class Mode : uint8_t {
        NONE = 0,
        DELETE,
        RECOVERY,
    };

    enum class Step : uint8_t {
        IDLE = 0,
        CHECK_DELETE,
        HIDE,
        CHECK_RECOVERY,
        CLEAN_FILE,
        CLEAN_DIRECTORY,
        COMPLETE,
        FAILED,
    };

    static bool firstEntryVisitor_(
        const oc::interface::DirectoryEntry& entry,
        void* context
    );
    static bool samePathCaseFolded_(const char* lhs, const char* rhs);

    void reset_();
    bool advance_(
        ProductFileService& files,
        const ProductMutationLease& lease,
        ProductPersistenceWorkMeasurement* measurement
    );
    bool checkDelete_(
        ProductFileService& files,
        const ProductMutationLease& lease,
        ProductPersistenceWorkMeasurement* measurement
    );
    bool hide_(
        ProductFileService& files,
        const ProductMutationLease& lease,
        ProductPersistenceWorkMeasurement* measurement
    );
    bool checkRecovery_(
        ProductFileService& files,
        const ProductMutationLease& lease,
        ProductPersistenceWorkMeasurement* measurement
    );
    bool cleanFile_(
        ProductFileService& files,
        const ProductMutationLease& lease,
        ProductPersistenceWorkMeasurement* measurement
    );
    bool cleanDirectory_(
        ProductFileService& files,
        const ProductMutationLease& lease,
        ProductPersistenceWorkMeasurement* measurement
    );
    bool complete_(ProductFileService& files);
    bool fail_(ProductFileService& files, oc::type::Error error);
    bool releaseOwned_(
        ProductFileService& files,
        bool requireRecovery,
        oc::type::ErrorCode error
    );
    bool joinChild_(const char* parent, const char* name);
    static void recordPathBytes_(
        ProductPersistenceWorkMeasurement* measurement,
        const char* first,
        const char* second = nullptr
    );

    oc::interface::DirectoryEntry first_entry_{};
    char source_path_[PATH_SIZE] = {};
    char path_stack_[MAX_DEPTH][PATH_SIZE] = {};
    char candidate_path_[PATH_SIZE] = {};
    ProductMutationLease lease_{};
    oc::type::Error error_{oc::type::ErrorCode::OK, nullptr};
    uint8_t depth_ = 0U;
    Mode mode_ = Mode::NONE;
    Step step_ = Step::IDLE;
    bool first_entry_present_ = false;
    bool cleanup_required_ = false;
    bool root_is_directory_ = false;
};

static_assert(
    sizeof(ProductTreeCleanupPlan) <= 2304U,
    "tree cleanup continuation exceeds PSRAM control ceiling"
);

}  // namespace core::persistence
