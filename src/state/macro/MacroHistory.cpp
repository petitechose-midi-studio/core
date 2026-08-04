#include "state/macro/MacroHistoryInternals.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

#include <config/PlatformCompat.hpp>

#include "state/modulation/ProjectControlMacroOps.hpp"
#include "state/modulation/ProjectModulationDomainOps.hpp"
#include "state/macro/MacroWorkflow.hpp"
#include "state/project/ProjectTrackDomainOps.hpp"
namespace core::state::macro {

using namespace history_detail;

namespace {

using RetainedUsage =
    core::state::project::ProjectHistoryRetainedUsage;

constexpr uint32_t kExtmemAllocationOverheadEstimate = 16U;
constexpr uint32_t kMacroHistoryChangeBytes = 444U;
constexpr uint32_t kMacroSlotHistoryChangePayloadBytes = 32'888U;
constexpr uint32_t kMacroAutomationHistoryPayloadBytes = 96U;
constexpr uint32_t kMacroAutomationTakeHistoryPayloadBytes = 456U;
constexpr uint32_t kMacroModulationAssignmentsHistoryPayloadBytes = 6'184U;
constexpr uint32_t kMacroSlotDeletionHistoryPayloadBytes = 6'256U;
constexpr uint32_t kMacroPageStructureHistoryPayloadBytes = 1'952U;
constexpr uint32_t kProjectControlDomainStateBytes = 159'516U;
constexpr uint32_t kRecordedShapeCreationHistoryPayloadBytes = 72U;
constexpr uint32_t kMacroDestinationStructureHistoryPayloadBytes = 1'948U;
constexpr uint32_t kRecordedShapeEditHistoryPayloadBytes = 216U;
constexpr uint32_t kMacroAuxiliaryHistoryPayloadBytes = 244U;
constexpr uint32_t kProjectModulatorDeleteHistoryPayloadBytes = 120U;
constexpr uint32_t kProjectModulatorSplitHistoryPayloadBytes = 232U;
constexpr uint32_t kPackedCurvePointBytes = 4U;
constexpr uint32_t kDeleteBindingEntryBytes = 24U;
constexpr uint32_t kDeleteTriggerEntryBytes = 20U;
constexpr uint32_t kDeleteScaleEntryBytes = 6U;
constexpr uint32_t kSplitBindingEntryBytes = 44U;

#if defined(ARDUINO_TEENSY41) && !defined(OC_DESKTOP)
static_assert(sizeof(MacroHistoryChange) == kMacroHistoryChangeBytes);
static_assert(
    sizeof(MacroSlotHistoryChangePayload) ==
    kMacroSlotHistoryChangePayloadBytes
);
static_assert(
    sizeof(MacroAutomationHistoryPayload) ==
    kMacroAutomationHistoryPayloadBytes
);
static_assert(
    sizeof(MacroAutomationTakeHistoryPayload) ==
    kMacroAutomationTakeHistoryPayloadBytes
);
static_assert(
    sizeof(MacroModulationAssignmentsHistoryPayload) ==
    kMacroModulationAssignmentsHistoryPayloadBytes
);
static_assert(
    sizeof(MacroSlotDeletionHistoryPayload) ==
    kMacroSlotDeletionHistoryPayloadBytes
);
static_assert(
    sizeof(MacroPageStructureHistoryPayload) ==
    kMacroPageStructureHistoryPayloadBytes
);
static_assert(
    sizeof(core::state::modulation::ProjectControlDomainState) ==
    kProjectControlDomainStateBytes
);
static_assert(
    sizeof(ProjectRecordedShapeCreationHistoryPayload) ==
    kRecordedShapeCreationHistoryPayloadBytes
);
static_assert(
    sizeof(MacroDestinationStructureHistoryPayload) ==
    kMacroDestinationStructureHistoryPayloadBytes
);
static_assert(
    sizeof(ProjectRecordedShapeEditHistoryPayload) ==
    kRecordedShapeEditHistoryPayloadBytes
);
static_assert(
    sizeof(MacroAuxiliaryHistoryPayload) ==
    kMacroAuxiliaryHistoryPayloadBytes
);
static_assert(
    sizeof(ProjectModulatorDeleteHistoryPayload) ==
    kProjectModulatorDeleteHistoryPayloadBytes
);
static_assert(
    sizeof(ProjectModulatorSplitHistoryPayload) ==
    kProjectModulatorSplitHistoryPayloadBytes
);
static_assert(
    sizeof(core::state::modulation::ProjectPackedCurvePoint) ==
    kPackedCurvePointBytes
);
static_assert(
    sizeof(ProjectModulatorDeleteBindingEntry) == kDeleteBindingEntryBytes
);
static_assert(
    sizeof(ProjectModulatorDeleteTriggerEntry) == kDeleteTriggerEntryBytes
);
static_assert(
    sizeof(ProjectModulatorDeleteScaleEntry) == kDeleteScaleEntryBytes
);
static_assert(
    sizeof(ProjectModulatorSplitBindingEntry) == kSplitBindingEntryBytes
);
#endif

void addRetainedOwner(
    RetainedUsage& usage,
    bool present,
    uint32_t payloadBytes
) {
    if (!present) return;
    usage.bytes += payloadBytes + kExtmemAllocationOverheadEstimate;
    ++usage.spans;
}

void addRetainedUsage(RetainedUsage& target, RetainedUsage added) {
    target.bytes += added.bytes;
    target.spans = static_cast<uint16_t>(target.spans + added.spans);
}

void addAutomationSnapshotUsage(
    RetainedUsage& usage,
    const MacroAutomationHistorySnapshot& snapshot,
    uint16_t retainedPointCapacity
) {
    addRetainedOwner(
        usage,
        snapshot.points != nullptr,
        static_cast<uint32_t>(retainedPointCapacity) *
            kPackedCurvePointBytes
    );
}

RetainedUsage macroChangeRetainedUsage(const MacroHistoryChange& change) {
    RetainedUsage usage{};
    addRetainedOwner(usage, true, kMacroHistoryChangeBytes);

    addRetainedOwner(
        usage,
        change.slot != nullptr,
        kMacroSlotHistoryChangePayloadBytes
    );

    addRetainedOwner(
        usage,
        change.automation != nullptr,
        kMacroAutomationHistoryPayloadBytes
    );
    if (change.automation != nullptr) {
        addAutomationSnapshotUsage(
            usage,
            change.automation->before,
            change.automation->before.pointCount
        );
        addAutomationSnapshotUsage(
            usage,
            change.automation->after,
            change.automation->after.pointCount
        );
    }

    addRetainedOwner(
        usage,
        change.automationTake != nullptr,
        kMacroAutomationTakeHistoryPayloadBytes
    );
    if (change.automationTake != nullptr) {
        for (const auto& snapshot : change.automationTake->before) {
            addAutomationSnapshotUsage(
                usage,
                snapshot,
                snapshot.pointCount
            );
        }
        for (const auto& snapshot : change.automationTake->after) {
            addAutomationSnapshotUsage(
                usage,
                snapshot,
                MACRO_AUTOMATION_RECORDING_MAX_POINTS
            );
        }
    }

    addRetainedOwner(
        usage,
        change.modulationAssignments != nullptr,
        kMacroModulationAssignmentsHistoryPayloadBytes
    );
    addRetainedOwner(
        usage,
        change.slotDeletion != nullptr,
        kMacroSlotDeletionHistoryPayloadBytes
    );
    if (change.slotDeletion != nullptr) {
        addAutomationSnapshotUsage(
            usage,
            change.slotDeletion->before.automation,
            change.slotDeletion->before.automation.pointCount
        );
        addAutomationSnapshotUsage(
            usage,
            change.slotDeletion->after.automation,
            change.slotDeletion->after.automation.pointCount
        );
    }

    addRetainedOwner(
        usage,
        change.pageStructure != nullptr,
        kMacroPageStructureHistoryPayloadBytes
    );
    if (change.pageStructure != nullptr) {
        addRetainedOwner(
            usage,
            change.pageStructure->beforeControl != nullptr,
            kProjectControlDomainStateBytes
        );
        addRetainedOwner(
            usage,
            change.pageStructure->afterControl != nullptr,
            kProjectControlDomainStateBytes
        );
    }

    addRetainedOwner(
        usage,
        change.modulator.recordedShape != nullptr,
        kRecordedShapeCreationHistoryPayloadBytes
    );
    if (change.modulator.recordedShape != nullptr) {
        const auto& recorded = *change.modulator.recordedShape;
        addRetainedOwner(
            usage,
            recorded.points != nullptr,
            static_cast<uint32_t>(recorded.pointCount) *
                kPackedCurvePointBytes
        );
        addRetainedOwner(
            usage,
            recorded.beforePointTail != nullptr,
            static_cast<uint32_t>(recorded.pointCount) *
                kPackedCurvePointBytes
        );
    }
    addRetainedOwner(
        usage,
        change.modulator.destinationStructure != nullptr,
        kMacroDestinationStructureHistoryPayloadBytes
    );

    addRetainedOwner(
        usage,
        change.recordedShapeEdit != nullptr,
        kRecordedShapeEditHistoryPayloadBytes
    );
    if (change.recordedShapeEdit != nullptr) {
        const auto& recorded = *change.recordedShapeEdit;
        addRetainedOwner(
            usage,
            recorded.beforePoints != nullptr,
            static_cast<uint32_t>(recorded.beforeCurve.pointCount) *
                kPackedCurvePointBytes
        );
        addRetainedOwner(
            usage,
            recorded.afterPoints != nullptr,
            static_cast<uint32_t>(recorded.afterCurve.pointCount) *
                kPackedCurvePointBytes
        );
        addRetainedOwner(
            usage,
            recorded.boundaryPoints != nullptr,
            static_cast<uint32_t>(recorded.boundaryPointCount) *
                kPackedCurvePointBytes
        );
    }

    addRetainedOwner(
        usage,
        change.auxiliary != nullptr,
        kMacroAuxiliaryHistoryPayloadBytes
    );

    addRetainedOwner(
        usage,
        change.modulatorDelete != nullptr,
        kProjectModulatorDeleteHistoryPayloadBytes
    );
    if (change.modulatorDelete != nullptr) {
        const auto& deleted = *change.modulatorDelete;
        addRetainedOwner(
            usage,
            deleted.bindings != nullptr,
            static_cast<uint32_t>(deleted.bindingCount) *
                kDeleteBindingEntryBytes
        );
        addRetainedOwner(
            usage,
            deleted.triggers != nullptr,
            static_cast<uint32_t>(deleted.triggerCount) *
                kDeleteTriggerEntryBytes
        );
        addRetainedOwner(
            usage,
            deleted.scales != nullptr,
            static_cast<uint32_t>(deleted.scaleCount) *
                kDeleteScaleEntryBytes
        );
        addRetainedOwner(
            usage,
            deleted.curvePoints != nullptr,
            static_cast<uint32_t>(deleted.curvePointCount) *
                kPackedCurvePointBytes
        );
    }

    addRetainedOwner(
        usage,
        change.modulatorSplit != nullptr,
        kProjectModulatorSplitHistoryPayloadBytes
    );
    if (change.modulatorSplit != nullptr) {
        addRetainedOwner(
            usage,
            change.modulatorSplit->movedBindings != nullptr,
            static_cast<uint32_t>(change.modulatorSplit->movedBindingCount) *
                kSplitBindingEntryBytes
        );
    }

    return usage;
}

template <size_t Capacity>
RetainedUsage macroEntriesRetainedUsage(
    const std::array<MacroHistoryChangePtr, Capacity>& entries,
    uint8_t count
) {
    RetainedUsage usage{};
    for (uint8_t index = 0U; index < count; ++index) {
        if (entries[index] != nullptr) {
            addRetainedUsage(
                usage,
                macroChangeRetainedUsage(*entries[index])
            );
        }
    }
    return usage;
}

}  // namespace

FLASHMEM MacroHistoryChange::~MacroHistoryChange() = default;

FLASHMEM MacroHistoryService::MacroHistoryService() = default;
FLASHMEM MacroHistoryService::~MacroHistoryService() = default;

FLASHMEM MacroHistoryChangePtr MacroHistoryService::prepare(
    const MacroPagesState& pages,
    const MacroAutomationSlotAddress& address,
    MacroHistoryActionKind kind
) const {
    if (pendingModulatorSlot_() != nullptr) return {};
    auto change = core::app::makeExtmemUnique<MacroHistoryChange>();
    if (!change) return {};
    change->slot = core::app::makeExtmemUnique<MacroSlotHistoryChangePayload>();
    if (!change->slot) return {};
    change->kind = kind;
    change->address = address;
    if (!captureMacroSlotHistorySnapshot(
            pages,
            address,
            change->slot->before
        )) {
        return {};
    }
    return change;
}

FLASHMEM bool MacroHistoryService::compactPages(
    MacroPagesState& pages,
    uint8_t track,
    uint16_t retainedPageMask
) {
    if (pendingModulatorSlot_() != nullptr || track >= TRACK_COUNT) {
        return false;
    }
    retainedPageMask = static_cast<uint16_t>(
        retainedPageMask & pages.tracks[track].enabledPageMask
    );
    if (retainedPageMask == 0U ||
        retainedPageMask == pages.tracks[track].enabledPageMask) {
        return false;
    }

    auto change = core::app::makeExtmemUnique<MacroHistoryChange>();
    if (!change) return false;
    change->pageStructure =
        core::app::makeExtmemUnique<MacroPageStructureHistoryPayload>();
    if (!change->pageStructure) return false;
    auto& payload = *change->pageStructure;
    payload.beforeControl = core::app::makeExtmemUnique<
        core::state::modulation::ProjectControlDomainState
    >();
    if (!payload.beforeControl) return false;

    change->kind = MacroHistoryActionKind::PAGE_STRUCTURE;
    payload.operation = MacroPageStructureHistoryOperation::COMPACT;
    payload.track = track;
    payload.retainedPageMask = retainedPageMask;
    payload.beforeTrack = pages.tracks[track];
    *payload.beforeControl = pages.control.authored;

    if (!core::state::modulation::compactProjectControlPages(
            pages.control,
            track,
            retainedPageMask
        ) || !pages.tracks[track].compactPages(retainedPageMask)) {
        pages.control.authored = *payload.beforeControl;
        pages.control.markAuthoredMutation();
        pages.tracks[track] = payload.beforeTrack;
        syncPageStructureTrack(pages, track);
        return false;
    }
    syncPageStructureTrack(pages, track);
    payload.afterTrack = pages.tracks[track];
    payload.afterControlHash = pageStructureControlHash(
        pages.control.authored
    );
    if (!pageStructureAfterMatches(pages, payload)) {
        pages.control.authored = *payload.beforeControl;
        pages.control.markAuthoredMutation();
        pages.tracks[track] = payload.beforeTrack;
        syncPageStructureTrack(pages, track);
        return false;
    }

    change->address = {
        .track = track,
        .page = pages.tracks[track].activePage,
        .macro = 0U,
    };
    endCoalescing();
    recordNewEntry_(std::move(change));
    return true;
}

FLASHMEM MacroHistoryChangePtr
MacroHistoryService::preparePageStructureSnapshot(
    const MacroPagesState& pages,
    uint8_t track
) const {
    if (pendingModulatorSlot_() != nullptr || track >= TRACK_COUNT) return {};
    auto change = core::app::makeExtmemUnique<MacroHistoryChange>();
    if (!change) return {};
    change->pageStructure =
        core::app::makeExtmemUnique<MacroPageStructureHistoryPayload>();
    if (!change->pageStructure) return {};
    auto& payload = *change->pageStructure;
    payload.beforeControl = core::app::makeExtmemUnique<
        core::state::modulation::ProjectControlDomainState
    >();
    payload.afterControl = core::app::makeExtmemUnique<
        core::state::modulation::ProjectControlDomainState
    >();
    if (!payload.beforeControl || !payload.afterControl) return {};

    change->kind = MacroHistoryActionKind::PAGE_STRUCTURE;
    change->address = {
        .track = track,
        .page = pages.tracks[track].activePage,
        .macro = 0U,
    };
    payload.operation = MacroPageStructureHistoryOperation::SNAPSHOT;
    payload.track = track;
    payload.beforeTrack = pages.tracks[track];
    *payload.beforeControl = pages.control.authored;
    return change;
}

FLASHMEM bool MacroHistoryService::commitPreparedPageStructureSnapshot(
    MacroPagesState& pages,
    MacroHistoryChangePtr change
) {
    if (!change || change->kind != MacroHistoryActionKind::PAGE_STRUCTURE ||
        !change->pageStructure ||
        change->pageStructure->operation !=
            MacroPageStructureHistoryOperation::SNAPSHOT ||
        !change->pageStructure->beforeControl ||
        !change->pageStructure->afterControl) {
        return false;
    }
    auto& payload = *change->pageStructure;
    if (payload.track >= TRACK_COUNT) return false;
    payload.afterTrack = pages.tracks[payload.track];
    *payload.afterControl = pages.control.authored;
    payload.afterControlHash = pageStructureControlHash(
        pages.control.authored
    );
    const bool sameControl = std::memcmp(
        payload.beforeControl.get(),
        payload.afterControl.get(),
        sizeof(core::state::modulation::ProjectControlDomainState)
    ) == 0;
    if (sameMacroTrackData(payload.beforeTrack, payload.afterTrack) &&
        sameControl) {
        return false;
    }
    if (sameControl) payload.afterControl.reset();
    change->address.page = payload.afterTrack.activePage;
    endCoalescing();
    recordNewEntry_(std::move(change));
    return true;
}

FLASHMEM MacroHistoryChangePtr
MacroHistoryService::prepareAutomationRecording(
    const MacroPagesState& pages,
    const MacroAutomationSlotAddress& address
) const {
    if (pendingModulatorSlot_() != nullptr) return {};
    auto change = core::app::makeExtmemUnique<MacroHistoryChange>();
    if (!change) return {};
    change->automation = core::app::makeExtmemUnique<
        MacroAutomationHistoryPayload
    >();
    if (!change->automation) return {};
    change->kind = MacroHistoryActionKind::RECORD_AUTOMATION;
    change->address = address;
    if (!captureMacroAutomationHistorySnapshot(
            pages,
            address,
            change->automation->before
        )) {
        return {};
    }
    return change;
}

FLASHMEM MacroHistoryChangePtr MacroHistoryService::prepareAutomationTake(
    const MacroPagesState& pages,
    uint8_t track,
    uint8_t page,
    uint16_t candidateMask
) const {
    candidateMask = static_cast<uint16_t>(candidateMask & 0x00FFU);
    if (pendingModulatorSlot_() != nullptr || candidateMask == 0U ||
        track >= TRACK_COUNT || page >= PAGE_COUNT) {
        return {};
    }
    auto change = core::app::makeExtmemUnique<MacroHistoryChange>();
    if (!change) return {};
    change->automationTake = core::app::makeExtmemUnique<
        MacroAutomationTakeHistoryPayload
    >();
    if (!change->automationTake) return {};
    change->kind = MacroHistoryActionKind::RECORD_AUTOMATION;
    change->address = {.track = track, .page = page, .macro = 0U};
    auto& payload = *change->automationTake;
    payload.candidateMask = candidateMask;
    payload.track = track;
    payload.page = page;
    for (uint8_t macro = 0U; macro < MACRO_COUNT; ++macro) {
        const uint16_t bit = static_cast<uint16_t>(1U << macro);
        if ((candidateMask & bit) == 0U) continue;
        const MacroAutomationSlotAddress address{
            .track = track,
            .page = page,
            .macro = macro,
        };
        if (!captureMacroAutomationHistorySnapshot(
                pages,
                address,
                payload.before[macro]
            )) {
            return {};
        }
        auto& after = payload.after[macro];
        after.address = address;
        after.points = core::app::makeExtmemUniqueArrayForOverwrite<
            core::state::modulation::ProjectPackedCurvePoint
        >(MACRO_AUTOMATION_RECORDING_MAX_POINTS);
        if (!after.points) return {};
    }
    if (!automationTakePayloadConsistent(payload, false)) return {};
    return change;
}

FLASHMEM bool MacroHistoryService::commitPreparedAutomationTake(
    MacroPagesState& pages,
    MacroHistoryChangePtr& change
) {
    if (!change || change->kind != MacroHistoryActionKind::RECORD_AUTOMATION ||
        !change->automationTake ||
        !automationTakePayloadConsistent(*change->automationTake, true) ||
        !liveAutomationTakeMatches(pages, *change->automationTake, true)) {
        return false;
    }
    bool changed = false;
    for (uint8_t macro = 0U; macro < MACRO_COUNT; ++macro) {
        const uint16_t bit = static_cast<uint16_t>(1U << macro);
        if ((change->automationTake->touchedMask & bit) == 0U) continue;
        if (!sameMacroAutomationHistorySnapshot(
                change->automationTake->before[macro],
                change->automationTake->after[macro]
            )) {
            changed = true;
            break;
        }
    }
    if (!changed) return false;
    endCoalescing();
    recordNewEntry_(std::move(change));
    return true;
}

FLASHMEM void MacroHistoryService::clear() {
    if (project_history_sink_ != nullptr) {
        project_history_sink_->notifyCleared(
            core::state::project::ProjectHistoryDomain::Macro
        );
    }
    for (auto& entry : undo_) entry.reset();
    for (auto& entry : redo_) entry.reset();
    undo_count_ = 0;
    redo_count_ = 0;
    endCoalescing();
    publishRetainedUsage_();
}

FLASHMEM MacroHistoryChangePtr* MacroHistoryService::pendingModulatorSlot_() {
    for (uint8_t i = undo_count_; i < ENTRY_LIMIT; ++i) {
        if (undo_[i] && undo_[i]->modulator.pending) return &undo_[i];
    }
    for (uint8_t i = redo_count_; i < ENTRY_LIMIT; ++i) {
        if (redo_[i] && redo_[i]->modulator.pending) return &redo_[i];
    }
    return nullptr;
}

FLASHMEM const MacroHistoryChangePtr*
MacroHistoryService::pendingModulatorSlot_() const {
    for (uint8_t i = undo_count_; i < ENTRY_LIMIT; ++i) {
        if (undo_[i] && undo_[i]->modulator.pending) return &undo_[i];
    }
    for (uint8_t i = redo_count_; i < ENTRY_LIMIT; ++i) {
        if (redo_[i] && redo_[i]->modulator.pending) return &redo_[i];
    }
    return nullptr;
}

FLASHMEM bool MacroHistoryService::parkPending_(
    MacroHistoryChangePtr change
) {
    if (!change || pendingModulatorSlot_() != nullptr) return false;
    if (undo_count_ < ENTRY_LIMIT && !undo_[ENTRY_LIMIT - 1U]) {
        undo_[ENTRY_LIMIT - 1U] = std::move(change);
        return true;
    }
    if (redo_count_ < ENTRY_LIMIT && !redo_[ENTRY_LIMIT - 1U]) {
        redo_[ENTRY_LIMIT - 1U] = std::move(change);
        return true;
    }
    return false;
}

FLASHMEM MacroHistoryChangePtr MacroHistoryService::takePending_() {
    auto* slot = pendingModulatorSlot_();
    return slot != nullptr ? std::move(*slot) : MacroHistoryChangePtr{};
}

FLASHMEM void MacroHistoryService::push_(
    std::array<MacroHistoryChangePtr, ENTRY_LIMIT>& stack,
    uint8_t& count,
    MacroHistoryChangePtr change,
    const core::state::project::ProjectHistoryEventSink* sink
) {
    if (!change) return;
    if (count >= ENTRY_LIMIT) {
        if (sink != nullptr && stack[0]) {
            sink->notifyEvicted(
                core::state::project::ProjectHistoryDomain::Macro,
                reinterpret_cast<uintptr_t>(stack[0].get())
            );
        }
        for (uint8_t i = 1; i < ENTRY_LIMIT; ++i) {
            stack[i - 1U] = std::move(stack[i]);
        }
        stack[ENTRY_LIMIT - 1U].reset();
        count = static_cast<uint8_t>(ENTRY_LIMIT - 1U);
    }
    stack[count++] = std::move(change);
}

FLASHMEM void MacroHistoryService::recordNewEntry_(MacroHistoryChangePtr change) {
    if (!change) return;
    const auto incoming = macroChangeRetainedUsage(*change);
    assert(incoming.bytes <= RETAINED_BYTE_BUDGET);
    assert(incoming.spans <= RETAINED_SPAN_BUDGET);
    const uintptr_t identity = reinterpret_cast<uintptr_t>(change.get());
    const auto kind = change->kind;
    clearRedo_();

    const auto removeOldestUndo = [this]() {
        assert(undo_count_ > 0U);
        if (project_history_sink_ != nullptr && undo_[0]) {
            project_history_sink_->notifyEvicted(
                core::state::project::ProjectHistoryDomain::Macro,
                reinterpret_cast<uintptr_t>(undo_[0].get())
            );
        }
        for (uint8_t index = 1U; index < undo_count_; ++index) {
            undo_[index - 1U] = std::move(undo_[index]);
        }
        --undo_count_;
        undo_[undo_count_].reset();
    };

    if (undo_count_ >= ENTRY_LIMIT) removeOldestUndo();
    auto projected = retainedUsage_();
    addRetainedUsage(projected, incoming);
    while (undo_count_ > 0U &&
           (projected.bytes > RETAINED_BYTE_BUDGET ||
            projected.spans > RETAINED_SPAN_BUDGET ||
            (project_history_sink_ != nullptr &&
             !project_history_sink_->admitsRetainedUsage(
                 core::state::project::ProjectHistoryDomain::Macro,
                 projected
             )))) {
        removeOldestUndo();
        projected = retainedUsage_();
        addRetainedUsage(projected, incoming);
    }
    assert(projected.bytes <= RETAINED_BYTE_BUDGET);
    assert(projected.spans <= RETAINED_SPAN_BUDGET);
    assert(
        project_history_sink_ == nullptr ||
        project_history_sink_->admitsRetainedUsage(
            core::state::project::ProjectHistoryDomain::Macro,
            projected
        )
    );

    push_(undo_, undo_count_, std::move(change), project_history_sink_);
    publishRetainedUsage_();
    if (project_history_sink_ != nullptr) {
        project_history_sink_->notifyCommitted(
            core::state::project::ProjectHistoryDomain::Macro,
            identity,
            static_cast<uint8_t>(kind)
        );
    }
}

FLASHMEM void MacroHistoryService::clearRedo_() {
    for (uint8_t index = 0U; index < redo_count_; ++index) {
        if (project_history_sink_ != nullptr && redo_[index]) {
            project_history_sink_->notifyEvicted(
                core::state::project::ProjectHistoryDomain::Macro,
                reinterpret_cast<uintptr_t>(redo_[index].get())
            );
        }
        redo_[index].reset();
    }
    redo_count_ = 0;
}

FLASHMEM void MacroHistoryService::discardRedoBranch() {
    clearRedo_();
    publishRetainedUsage_();
}

FLASHMEM core::state::project::ProjectHistoryRetainedUsage
MacroHistoryService::retainedUsage_() const {
    auto usage = macroEntriesRetainedUsage(undo_, undo_count_);
    addRetainedUsage(
        usage,
        macroEntriesRetainedUsage(redo_, redo_count_)
    );
    return usage;
}

FLASHMEM size_t MacroHistoryService::retainedBytes() const {
    return retainedUsage_().bytes;
}

FLASHMEM uint16_t MacroHistoryService::retainedSpans() const {
    return retainedUsage_().spans;
}

FLASHMEM void MacroHistoryService::publishRetainedUsage_() const {
    if (project_history_sink_ != nullptr) {
        project_history_sink_->notifyRetainedUsage(
            core::state::project::ProjectHistoryDomain::Macro,
            retainedUsage_()
        );
    }
}

}  // namespace core::state::macro
