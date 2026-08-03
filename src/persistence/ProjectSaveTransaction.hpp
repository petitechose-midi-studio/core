#pragma once

#include <cstdint>

#include <oc/type/Result.hpp>

#include "persistence/AtomicProductFile.hpp"
#include "persistence/ProjectFileWorkspace.hpp"
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
    ProjectSaveTransaction(ProductFileService& files, ProjectFileWorkspace& workspace);
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
    oc::type::Result<ProjectSaveProgress> advance();

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
        const ProductMutationLease& recoveryLease
    );
    void cancel();

    bool active() const;
    bool writeSessionActive() const;

private:
    enum class Phase : uint8_t {
        IDLE = 0,
        PREPARE,
        ENCODE,
        WRITE,
        COMMIT,
    };

    oc::type::Result<ProjectSaveProgress> advance_(
        const ProductMutationLease& lease,
        bool releaseLeaseOnCompletion
    );
    void cancel_(const ProductMutationLease& lease, bool releaseLease);
    void reset_();
    void cleanupTmp_(const ProductMutationLease& lease);

    ProductFileService& files_;
    ProjectFileWorkspace& workspace_;
    const core::state::project::ProjectSnapshot* snapshot_ = nullptr;
    AtomicProductFilePaths paths_{};
    Phase phase_ = Phase::IDLE;
    uint32_t encoded_size_ = 0;
    uint32_t write_offset_ = 0;
    bool tmp_prepared_ = false;
    ProductMutationLease lease_{};
};

#if defined(ARDUINO_TEENSY41) && !defined(OC_DESKTOP)
static_assert(sizeof(ProjectSaveTransaction) == 48U, "project save lease ABI drift");
static_assert(alignof(ProjectSaveTransaction) == 4U, "project save alignment drift");
#endif

}  // namespace core::persistence
