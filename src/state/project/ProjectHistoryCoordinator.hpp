#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <oc/state/Signal.hpp>

#include "state/project/ProjectHistoryEventSink.hpp"

namespace core::state::project {

/**
 * Lightweight global ordering for Project mutations.
 *
 * Entries contain no musical data. Payloads remain owned by the Macro,
 * Sequencer, and Track histories in PSRAM; identity lets eviction notifications
 * insert a safety barrier when a domain reaches its own bounded capacity.
 */
class ProjectHistoryCoordinator {
public:
    // Sized for every payload that MacroHistory (8), SequencerHistory
    // (32 Pattern + 8 Structure + 4 Full Bank + 24 Drum), TrackHistory (8), and
    // SettingsHistory (8) can
    // retain simultaneously, so
    // capacity alone never drops a global reference. The complete coordinator
    // lives in PSRAM. A domain eviction may still establish an intentional
    // boundary: actions older than an unavailable payload become unreachable.
    static constexpr uint8_t ENTRY_LIMIT = 92;
    static constexpr uint32_t RETAINED_BYTE_BUDGET = 2U * 1024U * 1024U;
    static constexpr uint16_t RETAINED_SPAN_BUDGET = 655U;

    struct Entry {
        uintptr_t identity = 0;
        ProjectHistoryDomain domain = ProjectHistoryDomain::Macro;
        uint8_t actionKind = 0;

        [[nodiscard]] bool valid() const {
            return identity != 0U;
        }
    };

    using BranchInvalidatedFn = void (*)(void* context);

    ProjectHistoryCoordinator();

    ProjectHistoryEventSink& eventSink() { return sink_; }
    const ProjectHistoryEventSink& eventSink() const { return sink_; }

    void setBranchInvalidatedCallback(
        void* context,
        BranchInvalidatedFn callback
    );

    [[nodiscard]] bool canUndo() const { return cursor_ > 0U; }
    [[nodiscard]] bool canRedo() const { return cursor_ < count_; }
    [[nodiscard]] uint8_t undoCount() const { return cursor_; }
    [[nodiscard]] uint8_t redoCount() const {
        return static_cast<uint8_t>(count_ - cursor_);
    }
    [[nodiscard]] const Entry* peekUndo() const;
    [[nodiscard]] const Entry* peekRedo() const;
    [[nodiscard]] ProjectHistoryRetainedUsage retainedUsage(
        ProjectHistoryDomain domain
    ) const;
    [[nodiscard]] uint32_t retainedBytes() const;
    [[nodiscard]] uint16_t retainedSpans() const;

    void clear();

    void formatUndoLabel(char* out, size_t capacity) const;
    void formatRedoLabel(char* out, size_t capacity) const;
    [[nodiscard]] static const char* actionLabel(const Entry& entry);

    oc::state::Signal<uint32_t, 1> revision{0};

private:
    static void onCommitted(
        void* context,
        ProjectHistoryDomain domain,
        uintptr_t identity,
        uint8_t actionKind
    );
    static void onEvicted(
        void* context,
        ProjectHistoryDomain domain,
        uintptr_t identity
    );
    static void onCleared(void* context, ProjectHistoryDomain domain);
    static void onApplied(
        void* context,
        ProjectHistoryDomain domain,
        uintptr_t identity,
        ProjectHistoryDirection direction
    );
    static bool onCanRetain(
        void* context,
        ProjectHistoryDomain domain,
        ProjectHistoryRetainedUsage projected
    );
    static void onRetained(
        void* context,
        ProjectHistoryDomain domain,
        ProjectHistoryRetainedUsage retained
    );

    void commitEntry(
        ProjectHistoryDomain domain,
        uintptr_t identity,
        uint8_t actionKind
    );
    void evictEntry(ProjectHistoryDomain domain, uintptr_t identity);
    void clearDomain(ProjectHistoryDomain domain);
    void applyEntry(
        ProjectHistoryDomain domain,
        uintptr_t identity,
        ProjectHistoryDirection direction
    );
    void bumpRevision();
    [[nodiscard]] bool canRetain(
        ProjectHistoryDomain domain,
        ProjectHistoryRetainedUsage projected
    ) const;
    void setRetainedUsage(
        ProjectHistoryDomain domain,
        ProjectHistoryRetainedUsage retained
    );

    bool eraseIdentity(
        ProjectHistoryDomain domain,
        uintptr_t identity
    );
    bool eraseWithBarrier(
        ProjectHistoryDomain domain,
        uintptr_t identity
    );
    void append(Entry entry);

    ProjectHistoryEventSink sink_{};
    std::array<Entry, ENTRY_LIMIT> timeline_{};
    std::array<ProjectHistoryRetainedUsage, 4U> retained_usage_{};
    uint8_t count_ = 0;
    uint8_t cursor_ = 0;
    void* branch_context_ = nullptr;
    BranchInvalidatedFn branch_invalidated_ = nullptr;
};

}  // namespace core::state::project
