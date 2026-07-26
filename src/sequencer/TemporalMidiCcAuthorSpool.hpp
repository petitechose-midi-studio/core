#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "state/shared/MidiCcDestinationResolver.hpp"

namespace core::sequencer {

enum class TemporalMidiCcAuthorOperation : uint8_t {
    UPDATE = 0,
    REMOVE,
};

/** One scheduled mutation of the canonical author set, before arbitration. */
struct TemporalMidiCcAuthorTransition {
    uint32_t deadlineUs = 0;
    core::state::shared::MidiCcAuthor author{};
    core::state::shared::MidiCcDestination destination{};
    uint8_t localValue = 0;
    uint8_t trackIndex = 0;
    TemporalMidiCcAuthorOperation operation =
        TemporalMidiCcAuthorOperation::REMOVE;

    [[nodiscard]] core::state::shared::MidiCcCandidate candidate() const {
        return {
            .destination = destination,
            .author = author,
            .localValue = localValue,
        };
    }
};

enum class TemporalMidiCcAuthorSpoolStatus : uint8_t {
    OK = 0,
    INVALID_INPUT,
    CAPACITY_EXCEEDED,
    TRANSACTION_ACTIVE,
};

struct TemporalMidiCcAuthorSpoolResult {
    TemporalMidiCcAuthorSpoolStatus status =
        TemporalMidiCcAuthorSpoolStatus::INVALID_INPUT;
    uint16_t requestedCount = 0;
    uint16_t transferredCount = 0;
    uint16_t deadlineGroupCount = 0;

    [[nodiscard]] bool ok() const {
        return status == TemporalMidiCcAuthorSpoolStatus::OK;
    }
};

struct TemporalMidiCcAuthorSpoolDiagnostics {
    uint32_t pushedTransitionCount = 0;
    uint32_t committedTransitionCount = 0;
    uint32_t rolledBackTransitionCount = 0;
    uint32_t cancelledTransitionCount = 0;
    uint32_t clearedTransitionCount = 0;
    uint32_t rejectedTransitionCount = 0;
    uint32_t rejectedBatchCount = 0;
    uint16_t highWaterMark = 0;
};

/**
 * PSRAM-owned, allocation-free scheduler for MIDI CC author transitions.
 *
 * Temporalization happens before destination arbitration: distinct authors
 * keep distinct deadlines without hiding conflicts or removal tombstones.
 * Deadlines use the realtime queue's wrap-safe half-range contract; equal
 * deadlines retain producer order through a stable sequence.
 *
 * The stable author space has 4,160 slots: 64 Sequencer lanes followed by
 * 2,048 LIVE_MANUAL overrides and 2,048 Macro bases (16 Tracks x 16 Pages x
 * 8 Macros). A manual override may coexist with its base; only
 * MACRO_COMPUTED and MACRO_STATIC are mutually exclusive states of one Macro
 * position and intentionally share that base slot. Every slot owns an
 * intrusive head/tail; the 8,192 mutation nodes come from a
 * fixed pool and a heap of 16-bit node indices.
 *
 * `beginDue()` reserves only complete deadline groups and copies them to
 * caller-owned scratch. Apply/resolve success calls `commitDue()`; downstream
 * failure calls `rollbackDue()` and restores identical observable ordering.
 * While reserved, another push/begin is rejected, guaranteeing rollback space.
 * This object is intentionally large and MUST be allocated in PSRAM on target.
 */
class TemporalMidiCcAuthorSpool final {
public:
    static constexpr size_t CAPACITY = 8192U;
    static constexpr uint16_t LANE_AUTHOR_SLOT_COUNT = 64U;
    static constexpr uint16_t MACRO_POSITION_COUNT = 2048U;
    static constexpr uint16_t LIVE_AUTHOR_SLOT_COUNT = MACRO_POSITION_COUNT;
    static constexpr uint16_t MACRO_AUTHOR_SLOT_COUNT = MACRO_POSITION_COUNT;
    static constexpr uint16_t AUTHOR_SLOT_COUNT =
        LANE_AUTHOR_SLOT_COUNT + LIVE_AUTHOR_SLOT_COUNT +
        MACRO_AUTHOR_SLOT_COUNT;
    static constexpr uint16_t INVALID_INDEX = UINT16_MAX;
    // A complete frame can replace every author at one deadline: 320 REMOVE
    // plus 320 UPDATE transitions must remain one indivisible group.
    static constexpr size_t MAX_DUE_TRANSITIONS =
        2U * core::state::shared::MidiCcResolutionTelemetry::MAX_CANDIDATES;

    TemporalMidiCcAuthorSpool();
    TemporalMidiCcAuthorSpool(const TemporalMidiCcAuthorSpool&) = delete;
    TemporalMidiCcAuthorSpool& operator=(
        const TemporalMidiCcAuthorSpool&
    ) = delete;

    TemporalMidiCcAuthorSpoolResult pushBatch(
        const TemporalMidiCcAuthorTransition* transitions,
        size_t count
    );

    /**
     * Reserves due transitions without splitting equal-deadline groups.
     * If the earliest group does not fit, nothing is reserved.
     */
    TemporalMidiCcAuthorSpoolResult beginDue(
        uint32_t nowUs,
        TemporalMidiCcAuthorTransition* scratch,
        size_t scratchCapacity
    );

    [[nodiscard]] bool commitDue();
    [[nodiscard]] bool rollbackDue();

    /** Cold O(capacity + author slots); zero while a transaction is active. */
    size_t cancelTrack(uint8_t trackIndex);
    /**
     * Cold O(capacity + 64), one-pass cancellation for a set of Lane authors.
     * Bit N addresses Sequencer Lane stable address N. This is the lifecycle
     * restore/paste path: a frame may replace all 64 generations at once and
     * must not rebuild the 8,192-node heap once per Lane.
     */
    size_t cancelLaneAuthors(uint64_t laneAuthorMask);
    /** Cold O(capacity + author slots); used by the transport Lane boundary. */
    size_t cancelCandidateClass(
        core::state::shared::MidiCcCandidateClass candidateClass
    );
    void clear();

    /** Includes reserved, not-yet-committed transitions. */
    [[nodiscard]] size_t size() const { return heap_count_ + pending_count_; }
    [[nodiscard]] size_t queuedSize() const { return heap_count_; }
    [[nodiscard]] constexpr size_t capacity() const { return CAPACITY; }
    [[nodiscard]] bool empty() const { return size() == 0U; }
    [[nodiscard]] bool hasDue(uint32_t nowUs) const {
        return heap_count_ > 0U && due_(nodes_[heap_[0]], nowUs);
    }
    [[nodiscard]] bool transactionActive() const { return transaction_active_; }
    [[nodiscard]] const TemporalMidiCcAuthorSpoolDiagnostics& diagnostics() const {
        return diagnostics_;
    }

    /** Canonical collision-free lane/Macro-position mapping. */
    [[nodiscard]] static bool authorSlotIndex(
        const core::state::shared::MidiCcAuthor& author,
        uint16_t& slotIndex
    );
private:
    struct Node {
        TemporalMidiCcAuthorTransition transition{};
        uint32_t sequence = 0;
        uint16_t previousAuthorNode = INVALID_INDEX;
        uint16_t nextAuthorNode = INVALID_INDEX;
    };

    struct AuthorSlot {
        uint16_t head = INVALID_INDEX;
        uint16_t tail = INVALID_INDEX;
    };

    static bool validTransition_(
        const TemporalMidiCcAuthorTransition& transition
    );
    static bool due_(const Node& node, uint32_t nowUs);
    static bool comesBefore_(const Node& lhs, const Node& rhs);
    static uint16_t boundedCount_(size_t count);
    static uint32_t saturatingAdd_(uint32_t lhs, uint32_t rhs);
    static uint8_t trackForSlot_(uint16_t slotIndex);

    uint16_t allocateNodeNoFail_();
    void releaseNode_(uint16_t nodeIndex);
    void linkAuthorTail_(uint16_t nodeIndex);
    void unlinkAuthorNode_(uint16_t nodeIndex);
    void restoreReservedNode_(uint16_t nodeIndex);
    void heapPushNoFail_(uint16_t nodeIndex);
    uint16_t heapPopMinNoFail_();
    void siftUp_(size_t heapPosition);
    void siftDown_(size_t heapPosition);
    void rebuildHeap_();
    void initializeStorage_();
    void updateHighWaterMark_();
    void recordRejected_(size_t count);

    std::array<Node, CAPACITY> nodes_{};
    std::array<uint16_t, CAPACITY> heap_{};
    std::array<uint16_t, MAX_DUE_TRANSITIONS> pending_{};
    std::array<AuthorSlot, AUTHOR_SLOT_COUNT> author_slots_{};
    size_t heap_count_ = 0;
    size_t pending_count_ = 0;
    uint16_t free_head_ = INVALID_INDEX;
    uint32_t next_sequence_ = 0;
    bool transaction_active_ = false;
    TemporalMidiCcAuthorSpoolDiagnostics diagnostics_{};
};

static_assert(sizeof(TemporalMidiCcAuthorTransition) <= 16U);
static_assert(sizeof(TemporalMidiCcAuthorSpool) < 256U * 1024U);

}  // namespace core::sequencer
