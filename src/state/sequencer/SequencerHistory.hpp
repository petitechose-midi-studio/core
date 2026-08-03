#pragma once

#include <cstddef>
#include <cstdint>

#include <array>

#include <oc/note/sequencer/StepSequencerGraph.hpp>

#include "app/ExtmemAllocator.hpp"
#include "state/project/ProjectHistoryEventSink.hpp"
#include "state/sequencer/SequencerCcLanePatternOps.hpp"
#include "state/sequencer/SequencerChordContextProjection.hpp"
#include "state/sequencer/SequencerSnapshots.hpp"
#include "state/sequencer/SequencerState.hpp"
#include "state/sequencer/SequencerTrackActivationQueue.hpp"
#include "state/sequencer/SequencerTrackBankState.hpp"

namespace core::state::macro {
struct MacroPagesState;
}

namespace core::state::sequencer {

using SequencerHistoryGraphPtr =
    core::app::ExtmemUniquePtr<oc::note::sequencer::StepSequencerGraph>;
using SequencerHistoryCcLanePtr = SequencerCcLaneBankPtr;

// Detached Graph/CC ownership reserved before a prepared transaction crosses
// its live publication barrier. A successful strict capture into this storage
// never allocates; a newly-required owner instead rejects the stale plan.
struct SequencerHistoryPatternPayloadStorage {
    SequencerHistoryGraphPtr graph;
    SequencerHistoryCcLanePtr ccLanes;

    SequencerHistoryPatternPayloadStorage();
    ~SequencerHistoryPatternPayloadStorage();
    SequencerHistoryPatternPayloadStorage(const SequencerHistoryPatternPayloadStorage&) = delete;
    SequencerHistoryPatternPayloadStorage& operator=(const SequencerHistoryPatternPayloadStorage&) =
        delete;
    SequencerHistoryPatternPayloadStorage(SequencerHistoryPatternPayloadStorage&&) noexcept;
    SequencerHistoryPatternPayloadStorage& operator=(
        SequencerHistoryPatternPayloadStorage&&) noexcept;
    void reset();
};

struct SequencerHistoryPatternSnapshot {
    SequencerPatternSnapshot flat{};
    // FlatOnly does not retain a CC payload. Prepared Page FullGraph also sets
    // ccLanesCaptured=false for an already-allocated, musically empty owner so
    // commit/rollback/traversal preserve that live owner instead of replaying
    // nullptr. This field uses existing scalar/pointer alignment padding on ARM.
    uint32_t ccLaneRevision = 0;
    uint8_t focusedStep = 0;
    bool ccLanesCaptured = false;
    SequencerHistoryGraphPtr graph;
    SequencerHistoryCcLanePtr ccLanes;

    SequencerHistoryPatternSnapshot();
    ~SequencerHistoryPatternSnapshot();
    SequencerHistoryPatternSnapshot(const SequencerHistoryPatternSnapshot&) = delete;
    SequencerHistoryPatternSnapshot& operator=(const SequencerHistoryPatternSnapshot&) = delete;
    SequencerHistoryPatternSnapshot(SequencerHistoryPatternSnapshot&&) noexcept;
    SequencerHistoryPatternSnapshot& operator=(SequencerHistoryPatternSnapshot&&) noexcept;
    void reset();
};

struct SequencerHistoryTrackBankSnapshot {
    SequencerTrackBankSnapshot flat{};
    uint8_t focusedStep = 0;
    StepProperty activeStepProperty = StepProperty::NOTE;
    SequencerHistoryGraphPtr editorGraph;
    std::array<SequencerHistoryGraphPtr, SequencerTrackBankState::TRACK_COUNT> bankGraphs{};
    SequencerHistoryCcLanePtr editorCcLanes;
    std::array<SequencerHistoryCcLanePtr, SequencerTrackBankState::TRACK_COUNT> bankCcLanes{};

    SequencerHistoryTrackBankSnapshot();
    ~SequencerHistoryTrackBankSnapshot();
    SequencerHistoryTrackBankSnapshot(const SequencerHistoryTrackBankSnapshot&) = delete;
    SequencerHistoryTrackBankSnapshot& operator=(const SequencerHistoryTrackBankSnapshot&) = delete;
    SequencerHistoryTrackBankSnapshot(SequencerHistoryTrackBankSnapshot&&) noexcept;
    SequencerHistoryTrackBankSnapshot& operator=(SequencerHistoryTrackBankSnapshot&&) noexcept;
    void reset();
};

struct SequencerHistoryTrackStructureChange;
struct SequencerHistoryMacroTrackStructurePayload;
struct SequencerPreparedStructureHistoryReplay;
enum class SequencerStructureHistoryReplayPrepareOutcome : uint8_t;

enum class SequencerHistoryScope : uint8_t {
    PatternOnly = 0,
    Structure,
    FullBank,
};

enum class SequencerHistoryPatternStorage : uint8_t {
    FullGraph = 0,
    // Restores flat pattern data while retaining the graph already owned by
    // the editor/track. Recording rejects entries whose graph revisions differ.
    FlatOnly,
};

// Frozen payload plan for one coalesced Pattern edit. FullCurrentPayload
// mirrors the currently-owned Graph/CC shape. FullWithProspectiveGraph also
// reserves a post-edit Graph when the live source is still graphless; CC
// presence always remains source-derived and may never grow through this plan.
enum class SequencerCoalescedPatternPayloadPlan : uint8_t {
    FlatOnly = 0,
    FullCurrentPayload,
    FullWithProspectiveGraph,
};

// Retained Pattern edits share the same prepared owner set as the coalesced
// Step-property path. The owner/key pair is deliberately scalar: all
// Graph/CC/Change ownership remains in CoreState.
enum class SequencerPreparedPatternEditOwner : uint8_t {
    PatternPitch = 0,
    PropertySelector,
    StepContent,
    StepEditSession,
    StepToggle,
    PatternEditor,
    PageStructure,
    QuickControls,
};

// FullBank edit ownership remains typed across the handler facade so Project
// navigation and the Settings modal can retain distinct boundary semantics.
enum class SequencerPreparedFullBankEditOwner : uint8_t {
    ProjectScale = 0,
    SequencerSettingsScale,
};

enum class SequencerPreparedFullBankEditOutcome : uint8_t {
    Failed = 0,
    NoChange,
    Committed,
};

struct SequencerPreparedFullBankEditResult {
    SequencerPreparedFullBankEditOutcome outcome =
        SequencerPreparedFullBankEditOutcome::Failed;
    SequencerChordContextProjectionStats projection{};
};

enum class SequencerPreparedPatternEditBeginOutcome : uint8_t {
    Failed = 0,
    Started,
    Continued,
};

enum class SequencerPreparedPatternEditSealOutcome : uint8_t {
    Failed = 0,
    Cleared,
    Sealed,
    // Seal rejected the mutation after proving exact rollback and consuming
    // the pending owner. Callers report failure but must not abort again.
    FailedClosed,
};

constexpr bool sequencerPreparedPatternEditSealFailed(
    SequencerPreparedPatternEditSealOutcome outcome
) {
    return outcome == SequencerPreparedPatternEditSealOutcome::Failed ||
           outcome == SequencerPreparedPatternEditSealOutcome::FailedClosed;
}

// True only when seal consumed the pending bundle without publishing it.
// A successful Sealed outcome remains pending until commit or abort.
constexpr bool sequencerPreparedPatternEditSealClosed(
    SequencerPreparedPatternEditSealOutcome outcome
) {
    return outcome == SequencerPreparedPatternEditSealOutcome::Cleared ||
           outcome == SequencerPreparedPatternEditSealOutcome::FailedClosed;
}

enum class SequencerPreparedPatternEditAbortOutcome : uint8_t {
    Failed = 0,
    NoPending,
    // Matching owner/key restored exact Before and consumed the bundle.
    Aborted,
};

enum class SequencerPreparedPatternGraphPrecompactionOutcome : uint8_t {
    Failed = 0,
    Unchanged,
    Compacted,
};

// Typed result shared by generic and family-owned Pattern commit barriers.
// Callers that are about to retarget or publish another history entry must
// distinguish a harmless empty boundary from a rollback/admission failure.
enum class SequencerPatternHistoryCommitOutcome : uint8_t {
    Failed = 0,
    NoPending,
    NoChange,
    Committed,
};

enum class SequencerTrackStructureChronologyStatus : uint8_t {
    Unavailable = 0,
    MacroAuditionBlocked,
    ProjectTrackGestureBlocked,
    PatternFailed,
    Opened,
};

/** Exact result of the central Track Structure predecessor boundary. */
struct SequencerTrackStructureChronologyResult {
    SequencerTrackStructureChronologyStatus status =
        SequencerTrackStructureChronologyStatus::Unavailable;
    SequencerPatternHistoryCommitOutcome predecessorPattern =
        SequencerPatternHistoryCommitOutcome::NoPending;
};

using SequencerPreparedPatternEditCommitOutcome = SequencerPatternHistoryCommitOutcome;

// Active editor-to-bank ownership prepared for one frozen Track identity.
// Callers must revalidate matchesActiveTrack() immediately before their first
// live write and abandon the entire object after any failed reservation.
struct SequencerPreparedActiveTrackSynchronization {
    uint8_t trackIndex = SequencerTrackBankState::TRACK_COUNT;
    SequencerHistoryPatternStorage storage = SequencerHistoryPatternStorage::FullGraph;
    bool reserved = false;
    bool captured = false;
    SequencerHistoryPatternPayloadStorage payload;

    SequencerPreparedActiveTrackSynchronization();
    ~SequencerPreparedActiveTrackSynchronization();
    SequencerPreparedActiveTrackSynchronization(
        const SequencerPreparedActiveTrackSynchronization&) = delete;
    SequencerPreparedActiveTrackSynchronization& operator=(
        const SequencerPreparedActiveTrackSynchronization&) = delete;
    SequencerPreparedActiveTrackSynchronization(
        SequencerPreparedActiveTrackSynchronization&&) noexcept;
    SequencerPreparedActiveTrackSynchronization& operator=(
        SequencerPreparedActiveTrackSynchronization&&) noexcept;
    void reset();
};

enum class SequencerHistoryDirection : uint8_t {
    Undo = 0,
    Redo,
};

enum class SequencerHistoryActionKind : uint8_t {
    PatternEdit = 0,
    StepToggle,
    StepPropertyEdit,
    StepEdit,
    QuickControls,
    PatternSettings,
    PatternVariation,
    ProjectScaleSettings,
    PageStructure,
    TrackStructure,
    CcLaneCreate,
    CcLaneEventEdit,
    CcLaneEventClear,
    CcLaneSettings,
    CcLaneDelete,
    CcLaneTransitionEdit,
    FullBank,
    // Appended so persisted/diagnostic identities of existing actions remain stable.
    PatternRandomize,
};

struct SequencerHistoryDescriptor {
    static constexpr uint8_t INVALID_INDEX = 0xFF;

    SequencerHistoryActionKind kind = SequencerHistoryActionKind::PatternEdit;
    uint8_t trackIndex = INVALID_INDEX;
    uint8_t laneIndex = INVALID_INDEX;
    uint8_t stepIndex = INVALID_INDEX;
    StepProperty property = StepProperty::NOTE;
    bool hasValue = false;
    int32_t beforeValue = 0;
    int32_t afterValue = 0;
};

struct SequencerHistoryApplyResult {
    bool applied = false;
    SequencerHistoryDirection direction = SequencerHistoryDirection::Undo;
    SequencerHistoryDescriptor descriptor{};
};

struct SequencerHistoryPatternActivationMetadata {
    SequencerTrackActivationHistoryRef reference{};
    uint16_t targetAudibleMask = 0;
};

struct SequencerPreparedPatternPayloadOwnerProof {
    uintptr_t graphOwner = 0;
    uintptr_t ccLaneOwner = 0;
};

// Activation metadata is needed only after a change becomes a retained entry;
// exact live-owner proof is needed only while a prepared family is pending.
// Overlay them in the existing cold Pattern-change span so neither RAM1 nor
// the LOCK-P PSRAM allocation size grows.
union SequencerHistoryPatternAuxiliary {
    SequencerHistoryPatternActivationMetadata activation;
    SequencerPreparedPatternPayloadOwnerProof preparedOwners;

    constexpr SequencerHistoryPatternAuxiliary() : activation{} {}
};

static_assert(sizeof(SequencerPreparedPatternPayloadOwnerProof) <=
                  sizeof(SequencerHistoryPatternActivationMetadata),
              "Exact prepared owner proof must reuse existing activation storage");

struct SequencerHistoryPatternChange {
    uint8_t trackIndex = 0;
    SequencerHistoryPatternStorage storage = SequencerHistoryPatternStorage::FullGraph;
    SequencerHistoryDescriptor descriptor{};
    // Deferred runtime activation owned by this exact history operation. Pattern
    // snapshots do not carry canonical Project Track audibility, so the
    // unchanged target mask is retained explicitly for safe Undo/Redo boundary
    // planning (including exclusive Solo selection).
    SequencerHistoryPatternAuxiliary auxiliary{};
    SequencerHistoryPatternSnapshot before;
    SequencerHistoryPatternSnapshot after;

    SequencerHistoryPatternChange();
    ~SequencerHistoryPatternChange();
    SequencerHistoryPatternChange(const SequencerHistoryPatternChange&) = delete;
    SequencerHistoryPatternChange& operator=(const SequencerHistoryPatternChange&) = delete;
    SequencerHistoryPatternChange(SequencerHistoryPatternChange&&) noexcept;
    SequencerHistoryPatternChange& operator=(SequencerHistoryPatternChange&&) noexcept;
    void setPreparedPayloadOwnerProof(const SequencerPatternState& pattern);
    bool preparedPayloadOwnerProofMatches(const SequencerPatternState& pattern) const;
    bool preparedGraphOwnerProofMatches(const SequencerPatternState& pattern) const;
    bool preparedCcLaneOwnerProofMatches(const SequencerPatternState& pattern) const;
    bool preparedGraphOwnerProofPresent() const;
    bool preparedCcLaneOwnerProofPresent() const;
    void clearPreparedPayloadOwnerProof();
};

using SequencerHistoryPatternChangePtr = core::app::ExtmemUniquePtr<SequencerHistoryPatternChange>;

struct SequencerHistoryFullBankChange {
    SequencerHistoryDescriptor descriptor{};
    SequencerHistoryTrackBankSnapshot before;
    SequencerHistoryTrackBankSnapshot after;

    SequencerHistoryFullBankChange();
    ~SequencerHistoryFullBankChange();
    SequencerHistoryFullBankChange(const SequencerHistoryFullBankChange&) = delete;
    SequencerHistoryFullBankChange& operator=(const SequencerHistoryFullBankChange&) = delete;
    SequencerHistoryFullBankChange(SequencerHistoryFullBankChange&&) noexcept;
    SequencerHistoryFullBankChange& operator=(SequencerHistoryFullBankChange&&) noexcept;
};

#if defined(ARDUINO_TEENSY41) && !defined(OC_DESKTOP)
static_assert(sizeof(SequencerHistoryPatternChange) == 1736U,
              "LOCK-P: ARM Pattern History transaction ABI changed");
static_assert(sizeof(SequencerHistoryFullBankChange) == 26960U,
              "LOCK-P: ARM FullBank History transaction ABI changed");
static_assert(sizeof(oc::note::sequencer::StepSequencerGraph) == 14792U,
              "LOCK-P: ARM Sequencer Graph allocation span changed");
static_assert(sizeof(SequencerCcLaneBank) == 840U,
              "LOCK-P: ARM Sequencer CC allocation span changed");
#endif

using SequencerHistoryFullBankChangePtr =
    core::app::ExtmemUniquePtr<SequencerHistoryFullBankChange>;
using SequencerHistoryTrackStructureChangePtr =
    core::app::ExtmemUniquePtr<SequencerHistoryTrackStructureChange>;

// Prepared-provider protocol:
// 1. capture Before and reserve every After/publication owner before live data
//    changes; 2. abandon the whole bundle on any false return (partial owners
//    are valid only for destruction, never for retry); 3. revalidate frozen
//    Track identity immediately before the first live write; 4. capture After,
//    call canRecord*, then keep the change immutable until recordPrepared*.
SequencerHistoryPatternChangePtr prepareHistoryPatternChangeBefore(
    const SequencerTrackBankState& bank, const SequencerState& active, uint8_t trackIndex,
    SequencerHistoryPatternStorage storage, SequencerHistoryDescriptor descriptor = {});
// Coalesced overload. Allocation order is final Change, before Graph or the
// mutually-exclusive prospective Graph, then before CC. A prospective owner
// is returned only when the plan requires a post-edit Graph and the live
// source owns no Graph; the caller must discard it with a failed preparation.
SequencerHistoryPatternChangePtr prepareHistoryPatternChangeBefore(
    const SequencerTrackBankState& bank, const SequencerState& active, uint8_t trackIndex,
    SequencerCoalescedPatternPayloadPlan plan, SequencerHistoryGraphPtr& prospectiveGraph,
    SequencerHistoryDescriptor descriptor = {});
bool reservePreparedHistoryPatternAfter(const SequencerTrackBankState& bank,
                                        const SequencerState& active,
                                        SequencerHistoryPatternChange& change);
// Reserves the exact declared post-edit shape. Only
// FullWithProspectiveGraph may force a Graph owner absent from the current
// source; CC ownership remains strictly current-source-derived.
bool reservePreparedHistoryPatternAfter(const SequencerTrackBankState& bank,
                                        const SequencerState& active,
                                        SequencerHistoryPatternChange& change,
                                        SequencerCoalescedPatternPayloadPlan plan);
bool capturePreparedHistoryPatternAfterUsingReservedStorage(const SequencerTrackBankState& bank,
                                                            const SequencerState& active,
                                                            SequencerHistoryPatternChange& change);
// Allocation-free emergency rollback for a prepared frozen-Track edit,
// including when that Track became inactive before abort. This consumes Full
// before-payload ownership; callers destroy the transaction after success.
bool restorePreparedHistoryPatternBefore(SequencerTrackBankState& bank,
                                         SequencerState& active,
                                         SequencerHistoryPatternChange& change,
                                         bool prospectiveGraphInstalled);

SequencerHistoryFullBankChangePtr prepareHistoryFullBankChangeBefore(
    const SequencerTrackBankState& bank, const SequencerState& active,
    SequencerHistoryDescriptor descriptor = {});
bool reservePreparedHistoryFullBankAfter(const SequencerTrackBankState& bank,
                                         const SequencerState& active,
                                         SequencerHistoryFullBankChange& change);
bool capturePreparedHistoryFullBankAfterUsingReservedStorage(
    const SequencerTrackBankState& bank, const SequencerState& active,
    SequencerHistoryFullBankChange& change);

// Populates two already allocated PSRAM roots from a captured FullBank Before.
// Payload allocation order is editor Graph/CC followed by inactive Tracks in
// ascending order. The active bank slot remains noncanonical scratch.
bool populatePreparedHistoryFullBankStaging(
    const SequencerTrackBankState& liveBank,
    const SequencerState& liveActive,
    const SequencerHistoryTrackBankSnapshot& before,
    SequencerTrackBankState& stagedBank,
    SequencerState& stagedActive
);

struct SequencerHistoryEntry {
    SequencerHistoryScope scope = SequencerHistoryScope::PatternOnly;
    core::app::ExtmemUniquePtr<SequencerHistoryPatternChange> pattern;
    SequencerHistoryTrackStructureChangePtr structure;
    core::app::ExtmemUniquePtr<SequencerHistoryFullBankChange> fullBank;

    SequencerHistoryEntry();
    ~SequencerHistoryEntry();
    SequencerHistoryEntry(const SequencerHistoryEntry&) = delete;
    SequencerHistoryEntry& operator=(const SequencerHistoryEntry&) = delete;
    SequencerHistoryEntry(SequencerHistoryEntry&&) noexcept;
    SequencerHistoryEntry& operator=(SequencerHistoryEntry&&) noexcept;

    bool valid() const {
        return (scope == SequencerHistoryScope::PatternOnly && pattern.get() != nullptr) ||
               (scope == SequencerHistoryScope::Structure && structure.get() != nullptr) ||
               (scope == SequencerHistoryScope::FullBank && fullBank.get() != nullptr);
    }
};

bool captureHistorySnapshot(const SequencerState& source, SequencerHistoryPatternSnapshot& out);
// Restores the complete musical revision vector without copying payload data
// or allocating. Used when exact prepared publication/no-op cancellation has
// already proven the corresponding musical bytes.
void synchronizeHistoryPatternRevisionSignals(SequencerPatternState& target,
                                              const SequencerPatternSnapshot& snapshot,
                                              uint32_t ccLaneRevision);
bool reserveHistoryPatternPayloadStorage(const SequencerPatternState& source,
                                         SequencerHistoryPatternPayloadStorage& storage);
bool captureHistoryPatternPayloadUsingReservedStorage(
    const SequencerPatternState& source, SequencerHistoryPatternPayloadStorage& storage);
bool reserveHistorySnapshotStorage(const SequencerState& source,
                                   SequencerHistoryPatternSnapshot& snapshot);
bool captureHistorySnapshotUsingReservedStorage(const SequencerState& source,
                                                SequencerHistoryPatternSnapshot& out);
bool reserveHistorySnapshotGraphStorage(SequencerHistoryPatternSnapshot& snapshot);
bool captureHistorySnapshotUsingReservedGraph(const SequencerState& source,
                                              SequencerHistoryPatternSnapshot& out);

void captureFlatHistorySnapshot(const SequencerState& source, SequencerHistoryPatternSnapshot& out);

bool captureHistorySnapshot(const SequencerTrackBankState& bank, const SequencerState& active,
                            uint8_t trackIndex, SequencerHistoryPatternSnapshot& out);
bool reserveHistorySnapshotStorage(const SequencerTrackBankState& bank,
                                   const SequencerState& active, uint8_t trackIndex,
                                   SequencerHistoryPatternSnapshot& snapshot);
bool captureHistorySnapshotUsingReservedStorage(const SequencerTrackBankState& bank,
                                                const SequencerState& active, uint8_t trackIndex,
                                                SequencerHistoryPatternSnapshot& out);
bool captureHistorySnapshotUsingReservedGraph(const SequencerTrackBankState& bank,
                                              const SequencerState& active, uint8_t trackIndex,
                                              SequencerHistoryPatternSnapshot& out);

void captureFlatHistorySnapshot(const SequencerTrackBankState& bank, const SequencerState& active,
                                uint8_t trackIndex, SequencerHistoryPatternSnapshot& out);

bool captureHistorySnapshot(const SequencerTrackBankState& bank, const SequencerState& active,
                            SequencerHistoryTrackBankSnapshot& out);
bool reserveHistoryTrackBankSnapshotStorage(const SequencerTrackBankState& bank,
                                            const SequencerState& active,
                                            SequencerHistoryTrackBankSnapshot& snapshot);
// The active editor is canonical. These FullBank helpers freeze its Track
// identity and deliberately keep the corresponding bank Graph/CC slots empty;
// that slot is noncanonical scratch even when legacy synchronization has
// temporarily populated it.
bool captureHistoryTrackBankSnapshotUsingReservedStorage(const SequencerTrackBankState& bank,
                                                         const SequencerState& active,
                                                         SequencerHistoryTrackBankSnapshot& out);

// Reserves ownership for one frozen active Track before the live write. A
// failed reservation leaves a discardable, possibly partial object and must
// never be retried. Revalidate matches immediately before the first live write,
// capture afterwards, then transfer the captured payload exactly once. A
// reserved-but-not-captured synchronization is never publishable.
bool reservePreparedActiveTrackSynchronization(
    const SequencerTrackBankState& bank, const SequencerState& after, uint8_t trackIndex,
    SequencerHistoryPatternStorage storage,
    SequencerPreparedActiveTrackSynchronization& synchronization);
bool reservePreparedActiveTrackSynchronization(
    const SequencerTrackBankState& bank, const SequencerState& after, uint8_t trackIndex,
    SequencerCoalescedPatternPayloadPlan plan,
    SequencerPreparedActiveTrackSynchronization& synchronization);
bool preparedActiveTrackSynchronizationMatches(
    const SequencerTrackBankState& bank,
    const SequencerPreparedActiveTrackSynchronization& synchronization);
bool capturePreparedActiveTrackSynchronizationUsingReservedStorage(
    const SequencerTrackBankState& bank, const SequencerState& after,
    SequencerPreparedActiveTrackSynchronization& synchronization);
// Re-seals a continuation into the same reserved synchronization owners.
// Unlike the one-shot capture above, this operation deliberately accepts an
// already-captured bundle and never allocates missing storage.
bool refreshPreparedActiveTrackSynchronizationUsingReservedStorage(
    const SequencerTrackBankState& bank, const SequencerState& after,
    SequencerPreparedActiveTrackSynchronization& synchronization);
void publishPreparedActiveTrackSynchronization(
    SequencerTrackBankState& bank, const SequencerState& active,
    SequencerPreparedActiveTrackSynchronization synchronization);
// Coalesced publication consumes the exact flat After captured at seal time;
// the delayed boundary performs no live snapshot capture or allocation.
void publishPreparedActiveTrackSynchronization(
    SequencerTrackBankState& bank, const SequencerState& active,
    const SequencerHistoryPatternSnapshot& sealedAfter,
    SequencerPreparedActiveTrackSynchronization synchronization);

bool applyHistorySnapshot(SequencerTrackBankState& bank, SequencerState& active,
                          const SequencerHistoryPatternSnapshot& snapshot);
bool applyHistorySnapshotToEditor(SequencerState& active,
                                  const SequencerHistoryPatternSnapshot& snapshot);

bool applyHistorySnapshotToTrack(SequencerTrackBankState& bank, SequencerState& active,
                                 uint8_t trackIndex,
                                 const SequencerHistoryPatternSnapshot& snapshot);

bool applyHistorySnapshot(SequencerTrackBankState& bank, SequencerState& active,
                          const SequencerHistoryTrackBankSnapshot& snapshot);

bool sameMusicalHistorySnapshot(const SequencerHistoryPatternSnapshot& lhs,
                                const SequencerHistoryPatternSnapshot& rhs);

// Allocation-free exact revalidation of one full captured Pattern payload.
// Focus is owned by the enclosing editor/Structure snapshot and is therefore
// intentionally excluded here.
bool liveHistoryPatternSnapshotMatches(
    const SequencerPatternState& live,
    const SequencerHistoryPatternSnapshot& snapshot
);

// Allocation-free identity proof for a sealed prepared After. Focus is
// intentionally excluded for an inactive Track because it is editor-only.
bool preparedHistoryPatternAfterMatchesTrack(const SequencerTrackBankState& bank,
                                             const SequencerState& active, uint8_t trackIndex,
                                             const SequencerHistoryPatternSnapshot& after,
                                             SequencerHistoryPatternStorage storage);

bool sameMusicalHistorySnapshot(const SequencerHistoryTrackBankSnapshot& lhs,
                                const SequencerHistoryTrackBankSnapshot& rhs);

class SequencerHistoryService {
public:
    static constexpr uint8_t PATTERN_ENTRY_LIMIT = 32;
    static constexpr uint8_t STRUCTURE_ENTRY_LIMIT = 8;
    static constexpr uint8_t FULL_BANK_ENTRY_LIMIT = 4;
    static constexpr uint8_t ENTRY_LIMIT =
        PATTERN_ENTRY_LIMIT + STRUCTURE_ENTRY_LIMIT + FULL_BANK_ENTRY_LIMIT;
    static constexpr size_t RETAINED_BYTE_BUDGET = 1024U * 1024U;

    SequencerHistoryService();
    ~SequencerHistoryService();

    void setProjectHistoryEventSink(const core::state::project::ProjectHistoryEventSink* sink) {
        project_history_sink_ = sink;
    }

    bool recordPattern(uint8_t trackIndex, SequencerHistoryPatternSnapshot before,
                       SequencerHistoryPatternSnapshot after,
                       SequencerHistoryDescriptor descriptor = {});
    bool recordPattern(SequencerHistoryPatternChangePtr change);
    // Side-effect-free admission check for a fully prepared Pattern change.
    bool canRecordPattern(const SequencerHistoryPatternChange& change) const;
    // Precondition: canRecordPattern(change) was true and change was not
    // modified afterwards. Under that contract this commit cannot fail.
    void recordPreparedPattern(SequencerHistoryPatternChangePtr change);

    bool recordFlatPattern(uint8_t trackIndex, SequencerHistoryPatternSnapshot before,
                           SequencerHistoryPatternSnapshot after,
                           SequencerHistoryDescriptor descriptor = {});

    bool recordPattern(SequencerHistoryPatternSnapshot before,
                       SequencerHistoryPatternSnapshot after,
                       SequencerHistoryDescriptor descriptor = {});

    bool recordFlatPattern(SequencerHistoryPatternSnapshot before,
                           SequencerHistoryPatternSnapshot after,
                           SequencerHistoryDescriptor descriptor = {});

    bool recordFullBank(SequencerHistoryTrackBankSnapshot before,
                        SequencerHistoryTrackBankSnapshot after,
                        SequencerHistoryDescriptor descriptor = {});
    bool recordFullBank(SequencerHistoryFullBankChangePtr change);
    bool canRecordFullBank(const SequencerHistoryFullBankChange& change) const;
    // Precondition: canRecordFullBank(change) was true and change was not
    // modified afterwards. Under that contract this commit cannot fail.
    void recordPreparedFullBank(SequencerHistoryFullBankChangePtr change);
    // Internal no-fail tail for a FullBank change whose immutable payload and
    // retained-byte admission were proven before the first live write. This
    // performs no allocation and no policy recheck.
    void commitAdmittedFullBank(SequencerHistoryFullBankChangePtr change);
    // Side-effect-free admission check for a fully prepared change. Callers
    // must repeat it if snapshot graph ownership changes before recording.
    bool canRecordStructure(const SequencerHistoryTrackStructureChange& change) const;
    // Precondition: canRecordStructure(change) was true and change was not
    // modified afterwards. Under that contract this commit cannot fail.
    void recordPreparedStructure(SequencerHistoryTrackStructureChangePtr change);
    // Internal no-fail tail for an immutable Structure change admitted before
    // the first live write. This performs no equality or policy recheck.
    void commitAdmittedStructure(
        SequencerHistoryTrackStructureChangePtr change
    ) noexcept;
    bool recordStructure(SequencerHistoryTrackStructureChangePtr change);

    bool canUndo() const { return undo_count_ > 0; }
    bool canRedo() const { return redo_count_ > 0; }

    bool undo(SequencerTrackBankState& bank, SequencerState& active);
    bool redo(SequencerTrackBankState& bank, SequencerState& active);
    SequencerHistoryApplyResult undoWithResult(SequencerTrackBankState& bank,
                                               SequencerState& active);
    SequencerHistoryApplyResult redoWithResult(SequencerTrackBankState& bank,
                                               SequencerState& active);
    SequencerStructureHistoryReplayPrepareOutcome prepareStructureHistoryReplay(
        SequencerHistoryDirection direction,
        const SequencerTrackBankState& bank,
        const SequencerState& active,
        const core::state::macro::MacroPagesState& pages,
        SequencerPreparedStructureHistoryReplay& out
    ) const;
    SequencerHistoryApplyResult commitPreparedStructureHistoryReplay(
        SequencerTrackBankState& bank,
        SequencerState& active,
        core::state::macro::MacroPagesState& pages,
        SequencerPreparedStructureHistoryReplay&& replay
    ) noexcept;
    bool peekUndoTrackActivation(SequencerTrackActivationHistoryPlan& out) const;
    bool peekRedoTrackActivation(SequencerTrackActivationHistoryPlan& out) const;

    void clear();
    void discardRedoBranch();

    uint8_t undoCount() const { return undo_count_; }
    uint8_t redoCount() const { return redo_count_; }
    uintptr_t projectHistoryUndoIdentity() const;
    uintptr_t projectHistoryRedoIdentity() const;
    uint8_t undoCount(SequencerHistoryScope scope) const;
    uint8_t redoCount(SequencerHistoryScope scope) const;
    size_t retainedBytes() const;
private:
    std::array<SequencerHistoryEntry, ENTRY_LIMIT> undo_{};
    std::array<SequencerHistoryEntry, ENTRY_LIMIT> redo_{};
    uint8_t undo_count_ = 0;
    uint8_t redo_count_ = 0;
    const core::state::project::ProjectHistoryEventSink* project_history_sink_ = nullptr;

    bool pushUndo(SequencerHistoryEntry entry);
    bool pushRedo(SequencerHistoryEntry entry);
    void commitPreparedEntry(SequencerHistoryEntry entry);
    bool recordPatternWithStorage(uint8_t trackIndex, SequencerHistoryPatternSnapshot before,
                                  SequencerHistoryPatternSnapshot after,
                                  SequencerHistoryDescriptor descriptor,
                                  SequencerHistoryPatternStorage storage);
};

}  // namespace core::state::sequencer
