#include <array>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <iostream>

#include "sequencer/TemporalMidiCcAuthorSpool.hpp"

namespace {

using core::sequencer::TemporalMidiCcAuthorOperation;
using core::sequencer::TemporalMidiCcAuthorSpool;
using core::sequencer::TemporalMidiCcAuthorSpoolStatus;
using core::sequencer::TemporalMidiCcAuthorTransition;
using core::state::shared::MidiCcAuthor;
using core::state::shared::MidiCcCandidateClass;
using core::state::shared::MidiCcRouteValidity;

TemporalMidiCcAuthorTransition update(
    uint32_t deadlineUs,
    uint8_t track,
    MidiCcCandidateClass candidateClass,
    uint16_t stableAddress,
    uint8_t value,
    uint8_t controller = 74U
) {
    return {
        .deadlineUs = deadlineUs,
        .author = {
            .candidateClass = candidateClass,
            .stableAddress = stableAddress,
        },
        .destination = {
            .identity = {
                .port = 0U,
                .channel = track,
                .controller = controller,
            },
            .routeValidity = MidiCcRouteValidity::VALID,
        },
        .localValue = value,
        .trackIndex = track,
        .operation = TemporalMidiCcAuthorOperation::UPDATE,
    };
}

TemporalMidiCcAuthorTransition remove(
    uint32_t deadlineUs,
    uint8_t track,
    MidiCcCandidateClass candidateClass,
    uint16_t stableAddress
) {
    auto result = update(
        deadlineUs,
        track,
        candidateClass,
        stableAddress,
        0U
    );
    result.operation = TemporalMidiCcAuthorOperation::REMOVE;
    return result;
}

void test_author_slot_mapping_is_canonical_and_collision_free() {
    uint16_t slot = 0U;
    assert(TemporalMidiCcAuthorSpool::authorSlotIndex(
        MidiCcAuthor{MidiCcCandidateClass::SEQUENCER_CC_LANE, 0U}, slot
    ));
    assert(slot == 0U);
    assert(TemporalMidiCcAuthorSpool::authorSlotIndex(
        MidiCcAuthor{MidiCcCandidateClass::SEQUENCER_CC_LANE, 63U}, slot
    ));
    assert(slot == 63U);

    assert(TemporalMidiCcAuthorSpool::authorSlotIndex(
        MidiCcAuthor{MidiCcCandidateClass::LIVE_MANUAL, 0U}, slot
    ));
    assert(slot == 64U);
    assert(TemporalMidiCcAuthorSpool::authorSlotIndex(
        MidiCcAuthor{MidiCcCandidateClass::LIVE_MANUAL, 2047U}, slot
    ));
    assert(slot == 2111U);
    for (const auto candidateClass : {
             MidiCcCandidateClass::MACRO_COMPUTED,
             MidiCcCandidateClass::MACRO_STATIC,
         }) {
        assert(TemporalMidiCcAuthorSpool::authorSlotIndex(
            MidiCcAuthor{candidateClass, 0U}, slot
        ));
        assert(slot == 2112U);
    }
    assert(TemporalMidiCcAuthorSpool::authorSlotIndex(
        MidiCcAuthor{MidiCcCandidateClass::MACRO_STATIC, 2047U}, slot
    ));
    assert(slot == 4159U);
    assert(!TemporalMidiCcAuthorSpool::authorSlotIndex(
        MidiCcAuthor{MidiCcCandidateClass::SEQUENCER_CC_LANE, 64U}, slot
    ));
    assert(!TemporalMidiCcAuthorSpool::authorSlotIndex(
        MidiCcAuthor{MidiCcCandidateClass::MACRO_STATIC, 2048U}, slot
    ));

    std::cout << "[PASS] canonical 4160-slot author mapping\n";
}

void test_deadline_order_and_commit() {
    TemporalMidiCcAuthorSpool spool;
    const std::array transitions{
        update(300U, 0U, MidiCcCandidateClass::SEQUENCER_CC_LANE, 0U, 30U),
        update(100U, 0U, MidiCcCandidateClass::SEQUENCER_CC_LANE, 1U, 10U),
        remove(200U, 0U, MidiCcCandidateClass::SEQUENCER_CC_LANE, 2U),
    };
    assert(spool.pushBatch(transitions.data(), transitions.size()).ok());

    std::array<TemporalMidiCcAuthorTransition, 8> scratch{};
    auto due = spool.beginDue(250U, scratch.data(), scratch.size());
    assert(due.ok());
    assert(due.transferredCount == 1U);
    assert(due.deadlineGroupCount == 1U);
    assert(scratch[0].deadlineUs == 100U);
    assert(scratch[0].localValue == 10U);
    assert(spool.size() == 3U);
    assert(spool.queuedSize() == 2U);
    assert(spool.commitDue());
    assert(spool.size() == 2U);

    due = spool.beginDue(250U, scratch.data(), scratch.size());
    assert(due.transferredCount == 1U);
    assert(scratch[0].deadlineUs == 200U);
    assert(scratch[0].operation == TemporalMidiCcAuthorOperation::REMOVE);
    assert(spool.commitDue());

    due = spool.beginDue(300U, scratch.data(), scratch.size());
    assert(due.transferredCount == 1U);
    assert(scratch[0].localValue == 30U);
    assert(spool.commitDue());
    assert(spool.empty());

    std::cout << "[PASS] deadline order and commit\n";
}

void test_equal_deadline_is_stable_even_for_one_author() {
    TemporalMidiCcAuthorSpool spool;
    std::array<TemporalMidiCcAuthorTransition, 6> transitions{};
    for (uint8_t index = 0U; index < transitions.size(); ++index) {
        transitions[index] = update(
            1000U,
            0U,
            MidiCcCandidateClass::MACRO_COMPUTED,
            0U,
            static_cast<uint8_t>(20U + index)
        );
    }
    assert(spool.pushBatch(transitions.data(), transitions.size()).ok());

    std::array<TemporalMidiCcAuthorTransition, 8> scratch{};
    const auto due = spool.beginDue(1000U, scratch.data(), scratch.size());
    assert(due.transferredCount == transitions.size());
    assert(due.deadlineGroupCount == 1U);
    for (uint8_t index = 0U; index < transitions.size(); ++index) {
        assert(scratch[index].localValue == 20U + index);
    }
    assert(spool.commitDue());

    std::cout << "[PASS] stable producer order at equal deadline\n";
}

void test_wrap_safe_deadlines() {
    TemporalMidiCcAuthorSpool spool;
    const std::array transitions{
        update(5U, 0U, MidiCcCandidateClass::SEQUENCER_CC_LANE, 0U, 3U),
        update(UINT32_MAX - 2U, 0U,
               MidiCcCandidateClass::SEQUENCER_CC_LANE, 1U, 1U),
        update(1U, 0U, MidiCcCandidateClass::SEQUENCER_CC_LANE, 2U, 2U),
    };
    assert(spool.pushBatch(transitions.data(), transitions.size()).ok());
    std::array<TemporalMidiCcAuthorTransition, 4> scratch{};

    auto due = spool.beginDue(UINT32_MAX - 1U, scratch.data(), scratch.size());
    assert(due.transferredCount == 1U && scratch[0].localValue == 1U);
    assert(spool.commitDue());
    due = spool.beginDue(2U, scratch.data(), scratch.size());
    assert(due.transferredCount == 1U && scratch[0].localValue == 2U);
    assert(spool.commitDue());
    due = spool.beginDue(5U, scratch.data(), scratch.size());
    assert(due.transferredCount == 1U && scratch[0].localValue == 3U);
    assert(spool.commitDue());

    std::cout << "[PASS] wrap-safe microsecond deadlines\n";
}

void test_push_batch_is_atomic_and_capacity_bounded() {
    TemporalMidiCcAuthorSpool spool;
    const auto valid = update(
        100U, 0U, MidiCcCandidateClass::SEQUENCER_CC_LANE, 0U, 10U
    );
    auto invalid = valid;
    invalid.trackIndex = 1U;  // Stable lane address 0 belongs to Track 0.
    const std::array malformed{valid, invalid};
    auto result = spool.pushBatch(malformed.data(), malformed.size());
    assert(result.status == TemporalMidiCcAuthorSpoolStatus::INVALID_INPUT);
    assert(spool.empty());

    static std::array<TemporalMidiCcAuthorTransition,
                      TemporalMidiCcAuthorSpool::CAPACITY> full{};
    for (size_t index = 0; index < full.size(); ++index) {
        full[index] = update(
            static_cast<uint32_t>(1000U + index),
            0U,
            MidiCcCandidateClass::MACRO_STATIC,
            0U,
            static_cast<uint8_t>(index & 0x7FU)
        );
    }
    constexpr size_t batchSize = 512U;
    for (size_t offset = 0U; offset < full.size(); offset += batchSize) {
        result = spool.pushBatch(full.data() + offset, batchSize);
        assert(result.ok());
    }
    assert(spool.size() == TemporalMidiCcAuthorSpool::CAPACITY);
    const auto before = spool.size();
    result = spool.pushBatch(&valid, 1U);
    assert(result.status == TemporalMidiCcAuthorSpoolStatus::CAPACITY_EXCEEDED);
    assert(spool.size() == before);
    assert(spool.diagnostics().highWaterMark ==
           TemporalMidiCcAuthorSpool::CAPACITY);

    std::cout << "[PASS] transactional push and fixed capacity\n";
}

void test_deadline_group_cannot_become_permanently_undrainable() {
    TemporalMidiCcAuthorSpool spool;
    static std::array<
        TemporalMidiCcAuthorTransition,
        TemporalMidiCcAuthorSpool::MAX_DUE_TRANSITIONS
    > completeGroup{};
    for (size_t index = 0U; index < completeGroup.size(); ++index) {
        completeGroup[index] = update(
            500U,
            0U,
            MidiCcCandidateClass::MACRO_STATIC,
            0U,
            static_cast<uint8_t>(index & 0x7FU)
        );
    }
    assert(spool.pushBatch(completeGroup.data(), completeGroup.size()).ok());

    const auto neighbor = update(
        501U, 0U, MidiCcCandidateClass::MACRO_STATIC, 1U, 77U
    );
    assert(spool.pushBatch(&neighbor, 1U).ok());
    const size_t acceptedSize = spool.size();
    const auto overflow = update(
        500U, 0U, MidiCcCandidateClass::MACRO_STATIC, 2U, 88U
    );
    const auto rejected = spool.pushBatch(&overflow, 1U);
    assert(rejected.status ==
           TemporalMidiCcAuthorSpoolStatus::CAPACITY_EXCEEDED);
    assert(spool.size() == acceptedSize);  // atomic rejection

    static std::array<
        TemporalMidiCcAuthorTransition,
        TemporalMidiCcAuthorSpool::MAX_DUE_TRANSITIONS
    > scratch{};
    auto due = spool.beginDue(500U, scratch.data(), scratch.size());
    assert(due.ok());
    assert(due.transferredCount == completeGroup.size());
    assert(spool.commitDue());
    due = spool.beginDue(501U, scratch.data(), scratch.size());
    assert(due.ok() && due.transferredCount == 1U);
    assert(scratch[0].localValue == 77U);
    assert(spool.commitDue());
    assert(spool.empty());

    std::cout << "[PASS] canonical deadline group bound preserves liveness\n";
}

void test_begin_rollback_is_exact_and_blocks_mutation() {
    TemporalMidiCcAuthorSpool spool;
    const std::array transitions{
        update(100U, 0U, MidiCcCandidateClass::MACRO_STATIC, 0U, 10U),
        update(100U, 0U, MidiCcCandidateClass::MACRO_STATIC, 0U, 11U),
        remove(100U, 0U, MidiCcCandidateClass::MACRO_STATIC, 0U),
    };
    assert(spool.pushBatch(transitions.data(), transitions.size()).ok());
    std::array<TemporalMidiCcAuthorTransition, 4> scratch{};
    auto due = spool.beginDue(100U, scratch.data(), scratch.size());
    assert(due.transferredCount == 3U);
    assert(spool.transactionActive());
    assert(spool.size() == 3U);
    const auto rejected = spool.pushBatch(transitions.data(), 1U);
    assert(rejected.status ==
           TemporalMidiCcAuthorSpoolStatus::TRANSACTION_ACTIVE);
    assert(spool.rollbackDue());
    assert(!spool.transactionActive());
    assert(spool.size() == 3U);

    due = spool.beginDue(100U, scratch.data(), scratch.size());
    assert(due.transferredCount == 3U);
    assert(scratch[0].localValue == 10U);
    assert(scratch[1].localValue == 11U);
    assert(scratch[2].operation == TemporalMidiCcAuthorOperation::REMOVE);
    assert(spool.commitDue());
    assert(spool.empty());
    assert(spool.diagnostics().rolledBackTransitionCount == 3U);

    std::cout << "[PASS] exact begin/rollback transaction\n";
}

void test_complete_deadline_groups_and_multiple_batches() {
    TemporalMidiCcAuthorSpool spool;
    std::array<TemporalMidiCcAuthorTransition, 321> transitions{};
    for (size_t index = 0; index < 300U; ++index) {
        transitions[index] = update(
            100U, 0U, MidiCcCandidateClass::MACRO_STATIC, 0U,
            static_cast<uint8_t>(index & 0x7FU)
        );
    }
    for (size_t index = 300U; index < transitions.size(); ++index) {
        transitions[index] = update(
            200U, 0U, MidiCcCandidateClass::MACRO_STATIC, 0U,
            static_cast<uint8_t>(index & 0x7FU)
        );
    }
    assert(spool.pushBatch(transitions.data(), transitions.size()).ok());
    std::array<TemporalMidiCcAuthorTransition,
               TemporalMidiCcAuthorSpool::MAX_DUE_TRANSITIONS> scratch{};
    auto due = spool.beginDue(200U, scratch.data(), scratch.size());
    assert(due.transferredCount == 300U);
    assert(due.deadlineGroupCount == 1U);
    for (size_t index = 0; index < due.transferredCount; ++index) {
        assert(scratch[index].deadlineUs == 100U);
    }
    assert(spool.commitDue());
    assert(spool.size() == 21U);
    due = spool.beginDue(200U, scratch.data(), scratch.size());
    assert(due.transferredCount == 21U);
    assert(due.deadlineGroupCount == 1U);
    assert(spool.commitDue());

    const std::array oversizedGroup{
        update(300U, 0U, MidiCcCandidateClass::MACRO_STATIC, 1U, 1U),
        update(300U, 0U, MidiCcCandidateClass::MACRO_STATIC, 1U, 2U),
        update(300U, 0U, MidiCcCandidateClass::MACRO_STATIC, 1U, 3U),
        update(300U, 0U, MidiCcCandidateClass::MACRO_STATIC, 1U, 4U),
    };
    assert(spool.pushBatch(oversizedGroup.data(), oversizedGroup.size()).ok());
    std::array<TemporalMidiCcAuthorTransition, 3> smallScratch{};
    due = spool.beginDue(300U, smallScratch.data(), smallScratch.size());
    assert(due.status == TemporalMidiCcAuthorSpoolStatus::CAPACITY_EXCEEDED);
    assert(spool.size() == oversizedGroup.size());
    assert(!spool.transactionActive());

    std::cout << "[PASS] complete deadline groups across due batches\n";
}

void test_cancel_track_keeps_neighbors() {
    TemporalMidiCcAuthorSpool spool;
    const std::array transitions{
        update(100U, 0U, MidiCcCandidateClass::SEQUENCER_CC_LANE, 0U, 1U),
        update(100U, 1U, MidiCcCandidateClass::SEQUENCER_CC_LANE, 4U, 2U),
        update(100U, 1U, MidiCcCandidateClass::MACRO_COMPUTED, 128U, 3U),
        remove(100U, 1U, MidiCcCandidateClass::MACRO_STATIC, 129U),
        update(100U, 2U, MidiCcCandidateClass::MACRO_STATIC, 256U, 4U),
    };
    assert(spool.pushBatch(transitions.data(), transitions.size()).ok());
    assert(spool.cancelTrack(1U) == 3U);
    assert(spool.size() == 2U);

    std::array<TemporalMidiCcAuthorTransition, 4> scratch{};
    const auto due = spool.beginDue(100U, scratch.data(), scratch.size());
    assert(due.transferredCount == 2U);
    assert(scratch[0].trackIndex == 0U);
    assert(scratch[1].trackIndex == 2U);
    assert(spool.commitDue());

    std::cout << "[PASS] per-Track cancellation keeps neighbors\n";
}

void test_lane_generation_batch_cancels_once_and_keeps_neighbors() {
    TemporalMidiCcAuthorSpool spool;
    const std::array transitions{
        update(100U, 0U, MidiCcCandidateClass::SEQUENCER_CC_LANE, 0U, 10U),
        update(200U, 0U, MidiCcCandidateClass::SEQUENCER_CC_LANE, 0U, 11U),
        update(100U, 0U, MidiCcCandidateClass::SEQUENCER_CC_LANE, 1U, 20U),
        update(100U, 1U, MidiCcCandidateClass::SEQUENCER_CC_LANE, 4U, 30U),
        update(100U, 1U, MidiCcCandidateClass::SEQUENCER_CC_LANE, 5U, 40U),
        update(200U, 1U, MidiCcCandidateClass::SEQUENCER_CC_LANE, 5U, 41U),
        update(100U, 0U, MidiCcCandidateClass::MACRO_STATIC, 0U, 50U),
    };
    assert(spool.pushBatch(transitions.data(), transitions.size()).ok());
    const uint64_t changedLanes = (UINT64_C(1) << 0U) |
                                  (UINT64_C(1) << 5U);
    assert(spool.cancelLaneAuthors(changedLanes) == 4U);
    assert(spool.size() == 3U);
    assert(spool.diagnostics().cancelledTransitionCount == 4U);

    std::array<TemporalMidiCcAuthorTransition, 4> scratch{};
    const auto due = spool.beginDue(100U, scratch.data(), scratch.size());
    assert(due.transferredCount == 3U);
    assert(scratch[0].author.stableAddress == 1U);
    assert(scratch[1].author.stableAddress == 4U);
    assert(scratch[2].author.candidateClass == MidiCcCandidateClass::MACRO_STATIC);
    assert(spool.commitDue());
    assert(spool.empty());

    std::cout << "[PASS] one-pass Lane lifecycle cancellation keeps neighbors\n";
}

void test_worst_case_lane_batch_cancellation_measurement() {
    TemporalMidiCcAuthorSpool spool;
    static std::array<
        TemporalMidiCcAuthorTransition,
        TemporalMidiCcAuthorSpool::CAPACITY
    > transitions{};
    for (size_t index = 0U; index < transitions.size(); ++index) {
        const uint16_t address = static_cast<uint16_t>(
            index % TemporalMidiCcAuthorSpool::LANE_AUTHOR_SLOT_COUNT
        );
        transitions[index] = update(
            static_cast<uint32_t>(1000U + index),
            static_cast<uint8_t>(address / 4U),
            MidiCcCandidateClass::SEQUENCER_CC_LANE,
            address,
            static_cast<uint8_t>(index & 0x7FU)
        );
    }
    constexpr size_t kPushChunk = 512U;
    for (size_t offset = 0U; offset < transitions.size(); offset += kPushChunk) {
        const size_t count = std::min(kPushChunk, transitions.size() - offset);
        assert(spool.pushBatch(transitions.data() + offset, count).ok());
    }

    const auto started = std::chrono::steady_clock::now();
    const size_t removed = spool.cancelLaneAuthors(UINT64_MAX);
    const auto elapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - started
    ).count();
    assert(removed == TemporalMidiCcAuthorSpool::CAPACITY);
    assert(spool.empty());
    std::cout << "[MEASURE] one-pass cancel 64 Lane authors / 8192 nodes="
              << elapsedUs << " us (host, informational)\n";
}

void test_near_capacity_multi_deadline_preflight_measurement() {
    TemporalMidiCcAuthorSpool spool;
    constexpr size_t kIncomingDeadlineCount = 16U;
    constexpr size_t kExistingCount =
        TemporalMidiCcAuthorSpool::CAPACITY - kIncomingDeadlineCount;
    static std::array<
        TemporalMidiCcAuthorTransition,
        kExistingCount
    > existing{};
    for (size_t index = 0U; index < existing.size(); ++index) {
        const uint16_t address = static_cast<uint16_t>(
            index % TemporalMidiCcAuthorSpool::LANE_AUTHOR_SLOT_COUNT
        );
        existing[index] = update(
            static_cast<uint32_t>(1000U + index % kIncomingDeadlineCount),
            static_cast<uint8_t>(address / 4U),
            MidiCcCandidateClass::SEQUENCER_CC_LANE,
            address,
            static_cast<uint8_t>(index & 0x7FU)
        );
    }
    constexpr size_t kPushChunk = 512U;
    for (size_t offset = 0U; offset < existing.size(); offset += kPushChunk) {
        const size_t count = std::min(kPushChunk, existing.size() - offset);
        assert(spool.pushBatch(existing.data() + offset, count).ok());
    }

    std::array<TemporalMidiCcAuthorTransition, kIncomingDeadlineCount> incoming{};
    for (size_t index = 0U; index < incoming.size(); ++index) {
        incoming[index] = update(
            static_cast<uint32_t>(1000U + index),
            0U,
            MidiCcCandidateClass::MACRO_STATIC,
            static_cast<uint16_t>(index),
            static_cast<uint8_t>(64U + index)
        );
    }

    const auto started = std::chrono::steady_clock::now();
    const auto pushed = spool.pushBatch(incoming.data(), incoming.size());
    const auto elapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - started
    ).count();
    assert(pushed.ok());
    assert(spool.size() == TemporalMidiCcAuthorSpool::CAPACITY);
    std::cout << "[MEASURE] preflight 8176 nodes / 16 deadline groups="
              << elapsedUs << " us (host, informational)\n";
}

void test_clear_reinitializes_fixed_pool() {
    TemporalMidiCcAuthorSpool spool;
    const auto transition = update(
        100U, 0U, MidiCcCandidateClass::MACRO_STATIC, 0U, 64U
    );
    assert(spool.pushBatch(&transition, 1U).ok());
    spool.clear();
    assert(spool.empty());
    assert(spool.pushBatch(&transition, 1U).ok());

    std::cout << "[PASS] cold clear reinitializes pool\n";
}

void test_full_disjoint_frame_group_and_independent_manual_slot() {
    TemporalMidiCcAuthorSpool spool;
    static std::array<
        TemporalMidiCcAuthorTransition,
        TemporalMidiCcAuthorSpool::MAX_DUE_TRANSITIONS
    > transitions{};
    for (uint16_t index = 0U; index < transitions.size(); ++index) {
        const uint16_t address = static_cast<uint16_t>(index % 128U);
        transitions[index] = update(
            1000U,
            0U,
            index < transitions.size() / 2U
                ? MidiCcCandidateClass::LIVE_MANUAL
                : MidiCcCandidateClass::MACRO_STATIC,
            address,
            static_cast<uint8_t>(index & 0x7FU)
        );
    }
    assert(spool.pushBatch(transitions.data(), transitions.size()).ok());
    static std::array<
        TemporalMidiCcAuthorTransition,
        TemporalMidiCcAuthorSpool::MAX_DUE_TRANSITIONS
    > scratch{};
    const auto due = spool.beginDue(1000U, scratch.data(), scratch.size());
    assert(due.transferredCount == transitions.size());
    assert(due.deadlineGroupCount == 1U);
    assert(spool.rollbackDue());

    assert(spool.cancelCandidateClass(MidiCcCandidateClass::LIVE_MANUAL) ==
           transitions.size() / 2U);
    assert(spool.size() == transitions.size() / 2U);

    std::cout << "[PASS] 640-transition group and independent Manual slot\n";
}

}  // namespace

int main() {
    test_author_slot_mapping_is_canonical_and_collision_free();
    test_deadline_order_and_commit();
    test_equal_deadline_is_stable_even_for_one_author();
    test_wrap_safe_deadlines();
    test_push_batch_is_atomic_and_capacity_bounded();
    test_deadline_group_cannot_become_permanently_undrainable();
    test_begin_rollback_is_exact_and_blocks_mutation();
    test_complete_deadline_groups_and_multiple_batches();
    test_cancel_track_keeps_neighbors();
    test_lane_generation_batch_cancels_once_and_keeps_neighbors();
    test_worst_case_lane_batch_cancellation_measurement();
    test_near_capacity_multi_deadline_preflight_measurement();
    test_clear_reinitializes_fixed_pool();
    test_full_disjoint_frame_group_and_independent_manual_slot();

    std::cout << "sizeof(TemporalMidiCcAuthorSpool)="
              << sizeof(TemporalMidiCcAuthorSpool) << " bytes\n";
    std::cout << "All TemporalMidiCcAuthorSpool tests passed\n";
    return 0;
}
