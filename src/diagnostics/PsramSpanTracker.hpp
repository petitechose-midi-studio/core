#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace core::diagnostics::detail {

inline constexpr uint32_t PSRAM_SPAN_CAPACITY = 1'034U;

struct PsramSpan {
    uint32_t beginOffset = 0U;
    uint32_t endOffset = 0U;
    uint32_t userBytes = 0U;
};

static_assert(sizeof(PsramSpan) == 12U);
static_assert(PSRAM_SPAN_CAPACITY <= std::numeric_limits<uint16_t>::max());

using PsramSpanTable = std::array<PsramSpan, PSRAM_SPAN_CAPACITY>;

inline constexpr size_t PSRAM_SPAN_TABLE_BYTES = sizeof(PsramSpanTable);
static_assert(PSRAM_SPAN_TABLE_BYTES == 12'408U);

struct PsramTrackerSnapshot {
    uint32_t poolBytes = 0U;
    uint32_t allocatedBytes = 0U;
    uint32_t userBytes = 0U;
    uint32_t largestBlock = 0U;
    uint32_t blockCount = 0U;
    bool ready = false;
    bool overflow = false;
    bool largestBlockValid = false;
};

/**
 * Allocation-free scalar authority over one caller-owned, sorted span table.
 *
 * Mutation and gap derivation are deliberately foreground/cold work. The
 * target reporter publishes only the resulting scalar snapshot while
 * interrupts are masked; this class never owns or allocates its table.
 */
class PsramSpanTracker final {
public:
    void reset(uint32_t poolBytes, uint32_t allocationOverhead) {
        poolBytes_ = poolBytes;
        allocationOverhead_ = allocationOverhead;
        allocatedBytes_ = 0U;
        userBytes_ = 0U;
        largestBlock_ = 0U;
        liveBlockCount_ = 0U;
        tableEntryCount_ = 0U;
        ready_ = poolBytes > allocationOverhead;
        overflow_ = false;
        largestBlockValid_ = ready_;
        if (ready_) largestBlock_ = poolBytes - allocationOverhead;
    }

    bool insert(PsramSpanTable& table, const PsramSpan& span) {
        if (!ready_ || !validSpan_(span)) {
            markOverflow();
            return false;
        }

        if (overflow_) {
            (void)addLive_(span);
            return false;
        }

        uint16_t position = 0U;
        while (position < tableEntryCount_ &&
               table[position].beginOffset < span.beginOffset) {
            ++position;
        }
        if ((position < tableEntryCount_ &&
             table[position].beginOffset == span.beginOffset) ||
            (position > 0U &&
             table[position - 1U].endOffset > span.beginOffset) ||
            (position < tableEntryCount_ &&
             span.endOffset > table[position].beginOffset)) {
            markOverflow();
            return false;
        }

        if (!addLive_(span)) return false;
        if (tableEntryCount_ >= PSRAM_SPAN_CAPACITY) {
            markOverflow();
            return false;
        }

        for (uint16_t index = tableEntryCount_;
             index > position;
             --index) {
            table[index] = table[index - 1U];
        }
        table[position] = span;
        ++tableEntryCount_;
        recomputeLargest_(table);
        return true;
    }

    bool remove(PsramSpanTable& table, const PsramSpan& span) {
        if (!ready_ || !validSpan_(span)) {
            markOverflow();
            return false;
        }
        if (overflow_) {
            (void)subtractLive_(span);
            return false;
        }

        uint16_t position = 0U;
        while (position < tableEntryCount_ &&
               table[position].beginOffset != span.beginOffset) {
            ++position;
        }
        if (position >= tableEntryCount_ ||
            table[position].endOffset != span.endOffset ||
            table[position].userBytes != span.userBytes) {
            markOverflow();
            return false;
        }

        for (uint16_t index = position + 1U;
             index < tableEntryCount_;
             ++index) {
            table[index - 1U] = table[index];
        }
        --tableEntryCount_;
        table[tableEntryCount_] = {};
        if (!subtractLive_(span)) return false;
        recomputeLargest_(table);
        return true;
    }

    void markOverflow() {
        overflow_ = true;
        largestBlockValid_ = false;
    }

    bool ready() const { return ready_; }

    PsramTrackerSnapshot snapshot() const {
        return {
            poolBytes_,
            allocatedBytes_,
            userBytes_,
            largestBlock_,
            liveBlockCount_,
            ready_,
            overflow_,
            largestBlockValid_,
        };
    }

private:
    bool validSpan_(const PsramSpan& span) const {
        return span.endOffset > span.beginOffset &&
            span.endOffset <= poolBytes_ &&
            span.userBytes <= span.endOffset - span.beginOffset;
    }

    bool addLive_(const PsramSpan& span) {
        const uint32_t allocationBytes = span.endOffset - span.beginOffset;
        if (allocationBytes > poolBytes_ - allocatedBytes_ ||
            span.userBytes > poolBytes_ - userBytes_ ||
            liveBlockCount_ == std::numeric_limits<uint32_t>::max()) {
            markOverflow();
            return false;
        }
        allocatedBytes_ += allocationBytes;
        userBytes_ += span.userBytes;
        ++liveBlockCount_;
        return true;
    }

    bool subtractLive_(const PsramSpan& span) {
        const uint32_t allocationBytes = span.endOffset - span.beginOffset;
        if (allocationBytes > allocatedBytes_ ||
            span.userBytes > userBytes_ ||
            liveBlockCount_ == 0U) {
            markOverflow();
            return false;
        }
        allocatedBytes_ -= allocationBytes;
        userBytes_ -= span.userBytes;
        --liveBlockCount_;
        return true;
    }

    uint32_t allocatableBytes_(uint32_t begin, uint32_t end) const {
        if (end <= begin) return 0U;
        const uint32_t gap = end - begin;
        return gap > allocationOverhead_ ? gap - allocationOverhead_ : 0U;
    }

    void recomputeLargest_(const PsramSpanTable& table) {
        if (!ready_ || overflow_) return;
        uint32_t cursor = 0U;
        uint32_t largest = 0U;
        for (uint16_t index = 0U; index < tableEntryCount_; ++index) {
            const auto& span = table[index];
            const uint32_t gap = allocatableBytes_(cursor, span.beginOffset);
            if (gap > largest) largest = gap;
            if (span.endOffset > cursor) cursor = span.endOffset;
        }
        const uint32_t tail = allocatableBytes_(cursor, poolBytes_);
        largestBlock_ = tail > largest ? tail : largest;
        largestBlockValid_ = true;
    }

    uint32_t poolBytes_ = 0U;
    uint32_t allocationOverhead_ = 0U;
    uint32_t allocatedBytes_ = 0U;
    uint32_t userBytes_ = 0U;
    uint32_t largestBlock_ = 0U;
    uint32_t liveBlockCount_ = 0U;
    uint16_t tableEntryCount_ = 0U;
    bool ready_ = false;
    bool overflow_ = false;
    bool largestBlockValid_ = false;
};

}  // namespace core::diagnostics::detail
