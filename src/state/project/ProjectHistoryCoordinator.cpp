#include "state/project/ProjectHistoryCoordinator.hpp"

#include <cassert>
#include <cstdio>

#include <config/PlatformCompat.hpp>

#include "state/macro/MacroHistory.hpp"
#include "state/project/ProjectTrackHistory.hpp"
#include "state/project/ProjectSettingsHistory.hpp"
#include "state/sequencer/SequencerHistory.hpp"

namespace core::state::project {

static_assert(
    ProjectHistoryCoordinator::ENTRY_LIMIT >=
        core::state::macro::MacroHistoryService::ENTRY_LIMIT +
        core::state::sequencer::SequencerHistoryService::ENTRY_LIMIT +
        core::state::project::ProjectTrackHistoryService::ENTRY_LIMIT +
        core::state::project::ProjectSettingsHistoryService::ENTRY_LIMIT,
    "Global history must expose every payload retained by domain histories"
);
#if defined(ARDUINO_TEENSY41) && !defined(OC_DESKTOP)
static_assert(
    sizeof(ProjectHistoryCoordinator) <= 640U,
    "Global history metadata must remain a compact PSRAM allocation"
);
#endif

namespace {

constexpr uint8_t kRetainedDomainCount = 4U;

constexpr uint8_t retainedDomainIndex(ProjectHistoryDomain domain) {
    return static_cast<uint8_t>(domain);
}

constexpr bool retainedDomainValid(ProjectHistoryDomain domain) {
    return retainedDomainIndex(domain) < kRetainedDomainCount;
}

FLASHMEM const char* macroActionLabel(uint8_t rawKind) {
    using Kind = core::state::macro::MacroHistoryActionKind;
    switch (static_cast<Kind>(rawKind)) {
        case Kind::CONVERT_AUTOMATION:
            return "Convert Automation";
        case Kind::PASTE_SLOT:
            return "Paste Macro";
        case Kind::PASTE_DESTINATION:
            return "Paste Destination";
        case Kind::PASTE_AUTOMATION:
            return "Paste Automation";
        case Kind::PASTE_MODULATION:
            return "Paste Modulation";
        case Kind::CLEAR_AUTOMATION:
            return "Clear Automation";
        case Kind::CLEAR_MODULATION:
            return "Clear Modulation";
        case Kind::PAGE_STRUCTURE:
            return "Macro Page Structure";
        case Kind::DELETE_SLOT:
            return "Delete Macro";
        case Kind::DEPTH_EDIT:
            return "Modulation Depth";
        case Kind::GLOBAL_DEPTH_EDIT:
            return "Global Depth";
        case Kind::SOURCE_STATE:
            return "Modulation State";
        case Kind::AUTOMATION_STATE:
            return "Automation State";
        case Kind::CREATE_MODULATOR_ASSIGNMENT:
            return "Add Modulation";
        case Kind::REMOVE_MODULATOR_ASSIGNMENT:
            return "Remove Modulation";
        case Kind::PROJECT_MODULATOR_SOURCE_EDIT:
            return "Edit Modulator";
        case Kind::PROJECT_MODULATOR_TRIGGER_EDIT:
            return "Edit Trigger";
        case Kind::CREATE_PROJECT_MODULATOR:
            return "Create Modulator";
        case Kind::SPLIT_PROJECT_MODULATOR:
            return "Make Independent";
        case Kind::DELETE_PROJECT_MODULATOR:
            return "Delete Modulator";
        case Kind::RECORD_AUTOMATION:
            return "Record Automation";
        case Kind::STATIC_VALUE_EDIT:
            return "Macro Value";
        case Kind::CREATE_SLOT:
            return "Create Macro";
        case Kind::AUTOMATION_DURATION_EDIT:
            return "Automation Length";
        case Kind::AUTOMATION_WINDOW_EDIT:
            return "Automation Window";
        case Kind::CONFIG_EDIT:
            return "Macro Routing";
        case Kind::MANUAL_OVERRIDE_STATE:
            return "Manual Override";
        default:
            return "Macro Edit";
    }
}

FLASHMEM const char* sequencerActionLabel(uint8_t rawKind) {
    using Kind = core::state::sequencer::SequencerHistoryActionKind;
    switch (static_cast<Kind>(rawKind)) {
        case Kind::StepToggle:
            return "Step State";
        case Kind::StepPropertyEdit:
            return "Step Property";
        case Kind::StepEdit:
            return "Step Edit";
        case Kind::QuickControls:
            return "Pattern Control";
        case Kind::PatternSettings:
            return "Pattern Settings";
        case Kind::PatternVariation:
            return "Pattern Variation";
        case Kind::ProjectScaleSettings:
            return "Project Scale";
        case Kind::PageStructure:
            return "Page Structure";
        case Kind::TrackStructure:
            return "Track Structure";
        case Kind::CcLaneCreate:
            return "Create CC Lane";
        case Kind::CcLaneEventEdit:
            return "CC Lane Value";
        case Kind::CcLaneEventClear:
            return "Clear CC Event";
        case Kind::CcLaneSettings:
            return "CC Lane Settings";
        case Kind::CcLaneDelete:
            return "Remove CC Lane";
        case Kind::CcLaneTransitionEdit:
            return "CC Lane Curve";
        case Kind::FullBank:
            return "Sequencer Set";
        case Kind::PatternEdit:
        default:
            return "Pattern Edit";
    }
}

FLASHMEM const char* trackActionLabel(uint8_t rawKind) {
    using Kind = core::state::project::ProjectTrackHistoryActionKind;
    switch (static_cast<Kind>(rawKind)) {
        case Kind::MidiChannel:
            return "Track Channel";
        case Kind::Delay:
            return "Track Delay";
        case Kind::Mute:
            return "Track Mute";
        case Kind::Solo:
            return "Track Solo";
        default:
            return "Track Edit";
    }
}

FLASHMEM const char* settingsActionLabel(uint8_t rawKind) {
    using Kind = core::state::project::ProjectSettingsHistoryActionKind;
    switch (static_cast<Kind>(rawKind)) {
        case Kind::Tempo:
            return "Tempo";
        case Kind::Swing:
            return "Project Swing";
        case Kind::RunMode:
            return "Run Mode";
        case Kind::StepPasteMode:
            return "Step Paste Mode";
        case Kind::CcLaneDefault:
            return "CC Lane Default";
        case Kind::PatternsInheritScale:
            return "Pattern Scale Link";
        case Kind::ClipsInheritScale:
            return "Clip Scale Link";
        default:
            return "Project Setting";
    }
}

}  // namespace

FLASHMEM ProjectHistoryCoordinator::ProjectHistoryCoordinator() {
    sink_.context = this;
    sink_.committed = &ProjectHistoryCoordinator::onCommitted;
    sink_.evicted = &ProjectHistoryCoordinator::onEvicted;
    sink_.cleared = &ProjectHistoryCoordinator::onCleared;
    sink_.applied = &ProjectHistoryCoordinator::onApplied;
    sink_.canRetain = &ProjectHistoryCoordinator::onCanRetain;
    sink_.retained = &ProjectHistoryCoordinator::onRetained;
}

FLASHMEM void ProjectHistoryCoordinator::setBranchInvalidatedCallback(
    void* context,
    BranchInvalidatedFn callback
) {
    branch_context_ = context;
    branch_invalidated_ = callback;
}

FLASHMEM const ProjectHistoryCoordinator::Entry*
ProjectHistoryCoordinator::peekUndo() const {
    return cursor_ == 0U ? nullptr : &timeline_[cursor_ - 1U];
}

FLASHMEM const ProjectHistoryCoordinator::Entry*
ProjectHistoryCoordinator::peekRedo() const {
    return cursor_ >= count_ ? nullptr : &timeline_[cursor_];
}

FLASHMEM ProjectHistoryRetainedUsage
ProjectHistoryCoordinator::retainedUsage(ProjectHistoryDomain domain) const {
    return retainedDomainValid(domain)
        ? retained_usage_[retainedDomainIndex(domain)]
        : ProjectHistoryRetainedUsage{};
}

FLASHMEM uint32_t ProjectHistoryCoordinator::retainedBytes() const {
    uint32_t total = 0U;
    for (const auto usage : retained_usage_) total += usage.bytes;
    return total;
}

FLASHMEM uint16_t ProjectHistoryCoordinator::retainedSpans() const {
    uint32_t total = 0U;
    for (const auto usage : retained_usage_) total += usage.spans;
    return static_cast<uint16_t>(total);
}

FLASHMEM void ProjectHistoryCoordinator::clear() {
    if (count_ == 0U && cursor_ == 0U) return;
    timeline_ = {};
    count_ = 0U;
    cursor_ = 0U;
    bumpRevision();
}

FLASHMEM const char* ProjectHistoryCoordinator::actionLabel(const Entry& entry) {
    switch (entry.domain) {
        case ProjectHistoryDomain::Macro:
            return macroActionLabel(entry.actionKind);
        case ProjectHistoryDomain::Sequencer:
            return sequencerActionLabel(entry.actionKind);
        case ProjectHistoryDomain::Track:
            return trackActionLabel(entry.actionKind);
        case ProjectHistoryDomain::Settings:
            return settingsActionLabel(entry.actionKind);
        default:
            return "Project Edit";
    }
}

FLASHMEM void ProjectHistoryCoordinator::formatUndoLabel(
    char* out,
    size_t capacity
) const {
    if (out == nullptr || capacity == 0U) return;
    const auto* entry = peekUndo();
    if (entry == nullptr) {
        std::snprintf(out, capacity, "Undo -");
    } else {
        std::snprintf(out, capacity, "Undo %s", actionLabel(*entry));
    }
}

FLASHMEM void ProjectHistoryCoordinator::formatRedoLabel(
    char* out,
    size_t capacity
) const {
    if (out == nullptr || capacity == 0U) return;
    const auto* entry = peekRedo();
    if (entry == nullptr) {
        std::snprintf(out, capacity, "Redo -");
    } else {
        std::snprintf(out, capacity, "Redo %s", actionLabel(*entry));
    }
}

FLASHMEM void ProjectHistoryCoordinator::onCommitted(
    void* context,
    ProjectHistoryDomain domain,
    uintptr_t identity,
    uint8_t actionKind
) {
    if (context == nullptr) return;
    static_cast<ProjectHistoryCoordinator*>(context)->commitEntry(
        domain,
        identity,
        actionKind
    );
}

FLASHMEM void ProjectHistoryCoordinator::onEvicted(
    void* context,
    ProjectHistoryDomain domain,
    uintptr_t identity
) {
    if (context == nullptr) return;
    static_cast<ProjectHistoryCoordinator*>(context)->evictEntry(domain, identity);
}

FLASHMEM void ProjectHistoryCoordinator::onCleared(
    void* context,
    ProjectHistoryDomain domain
) {
    if (context == nullptr) return;
    static_cast<ProjectHistoryCoordinator*>(context)->clearDomain(domain);
}

FLASHMEM void ProjectHistoryCoordinator::onApplied(
    void* context,
    ProjectHistoryDomain domain,
    uintptr_t identity,
    ProjectHistoryDirection direction
) {
    if (context == nullptr) return;
    static_cast<ProjectHistoryCoordinator*>(context)->applyEntry(
        domain,
        identity,
        direction
    );
}

FLASHMEM bool ProjectHistoryCoordinator::onCanRetain(
    void* context,
    ProjectHistoryDomain domain,
    ProjectHistoryRetainedUsage projected
) {
    return context != nullptr &&
        static_cast<ProjectHistoryCoordinator*>(context)->canRetain(
            domain,
            projected
        );
}

FLASHMEM void ProjectHistoryCoordinator::onRetained(
    void* context,
    ProjectHistoryDomain domain,
    ProjectHistoryRetainedUsage retained
) {
    if (context == nullptr) return;
    static_cast<ProjectHistoryCoordinator*>(context)->setRetainedUsage(
        domain,
        retained
    );
}

FLASHMEM void ProjectHistoryCoordinator::commitEntry(
    ProjectHistoryDomain domain,
    uintptr_t identity,
    uint8_t actionKind
) {
    (void)eraseIdentity(domain, identity);

    for (uint8_t index = cursor_; index < count_; ++index) {
        timeline_[index] = {};
    }
    count_ = cursor_;
    if (branch_invalidated_ != nullptr) {
        branch_invalidated_(branch_context_);
    }

    append(Entry{
        .identity = identity,
        .domain = domain,
        .actionKind = actionKind,
    });
    bumpRevision();
}

FLASHMEM void ProjectHistoryCoordinator::evictEntry(
    ProjectHistoryDomain domain,
    uintptr_t identity
) {
    const bool changed = eraseWithBarrier(domain, identity);
    if (changed) bumpRevision();
}

FLASHMEM void ProjectHistoryCoordinator::clearDomain(
    ProjectHistoryDomain /*domain*/
) {
    // Domain histories are cleared at non-undoable load/reset boundaries. No
    // older action may remain reachable across such a boundary, even if its
    // payload belongs to the other domain.
    if (count_ > 0U) clear();
}

FLASHMEM void ProjectHistoryCoordinator::applyEntry(
    ProjectHistoryDomain domain,
    uintptr_t identity,
    ProjectHistoryDirection direction
) {
    const bool undo = direction == ProjectHistoryDirection::Undo;
    const bool available = undo ? cursor_ > 0U : cursor_ < count_;
    const uint8_t index = undo
        ? static_cast<uint8_t>(cursor_ - (available ? 1U : 0U))
        : cursor_;
    if (!available || timeline_[index].domain != domain ||
        timeline_[index].identity != identity) {
        // An out-of-order direct domain operation invalidates only that global
        // reference; production commands always enter through CoreState.
        evictEntry(domain, identity);
        return;
    }

    if (undo) {
        --cursor_;
    } else {
        ++cursor_;
    }
    bumpRevision();
}

FLASHMEM bool ProjectHistoryCoordinator::eraseIdentity(
    ProjectHistoryDomain domain,
    uintptr_t identity
) {
    for (uint8_t index = 0U; index < count_; ++index) {
        if (timeline_[index].domain != domain ||
            timeline_[index].identity != identity) {
            continue;
        }
        for (uint8_t cursor = index; cursor + 1U < count_; ++cursor) {
            timeline_[cursor] = timeline_[cursor + 1U];
        }
        --count_;
        timeline_[count_] = {};
        if (index < cursor_) --cursor_;
        return true;
    }
    return false;
}

FLASHMEM bool ProjectHistoryCoordinator::eraseWithBarrier(
    ProjectHistoryDomain domain,
    uintptr_t identity
) {
    for (uint8_t index = 0U; index < count_; ++index) {
        if (timeline_[index].domain != domain ||
            timeline_[index].identity != identity) {
            continue;
        }

        if (index < cursor_) {
            // Undo may continue only through actions newer than an evicted,
            // already-applied mutation. Drop the unreachable prefix.
            const uint8_t prefix = static_cast<uint8_t>(index + 1U);
            for (uint8_t read = prefix; read < count_; ++read) {
                timeline_[read - prefix] = timeline_[read];
            }
            const uint8_t previousCount = count_;
            count_ = static_cast<uint8_t>(count_ - prefix);
            cursor_ = static_cast<uint8_t>(cursor_ - prefix);
            for (uint8_t clear = count_; clear < previousCount; ++clear) {
                timeline_[clear] = {};
            }
        } else {
            // Redo may continue only up to an evicted future mutation.
            for (uint8_t clear = index; clear < count_; ++clear) {
                timeline_[clear] = {};
            }
            count_ = index;
        }
        return true;
    }
    return false;
}

FLASHMEM void ProjectHistoryCoordinator::append(Entry entry) {
    if (!entry.valid()) return;
    if (count_ >= ENTRY_LIMIT) {
        for (uint8_t index = 1U; index < ENTRY_LIMIT; ++index) {
            timeline_[index - 1U] = timeline_[index];
        }
        count_ = static_cast<uint8_t>(ENTRY_LIMIT - 1U);
        if (cursor_ > 0U) --cursor_;
    }
    timeline_[count_++] = entry;
    cursor_ = count_;
}

FLASHMEM void ProjectHistoryCoordinator::bumpRevision() {
    uint32_t next = revision.get() + 1U;
    if (next == 0U) next = 1U;
    revision.set(next);
}

FLASHMEM bool ProjectHistoryCoordinator::canRetain(
    ProjectHistoryDomain domain,
    ProjectHistoryRetainedUsage projected
) const {
    if (!retainedDomainValid(domain) ||
        projected.bytes > RETAINED_BYTE_BUDGET ||
        projected.spans > RETAINED_SPAN_BUDGET) {
        return false;
    }

    uint32_t bytes = projected.bytes;
    uint32_t spans = projected.spans;
    const uint8_t projectedIndex = retainedDomainIndex(domain);
    for (uint8_t index = 0U; index < retained_usage_.size(); ++index) {
        if (index == projectedIndex) continue;
        bytes += retained_usage_[index].bytes;
        spans += retained_usage_[index].spans;
    }
    return bytes <= RETAINED_BYTE_BUDGET &&
        spans <= RETAINED_SPAN_BUDGET;
}

FLASHMEM void ProjectHistoryCoordinator::setRetainedUsage(
    ProjectHistoryDomain domain,
    ProjectHistoryRetainedUsage retained
) {
    assert(canRetain(domain, retained));
    if (!retainedDomainValid(domain)) return;
    retained_usage_[retainedDomainIndex(domain)] = retained;
}

}  // namespace core::state::project
