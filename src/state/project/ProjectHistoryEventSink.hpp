#pragma once

#include <cstdint>

namespace core::state::project {

enum class ProjectHistoryDomain : uint8_t {
    Macro = 0,
    Sequencer,
    Track,
    Settings,
};

enum class ProjectHistoryDirection : uint8_t {
    Undo = 0,
    Redo,
};

/**
 * Allocation-free bridge from the domain histories to the global timeline.
 *
 * The identity is the stable address of the PSRAM-owned domain payload. The
 * global history never owns or copies that payload.
 */
struct ProjectHistoryEventSink {
    using EntryFn = void (*)(
        void* context,
        ProjectHistoryDomain domain,
        uintptr_t identity,
        uint8_t actionKind
    );
    using EvictedFn = void (*)(
        void* context,
        ProjectHistoryDomain domain,
        uintptr_t identity
    );
    using ClearedFn = void (*)(void* context, ProjectHistoryDomain domain);
    using AppliedFn = void (*)(
        void* context,
        ProjectHistoryDomain domain,
        uintptr_t identity,
        ProjectHistoryDirection direction
    );

    void* context = nullptr;
    EntryFn committed = nullptr;
    EvictedFn evicted = nullptr;
    ClearedFn cleared = nullptr;
    AppliedFn applied = nullptr;

    void notifyCommitted(
        ProjectHistoryDomain domain,
        uintptr_t identity,
        uint8_t actionKind
    ) const {
        if (committed != nullptr && identity != 0U) {
            committed(context, domain, identity, actionKind);
        }
    }

    void notifyEvicted(ProjectHistoryDomain domain, uintptr_t identity) const {
        if (evicted != nullptr && identity != 0U) {
            evicted(context, domain, identity);
        }
    }

    void notifyCleared(ProjectHistoryDomain domain) const {
        if (cleared != nullptr) cleared(context, domain);
    }

    void notifyApplied(
        ProjectHistoryDomain domain,
        uintptr_t identity,
        ProjectHistoryDirection direction
    ) const {
        if (applied != nullptr && identity != 0U) {
            applied(context, domain, identity, direction);
        }
    }
};

}  // namespace core::state::project
