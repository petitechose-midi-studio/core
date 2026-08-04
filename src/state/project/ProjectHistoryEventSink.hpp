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

/** Compact retained strict-PSRAM ownership published by one History domain. */
struct ProjectHistoryRetainedUsage {
    uint32_t bytes = 0U;
    uint16_t spans = 0U;
};

static_assert(sizeof(ProjectHistoryRetainedUsage) == 8U);

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
    using CanRetainFn = bool (*)(
        void* context,
        ProjectHistoryDomain domain,
        ProjectHistoryRetainedUsage projected
    );
    using RetainedFn = void (*)(
        void* context,
        ProjectHistoryDomain domain,
        ProjectHistoryRetainedUsage retained
    );

    void* context = nullptr;
    EntryFn committed = nullptr;
    EvictedFn evicted = nullptr;
    ClearedFn cleared = nullptr;
    AppliedFn applied = nullptr;
    CanRetainFn canRetain = nullptr;
    RetainedFn retained = nullptr;

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

    [[nodiscard]] bool admitsRetainedUsage(
        ProjectHistoryDomain domain,
        ProjectHistoryRetainedUsage projected
    ) const {
        return canRetain == nullptr || canRetain(context, domain, projected);
    }

    void notifyRetainedUsage(
        ProjectHistoryDomain domain,
        ProjectHistoryRetainedUsage usage
    ) const {
        if (retained != nullptr) retained(context, domain, usage);
    }
};

}  // namespace core::state::project
