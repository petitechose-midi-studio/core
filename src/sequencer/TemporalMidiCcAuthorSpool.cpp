#include "sequencer/TemporalMidiCcAuthorSpool.hpp"

#include <algorithm>
#include <cassert>
#include <utility>

#include <config/PlatformCompat.hpp>

#include "state/macro/MacroConstants.hpp"
#include "state/sequencer/SequencerCcLaneDomain.hpp"

namespace core::sequencer {

namespace {

using core::state::shared::MidiCcCandidateClass;

constexpr uint16_t kMacroSlotsPerTrack =
    core::state::macro::PAGE_COUNT * core::state::macro::MACRO_COUNT;

static_assert(core::state::macro::TRACK_COUNT == 16U);
static_assert(kMacroSlotsPerTrack == 128U);
static_assert(
    core::state::macro::TRACK_COUNT * kMacroSlotsPerTrack ==
    TemporalMidiCcAuthorSpool::MACRO_POSITION_COUNT
);
static_assert(
    core::state::macro::TRACK_COUNT *
        core::state::sequencer::SequencerCcLaneBank::MAX_LANES ==
    TemporalMidiCcAuthorSpool::LANE_AUTHOR_SLOT_COUNT
);

bool validUpdateBody(const TemporalMidiCcAuthorTransition& transition) {
    using core::state::shared::MidiCcDestinationIdentity;
    using core::state::shared::MidiCcRouteValidity;

    if (transition.localValue > 127U ||
        transition.destination.identity.controller > 127U) {
        return false;
    }
    const auto validity = transition.destination.routeValidity;
    if (validity == MidiCcRouteValidity::VALID) {
        return transition.destination.identity.port !=
                   MidiCcDestinationIdentity::INVALID_PORT &&
               transition.destination.identity.channel <= 15U;
    }
    if (validity == MidiCcRouteValidity::NO_ROUTE) {
        return transition.destination.identity.channel <= 15U ||
               transition.destination.identity.channel ==
                   MidiCcDestinationIdentity::INVALID_CHANNEL;
    }
    return false;
}

}  // namespace

FLASHMEM TemporalMidiCcAuthorSpool::TemporalMidiCcAuthorSpool() {
    initializeStorage_();
}

TemporalMidiCcAuthorSpoolResult TemporalMidiCcAuthorSpool::pushBatch(
    const TemporalMidiCcAuthorTransition* transitions,
    size_t count
) {
    TemporalMidiCcAuthorSpoolResult result{};
    result.requestedCount = boundedCount_(count);
    if (transaction_active_) {
        result.status = TemporalMidiCcAuthorSpoolStatus::TRANSACTION_ACTIVE;
        recordRejected_(count);
        return result;
    }
    if (count > 0U && transitions == nullptr) {
        result.status = TemporalMidiCcAuthorSpoolStatus::INVALID_INPUT;
        recordRejected_(count);
        return result;
    }
    // beginDue() deliberately refuses to split one physical deadline: doing
    // so would expose an intermediate author set to arbitration. Therefore no
    // accepted push may make a deadline group larger than the canonical due
    // scratch. Production batches are already bounded by this same 320 remove
    // + 320 update frame envelope.
    if (count > MAX_DUE_TRANSITIONS) {
        result.status = TemporalMidiCcAuthorSpoolStatus::CAPACITY_EXCEEDED;
        recordRejected_(count);
        return result;
    }
    if (count > CAPACITY - heap_count_) {
        result.status = TemporalMidiCcAuthorSpoolStatus::CAPACITY_EXCEEDED;
        recordRejected_(count);
        return result;
    }
    for (size_t index = 0; index < count; ++index) {
        if (!validTransition_(transitions[index])) {
            result.status = TemporalMidiCcAuthorSpoolStatus::INVALID_INPUT;
            recordRejected_(count);
            return result;
        }
    }
    // Count each deadline touched by this batch exactly once. This avoids any
    // persistent hash/scratch allocation (the spool lives in PSRAM) while the
    // real producer has at most one deadline per Track. The deliberately
    // adversarial 640-distinct-deadline case remains bounded and cold.
    for (size_t first = 0U; first < count; ++first) {
        const uint32_t deadline = transitions[first].deadlineUs;
        bool alreadyCounted = false;
        for (size_t previous = 0U; previous < first; ++previous) {
            if (transitions[previous].deadlineUs == deadline) {
                alreadyCounted = true;
                break;
            }
        }
        if (alreadyCounted) continue;

        size_t deadlineCount = 0U;
        for (size_t incoming = first; incoming < count; ++incoming) {
            if (transitions[incoming].deadlineUs == deadline) ++deadlineCount;
        }
        for (size_t queued = 0U; queued < heap_count_; ++queued) {
            if (nodes_[heap_[queued]].transition.deadlineUs == deadline) {
                ++deadlineCount;
            }
        }
        if (deadlineCount > MAX_DUE_TRANSITIONS) {
            result.status = TemporalMidiCcAuthorSpoolStatus::CAPACITY_EXCEEDED;
            recordRejected_(count);
            return result;
        }
    }

    for (size_t index = 0; index < count; ++index) {
        const uint16_t nodeIndex = allocateNodeNoFail_();
        nodes_[nodeIndex] = Node{
            .transition = transitions[index],
            .sequence = next_sequence_++,
        };
        linkAuthorTail_(nodeIndex);
        heapPushNoFail_(nodeIndex);
    }
    diagnostics_.pushedTransitionCount = saturatingAdd_(
        diagnostics_.pushedTransitionCount,
        static_cast<uint32_t>(std::min<size_t>(count, UINT32_MAX))
    );
    updateHighWaterMark_();
    result.status = TemporalMidiCcAuthorSpoolStatus::OK;
    result.transferredCount = result.requestedCount;
    return result;
}

TemporalMidiCcAuthorSpoolResult TemporalMidiCcAuthorSpool::beginDue(
    uint32_t nowUs,
    TemporalMidiCcAuthorTransition* scratch,
    size_t scratchCapacity
) {
    TemporalMidiCcAuthorSpoolResult result{};
    if (transaction_active_) {
        result.status = TemporalMidiCcAuthorSpoolStatus::TRANSACTION_ACTIVE;
        return result;
    }
    if (scratchCapacity > MAX_DUE_TRANSITIONS ||
        (scratchCapacity > 0U && scratch == nullptr)) {
        result.status = TemporalMidiCcAuthorSpoolStatus::INVALID_INPUT;
        return result;
    }

    result.status = TemporalMidiCcAuthorSpoolStatus::OK;
    if (heap_count_ == 0U || !due_(nodes_[heap_[0]], nowUs)) {
        return result;
    }
    if (scratchCapacity == 0U) {
        result.status = TemporalMidiCcAuthorSpoolStatus::CAPACITY_EXCEEDED;
        result.requestedCount = 1U;
        return result;
    }

    const uint32_t currentDeadline = nodes_[heap_[0]].transition.deadlineUs;
    while (pending_count_ < scratchCapacity &&
           heap_count_ > 0U &&
           due_(nodes_[heap_[0]], nowUs) &&
           nodes_[heap_[0]].transition.deadlineUs == currentDeadline) {
        pending_[pending_count_++] = heapPopMinNoFail_();
    }

    if (pending_count_ == scratchCapacity &&
        heap_count_ > 0U &&
        due_(nodes_[heap_[0]], nowUs) &&
        nodes_[heap_[0]].transition.deadlineUs == currentDeadline) {
        for (size_t index = 0U; index < pending_count_; ++index) {
            restoreReservedNode_(pending_[index]);
        }
        pending_count_ = 0U;
        result.status = TemporalMidiCcAuthorSpoolStatus::CAPACITY_EXCEEDED;
        result.requestedCount = boundedCount_(scratchCapacity + 1U);
        return result;
    }

    for (size_t index = 0; index < pending_count_; ++index) {
        scratch[index] = nodes_[pending_[index]].transition;
    }
    transaction_active_ = pending_count_ > 0U;
    result.requestedCount = boundedCount_(pending_count_);
    result.transferredCount = result.requestedCount;
    result.deadlineGroupCount = pending_count_ > 0U ? 1U : 0U;
    return result;
}

bool TemporalMidiCcAuthorSpool::commitDue() {
    if (!transaction_active_) {
        return false;
    }
    const size_t committed = pending_count_;
    for (size_t index = 0; index < pending_count_; ++index) {
        releaseNode_(pending_[index]);
    }
    pending_count_ = 0U;
    transaction_active_ = false;
    diagnostics_.committedTransitionCount = saturatingAdd_(
        diagnostics_.committedTransitionCount,
        static_cast<uint32_t>(committed)
    );
    return true;
}

bool TemporalMidiCcAuthorSpool::rollbackDue() {
    if (!transaction_active_) {
        return false;
    }
    const size_t rolledBack = pending_count_;
    for (size_t index = 0; index < pending_count_; ++index) {
        restoreReservedNode_(pending_[index]);
    }
    pending_count_ = 0U;
    transaction_active_ = false;
    diagnostics_.rolledBackTransitionCount = saturatingAdd_(
        diagnostics_.rolledBackTransitionCount,
        static_cast<uint32_t>(rolledBack)
    );
    return true;
}

FLASHMEM size_t TemporalMidiCcAuthorSpool::cancelTrack(uint8_t trackIndex) {
    if (transaction_active_ || trackIndex >= 16U) {
        return 0U;
    }

    size_t writePosition = 0U;
    for (size_t readPosition = 0; readPosition < heap_count_; ++readPosition) {
        const uint16_t nodeIndex = heap_[readPosition];
        if (nodes_[nodeIndex].transition.trackIndex != trackIndex) {
            heap_[writePosition++] = nodeIndex;
        }
    }
    const size_t removed = heap_count_ - writePosition;
    heap_count_ = writePosition;

    for (uint16_t slotIndex = 0; slotIndex < AUTHOR_SLOT_COUNT; ++slotIndex) {
        if (trackForSlot_(slotIndex) != trackIndex) {
            continue;
        }
        auto& slot = author_slots_[slotIndex];
        uint16_t nodeIndex = slot.head;
        while (nodeIndex != INVALID_INDEX) {
            const uint16_t next = nodes_[nodeIndex].nextAuthorNode;
            releaseNode_(nodeIndex);
            nodeIndex = next;
        }
        slot.head = INVALID_INDEX;
        slot.tail = INVALID_INDEX;
    }
    rebuildHeap_();
    diagnostics_.cancelledTransitionCount = saturatingAdd_(
        diagnostics_.cancelledTransitionCount,
        static_cast<uint32_t>(removed)
    );
    return removed;
}

FLASHMEM size_t TemporalMidiCcAuthorSpool::cancelLaneAuthors(
    uint64_t laneAuthorMask
) {
    if (transaction_active_ || laneAuthorMask == 0U) return 0U;

    // Compact the heap once while every node and author chain is still valid.
    // Releasing nodes first would let the free-list links overwrite the author
    // links needed by the scan below.
    size_t writePosition = 0U;
    for (size_t readPosition = 0U; readPosition < heap_count_; ++readPosition) {
        const uint16_t nodeIndex = heap_[readPosition];
        uint16_t slotIndex = 0U;
        const bool valid = authorSlotIndex(
            nodes_[nodeIndex].transition.author,
            slotIndex
        );
        assert(valid);
        const bool cancelledLane = slotIndex < LANE_AUTHOR_SLOT_COUNT &&
            (laneAuthorMask & (UINT64_C(1) << slotIndex)) != 0U;
        if (!cancelledLane) heap_[writePosition++] = nodeIndex;
    }
    heap_count_ = writePosition;

    size_t removed = 0U;
    for (uint16_t slotIndex = 0U;
         slotIndex < LANE_AUTHOR_SLOT_COUNT;
         ++slotIndex) {
        if ((laneAuthorMask & (UINT64_C(1) << slotIndex)) == 0U) continue;
        auto& slot = author_slots_[slotIndex];
        uint16_t nodeIndex = slot.head;
        while (nodeIndex != INVALID_INDEX) {
            const uint16_t next = nodes_[nodeIndex].nextAuthorNode;
            releaseNode_(nodeIndex);
            nodeIndex = next;
            ++removed;
        }
        slot.head = INVALID_INDEX;
        slot.tail = INVALID_INDEX;
    }

    rebuildHeap_();
    diagnostics_.cancelledTransitionCount = saturatingAdd_(
        diagnostics_.cancelledTransitionCount,
        static_cast<uint32_t>(removed)
    );
    return removed;
}

FLASHMEM size_t TemporalMidiCcAuthorSpool::cancelCandidateClass(
    MidiCcCandidateClass candidateClass
) {
    if (transaction_active_) return 0U;
    size_t removed = 0U;
    // Computed and Static share one base slot, so either request deliberately
    // invalidates the complete base class. LIVE and Lane remain independent.
    const bool baseClass = candidateClass == MidiCcCandidateClass::MACRO_COMPUTED ||
                           candidateClass == MidiCcCandidateClass::MACRO_STATIC;
    for (uint16_t slotIndex = 0U; slotIndex < AUTHOR_SLOT_COUNT; ++slotIndex) {
        const bool matches = candidateClass == MidiCcCandidateClass::SEQUENCER_CC_LANE
            ? slotIndex < LANE_AUTHOR_SLOT_COUNT
            : candidateClass == MidiCcCandidateClass::LIVE_MANUAL
                ? slotIndex >= LANE_AUTHOR_SLOT_COUNT &&
                      slotIndex < LANE_AUTHOR_SLOT_COUNT + LIVE_AUTHOR_SLOT_COUNT
                : baseClass
                    ? slotIndex >= LANE_AUTHOR_SLOT_COUNT + LIVE_AUTHOR_SLOT_COUNT
                    : false;
        if (!matches) continue;
        auto& slot = author_slots_[slotIndex];
        uint16_t nodeIndex = slot.head;
        while (nodeIndex != INVALID_INDEX) {
            const uint16_t next = nodes_[nodeIndex].nextAuthorNode;
            releaseNode_(nodeIndex);
            nodeIndex = next;
            ++removed;
        }
        slot.head = INVALID_INDEX;
        slot.tail = INVALID_INDEX;
    }

    size_t writePosition = 0U;
    for (size_t readPosition = 0U; readPosition < heap_count_; ++readPosition) {
        const auto cls = nodes_[heap_[readPosition]].transition.author.candidateClass;
        const bool matches = cls == candidateClass ||
            (baseClass && (cls == MidiCcCandidateClass::MACRO_COMPUTED ||
                           cls == MidiCcCandidateClass::MACRO_STATIC));
        if (!matches) heap_[writePosition++] = heap_[readPosition];
    }
    heap_count_ = writePosition;
    rebuildHeap_();
    diagnostics_.cancelledTransitionCount = saturatingAdd_(
        diagnostics_.cancelledTransitionCount,
        static_cast<uint32_t>(removed)
    );
    return removed;
}

FLASHMEM void TemporalMidiCcAuthorSpool::clear() {
    diagnostics_.clearedTransitionCount = saturatingAdd_(
        diagnostics_.clearedTransitionCount,
        static_cast<uint32_t>(size())
    );
    initializeStorage_();
}

bool TemporalMidiCcAuthorSpool::authorSlotIndex(
    const core::state::shared::MidiCcAuthor& author,
    uint16_t& slotIndex
) {
    switch (author.candidateClass) {
        case MidiCcCandidateClass::SEQUENCER_CC_LANE:
            if (author.stableAddress >= LANE_AUTHOR_SLOT_COUNT) return false;
            slotIndex = author.stableAddress;
            return true;
        case MidiCcCandidateClass::LIVE_MANUAL:
            if (author.stableAddress >= MACRO_POSITION_COUNT) return false;
            slotIndex = static_cast<uint16_t>(
                LANE_AUTHOR_SLOT_COUNT + author.stableAddress
            );
            return true;
        case MidiCcCandidateClass::MACRO_COMPUTED:
        case MidiCcCandidateClass::MACRO_STATIC:
            if (author.stableAddress >= MACRO_POSITION_COUNT) return false;
            slotIndex = static_cast<uint16_t>(
                LANE_AUTHOR_SLOT_COUNT + LIVE_AUTHOR_SLOT_COUNT +
                author.stableAddress
            );
            return true;
        default:
            return false;
    }
}

bool TemporalMidiCcAuthorSpool::validTransition_(
    const TemporalMidiCcAuthorTransition& transition
) {
    uint16_t slotIndex = 0U;
    if (transition.trackIndex >= 16U ||
        !authorSlotIndex(transition.author, slotIndex) ||
        trackForSlot_(slotIndex) != transition.trackIndex) {
        return false;
    }
    switch (transition.operation) {
        case TemporalMidiCcAuthorOperation::UPDATE:
            return validUpdateBody(transition);
        case TemporalMidiCcAuthorOperation::REMOVE:
            return true;
        default:
            return false;
    }
}

bool TemporalMidiCcAuthorSpool::due_(const Node& node, uint32_t nowUs) {
    return static_cast<int32_t>(nowUs - node.transition.deadlineUs) >= 0;
}

bool TemporalMidiCcAuthorSpool::comesBefore_(const Node& lhs, const Node& rhs) {
    const int32_t deadlineDelta = static_cast<int32_t>(
        lhs.transition.deadlineUs - rhs.transition.deadlineUs
    );
    if (deadlineDelta != 0) {
        return deadlineDelta < 0;
    }
    return static_cast<int32_t>(lhs.sequence - rhs.sequence) < 0;
}

uint16_t TemporalMidiCcAuthorSpool::boundedCount_(size_t count) {
    return static_cast<uint16_t>(std::min<size_t>(count, UINT16_MAX));
}

uint32_t TemporalMidiCcAuthorSpool::saturatingAdd_(uint32_t lhs, uint32_t rhs) {
    return rhs > UINT32_MAX - lhs ? UINT32_MAX : lhs + rhs;
}

uint8_t TemporalMidiCcAuthorSpool::trackForSlot_(uint16_t slotIndex) {
    if (slotIndex < LANE_AUTHOR_SLOT_COUNT) {
        return static_cast<uint8_t>(
            slotIndex / core::state::sequencer::SequencerCcLaneBank::MAX_LANES
        );
    }
    return static_cast<uint8_t>(
        ((slotIndex - LANE_AUTHOR_SLOT_COUNT) % MACRO_POSITION_COUNT) /
        kMacroSlotsPerTrack
    );
}

uint16_t TemporalMidiCcAuthorSpool::allocateNodeNoFail_() {
    assert(free_head_ != INVALID_INDEX);
    const uint16_t result = free_head_;
    free_head_ = nodes_[result].nextAuthorNode;
    return result;
}

void TemporalMidiCcAuthorSpool::releaseNode_(uint16_t nodeIndex) {
    nodes_[nodeIndex].previousAuthorNode = INVALID_INDEX;
    nodes_[nodeIndex].nextAuthorNode = free_head_;
    free_head_ = nodeIndex;
}

void TemporalMidiCcAuthorSpool::linkAuthorTail_(uint16_t nodeIndex) {
    uint16_t slotIndex = 0U;
    const bool valid = authorSlotIndex(
        nodes_[nodeIndex].transition.author,
        slotIndex
    );
    assert(valid);
    auto& slot = author_slots_[slotIndex];
    nodes_[nodeIndex].previousAuthorNode = slot.tail;
    nodes_[nodeIndex].nextAuthorNode = INVALID_INDEX;
    if (slot.tail == INVALID_INDEX) {
        slot.head = nodeIndex;
    } else {
        nodes_[slot.tail].nextAuthorNode = nodeIndex;
    }
    slot.tail = nodeIndex;
}

void TemporalMidiCcAuthorSpool::unlinkAuthorNode_(uint16_t nodeIndex) {
    uint16_t slotIndex = 0U;
    const bool valid = authorSlotIndex(
        nodes_[nodeIndex].transition.author,
        slotIndex
    );
    assert(valid);
    auto& slot = author_slots_[slotIndex];
    const uint16_t previous = nodes_[nodeIndex].previousAuthorNode;
    const uint16_t next = nodes_[nodeIndex].nextAuthorNode;
    if (previous == INVALID_INDEX) {
        slot.head = next;
    } else {
        nodes_[previous].nextAuthorNode = next;
    }
    if (next == INVALID_INDEX) {
        slot.tail = previous;
    } else {
        nodes_[next].previousAuthorNode = previous;
    }
    nodes_[nodeIndex].previousAuthorNode = INVALID_INDEX;
    nodes_[nodeIndex].nextAuthorNode = INVALID_INDEX;
}

void TemporalMidiCcAuthorSpool::restoreReservedNode_(uint16_t nodeIndex) {
    linkAuthorTail_(nodeIndex);
    heapPushNoFail_(nodeIndex);
}

void TemporalMidiCcAuthorSpool::heapPushNoFail_(uint16_t nodeIndex) {
    assert(heap_count_ < heap_.size());
    heap_[heap_count_] = nodeIndex;
    siftUp_(heap_count_);
    ++heap_count_;
}

uint16_t TemporalMidiCcAuthorSpool::heapPopMinNoFail_() {
    assert(heap_count_ > 0U);
    const uint16_t minimum = heap_[0];
    --heap_count_;
    if (heap_count_ > 0U) {
        heap_[0] = heap_[heap_count_];
        siftDown_(0U);
    }
    unlinkAuthorNode_(minimum);
    return minimum;
}

void TemporalMidiCcAuthorSpool::siftUp_(size_t heapPosition) {
    while (heapPosition > 0U) {
        const size_t parent = (heapPosition - 1U) / 2U;
        if (!comesBefore_(nodes_[heap_[heapPosition]], nodes_[heap_[parent]])) {
            return;
        }
        std::swap(heap_[heapPosition], heap_[parent]);
        heapPosition = parent;
    }
}

void TemporalMidiCcAuthorSpool::siftDown_(size_t heapPosition) {
    while (true) {
        const size_t left = heapPosition * 2U + 1U;
        if (left >= heap_count_) return;
        const size_t right = left + 1U;
        size_t next = left;
        if (right < heap_count_ &&
            comesBefore_(nodes_[heap_[right]], nodes_[heap_[left]])) {
            next = right;
        }
        if (!comesBefore_(nodes_[heap_[next]], nodes_[heap_[heapPosition]])) {
            return;
        }
        std::swap(heap_[heapPosition], heap_[next]);
        heapPosition = next;
    }
}

FLASHMEM void TemporalMidiCcAuthorSpool::rebuildHeap_() {
    for (size_t parent = heap_count_ / 2U; parent > 0U; --parent) {
        siftDown_(parent - 1U);
    }
}

FLASHMEM void TemporalMidiCcAuthorSpool::initializeStorage_() {
    for (uint16_t slotIndex = 0U; slotIndex < AUTHOR_SLOT_COUNT; ++slotIndex) {
        auto& slot = author_slots_[slotIndex];
        slot.head = INVALID_INDEX;
        slot.tail = INVALID_INDEX;
    }
    for (uint16_t nodeIndex = 0U; nodeIndex < CAPACITY; ++nodeIndex) {
        nodes_[nodeIndex].previousAuthorNode = INVALID_INDEX;
        nodes_[nodeIndex].nextAuthorNode = static_cast<uint16_t>(
            nodeIndex + 1U < CAPACITY ? nodeIndex + 1U : INVALID_INDEX
        );
    }
    free_head_ = 0U;
    heap_count_ = 0U;
    pending_count_ = 0U;
    transaction_active_ = false;
    next_sequence_ = 0U;
}

void TemporalMidiCcAuthorSpool::updateHighWaterMark_() {
    diagnostics_.highWaterMark = std::max<uint16_t>(
        diagnostics_.highWaterMark,
        static_cast<uint16_t>(size())
    );
}

void TemporalMidiCcAuthorSpool::recordRejected_(size_t count) {
    diagnostics_.rejectedBatchCount = saturatingAdd_(
        diagnostics_.rejectedBatchCount,
        1U
    );
    diagnostics_.rejectedTransitionCount = saturatingAdd_(
        diagnostics_.rejectedTransitionCount,
        static_cast<uint32_t>(std::min<size_t>(count, UINT32_MAX))
    );
}

}  // namespace core::sequencer
