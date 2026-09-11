#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include <config/PlatformCompat.hpp>

#include "state/project/ProjectHistoryEventSink.hpp"

namespace core::state::project {

/**
 * Fixed, allocation-free history. Payload addresses remain stable while only
 * slot indices move between Undo and Redo. Entry provides occupied;
 * the domain service owns gesture rules and validates/applies each payload.
 */
template <typename Entry, ProjectHistoryDomain Domain, uint8_t Capacity>
class ProjectHistorySlots {
    static_assert(Capacity > 0U);
    static_assert(std::is_trivially_copyable_v<Entry>);
    static constexpr uint8_t INVALID_SLOT = Capacity;

public:
    ProjectHistorySlots() = default;
    ProjectHistorySlots(const ProjectHistorySlots&) = delete;
    ProjectHistorySlots& operator=(const ProjectHistorySlots&) = delete;

    uint8_t undoCount() const { return undo_count_; }
    uint8_t redoCount() const { return redo_count_; }
    constexpr size_t retainedBytes() const {
        return sizeof(entries_) + sizeof(undo_slots_) + sizeof(redo_slots_);
    }

    FLASHMEM uintptr_t identity(ProjectHistoryDirection direction) const {
        return identity_(topSlot_(direction));
    }

    FLASHMEM const Entry* peek(ProjectHistoryDirection direction) const {
        const uint8_t slot = topSlot_(direction);
        return slot < Capacity && entries_[slot].occupied ? &entries_[slot] : nullptr;
    }

    FLASHMEM Entry* peek(ProjectHistoryDirection direction) {
        const auto& self = static_cast<const ProjectHistorySlots&>(*this);
        return const_cast<Entry*>(self.peek(direction));
    }

    /** Fill the admitted slot in place, without a second payload on the stack. */
    template <typename Fill>
    FLASHMEM bool record(uint8_t kind, const ProjectHistoryEventSink* sink, Fill&& fillEntry) {
        static_assert(std::is_nothrow_invocable_v<Fill, Entry&>);
        discardRedo(sink);
        uint8_t slot = acquireSlot_();
        if (slot == INVALID_SLOT && undo_count_ > 0U) {
            slot = undo_slots_[0];
            if (sink) sink->notifyEvicted(Domain, identity_(slot));
            for (uint8_t index = 1U; index < undo_count_; ++index) {
                undo_slots_[index - 1U] = undo_slots_[index];
            }
            undo_slots_[--undo_count_] = INVALID_SLOT;
            if (slot < Capacity) entries_[slot].occupied = false;
            slot = acquireSlot_();
        }
        if (slot == INVALID_SLOT || undo_count_ >= Capacity) return false;
        fillEntry(entries_[slot]);
        entries_[slot].occupied = true;
        undo_slots_[undo_count_++] = slot;
        if (sink) sink->notifyCommitted(Domain, identity_(slot), kind);
        return true;
    }

    /** Failed application leaves both stacks and their chronology unchanged. */
    template <typename Apply>
    FLASHMEM bool apply(ProjectHistoryDirection direction,
                        const ProjectHistoryEventSink* sink, Apply&& applyEntry) {
        const bool undo = direction == ProjectHistoryDirection::Undo;
        auto& from = undo ? undo_slots_ : redo_slots_;
        auto& to = undo ? redo_slots_ : undo_slots_;
        auto& fromCount = undo ? undo_count_ : redo_count_;
        auto& toCount = undo ? redo_count_ : undo_count_;
        if (fromCount == 0U || toCount >= Capacity) return false;
        const uint8_t slot = from[fromCount - 1U];
        if (slot >= Capacity || !entries_[slot].occupied || !applyEntry(entries_[slot])) {
            return false;
        }
        from[--fromCount] = INVALID_SLOT;
        to[toCount++] = slot;
        if (sink) sink->notifyApplied(Domain, identity_(slot), direction);
        return true;
    }

    FLASHMEM void discardRedo(const ProjectHistoryEventSink* sink) {
        for (uint8_t index = 0U; index < redo_count_; ++index) {
            const uint8_t slot = redo_slots_[index];
            if (sink) sink->notifyEvicted(Domain, identity_(slot));
            // Inactive trivial payloads own no resources and cannot be peeked.
            // The next record fills the payload before making it occupied.
            if (slot < Capacity) entries_[slot].occupied = false;
            redo_slots_[index] = INVALID_SLOT;
        }
        redo_count_ = 0U;
    }

    FLASHMEM void clear(const ProjectHistoryEventSink* sink) {
        if (sink) sink->notifyCleared(Domain);
        entries_ = {};
        undo_slots_.fill(INVALID_SLOT);
        redo_slots_.fill(INVALID_SLOT);
        undo_count_ = redo_count_ = 0U;
    }

private:
    FLASHMEM uint8_t topSlot_(ProjectHistoryDirection direction) const {
        const bool undo = direction == ProjectHistoryDirection::Undo;
        const auto& slots = undo ? undo_slots_ : redo_slots_;
        const uint8_t count = undo ? undo_count_ : redo_count_;
        return count == 0U ? INVALID_SLOT : slots[count - 1U];
    }

    FLASHMEM uintptr_t identity_(uint8_t slot) const {
        return slot < Capacity ? reinterpret_cast<uintptr_t>(&entries_[slot]) : 0U;
    }

    FLASHMEM uint8_t acquireSlot_() const {
        for (uint8_t slot = 0U; slot < Capacity; ++slot) {
            if (!entries_[slot].occupied) return slot;
        }
        return INVALID_SLOT;
    }

    std::array<Entry, Capacity> entries_{};
    std::array<uint8_t, Capacity> undo_slots_{};
    std::array<uint8_t, Capacity> redo_slots_{};
    uint8_t undo_count_ = 0U;
    uint8_t redo_count_ = 0U;
};

}  // namespace core::state::project
