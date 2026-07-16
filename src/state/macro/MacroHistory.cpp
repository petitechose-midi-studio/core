#include "state/macro/MacroHistory.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

#include <config/PlatformCompat.hpp>

#include "state/modulation/ProjectControlMacroOps.hpp"
#include "state/modulation/ProjectModulationDomainOps.hpp"

namespace core::state::macro {

namespace {

FLASHMEM bool sameCurveMetadata(
    const MacroAutomationCurveRef& lhs,
    const MacroAutomationCurveRef& rhs
) {
    return lhs.active == rhs.active &&
           lhs.playbackState == rhs.playbackState &&
           lhs.pointCount == rhs.pointCount &&
           lhs.sourceDurationTicks == rhs.sourceDurationTicks &&
           lhs.durationTicks == rhs.durationTicks &&
           lhs.windowOffsetTicks == rhs.windowOffsetTicks &&
           lhs.interpolation == rhs.interpolation &&
           lhs.modulationOrigin == rhs.modulationOrigin;
}

FLASHMEM bool sameFloatBits(float lhs, float rhs) {
    return std::memcmp(&lhs, &rhs, sizeof(float)) == 0;
}

FLASHMEM bool samePoint(
    const MacroPackedCurvePoint& lhs,
    const MacroPackedCurvePoint& rhs
) {
    return lhs.tick == rhs.tick && lhs.value == rhs.value;
}

FLASHMEM uint16_t snapshotPointCount(const MacroSlotHistorySnapshot& snapshot) {
    return static_cast<uint16_t>(
        snapshot.automationPointCount + snapshot.modulationPointCount
    );
}

FLASHMEM bool snapshotConsistent(const MacroSlotHistorySnapshot& snapshot) {
    if (!macroAutomationAddressValid(snapshot.address)) return false;
    const uint32_t total = static_cast<uint32_t>(snapshot.automationPointCount) +
                           snapshot.modulationPointCount;
    if (total > MACRO_HISTORY_POINT_CAPACITY) return false;
    if (!snapshot.slotPresent) {
        return snapshot.automationPointCount == 0 &&
               snapshot.modulationPointCount == 0;
    }
    if (snapshot.slot.automation.active != (snapshot.automationPointCount > 0) ||
        snapshot.slot.modulation.active != (snapshot.modulationPointCount > 0) ||
        snapshot.slot.automation.pointCount != snapshot.automationPointCount ||
        snapshot.slot.modulation.pointCount != snapshot.modulationPointCount) {
        return false;
    }
    if (snapshot.slot.automation.active && snapshot.slot.automation.pointOffset != 0) {
        return false;
    }
    return !snapshot.slot.modulation.active ||
           snapshot.slot.modulation.pointOffset == snapshot.automationPointCount;
}

FLASHMEM void normalizeCurveOffsets(MacroSlotHistorySnapshot& snapshot) {
    snapshot.slot.automation.pointOffset = 0;
    snapshot.slot.modulation.pointOffset = snapshot.automationPointCount;
}

FLASHMEM bool liveProjectCurveMatches(
    const core::state::modulation::ProjectControlState& control,
    core::state::modulation::ProjectCurveId curveId,
    const MacroAutomationCurveRef& live,
    const MacroAutomationCurveRef& expected,
    const MacroSlotHistorySnapshot& snapshot,
    uint16_t snapshotOffset
) {
    if (!sameCurveMetadata(live, expected)) return false;
    if (!live.active) return !core::state::modulation::valid(curveId);
    const auto* record = core::state::modulation::findProjectCurve(
        control.authored.curves,
        curveId
    );
    if (record == nullptr || record->pointCount != live.pointCount ||
        static_cast<uint32_t>(record->pointOffset) + record->pointCount >
            control.authored.curves.pointCount ||
        static_cast<uint32_t>(snapshotOffset) + live.pointCount >
            snapshot.points.size()) {
        return false;
    }
    for (uint16_t i = 0; i < live.pointCount; ++i) {
        const auto& point = control.authored.curves.points[
            static_cast<uint16_t>(record->pointOffset + i)
        ];
        const MacroPackedCurvePoint livePoint{point.tick, point.value};
        if (!samePoint(
                livePoint,
                snapshot.points[static_cast<uint16_t>(snapshotOffset + i)]
            )) {
            return false;
        }
    }
    return true;
}

FLASHMEM bool sameAddress(
    const MacroAutomationSlotAddress& lhs,
    const MacroAutomationSlotAddress& rhs
) {
    return macroAutomationAddressEquals(lhs, rhs);
}

template <typename T>
FLASHMEM bool sameObjectBits(const T& lhs, const T& rhs) {
    static_assert(std::is_trivially_copyable_v<T>);
    return std::memcmp(&lhs, &rhs, sizeof(T)) == 0;
}

FLASHMEM bool creationIdentityMatches(
    const core::state::modulation::ProjectControlState& control,
    const MacroModulatorCreationHistoryPayload& payload,
    bool exactAfter
) {
    const auto& graph = control.authored.modulation;
    if (graph.sourceCount != payload.beforeSourceCount + 1U ||
        graph.outputBindingCount != payload.beforeBindingCount + 1U ||
        graph.nextSourceId != payload.afterNextSourceId ||
        graph.nextBindingId != payload.afterNextBindingId) {
        return false;
    }
    const auto& source = graph.sources[payload.beforeSourceCount];
    const auto& binding = graph.outputBindings[payload.beforeBindingCount];
    if (source.id != payload.source.id || binding.id != payload.binding.id ||
        binding.sourceId != source.id ||
        binding.destination != payload.binding.destination) {
        return false;
    }
    return !exactAfter ||
           (sameObjectBits(source, payload.source) &&
            sameObjectBits(binding, payload.binding));
}

FLASHMEM bool creationBeforeMatches(
    const core::state::modulation::ProjectControlState& control,
    const MacroModulatorCreationHistoryPayload& payload
) {
    const auto& graph = control.authored.modulation;
    return graph.sourceCount == payload.beforeSourceCount &&
           graph.outputBindingCount == payload.beforeBindingCount &&
           graph.nextSourceId == payload.beforeNextSourceId &&
           graph.nextBindingId == payload.beforeNextBindingId &&
           sameObjectBits(
               graph.sources[payload.beforeSourceCount],
               payload.beforeSourceTail
           ) &&
           sameObjectBits(
               graph.outputBindings[payload.beforeBindingCount],
               payload.beforeBindingTail
           );
}

FLASHMEM void restoreCreationBefore(
    core::state::modulation::ProjectControlState& control,
    const MacroModulatorCreationHistoryPayload& payload,
    bool exactCancel
) {
    auto& graph = control.authored.modulation;
    graph.sources[payload.beforeSourceCount] = payload.beforeSourceTail;
    graph.outputBindings[payload.beforeBindingCount] =
        payload.beforeBindingTail;
    graph.sourceCount = payload.beforeSourceCount;
    graph.outputBindingCount = payload.beforeBindingCount;
    graph.nextSourceId = payload.beforeNextSourceId;
    graph.nextBindingId = payload.beforeNextBindingId;
    if (exactCancel) {
        control.authoredRevision = payload.beforeAuthoredRevision;
    } else {
        control.markAuthoredMutation();
    }
}

FLASHMEM void restoreCreationAfter(
    core::state::modulation::ProjectControlState& control,
    const MacroModulatorCreationHistoryPayload& payload
) {
    auto& graph = control.authored.modulation;
    graph.sources[payload.beforeSourceCount] = payload.source;
    graph.outputBindings[payload.beforeBindingCount] = payload.binding;
    graph.sourceCount = static_cast<uint16_t>(payload.beforeSourceCount + 1U);
    graph.outputBindingCount = static_cast<uint16_t>(
        payload.beforeBindingCount + 1U
    );
    graph.nextSourceId = payload.afterNextSourceId;
    graph.nextBindingId = payload.afterNextBindingId;
    control.markAuthoredMutation();
}

FLASHMEM uint32_t auditionGeneration(
    uint32_t revision,
    core::state::modulation::ModulatorId sourceId,
    core::state::modulation::ModulationBindingId bindingId
) {
    uint32_t generation = revision ^ (sourceId.value * 0x9E3779B9UL) ^
                          (bindingId.value * 0x85EBCA6BUL);
    return generation == 0U ? 1U : generation;
}

}  // namespace

FLASHMEM bool captureMacroSlotHistorySnapshot(
    const MacroPagesState& pages,
    const MacroAutomationSlotAddress& address,
    MacroSlotHistorySnapshot& out
) {
    if (!macroAutomationAddressValid(address)) return false;
    out = {};
    out.address = address;
    const auto& page = pages.pageData(address.track, address.page);
    out.macroActive = page.isMacroActive(address.macro);
    out.cc = page.cc[address.macro];
    out.staticValue = page.values[address.macro];

    if (!core::state::modulation::captureProjectControlMacroSlot(
            pages.control,
            address,
            out.slot,
            out.points.data(),
            static_cast<uint16_t>(out.points.size()),
            out.automationPointCount,
            out.modulationPointCount
        )) {
        return false;
    }
    out.slotPresent = macroCurveStored(out.slot.automation) ||
                      macroCurveStored(out.slot.modulation);
    normalizeCurveOffsets(out);
    return snapshotConsistent(out);
}

FLASHMEM bool sameMacroSlotHistorySnapshot(
    const MacroSlotHistorySnapshot& lhs,
    const MacroSlotHistorySnapshot& rhs
) {
    if (!sameAddress(lhs.address, rhs.address) ||
        lhs.macroActive != rhs.macroActive ||
        lhs.cc != rhs.cc ||
        !sameFloatBits(lhs.staticValue, rhs.staticValue) ||
        lhs.slotPresent != rhs.slotPresent ||
        lhs.automationPointCount != rhs.automationPointCount ||
        lhs.modulationPointCount != rhs.modulationPointCount) {
        return false;
    }
    if (!lhs.slotPresent) return true;
    if (!sameCurveMetadata(lhs.slot.automation, rhs.slot.automation) ||
        !sameCurveMetadata(lhs.slot.modulation, rhs.slot.modulation) ||
        !sameFloatBits(lhs.slot.modulationDepth, rhs.slot.modulationDepth)) {
        return false;
    }
    const uint16_t count = snapshotPointCount(lhs);
    for (uint16_t i = 0; i < count; ++i) {
        if (!samePoint(lhs.points[i], rhs.points[i])) return false;
    }
    return true;
}

FLASHMEM bool liveMacroSlotMatchesHistorySnapshot(
    const MacroPagesState& pages,
    const MacroSlotHistorySnapshot& snapshot
) {
    if (!snapshotConsistent(snapshot)) return false;
    const auto& address = snapshot.address;
    const auto& page = pages.pageData(address.track, address.page);
    if (page.isMacroActive(address.macro) != snapshot.macroActive ||
        page.cc[address.macro] != snapshot.cc ||
        !sameFloatBits(page.values[address.macro], snapshot.staticValue)) {
        return false;
    }

    core::state::modulation::ProjectControlMacroSlotView live{};
    if (!core::state::modulation::readProjectControlMacroSlot(
            pages.control,
            address,
            live
        ) || live.present != snapshot.slotPresent) {
        return false;
    }
    if (!live.present) return true;
    return sameFloatBits(
               live.legacy.modulationDepth,
               snapshot.slot.modulationDepth
           ) &&
           liveProjectCurveMatches(
               pages.control,
               live.automationCurveId,
               live.legacy.automation,
               snapshot.slot.automation,
               snapshot,
               0
           ) &&
           liveProjectCurveMatches(
               pages.control,
               live.modulationCurveId,
               live.legacy.modulation,
               snapshot.slot.modulation,
               snapshot,
               snapshot.automationPointCount
           );
}

FLASHMEM bool applyMacroSlotHistorySnapshot(
    MacroPagesState& pages,
    const MacroSlotHistorySnapshot& snapshot
) {
    if (!snapshotConsistent(snapshot)) return false;
    const auto& address = snapshot.address;
    const uint16_t required = snapshotPointCount(snapshot);
    MacroAutomationSlotState empty{};
    const auto& slot = snapshot.slotPresent ? snapshot.slot : empty;
    const auto* points = required > 0U ? snapshot.points.data() : nullptr;
    if (!core::state::modulation::replaceProjectControlMacroSlot(
            pages.control,
            address,
            slot,
            points,
            required
        )) {
        return false;
    }

    auto& page = pages.pageData(address.track, address.page);
    page.setMacroActive(address.macro, snapshot.macroActive);
    page.cc[address.macro] = snapshot.cc;
    page.values[address.macro] = snapshot.staticValue;
    if (pages.currentActiveTrack() == address.track &&
        pages.currentActivePage() == address.page) {
        pages.updateActiveConfigs();
    }
    return true;
}

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

FLASHMEM core::state::modulation::ProjectModulationResult
MacroHistoryService::beginLfoModulatorAudition(
    MacroPagesState& pages,
    const MacroAutomationSlotAddress& address,
    const core::state::modulation::ModulatorLfoDraft& sourceDraft,
    const core::state::modulation::ModulationBindingDraft& bindingDraft
) {
    using namespace core::state::modulation;
    ProjectModulationResult failure{};
    failure.status = ProjectModulationStatus::INVALID_ARGUMENT;
    if (!macroAutomationAddressValid(address) ||
        bindingDraft.destination != projectControlDestination(address) ||
        pendingModulatorSlot_() != nullptr || pages.control.audition.active) {
        return failure;
    }

    auto change = core::app::makeExtmemUnique<MacroHistoryChange>();
    if (!change) return failure;
    change->kind = MacroHistoryActionKind::CREATE_MODULATOR_ASSIGNMENT;
    change->address = address;
    auto& payload = change->modulator;
    auto& graph = pages.control.authored.modulation;
    if (graph.sourceCount >= PROJECT_MODULATOR_CAPACITY ||
        graph.outputBindingCount >= PROJECT_MODULATION_BINDING_CAPACITY) {
        failure.status = graph.sourceCount >= PROJECT_MODULATOR_CAPACITY
            ? ProjectModulationStatus::SOURCE_CAPACITY_EXCEEDED
            : ProjectModulationStatus::BINDING_CAPACITY_EXCEEDED;
        return failure;
    }
    payload.beforeSourceCount = graph.sourceCount;
    payload.beforeBindingCount = graph.outputBindingCount;
    payload.beforeNextSourceId = graph.nextSourceId;
    payload.beforeNextBindingId = graph.nextBindingId;
    payload.beforeAuthoredRevision = pages.control.authoredRevision;
    payload.beforeSourceTail = graph.sources[graph.sourceCount];
    payload.beforeBindingTail = graph.outputBindings[graph.outputBindingCount];
    payload.pending = true;
    MacroHistoryChange* reserved = change.get();
    if (!parkPending_(std::move(change))) return failure;

    const auto created = createLfoModulator(graph, sourceDraft);
    if (!created.changed()) {
        (void)takePending_();
        return created;
    }
    auto binding = bindingDraft;
    binding.sourceId = created.sourceId;
    const auto bound = addProjectModulationBinding(graph, binding);
    if (!bound.changed()) {
        restoreCreationBefore(pages.control, payload, true);
        (void)takePending_();
        return bound;
    }

    payload.source = graph.sources[payload.beforeSourceCount];
    payload.binding = graph.outputBindings[payload.beforeBindingCount];
    payload.afterNextSourceId = graph.nextSourceId;
    payload.afterNextBindingId = graph.nextBindingId;
    pages.control.markAuthoredMutation();
    payload.generation = auditionGeneration(
        pages.control.authoredRevision,
        created.sourceId,
        bound.bindingId
    );
    pages.control.audition = {
        .sourceId = created.sourceId,
        .bindingId = bound.bindingId,
        .destination = binding.destination,
        .generation = payload.generation,
        .active = true,
    };
    (void)reserved;
    return {
        .status = ProjectModulationStatus::OK,
        .sourceId = created.sourceId,
        .bindingId = bound.bindingId,
    };
}

FLASHMEM bool MacroHistoryService::cancelModulatorAudition(
    MacroPagesState& pages,
    const MacroAutomationSlotAddress& address
) {
    auto* slot = pendingModulatorSlot_();
    if (slot == nullptr || !*slot) return false;
    auto& change = **slot;
    auto& payload = change.modulator;
    const auto& audition = pages.control.audition;
    if (!payload.pending || !sameAddress(change.address, address) ||
        !audition.active || audition.generation != payload.generation ||
        audition.sourceId != payload.source.id ||
        audition.bindingId != payload.binding.id ||
        !creationIdentityMatches(pages.control, payload, false)) {
        return false;
    }
    restoreCreationBefore(pages.control, payload, true);
    pages.control.audition = {};
    (void)takePending_();
    return true;
}

FLASHMEM bool MacroHistoryService::commitModulatorAudition(
    MacroPagesState& pages,
    const MacroAutomationSlotAddress& address
) {
    auto* slot = pendingModulatorSlot_();
    if (slot == nullptr || !*slot) return false;
    auto& change = **slot;
    auto& payload = change.modulator;
    const auto& audition = pages.control.audition;
    if (!payload.pending || !sameAddress(change.address, address) ||
        !audition.active || audition.generation != payload.generation ||
        !creationIdentityMatches(pages.control, payload, false)) {
        return false;
    }
    const auto& graph = pages.control.authored.modulation;
    payload.source = graph.sources[payload.beforeSourceCount];
    payload.binding = graph.outputBindings[payload.beforeBindingCount];
    payload.afterNextSourceId = graph.nextSourceId;
    payload.afterNextBindingId = graph.nextBindingId;
    auto committed = takePending_();
    if (!committed) return false;
    committed->modulator.pending = false;
    pages.control.audition = {};
    endCoalescing();
    push_(undo_, undo_count_, std::move(committed));
    clearRedo_();
    return true;
}

FLASHMEM bool MacroHistoryService::modulatorAuditionPending(
    const MacroAutomationSlotAddress& address
) const {
    const auto* slot = pendingModulatorSlot_();
    return slot != nullptr && *slot && (*slot)->modulator.pending &&
           sameAddress((*slot)->address, address);
}

FLASHMEM bool MacroHistoryService::commitPrepared(
    MacroPagesState& pages,
    MacroHistoryChangePtr change,
    bool coalesce
) {
    if (!change || !change->slot ||
        !sameAddress(change->address, change->slot->before.address)) {
        return false;
    }
    if (!captureMacroSlotHistorySnapshot(
            pages,
            change->address,
            change->slot->after
        )) {
        (void)applyMacroSlotHistorySnapshot(pages, change->slot->before);
        return false;
    }
    if (sameMacroSlotHistorySnapshot(
            change->slot->before,
            change->slot->after
        )) {
        return false;
    }

    if (coalesce && coalescing_ && undo_count_ > 0 &&
        coalesced_kind_ == change->kind &&
        sameAddress(coalesced_address_, change->address)) {
        auto& previous = undo_[undo_count_ - 1U];
        if (previous && previous->slot &&
            sameMacroSlotHistorySnapshot(
                previous->slot->after,
                change->slot->before
            )) {
            previous->slot->after = change->slot->after;
            clearRedo_();
            return true;
        }
    }

    push_(undo_, undo_count_, std::move(change));
    clearRedo_();
    coalescing_ = coalesce;
    if (coalescing_) {
        coalesced_kind_ = undo_[undo_count_ - 1U]->kind;
        coalesced_address_ = undo_[undo_count_ - 1U]->address;
    }
    return true;
}

FLASHMEM bool MacroHistoryService::setModulationDepthCoalesced(
    MacroPagesState& pages,
    const MacroAutomationSlotAddress& address,
    float depth
) {
    if (!macroAutomationAddressValid(address) || !std::isfinite(depth)) return false;
    core::state::modulation::ProjectControlMacroSlotView slot{};
    if (!core::state::modulation::readProjectControlMacroSlot(
            pages.control,
            address,
            slot
        ) || !slot.modulationStored || slot.legacyMutationAmbiguous) {
        return false;
    }
    const float next = std::clamp(depth, 0.0f, 1.0f);
    if (sameFloatBits(next, slot.legacy.modulationDepth)) return false;

    if (coalescing_ && undo_count_ > 0 &&
        coalesced_kind_ == MacroHistoryActionKind::DEPTH_EDIT &&
        sameAddress(coalesced_address_, address)) {
        auto& previous = undo_[undo_count_ - 1U];
        if (previous && previous->slot &&
            liveMacroSlotMatchesHistorySnapshot(
                pages,
                previous->slot->after
            )) {
            if (!core::state::modulation::setProjectControlModulationAmount(
                    pages.control,
                    address,
                    next
                )) {
                return false;
            }
            core::state::modulation::ProjectControlMacroSlotView updated{};
            if (!core::state::modulation::readProjectControlMacroSlot(
                    pages.control,
                    address,
                    updated
                )) {
                return false;
            }
            previous->slot->after.slot.modulationDepth =
                updated.legacy.modulationDepth;
            clearRedo_();
            return true;
        }
    }

    auto change = prepare(pages, address, MacroHistoryActionKind::DEPTH_EDIT);
    if (!change) return false;
    if (!core::state::modulation::setProjectControlModulationAmount(
            pages.control,
            address,
            next
        )) {
        return false;
    }
    return commitPrepared(pages, std::move(change), true);
}

FLASHMEM void MacroHistoryService::endCoalescing() {
    coalescing_ = false;
}

FLASHMEM bool MacroHistoryService::undo(
    MacroPagesState& pages,
    MacroAutomationSlotAddress* appliedAddress
) {
    endCoalescing();
    if (pendingModulatorSlot_() != nullptr) return false;
    if (undo_count_ == 0) return false;
    auto& change = undo_[undo_count_ - 1U];
    if (!change) return false;
    if (change->kind == MacroHistoryActionKind::CREATE_MODULATOR_ASSIGNMENT) {
        if (!creationIdentityMatches(
                pages.control,
                change->modulator,
                true
            )) {
            return false;
        }
        restoreCreationBefore(pages.control, change->modulator, false);
    } else {
        if (!change->slot ||
            !liveMacroSlotMatchesHistorySnapshot(
                pages,
                change->slot->after
            ) ||
            !applyMacroSlotHistorySnapshot(pages, change->slot->before)) {
            return false;
        }
    }
    auto applied = std::move(change);
    if (appliedAddress != nullptr) *appliedAddress = applied->address;
    --undo_count_;
    push_(redo_, redo_count_, std::move(applied));
    return true;
}

FLASHMEM bool MacroHistoryService::redo(
    MacroPagesState& pages,
    MacroAutomationSlotAddress* appliedAddress
) {
    endCoalescing();
    if (pendingModulatorSlot_() != nullptr) return false;
    if (redo_count_ == 0) return false;
    auto& change = redo_[redo_count_ - 1U];
    if (!change) return false;
    if (change->kind == MacroHistoryActionKind::CREATE_MODULATOR_ASSIGNMENT) {
        if (!creationBeforeMatches(pages.control, change->modulator)) {
            return false;
        }
        restoreCreationAfter(pages.control, change->modulator);
    } else {
        if (!change->slot ||
            !liveMacroSlotMatchesHistorySnapshot(
                pages,
                change->slot->before
            ) ||
            !applyMacroSlotHistorySnapshot(pages, change->slot->after)) {
            return false;
        }
    }
    auto applied = std::move(change);
    if (appliedAddress != nullptr) *appliedAddress = applied->address;
    --redo_count_;
    push_(undo_, undo_count_, std::move(applied));
    return true;
}

FLASHMEM void MacroHistoryService::clear() {
    for (auto& entry : undo_) entry.reset();
    for (auto& entry : redo_) entry.reset();
    undo_count_ = 0;
    redo_count_ = 0;
    endCoalescing();
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
    MacroHistoryChangePtr change
) {
    if (!change) return;
    if (count >= ENTRY_LIMIT) {
        for (uint8_t i = 1; i < ENTRY_LIMIT; ++i) {
            stack[i - 1U] = std::move(stack[i]);
        }
        stack[ENTRY_LIMIT - 1U].reset();
        count = static_cast<uint8_t>(ENTRY_LIMIT - 1U);
    }
    stack[count++] = std::move(change);
}

FLASHMEM void MacroHistoryService::clearRedo_() {
    for (auto& entry : redo_) entry.reset();
    redo_count_ = 0;
}

}  // namespace core::state::macro
