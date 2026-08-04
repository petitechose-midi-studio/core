#pragma once

#include <cstdint>

#include <oc/type/Result.hpp>

#include "persistence/AtomicProductFile.hpp"
#include "state/project/ProjectSnapshot.hpp"

namespace core::persistence {

enum class ProjectSaveStage : uint8_t {
    PREPARE = 0,
    ENCODE,
    WRITE,
    COMMIT,
};

struct ProjectSaveProgress {
    ProjectSaveStage completedStage = ProjectSaveStage::PREPARE;
    bool complete = false;
    uint32_t bytesWritten = 0;
    // Logical non-filesystem bytes consumed by this advance. Filesystem bytes
    // are measured directly by ProductFileService and must not be duplicated.
    uint32_t workBytes = 0;
};

/**
 * Incremental atomic project writer.
 *
 * Each advance() executes one explicit stage or writes at most one bounded SD
 * chunk. The caller must keep advancing while active(); writeSessionActive()
 * identifies the interval during which this transaction owns the product-file
 * stream across main-loop iterations.
 */
class ProjectSaveTransaction {
public:
    explicit ProjectSaveTransaction(ProductFileService& files);
    ~ProjectSaveTransaction();
    ProjectSaveTransaction(const ProjectSaveTransaction&) = delete;
    ProjectSaveTransaction& operator=(const ProjectSaveTransaction&) = delete;
    ProjectSaveTransaction(ProjectSaveTransaction&&) = delete;
    ProjectSaveTransaction& operator=(ProjectSaveTransaction&&) = delete;

    // The path strings and snapshot must remain valid until completion or cancel().
    oc::type::Result<void> begin(
        const core::state::project::ProjectSnapshot& snapshot,
        AtomicProductFilePaths paths
    );
    /** The borrowed recovery lease must outlive this transaction. */
    oc::type::Result<void> beginWithRecoveryLease(
        const core::state::project::ProjectSnapshot& snapshot,
        AtomicProductFilePaths paths,
        const ProductMutationLease& recoveryLease
    );
    oc::type::Result<ProjectSaveProgress> advance(
        ProjectSaveStage* attemptedStage = nullptr
    );

    /**
     * Complete one save synchronously under the caller's exact RECOVERY lease.
     *
     * The lease is borrowed, never copied or released. This keeps recovery as
     * one transaction from layout/journal reconciliation through the exact
     * RAM-authoritative session commit without growing the retained ABI.
     */
    oc::type::Result<ProjectSaveProgress> saveToCompletionWithRecoveryLease(
        const core::state::project::ProjectSnapshot& snapshot,
        AtomicProductFilePaths paths,
        const ProductMutationLease& recoveryLease,
        ProjectSaveStage* failedStage = nullptr
    );
    void cancel();

    bool active() const;
    bool writeSessionActive() const;
    ProjectSaveStage stage() const { return currentStage_(); }

private:
    enum class Phase : uint8_t {
        IDLE = 0,
        PREPARE,
        ENCODE,
        BEGIN_WRITE,
        WRITE,
        FINISH_WRITE,
        COMMIT,
    };

    oc::type::Result<ProjectSaveProgress> advance_(
        const ProductMutationLease& lease,
        bool releaseLeaseOnCompletion
    );
    ProjectSaveStage currentStage_() const;
    void cancel_(const ProductMutationLease& lease, bool releaseLease);
    void reset_();
    void cleanupTmp_(const ProductMutationLease& lease);

    ProductFileService& files_;
    const core::state::project::ProjectSnapshot* snapshot_ = nullptr;
    AtomicProductFilePaths paths_{};
    Phase phase_ = Phase::IDLE;
    uint32_t encoded_size_ = 0;
    uint32_t write_offset_ = 0;
    bool tmp_prepared_ = false;
    bool commit_plan_started_ = false;
    ProductMutationLease lease_{};
    const ProductMutationLease* recovery_lease_ = nullptr;
};

#if defined(ARDUINO_TEENSY41) && !defined(OC_DESKTOP)
static_assert(sizeof(ProjectSaveTransaction) == 48U, "project save lease ABI drift");
static_assert(alignof(ProjectSaveTransaction) == 4U, "project save alignment drift");
#endif

}  // namespace core::persistence
