#include "handler/sequencer/SequencerPreparedPageStructureMutationPlan.hpp"

#include <algorithm>
#include <limits>
#include <new>
#include <type_traits>

#include <config/PlatformCompat.hpp>

#include "handler/sequencer/SequencerStructureStepOps.hpp"
#include "state/sequencer/SequencerCcLaneDomain.hpp"
#include "state/sequencer/SequencerGraphOps.hpp"
#include "state/sequencer/SequencerPatternRegionOps.hpp"
#include "state/sequencer/SequencerSnapshotOps.hpp"
#include "state/sequencer/SequencerStepPastePlan.hpp"

#if defined(ARDUINO)
// The linker places this complete authored-edit translation unit in cached
// Flash by object name. Let -ffunction-sections keep one input section per
// helper so --gc-sections can discard action families not yet linked.
#undef FLASHMEM
#define FLASHMEM
#endif

namespace core::handler {

namespace {

namespace seq = core::state::sequencer;
namespace project = core::state::project;

using Graph = oc::note::sequencer::StepSequencerGraph;
using GraphLimits = oc::note::sequencer::StepSequencerGraphLimits;
using GraphNode = oc::note::sequencer::StepSequencerStepNode;
using ContentContext = SequencerPreparedPageStructureContentContext;
using Preflight = SequencerPreparedPageStructurePreflightOutcome;
using Plan = SequencerPreparedPageStructureMutationPlan;

static_assert(std::is_trivially_destructible_v<Plan>);

constexpr uint8_t kFlagGraphDelta = 1U << 0;
constexpr uint8_t kFlagRequiresCompaction = 1U << 1;
constexpr uint8_t kFlagLiveGraphOwnerPresent = 1U << 2;
constexpr uint8_t kFlagRootContext = 1U << 3;
constexpr uint8_t kFlagChildResize = 1U << 4;
constexpr uint8_t kFlagNeedsGraphOwner = 1U << 5;
constexpr uint8_t kFlagFlatDelta = 1U << 6;
constexpr uint8_t kFlagLiveGraphActive = 1U << 7;

constexpr uint16_t kNodeChildFlags =
    oc::note::sequencer::STEP_NODE_CHILD_SEQUENCE |
    oc::note::sequencer::STEP_NODE_CYCLE_SET;

struct GraphAnalysis {
    seq::SequencerGraphCopyBudget budget{};
    bool valid = true;
    bool changed = false;
    bool requiresCompaction = false;
    bool sourcePayloadPresent = false;
};

struct MutationAccumulator {
    seq::SequencerSnapshotBatchDomains domains{};
    bool graphChanged = false;
    bool graphRevisionPublished = false;

    void merge(const seq::SequencerSnapshotBatchDomains& addition) noexcept {
        domains.stepData = domains.stepData || addition.stepData;
        domains.graph = domains.graph || addition.graph;
        domains.ccLanes = domains.ccLanes || addition.ccLanes;
        domains.timing = domains.timing || addition.timing;
        graphChanged = graphChanged || addition.graph;
    }

    [[nodiscard]] bool changed() const noexcept {
        return domains.any() || graphChanged;
    }
};

struct SourcePayload {
    const core::state::SequencerPageClipboard* page = nullptr;
    const core::state::SequencerStepClipboardEntry* step = nullptr;
    uint8_t localIndex = 0U;
    seq::SequencerGraphNodeId nodeId = GraphLimits::INVALID_ID;
    bool valid = false;
};

constexpr uint8_t pageCountForLength(uint8_t length) noexcept {
    return length == 0U
        ? 0U
        : static_cast<uint8_t>(
              (static_cast<uint16_t>(length) +
               seq::SequencerState::STEPS_PER_PAGE - 1U) /
              seq::SequencerState::STEPS_PER_PAGE);
}

FLASHMEM uint8_t pageCreateTargetFor(
    const seq::SequencerState& sequencer
) noexcept {
    return sequencer.structureUi.previewAddPageSlot.get()
        ? sequencer.structureUi.previewPageIndex.get()
        : sequencer.activePageCount();
}

FLASHMEM uint8_t pagePasteTargetFor(
    const seq::SequencerState& sequencer
) noexcept {
    return sequencer.structureUi.previewAddPageSlot.get()
        ? sequencer.structureUi.previewPageIndex.get()
        : sequencer.visiblePage();
}

constexpr bool flag(const Plan& plan, uint8_t value) noexcept {
    return (plan.flags & value) != 0U;
}

FLASHMEM ContentContext contentContextFor(
    const seq::SequencerPreparedGraphContentPath& path
) noexcept {
    if (!path.valid || path.stackDepth > path.frames.size()) {
        return ContentContext::Invalid;
    }
    if (path.stackDepth == 0U) return ContentContext::Root;

    const auto kind = path.frames[path.stackDepth - 1U].kind;
    if (kind == seq::SequencerContentViewKind::MICRO_SEQUENCE) {
        return ContentContext::MicroSequence;
    }
    if (kind == seq::SequencerContentViewKind::CYCLE_STATES) {
        return ContentContext::CycleStates;
    }
    return ContentContext::Invalid;
}

FLASHMEM bool sameFrame(
    const seq::SequencerContentViewFrame& lhs,
    const seq::SequencerContentViewFrame& rhs
) noexcept {
    return lhs.kind == rhs.kind &&
           lhs.ownerRootStep == rhs.ownerRootStep &&
           lhs.ownerLocalStep == rhs.ownerLocalStep &&
           lhs.pageSnapshot == rhs.pageSnapshot &&
           lhs.focusSnapshot == rhs.focusSnapshot &&
           lhs.ownerNodeId == rhs.ownerNodeId &&
           lhs.sequenceId == rhs.sequenceId &&
           lhs.cycleSetId == rhs.cycleSetId &&
           lhs.length == rhs.length;
}

FLASHMEM bool samePath(
    const seq::SequencerPreparedGraphContentPath& lhs,
    const seq::SequencerPreparedGraphContentPath& rhs
) noexcept {
    if (!lhs.valid || !rhs.valid || lhs.compacted != rhs.compacted ||
        lhs.stackDepth != rhs.stackDepth ||
        lhs.stackDepth > lhs.frames.size() ||
        rhs.stackDepth > rhs.frames.size()) {
        return false;
    }
    for (uint8_t index = 0U; index < lhs.stackDepth; ++index) {
        if (!sameFrame(lhs.frames[index], rhs.frames[index])) return false;
    }
    return true;
}

FLASHMEM bool validFlatStepValues(
    uint8_t note,
    uint8_t velocity,
    uint16_t gate,
    int8_t nudge,
    uint8_t probability
) noexcept {
    return seq::SequencerState::clampMidi7(note) == note &&
           seq::SequencerState::clampMidi7(velocity) == velocity &&
           seq::SequencerState::clampGatePercent(gate) == gate &&
           seq::SequencerState::clampNudge(nudge) == nudge &&
           seq::SequencerState::clampProbability(probability) == probability;
}

FLASHMEM bool validPageClipboard(
    const core::state::SequencerPageClipboard& page
) noexcept {
    if (!page.valid || page.count == 0U ||
        page.count > page.STEP_COUNT ||
        page.sourcePage >= seq::SequencerState::PAGE_COUNT) {
        return false;
    }
    const uint8_t validMask = static_cast<uint8_t>(
        (uint16_t{1} << page.count) - 1U);
    if ((page.enabledMask & static_cast<uint8_t>(~validMask)) != 0U) {
        return false;
    }
    for (uint8_t index = 0U; index < page.count; ++index) {
        if (!validFlatStepValues(
                page.note[index],
                page.velocity[index],
                page.gate[index],
                page.nudge[index],
                page.probability[index])) {
            return false;
        }
    }
    return true;
}

FLASHMEM bool validStepsClipboard(
    const core::state::SequencerStepsClipboard& steps
) noexcept {
    if (!steps.valid || steps.count == 0U ||
        steps.count > steps.entries.size() || steps.span == 0U) {
        return false;
    }

    uint16_t previousOffset = std::numeric_limits<uint16_t>::max();
    for (uint8_t index = 0U; index < steps.count; ++index) {
        const auto& entry = steps.entries[index];
        if (!entry.valid || entry.offset >= steps.span ||
            (index > 0U && entry.offset <= previousOffset) ||
            !validFlatStepValues(
                entry.note,
                entry.velocity,
                entry.gate,
                entry.nudge,
                entry.probability)) {
            return false;
        }
        previousOffset = entry.offset;
    }
    return true;
}

FLASHMEM bool validLiveGraph(const seq::SequencerPatternState& pattern) noexcept {
    if (!pattern.graph) return true;
    if (!pattern.graph->enabled) {
        return seq::isCanonicalDisabledSequencerGraph(*pattern.graph);
    }
    return seq::validInitializedSequencerGraph(*pattern.graph);
}

FLASHMEM bool validPattern(const seq::SequencerState& sequencer) noexcept {
    const uint8_t length = sequencer.pattern.length.get();
    if (length == 0U || length > seq::SequencerState::MAX_STEPS ||
        !seq::patternPlaybackRegion(sequencer.pattern).isValid() ||
        (sequencer.pattern.enabledMask.get() & ~seq::lengthMask(length)) !=
            oc::note::sequencer::StepBitMask128{} ||
        !validLiveGraph(sequencer.pattern)) {
        return false;
    }
    return !sequencer.pattern.ccLanes ||
           seq::validSequencerCcLaneBank(*sequencer.pattern.ccLanes);
}

FLASHMEM bool beginPlan(
    const seq::SequencerState& sequencer,
    uint8_t expectedTrack,
    SequencerPreparedPageStructureAction action,
    Plan& out
) noexcept {
    // Reconstruct directly in caller-owned storage. Plain aggregate assignment
    // materializes a complete 232-byte ARM temporary and used to push this
    // planner helper above its frozen 256-byte stack-frame contract.
    out.~Plan();
    ::new (static_cast<void*>(&out)) Plan{};
    out.targetToSource.fill(Plan::TARGET_UNTOUCHED);
    out.action = action;
    out.expectedTrack = expectedTrack;
    out.resetDepth = StepResetDepth::Shallow;
    if (expectedTrack >= seq::SequencerTrackBankState::TRACK_COUNT ||
        !validPattern(sequencer)) {
        return false;
    }

    out.contentPath = seq::capturePreparedSequencerGraphContentPath(sequencer);
    const auto context = contentContextFor(out.contentPath);
    if (context == ContentContext::Invalid) return false;

    out.patternLength = sequencer.pattern.length.get();
    out.contentLength = seq::preparedSequencerContentLength(
        sequencer, out.contentPath);
    if (out.contentLength == 0U) return false;
    out.resultingContentLength = out.contentLength;
    out.initialPage = sequencer.page.get();
    out.initialFocus = sequencer.focusedStep.get();
    if (out.initialFocus >= out.contentLength ||
        out.initialPage != seq::activeContentPageForStep(out.initialFocus)) {
        return false;
    }
    out.finalFocus = out.initialFocus;
    out.stepDataRevision = sequencer.pattern.stepDataRevision.get();
    out.graphRevision = sequencer.pattern.graphRevision.get();
    out.ccLaneRevision = sequencer.pattern.ccLaneRevision.get();
    out.timingRevision = sequencer.pattern.patternTimingRevision.get();
    if (sequencer.pattern.graph) out.flags |= kFlagLiveGraphOwnerPresent;
    if (context == ContentContext::Root) out.flags |= kFlagRootContext;
    if (seq::graphView(sequencer.pattern) != nullptr) {
        out.flags |= kFlagLiveGraphActive;
    }
    return true;
}

FLASHMEM bool assignTarget(
    Plan& plan,
    uint8_t target,
    uint8_t source
) noexcept {
    if (target >= plan.targetToSource.size()) return false;
    if (plan.targetToSource[target] == Plan::TARGET_UNTOUCHED) {
        if (plan.targetCount == std::numeric_limits<uint8_t>::max()) {
            return false;
        }
        ++plan.targetCount;
    }
    plan.targetToSource[target] = source;
    return true;
}

FLASHMEM bool assignDefaultRange(
    Plan& plan,
    uint8_t start,
    uint8_t endExclusive
) noexcept {
    if (start >= endExclusive || endExclusive > plan.targetToSource.size()) {
        return false;
    }
    for (uint16_t step = start; step < endExclusive; ++step) {
        // EXTEND plans may already contain clipboard destinations inside the
        // newly exposed span. Fill only the gaps; never erase an admitted
        // source mapping with the canonical default initializer.
        if (plan.targetToSource[step] != Plan::TARGET_UNTOUCHED) continue;
        if (!assignTarget(
                plan,
                static_cast<uint8_t>(step),
                Plan::TARGET_DEFAULT)) {
            return false;
        }
    }
    return true;
}

FLASHMEM uint8_t normalizedIndex(
    uint8_t logical,
    int8_t offset,
    uint8_t length
) noexcept {
    if (length == 0U) return 0U;
    int value = static_cast<int>(logical) - static_cast<int>(offset);
    const int modulus = static_cast<int>(length);
    value %= modulus;
    if (value < 0) value += modulus;
    return static_cast<uint8_t>(value);
}

FLASHMEM seq::SequencerGraphNodeId projectedTargetNodeIdAtLength(
    const Plan& plan,
    const seq::SequencerState& sequencer,
    uint8_t target,
    uint8_t contentLength
) noexcept {
    if (flag(plan, kFlagRootContext)) {
        return target < seq::SequencerState::MAX_STEPS
            ? seq::rootStepNodeId(target)
            : GraphLimits::INVALID_ID;
    }
    if (!plan.contentPath.valid || plan.contentPath.stackDepth == 0U ||
        plan.contentPath.stackDepth > plan.contentPath.frames.size() ||
        target >= contentLength) {
        return GraphLimits::INVALID_ID;
    }

    const auto* graph = seq::graphView(sequencer.pattern);
    if (graph == nullptr) return GraphLimits::INVALID_ID;
    const auto& frame =
        plan.contentPath.frames[plan.contentPath.stackDepth - 1U];
    if (frame.kind == seq::SequencerContentViewKind::MICRO_SEQUENCE) {
        const auto* sequence = graph->sequence(frame.sequenceId);
        if (sequence == nullptr ||
            contentLength >
                seq::sequencerMicroSequenceReservedCapacity(
                    *graph, frame.sequenceId)) {
            return GraphLimits::INVALID_ID;
        }
        return static_cast<uint16_t>(
            sequence->firstStepNode +
            normalizedIndex(
                target, sequence->offset, contentLength));
    }
    if (frame.kind == seq::SequencerContentViewKind::CYCLE_STATES) {
        const auto* cycleSet = graph->cycleSet(frame.cycleSetId);
        if (cycleSet == nullptr ||
            contentLength >
                seq::sequencerCycleStateSetReservedCapacity(
                    *graph, frame.cycleSetId)) {
            return GraphLimits::INVALID_ID;
        }
        return static_cast<uint16_t>(
            cycleSet->firstStateNode +
            normalizedIndex(
                target, cycleSet->offset, contentLength));
    }
    return GraphLimits::INVALID_ID;
}

FLASHMEM seq::SequencerGraphNodeId projectedTargetNodeId(
    const Plan& plan,
    const seq::SequencerState& sequencer,
    uint8_t target
) noexcept {
    return projectedTargetNodeIdAtLength(
        plan, sequencer, target, plan.resultingContentLength);
}

FLASHMEM bool newlyExposedChildTarget(
    const Plan& plan,
    uint8_t target
) noexcept {
    return flag(plan, kFlagChildResize) &&
           target >= plan.contentLength &&
           target < plan.resultingContentLength;
}

FLASHMEM bool newlyExposedRootTarget(
    const Plan& plan,
    uint8_t target
) noexcept {
    return flag(plan, kFlagRootContext) &&
           plan.resultingContentLength > plan.contentLength &&
           target >= plan.contentLength &&
           target < plan.resultingContentLength;
}

FLASHMEM bool canonicalTargetAfterExtension(
    const Plan& plan,
    uint8_t target,
    GraphNode& out,
    bool& payloadPresent
) noexcept {
    out = {};
    if (newlyExposedRootTarget(plan, target)) {
        payloadPresent = false;
        return true;
    }
    if (newlyExposedChildTarget(plan, target)) {
        out.flags = oc::note::sequencer::STEP_NODE_ENABLED_OVERRIDE;
        payloadPresent = true;
        return true;
    }
    payloadPresent = false;
    return false;
}

FLASHMEM seq::SequencerGraphNodeId analysisTargetNodeId(
    const Plan& plan,
    const seq::SequencerState& sequencer,
    uint8_t target
) noexcept {
    const uint8_t length = flag(plan, kFlagChildResize)
        ? plan.contentLength
        : plan.resultingContentLength;
    return projectedTargetNodeIdAtLength(
        plan, sequencer, target, length);
}

FLASHMEM bool sameGraphNodeStored(
    const GraphNode& lhs,
    const GraphNode& rhs
) noexcept {
    return seq::sameSequencerGraphNodePayload(lhs, rhs) &&
           lhs.childSequenceId == rhs.childSequenceId &&
           lhs.cycleSetId == rhs.cycleSetId;
}

FLASHMEM bool sameSourceAsCanonicalTarget(
    const Plan& plan,
    const SourcePayload& source,
    const seq::SequencerGraphPayloadInspection& sourceInspection,
    const GraphNode& canonicalTarget,
    bool canonicalPayloadPresent
) noexcept {
    if (!sourceInspection.payloadPresent) return !canonicalPayloadPresent;
    if (!canonicalPayloadPresent || plan.sourceGraphIdentity == nullptr ||
        source.nodeId == GraphLimits::INVALID_ID ||
        sourceInspection.budget.stepNodes != 0U ||
        sourceInspection.budget.sequences != 0U ||
        sourceInspection.budget.cycleSets != 0U) {
        return false;
    }
    const auto* sourceNode =
        plan.sourceGraphIdentity->stepNode(source.nodeId);
    return sourceNode != nullptr &&
           sameGraphNodeStored(*sourceNode, canonicalTarget);
}

FLASHMEM GraphNode resetNodeFor(
    const GraphNode& current,
    bool preserveChildren,
    seq::SequencerGraphNodeResetMode mode
) noexcept {
    GraphNode reset{};
    if (mode == seq::SequencerGraphNodeResetMode::DISABLED_OVERRIDE) {
        reset.flags = oc::note::sequencer::STEP_NODE_ENABLED_OVERRIDE;
    }
    if (preserveChildren) {
        reset.flags = static_cast<uint16_t>(
            reset.flags | (current.flags & kNodeChildFlags));
        reset.childSequenceId = current.childSequenceId;
        reset.cycleSetId = current.cycleSetId;
    }
    return reset;
}

FLASHMEM bool resetPreservesChildren(const Plan& plan) noexcept {
    switch (plan.action) {
        case SequencerPreparedPageStructureAction::FocusedStepReset:
        case SequencerPreparedPageStructureAction::StepSelectionReset:
        case SequencerPreparedPageStructureAction::PageSelectionReset:
            return plan.resetDepth == StepResetDepth::Shallow;
        default:
            return false;
    }
}

constexpr bool validResetDepth(StepResetDepth depth) noexcept {
    switch (depth) {
        case StepResetDepth::Shallow:
        case StepResetDepth::Deep:
            return true;
    }
    return false;
}

FLASHMEM SourcePayload sourcePayloadFor(
    const Plan& plan,
    uint8_t encoded
) noexcept {
    SourcePayload result;
    if (encoded >= Plan::TARGET_DEFAULT || plan.clipboard == nullptr) {
        result.valid = encoded == Plan::TARGET_DEFAULT;
        return result;
    }

    switch (plan.action) {
        case SequencerPreparedPageStructureAction::PagePaste: {
            const auto& page = plan.clipboard->sequencerPage;
            if (encoded >= page.count) return result;
            result.page = &page;
            result.localIndex = encoded;
            result.nodeId = seq::rootStepNodeId(static_cast<uint8_t>(
                page.sourcePage * seq::SequencerState::STEPS_PER_PAGE +
                encoded));
            result.valid = true;
            return result;
        }
        case SequencerPreparedPageStructureAction::PageSelectionPaste: {
            const uint8_t pageIndex = static_cast<uint8_t>(
                encoded / seq::SequencerState::STEPS_PER_PAGE);
            const uint8_t localIndex = static_cast<uint8_t>(
                encoded % seq::SequencerState::STEPS_PER_PAGE);
            const auto& selection = plan.clipboard->sequencerPageSelection;
            if (pageIndex >= selection.count) return result;
            const auto& page = selection.pages[pageIndex];
            if (localIndex >= page.count) return result;
            result.page = &page;
            result.localIndex = localIndex;
            result.nodeId = seq::rootStepNodeId(static_cast<uint8_t>(
                page.sourcePage * seq::SequencerState::STEPS_PER_PAGE +
                localIndex));
            result.valid = true;
            return result;
        }
        case SequencerPreparedPageStructureAction::StepPaste: {
            const auto& steps = plan.clipboard->sequencerSteps;
            if (encoded >= steps.count) return result;
            result.step = &steps.entries[encoded];
            result.nodeId = result.step->sourceNodeId;
            result.valid = result.step->valid;
            return result;
        }
        default:
            return result;
    }
}

FLASHMEM bool sourceClipboardIdentityValid(const Plan& plan) noexcept {
    if (plan.clipboard == nullptr) return true;
    const auto& clipboard = *plan.clipboard;
    if (clipboard.revision.get() != plan.clipboardRevision ||
        clipboard.sequencerGraph.get() != plan.sourceGraphIdentity) {
        return false;
    }
    if (plan.sourceGraphIdentity != nullptr &&
        !seq::validInitializedSequencerGraph(*plan.sourceGraphIdentity)) {
        return false;
    }

    switch (plan.action) {
        case SequencerPreparedPageStructureAction::PagePaste:
            return clipboard.kind.get() ==
                       core::state::StructureClipboardKind::SEQUENCER_PAGE &&
                   validPageClipboard(clipboard.sequencerPage);
        case SequencerPreparedPageStructureAction::PageSelectionPaste: {
            const auto& selection = clipboard.sequencerPageSelection;
            if (clipboard.kind.get() != core::state::StructureClipboardKind::
                    SEQUENCER_PAGE_SELECTION ||
                !selection.valid || selection.count == 0U ||
                selection.count > selection.pages.size() ||
                selection.sourceFirstPage >= seq::SequencerState::PAGE_COUNT) {
                return false;
            }
            uint8_t previous = seq::SequencerState::PAGE_COUNT;
            for (uint8_t index = 0U; index < selection.count; ++index) {
                const auto& page = selection.pages[index];
                if (!validPageClipboard(page) ||
                    page.sourcePage < selection.sourceFirstPage ||
                    (index > 0U && page.sourcePage <= previous)) {
                    return false;
                }
                previous = page.sourcePage;
            }
            return true;
        }
        case SequencerPreparedPageStructureAction::StepPaste:
            return clipboard.kind.get() ==
                       core::state::StructureClipboardKind::SEQUENCER_STEPS &&
                   validStepsClipboard(clipboard.sequencerSteps);
        default:
            return false;
    }
}

FLASHMEM bool sourceGraphPayload(
    const Plan& plan,
    const SourcePayload& source,
    seq::SequencerGraphPayloadInspection& out
) noexcept {
    out = {};
    if (!source.valid) return false;
    if (source.page == nullptr && source.step == nullptr) {
        out.status = seq::SequencerGraphPayloadInspectionStatus::Ok;
        out.payloadPresent = false;
        return true;
    }
    if (plan.sourceGraphIdentity == nullptr) {
        if (!flag(plan, kFlagRootContext) && source.step != nullptr) {
            return false;
        }
        out.status = seq::SequencerGraphPayloadInspectionStatus::Ok;
        out.payloadPresent = false;
        return true;
    }
    if (source.nodeId == GraphLimits::INVALID_ID) return false;
    if (!plan.sourceGraphIdentity->enabled) return false;
    out = seq::inspectSequencerGraphPayload(
        *plan.sourceGraphIdentity, source.nodeId, plan.depth());
    return out.ok();
}

FLASHMEM bool sameDesiredGraphPayload(
    const Plan& plan,
    const Graph* targetGraph,
    seq::SequencerGraphNodeId targetNodeId,
    const SourcePayload& source,
    const seq::SequencerGraphPayloadInspection& sourceInspection,
    const seq::SequencerGraphPayloadInspection& targetInspection
) noexcept {
    if (!sourceInspection.payloadPresent) return !targetInspection.payloadPresent;
    if (!targetInspection.payloadPresent || targetGraph == nullptr ||
        plan.sourceGraphIdentity == nullptr ||
        source.nodeId == GraphLimits::INVALID_ID) {
        return false;
    }
    const auto comparison = seq::compareSequencerGraphPayloads(
        *targetGraph,
        targetNodeId,
        *plan.sourceGraphIdentity,
        source.nodeId,
        plan.depth());
    return comparison.ok() && comparison.same;
}

FLASHMEM bool accumulateBudget(
    seq::SequencerGraphCopyBudget& aggregate,
    const seq::SequencerGraphCopyBudget& addition
) noexcept {
    return seq::appendSequencerGraphCopyBudget(aggregate, addition);
}

FLASHMEM bool analyzePhysicalRootExtension(
    const Plan& plan,
    const seq::SequencerState& sequencer,
    GraphAnalysis& analysis
) noexcept {
    if (!flag(plan, kFlagRootContext) ||
        plan.resultingContentLength <= plan.contentLength) {
        return true;
    }

    const auto* graph = seq::graphView(sequencer.pattern);
    if (graph == nullptr) return true;
    const GraphNode canonical{};
    for (uint16_t step = plan.contentLength;
         step < plan.resultingContentLength;
         ++step) {
        const auto inspection = seq::inspectSequencerGraphPayload(
            *graph, static_cast<uint16_t>(step), 0U);
        if (!inspection.ok()) return false;
        if (sameGraphNodeStored(graph->stepNodes[step], canonical)) continue;

        // Root resize canonicalizes the complete newly exposed physical span
        // before mapped paste. Descendants become detached at that point and
        // must be reclaimed before capacity is checked for source copies.
        analysis.changed = true;
        if (inspection.budget.stepNodes != 0U ||
            inspection.budget.sequences != 0U ||
            inspection.budget.cycleSets != 0U) {
            analysis.requiresCompaction = true;
        }
    }
    return true;
}

FLASHMEM bool analyzePhysicalChildExtension(
    const Plan& plan,
    const seq::SequencerState& sequencer,
    GraphAnalysis& analysis
) noexcept {
    if (!flag(plan, kFlagChildResize) ||
        plan.resultingContentLength <= plan.contentLength ||
        plan.contentPath.stackDepth == 0U) {
        return true;
    }

    const auto* graph = seq::graphView(sequencer.pattern);
    if (graph == nullptr) return false;
    const auto& frame =
        plan.contentPath.frames[plan.contentPath.stackDepth - 1U];
    uint16_t firstNode = GraphLimits::INVALID_ID;
    uint8_t capacity = 0U;
    if (frame.kind == seq::SequencerContentViewKind::MICRO_SEQUENCE) {
        const auto* sequence = graph->sequence(frame.sequenceId);
        if (sequence == nullptr) return false;
        firstNode = sequence->firstStepNode;
        capacity = seq::sequencerMicroSequenceReservedCapacity(
            *graph, frame.sequenceId);
    } else if (frame.kind == seq::SequencerContentViewKind::CYCLE_STATES) {
        const auto* cycleSet = graph->cycleSet(frame.cycleSetId);
        if (cycleSet == nullptr) return false;
        firstNode = cycleSet->firstStateNode;
        capacity = seq::sequencerCycleStateSetReservedCapacity(
            *graph, frame.cycleSetId);
    } else {
        return false;
    }
    if (plan.resultingContentLength > capacity) return false;

    for (uint16_t index = plan.contentLength;
         index < plan.resultingContentLength;
         ++index) {
        const auto nodeId = static_cast<uint16_t>(firstNode + index);
        const auto inspection = seq::inspectSequencerGraphPayload(
            *graph, nodeId, plan.depth());
        if (!inspection.ok()) return false;
        if (!inspection.payloadPresent) continue;
        analysis.changed = true;
        if (inspection.budget.stepNodes != 0U ||
            inspection.budget.sequences != 0U ||
            inspection.budget.cycleSets != 0U) {
            analysis.requiresCompaction = true;
        }
    }
    analysis.changed = true;  // The container length itself changes.
    return true;
}

FLASHMEM GraphAnalysis analyzeMappedGraphTargets(
    const Plan& plan,
    const seq::SequencerState& sequencer
) noexcept {
    GraphAnalysis analysis;
    const auto* targetGraph = seq::graphView(sequencer.pattern);
    if (!analyzePhysicalRootExtension(plan, sequencer, analysis) ||
        !analyzePhysicalChildExtension(plan, sequencer, analysis)) {
        analysis.valid = false;
        return analysis;
    }

    for (uint16_t target = 0U; target < plan.targetToSource.size(); ++target) {
        const uint8_t encoded = plan.targetToSource[target];
        if (encoded == Plan::TARGET_UNTOUCHED) continue;

        GraphNode canonicalTarget{};
        bool canonicalPayloadPresent = false;
        if (canonicalTargetAfterExtension(
                plan,
                static_cast<uint8_t>(target),
                canonicalTarget,
                canonicalPayloadPresent)) {
            // Root resize and the preserving child extension both publish a
            // deterministic canonical target independently of cold bytes.
            // Compare against that post-resize state; the physical scans above
            // separately account for overwritten descendants and compaction.
            if (encoded == Plan::TARGET_DEFAULT) continue;
            const auto source = sourcePayloadFor(plan, encoded);
            seq::SequencerGraphPayloadInspection sourceInspection{};
            if (!sourceGraphPayload(plan, source, sourceInspection)) {
                analysis.valid = false;
                return analysis;
            }
            if (sameSourceAsCanonicalTarget(
                    plan,
                    source,
                    sourceInspection,
                    canonicalTarget,
                    canonicalPayloadPresent)) {
                continue;
            }
            analysis.changed = true;
            if (sourceInspection.payloadPresent) {
                analysis.sourcePayloadPresent = true;
                if (!accumulateBudget(
                        analysis.budget, sourceInspection.budget)) {
                    analysis.valid = false;
                    return analysis;
                }
                if (sourceInspection.budget.stepNodes != 0U ||
                    sourceInspection.budget.sequences != 0U ||
                    sourceInspection.budget.cycleSets != 0U) {
                    analysis.requiresCompaction = true;
                }
            }
            continue;
        }

        const auto targetNodeId = analysisTargetNodeId(
            plan, sequencer, static_cast<uint8_t>(target));
        seq::SequencerGraphPayloadInspection targetInspection{};
        if (targetGraph == nullptr) {
            targetInspection.status =
                seq::SequencerGraphPayloadInspectionStatus::Ok;
        } else {
            if (targetNodeId == GraphLimits::INVALID_ID) {
                analysis.valid = false;
                return analysis;
            }
            targetInspection = seq::inspectSequencerGraphPayload(
                *targetGraph, targetNodeId, plan.depth());
            if (!targetInspection.ok()) {
                analysis.valid = false;
                return analysis;
            }
        }

        if (encoded == Plan::TARGET_DEFAULT) {
            if (targetGraph == nullptr) continue;
            const auto* node = targetGraph->stepNode(targetNodeId);
            if (node == nullptr) {
                analysis.valid = false;
                return analysis;
            }
            const auto mode = flag(plan, kFlagRootContext)
                ? seq::SequencerGraphNodeResetMode::DEFAULT
                : seq::SequencerGraphNodeResetMode::DISABLED_OVERRIDE;
            const auto desired = resetNodeFor(
                *node, resetPreservesChildren(plan), mode);
            if (sameGraphNodeStored(*node, desired)) continue;
            analysis.changed = true;
            if (!resetPreservesChildren(plan) &&
                (targetInspection.budget.stepNodes != 0U ||
                 targetInspection.budget.sequences != 0U ||
                 targetInspection.budget.cycleSets != 0U)) {
                analysis.requiresCompaction = true;
            }
            continue;
        }

        const auto source = sourcePayloadFor(plan, encoded);
        seq::SequencerGraphPayloadInspection sourceInspection{};
        if (!sourceGraphPayload(plan, source, sourceInspection)) {
            analysis.valid = false;
            return analysis;
        }
        if (sameDesiredGraphPayload(
                plan,
                targetGraph,
                targetNodeId,
                source,
                sourceInspection,
                targetInspection)) {
            continue;
        }

        analysis.changed = true;
        if (targetInspection.budget.stepNodes != 0U ||
            targetInspection.budget.sequences != 0U ||
            targetInspection.budget.cycleSets != 0U) {
            analysis.requiresCompaction = true;
        }
        if (sourceInspection.payloadPresent) {
            analysis.sourcePayloadPresent = true;
            if (!accumulateBudget(analysis.budget, sourceInspection.budget)) {
                analysis.valid = false;
                return analysis;
            }
            if (sourceInspection.budget.stepNodes != 0U ||
                sourceInspection.budget.sequences != 0U ||
                sourceInspection.budget.cycleSets != 0U) {
                analysis.requiresCompaction = true;
            }
        }
    }
    return analysis;
}

FLASHMEM bool sameFlatTarget(
    const Plan& plan,
    const seq::SequencerState& sequencer,
    uint8_t target,
    uint8_t encoded
) noexcept {
    if (!flag(plan, kFlagRootContext)) return true;
    bool enabled = false;
    uint8_t note = seq::SequencerState::DEFAULT_NOTE;
    uint8_t velocity = seq::SequencerState::DEFAULT_VELOCITY;
    uint16_t gate = seq::SequencerState::DEFAULT_GATE_PERCENT;
    int8_t nudge = 0;
    uint8_t probability = seq::SequencerState::DEFAULT_PROBABILITY;

    if (encoded != Plan::TARGET_DEFAULT) {
        const auto source = sourcePayloadFor(plan, encoded);
        if (!source.valid) return false;
        if (source.page != nullptr) {
            enabled = source.page->isEnabled(source.localIndex);
            note = source.page->note[source.localIndex];
            velocity = source.page->velocity[source.localIndex];
            gate = source.page->gate[source.localIndex];
            nudge = source.page->nudge[source.localIndex];
            probability = source.page->probability[source.localIndex];
        } else if (source.step != nullptr) {
            enabled = source.step->enabled;
            note = source.step->note;
            velocity = source.step->velocity;
            gate = source.step->gate;
            nudge = source.step->nudge;
            probability = source.step->probability;
        } else {
            return false;
        }
    }

    return sequencer.pattern.isEnabled(target) == enabled &&
           sequencer.pattern.note[target] == note &&
           sequencer.pattern.velocity[target] == velocity &&
           sequencer.pattern.gate[target] == gate &&
           sequencer.pattern.nudge[target] == nudge &&
           sequencer.pattern.probability[target] == probability;
}

FLASHMEM bool mappedFlatDelta(
    const Plan& plan,
    const seq::SequencerState& sequencer,
    bool& valid
) noexcept {
    valid = true;
    if (!flag(plan, kFlagRootContext)) return false;
    bool changed = plan.resultingContentLength != plan.contentLength;
    for (uint16_t target = 0U; target < plan.targetToSource.size(); ++target) {
        const uint8_t encoded = plan.targetToSource[target];
        if (encoded == Plan::TARGET_UNTOUCHED) continue;
        if (target >= plan.resultingContentLength ||
            (encoded != Plan::TARGET_DEFAULT &&
             !sourcePayloadFor(plan, encoded).valid)) {
            valid = false;
            return false;
        }
        changed = !sameFlatTarget(
            plan, sequencer, static_cast<uint8_t>(target), encoded) || changed;
    }
    return changed;
}

FLASHMEM bool packBudget(
    const seq::SequencerGraphCopyBudget& source,
    SequencerPreparedPageStructureGraphBudget& target
) noexcept {
    if (source.sequences > std::numeric_limits<uint16_t>::max() ||
        source.cycleSets > std::numeric_limits<uint16_t>::max()) {
        return false;
    }
    target.stepNodes = source.stepNodes;
    target.sequences = static_cast<uint16_t>(source.sequences);
    target.cycleSets = static_cast<uint16_t>(source.cycleSets);
    return true;
}

FLASHMEM bool sameBudget(
    const SequencerPreparedPageStructureGraphBudget& lhs,
    const seq::SequencerGraphCopyBudget& rhs
) noexcept {
    return lhs.stepNodes == rhs.stepNodes &&
           lhs.sequences == rhs.sequences &&
           lhs.cycleSets == rhs.cycleSets;
}

FLASHMEM Preflight finishMappedPlan(
    const seq::SequencerState& sequencer,
    Plan& plan,
    bool forceChanged = false
) noexcept {
    if (!sourceClipboardIdentityValid(plan)) return plan.outcome;
    if (plan.sourceGraphIdentity != nullptr &&
        plan.sourceGraphIdentity == sequencer.pattern.graph.get()) {
        return plan.outcome;
    }
    bool flatValid = true;
    const bool flatChanged = mappedFlatDelta(plan, sequencer, flatValid);
    const auto graph = analyzeMappedGraphTargets(plan, sequencer);
    if (!flatValid || !graph.valid ||
        !packBudget(graph.budget, plan.graphBudget)) {
        return plan.outcome;
    }

    if (flatChanged) plan.flags |= kFlagFlatDelta;
    if (graph.changed) plan.flags |= kFlagGraphDelta;
    if (graph.requiresCompaction) plan.flags |= kFlagRequiresCompaction;
    const bool needsGraphOwner =
        graph.sourcePayloadPresent &&
        seq::graphView(sequencer.pattern) == nullptr;
    if (needsGraphOwner) plan.flags |= kFlagNeedsGraphOwner;

    if (needsGraphOwner) {
        plan.payloadPlan =
            seq::SequencerCoalescedPatternPayloadPlan::FullWithProspectiveGraph;
    } else if (graph.changed) {
        plan.payloadPlan =
            seq::SequencerCoalescedPatternPayloadPlan::FullCurrentPayload;
    } else {
        plan.payloadPlan =
            seq::SequencerCoalescedPatternPayloadPlan::FlatOnly;
    }

    if (!flatChanged && !graph.changed && !forceChanged) {
        plan.outcome = Preflight::NoChange;
        return plan.outcome;
    }
    plan.outcome = Preflight::Ready;
    return plan.outcome;
}

FLASHMEM bool rootContextOnly(const Plan& plan) noexcept {
    return contentContextFor(plan.contentPath) == ContentContext::Root;
}

FLASHMEM bool setupClipboardPlan(
    Plan& plan,
    const core::state::StructureClipboardState& clipboard
) noexcept {
    plan.clipboard = &clipboard;
    plan.clipboardRevision = clipboard.revision.get();
    plan.sourceGraphIdentity = clipboard.sequencerGraph.get();
    return sourceClipboardIdentityValid(plan);
}

FLASHMEM bool buildPagePasteTargets(
    Plan& plan,
    const core::state::SequencerPageClipboard& page,
    uint8_t targetPage,
    uint8_t encodedPageIndex
) noexcept {
    const uint8_t targetStart = static_cast<uint8_t>(
        targetPage * seq::SequencerState::STEPS_PER_PAGE);
    const uint8_t pageEnd = static_cast<uint8_t>(std::min<uint16_t>(
        plan.resultingContentLength,
        static_cast<uint16_t>(targetStart) +
            seq::SequencerState::STEPS_PER_PAGE));
    if (!assignDefaultRange(plan, targetStart, pageEnd)) return false;
    for (uint8_t local = 0U; local < page.count; ++local) {
        const uint16_t target = static_cast<uint16_t>(targetStart) + local;
        if (target >= plan.resultingContentLength) return false;
        const uint16_t encoded =
            static_cast<uint16_t>(encodedPageIndex) *
                seq::SequencerState::STEPS_PER_PAGE +
            local;
        if (encoded >= Plan::TARGET_DEFAULT ||
            !assignTarget(
                plan,
                static_cast<uint8_t>(target),
                static_cast<uint8_t>(encoded))) {
            return false;
        }
    }
    return true;
}

FLASHMEM bool pageMaskContains(uint16_t mask, uint8_t page) noexcept {
    return (mask & static_cast<uint16_t>(uint16_t{1} << page)) != 0U;
}

FLASHMEM uint8_t countPages(uint16_t mask, uint8_t pageCount) noexcept {
    uint8_t count = 0U;
    for (uint8_t page = 0U; page < pageCount; ++page) {
        if (pageMaskContains(mask, page)) ++count;
    }
    return count;
}

FLASHMEM GraphAnalysis analyzeDeleteGraph(
    const Plan& plan,
    const seq::SequencerState& sequencer
) noexcept {
    GraphAnalysis analysis;
    const auto* graph = seq::graphView(sequencer.pattern);
    if (graph == nullptr) return analysis;

    const uint8_t oldLength = plan.patternLength;
    uint8_t destination = 0U;
    for (uint16_t source = 0U; source < oldLength; ++source) {
        const auto sourceStep = static_cast<uint8_t>(source);
        const uint8_t page = static_cast<uint8_t>(
            sourceStep / seq::SequencerState::STEPS_PER_PAGE);
        if (pageMaskContains(plan.pageMask, page)) {
            const auto inspection = seq::inspectSequencerGraphPayload(
                *graph, sourceStep, 0U);
            if (!inspection.ok()) {
                analysis.valid = false;
                return analysis;
            }
            if (inspection.budget.stepNodes != 0U ||
                inspection.budget.sequences != 0U ||
                inspection.budget.cycleSets != 0U) {
                analysis.requiresCompaction = true;
            }
            continue;
        }
        if (!sameGraphNodeStored(
                graph->stepNodes[destination],
                graph->stepNodes[sourceStep])) {
            analysis.changed = true;
        }
        ++destination;
    }
    const GraphNode reset{};
    for (uint16_t step = destination;
         step < seq::SequencerState::MAX_STEPS;
         ++step) {
        const auto inspection = seq::inspectSequencerGraphPayload(
            *graph, static_cast<uint16_t>(step), 0U);
        if (!inspection.ok()) {
            analysis.valid = false;
            return analysis;
        }
        if (inspection.budget.stepNodes != 0U ||
            inspection.budget.sequences != 0U ||
            inspection.budget.cycleSets != 0U) {
            analysis.requiresCompaction = true;
        }
        if (!sameGraphNodeStored(graph->stepNodes[step], reset)) {
            analysis.changed = true;
        }
    }
    return analysis;
}

FLASHMEM Preflight finishDeletePlan(
    const seq::SequencerState& sequencer,
    Plan& plan
) noexcept {
    const auto graph = analyzeDeleteGraph(plan, sequencer);
    if (!graph.valid) return plan.outcome;
    if (graph.changed) plan.flags |= kFlagGraphDelta;
    if (graph.requiresCompaction) plan.flags |= kFlagRequiresCompaction;
    const bool ccPayload = sequencer.pattern.ccLanes != nullptr &&
        seq::sequencerCcLaneCount(*sequencer.pattern.ccLanes) != 0U;
    plan.payloadPlan = graph.changed || ccPayload
        ? seq::SequencerCoalescedPatternPayloadPlan::FullCurrentPayload
        : seq::SequencerCoalescedPatternPayloadPlan::FlatOnly;
    plan.flags |= kFlagFlatDelta;
    plan.outcome = Preflight::Ready;
    return plan.outcome;
}

FLASHMEM bool validatePlanShape(
    const Plan& plan,
    const seq::SequencerState& sequencer
) noexcept {
    if (!plan.ready() ||
        plan.expectedTrack >= seq::SequencerTrackBankState::TRACK_COUNT ||
        !validPattern(sequencer) ||
        sequencer.pattern.length.get() != plan.patternLength ||
        sequencer.pattern.stepDataRevision.get() != plan.stepDataRevision ||
        sequencer.pattern.graphRevision.get() != plan.graphRevision ||
        sequencer.pattern.ccLaneRevision.get() != plan.ccLaneRevision ||
        sequencer.pattern.patternTimingRevision.get() != plan.timingRevision ||
        sequencer.page.get() != plan.initialPage ||
        sequencer.focusedStep.get() != plan.initialFocus) {
        return false;
    }

    const bool prospectiveInstalled =
        plan.payloadPlan == seq::SequencerCoalescedPatternPayloadPlan::
            FullWithProspectiveGraph &&
        !flag(plan, kFlagLiveGraphOwnerPresent);
    if (prospectiveInstalled) {
        if (!sequencer.pattern.graph ||
            !seq::isCanonicalDisabledSequencerGraph(
                *sequencer.pattern.graph)) {
            return false;
        }
    } else if ((sequencer.pattern.graph != nullptr) !=
               flag(plan, kFlagLiveGraphOwnerPresent)) {
        return false;
    }

    const bool liveGraphActive = seq::graphView(sequencer.pattern) != nullptr;
    if (!prospectiveInstalled &&
        liveGraphActive != flag(plan, kFlagLiveGraphActive)) {
        return false;
    }

    const auto livePath =
        seq::capturePreparedSequencerGraphContentPath(sequencer);
    return samePath(livePath, plan.contentPath) &&
           seq::preparedSequencerContentLength(sequencer, livePath) ==
               plan.contentLength &&
           sourceClipboardIdentityValid(plan);
}

FLASHMEM bool reanalyzePlan(
    const Plan& plan,
    const seq::SequencerState& sequencer
) noexcept {
    if (plan.action == SequencerPreparedPageStructureAction::PageDelete ||
        (plan.action == SequencerPreparedPageStructureAction::
             PageSelectionDeleteOrDeepReset &&
         flag(plan, kFlagRootContext))) {
        const auto graph = analyzeDeleteGraph(plan, sequencer);
        return graph.valid &&
               graph.changed == flag(plan, kFlagGraphDelta) &&
               graph.requiresCompaction ==
                   flag(plan, kFlagRequiresCompaction);
    }

    bool flatValid = true;
    const bool flatChanged = mappedFlatDelta(plan, sequencer, flatValid);
    const auto graph = analyzeMappedGraphTargets(plan, sequencer);
    return flatValid && graph.valid &&
           flatChanged == flag(plan, kFlagFlatDelta) &&
           graph.changed == flag(plan, kFlagGraphDelta) &&
           graph.requiresCompaction ==
               flag(plan, kFlagRequiresCompaction) &&
           sameBudget(plan.graphBudget, graph.budget) &&
           (graph.sourcePayloadPresent &&
                seq::graphView(sequencer.pattern) == nullptr) ==
               flag(plan, kFlagNeedsGraphOwner);
}

FLASHMEM bool revalidateMutationPlan(
    const void* context,
    const seq::SequencerState& sequencer
) noexcept {
    if (context == nullptr) return false;
    const auto& plan = *static_cast<const Plan*>(context);
    return validatePlanShape(plan, sequencer) &&
           reanalyzePlan(plan, sequencer);
}

FLASHMEM bool initializeProspectiveGraph(
    const Plan& plan,
    seq::SequencerState& sequencer,
    MutationAccumulator& mutation
) noexcept {
    if (!flag(plan, kFlagNeedsGraphOwner)) return true;
    if (!sequencer.pattern.graph ||
        !seq::isCanonicalDisabledSequencerGraph(*sequencer.pattern.graph) ||
        !seq::initializeSequencerGraphRootUnversioned(
            *sequencer.pattern.graph)) {
        return false;
    }
    mutation.graphChanged = true;
    return true;
}

FLASHMEM bool extendChildPreservingLogicalContent(
    Plan& plan,
    seq::SequencerState& sequencer,
    MutationAccumulator& mutation
) noexcept {
    if (!flag(plan, kFlagChildResize)) return true;
    if (plan.contentPath.stackDepth == 0U ||
        plan.contentPath.stackDepth > plan.contentPath.frames.size()) {
        return false;
    }
    auto* graph = sequencer.pattern.graph.get();
    if (graph == nullptr || !graph->enabled) return false;
    auto& frame = plan.contentPath.frames[plan.contentPath.stackDepth - 1U];
    bool resized = false;
    if (frame.kind == seq::SequencerContentViewKind::MICRO_SEQUENCE) {
        resized = seq::extendMicroSequencePreservingLogicalContentUnversioned(
            *graph, frame.sequenceId, plan.resultingContentLength);
    } else if (frame.kind == seq::SequencerContentViewKind::CYCLE_STATES) {
        resized = seq::extendCycleStateSetPreservingLogicalContentUnversioned(
            *graph, frame.cycleSetId, plan.resultingContentLength);
    }
    if (!resized) return false;
    frame.length = plan.resultingContentLength;
    mutation.graphChanged = true;
    return true;
}

FLASHMEM bool releaseMappedGraphTargets(
    const Plan& plan,
    seq::SequencerState& sequencer,
    MutationAccumulator& mutation
) noexcept {
    auto* graph = sequencer.pattern.graph.get();
    if (graph == nullptr || !graph->enabled) {
        return !flag(plan, kFlagGraphDelta);
    }

    for (uint16_t target = 0U; target < plan.targetToSource.size(); ++target) {
        const uint8_t encoded = plan.targetToSource[target];
        if (encoded == Plan::TARGET_UNTOUCHED) continue;
        const auto nodeId = projectedTargetNodeId(
            plan, sequencer, static_cast<uint8_t>(target));
        const auto* current = graph->stepNode(nodeId);
        if (current == nullptr) return false;

        bool preserveChildren = false;
        auto mode = flag(plan, kFlagRootContext)
            ? seq::SequencerGraphNodeResetMode::DEFAULT
            : seq::SequencerGraphNodeResetMode::DISABLED_OVERRIDE;
        if (encoded == Plan::TARGET_DEFAULT) {
            preserveChildren = resetPreservesChildren(plan);
            const auto desired = resetNodeFor(
                *current, preserveChildren, mode);
            if (sameGraphNodeStored(*current, desired)) continue;
        } else {
            const auto source = sourcePayloadFor(plan, encoded);
            seq::SequencerGraphPayloadInspection sourceInspection{};
            seq::SequencerGraphPayloadInspection targetInspection =
                seq::inspectSequencerGraphPayload(
                    *graph, nodeId, plan.depth());
            if (!targetInspection.ok() ||
                !sourceGraphPayload(plan, source, sourceInspection)) {
                return false;
            }
            if (sameDesiredGraphPayload(
                    plan,
                    graph,
                    nodeId,
                    source,
                    sourceInspection,
                    targetInspection)) {
                continue;
            }
        }


        // Paste may replace a node that is already in the canonical released
        // state (notably a freshly initialized prospective Graph). Reset is a
        // change-reporting primitive, so an idempotent false is not failure.
        const auto released = resetNodeFor(
            *current, preserveChildren, mode);
        if (sameGraphNodeStored(*current, released)) continue;

        const bool reset = preserveChildren
            ? seq::resetStepNodePayloadPreservingChildrenUnversioned(
                  *graph, nodeId, mode)
            : seq::resetStepNodePayloadUnversioned(*graph, nodeId, mode);
        if (!reset) return false;
        mutation.graphChanged = true;
    }
    return true;
}

FLASHMEM bool precompactIfRequired(
    Plan& plan,
    const SequencerHistoryDomainServices& history,
    MutationAccumulator& mutation
) noexcept {
    if (!flag(plan, kFlagRequiresCompaction)) return true;
    const auto outcome = history.precompactPreparedPatternEditGraph(
        seq::SequencerPreparedPatternEditOwner::PageStructure,
        static_cast<uint8_t>(plan.action),
        plan.expectedTrack,
        plan.contentPath);
    switch (outcome) {
        case seq::SequencerPreparedPatternGraphPrecompactionOutcome::Compacted:
            mutation.graphChanged = true;
            mutation.graphRevisionPublished = true;
            return true;
        case seq::SequencerPreparedPatternGraphPrecompactionOutcome::Unchanged:
            return true;
        case seq::SequencerPreparedPatternGraphPrecompactionOutcome::Failed:
            return false;
    }
    return false;
}

FLASHMEM bool graphCapacityAvailable(
    const Plan& plan,
    const seq::SequencerState& sequencer
) noexcept {
    const auto budget = plan.graphBudget.expanded();
    if (budget.stepNodes == 0U && budget.sequences == 0U &&
        budget.cycleSets == 0U) {
        return true;
    }
    const auto* graph = seq::graphView(sequencer.pattern);
    return graph != nullptr &&
           seq::sequencerGraphHasCopyCapacity(*graph, budget);
}

FLASHMEM bool appendMappedGraphPayloads(
    const Plan& plan,
    seq::SequencerState& sequencer,
    MutationAccumulator& mutation
) noexcept {
    auto* graph = sequencer.pattern.graph.get();
    for (uint16_t target = 0U; target < plan.targetToSource.size(); ++target) {
        const uint8_t encoded = plan.targetToSource[target];
        if (encoded >= Plan::TARGET_DEFAULT) continue;
        const auto source = sourcePayloadFor(plan, encoded);
        seq::SequencerGraphPayloadInspection inspection{};
        if (!sourceGraphPayload(plan, source, inspection)) return false;
        if (!inspection.payloadPresent && flag(plan, kFlagRootContext)) {
            continue;
        }
        if (graph == nullptr || !graph->enabled ||
            plan.sourceGraphIdentity == nullptr) {
            return false;
        }
        const auto targetNodeId = projectedTargetNodeId(
            plan, sequencer, static_cast<uint8_t>(target));
        if (targetNodeId == GraphLimits::INVALID_ID) return false;
        const auto targetInspection = seq::inspectSequencerGraphPayload(
            *graph, targetNodeId, plan.depth());
        if (!targetInspection.ok()) return false;
        if (sameDesiredGraphPayload(
                plan,
                graph,
                targetNodeId,
                source,
                inspection,
                targetInspection)) {
            continue;
        }
        if (!seq::copyStepNodePayloadFromGraphUnversioned(
                *graph,
                targetNodeId,
                *plan.sourceGraphIdentity,
                source.nodeId,
                plan.depth())) {
            return false;
        }
        mutation.graphChanged = true;
    }
    return true;
}

FLASHMEM bool writeMappedFlatTargets(
    const Plan& plan,
    seq::SequencerState& sequencer,
    MutationAccumulator& mutation
) noexcept {
    if (!flag(plan, kFlagRootContext)) return true;
    auto enabledMask = sequencer.pattern.enabledMask.get();
    bool changed = false;
    for (uint16_t target = 0U; target < plan.targetToSource.size(); ++target) {
        const uint8_t encoded = plan.targetToSource[target];
        if (encoded == Plan::TARGET_UNTOUCHED) continue;
        if (target >= plan.resultingContentLength) return false;

        bool enabled = false;
        uint8_t note = seq::SequencerState::DEFAULT_NOTE;
        uint8_t velocity = seq::SequencerState::DEFAULT_VELOCITY;
        uint16_t gate = seq::SequencerState::DEFAULT_GATE_PERCENT;
        int8_t nudge = 0;
        uint8_t probability = seq::SequencerState::DEFAULT_PROBABILITY;
        if (encoded != Plan::TARGET_DEFAULT) {
            const auto source = sourcePayloadFor(plan, encoded);
            if (!source.valid) return false;
            if (source.page != nullptr) {
                enabled = source.page->isEnabled(source.localIndex);
                note = source.page->note[source.localIndex];
                velocity = source.page->velocity[source.localIndex];
                gate = source.page->gate[source.localIndex];
                nudge = source.page->nudge[source.localIndex];
                probability = source.page->probability[source.localIndex];
            } else if (source.step != nullptr) {
                enabled = source.step->enabled;
                note = source.step->note;
                velocity = source.step->velocity;
                gate = source.step->gate;
                nudge = source.step->nudge;
                probability = source.step->probability;
            } else {
                return false;
            }
        }

        const auto step = static_cast<uint8_t>(target);
        if (sequencer.pattern.note[step] != note ||
            sequencer.pattern.velocity[step] != velocity ||
            sequencer.pattern.gate[step] != gate ||
            sequencer.pattern.nudge[step] != nudge ||
            sequencer.pattern.probability[step] != probability ||
            enabledMask.test(step) != enabled) {
            sequencer.pattern.note[step] = note;
            sequencer.pattern.velocity[step] = velocity;
            sequencer.pattern.gate[step] = gate;
            sequencer.pattern.nudge[step] = nudge;
            sequencer.pattern.probability[step] = probability;
            enabledMask.setBit(step, enabled);
            changed = true;
        }
    }
    if (changed) {
        sequencer.pattern.enabledMask.set(enabledMask);
        mutation.domains.stepData = true;
    }
    return true;
}

FLASHMEM bool resizeRootBeforeMappedGraphReclaim(
    const Plan& plan,
    seq::SequencerState& sequencer,
    MutationAccumulator& mutation
) noexcept {
    if (!flag(plan, kFlagRootContext) ||
        plan.resultingContentLength == plan.contentLength) {
        return true;
    }
    const auto result = seq::resizeSequencerRootContentUnversioned(
        sequencer, plan.resultingContentLength);
    if (!result.accepted() || !result.changed()) return false;
    mutation.merge(result.domains);
    return true;
}

FLASHMEM void publishMutationRevisions(
    seq::SequencerState& sequencer,
    MutationAccumulator& mutation
) noexcept {
    mutation.graphChanged = mutation.graphChanged || mutation.domains.graph;
    mutation.domains.graph = false;
    seq::publishSequencerSnapshotBatchRevisions(
        sequencer.pattern, mutation.domains);
    if (mutation.graphChanged && !mutation.graphRevisionPublished) {
        sequencer.pattern.bumpGraphRevision();
        mutation.graphRevisionPublished = true;
    }
}

FLASHMEM void publishReplayableFocus(
    const Plan& plan,
    seq::SequencerState& sequencer
) noexcept {
    uint8_t finalPage = plan.initialPage;
    switch (plan.action) {
        case SequencerPreparedPageStructureAction::PageCreate:
        case SequencerPreparedPageStructureAction::PageSelectionPaste:
        case SequencerPreparedPageStructureAction::PageClear:
        case SequencerPreparedPageStructureAction::PageDelete:
        case SequencerPreparedPageStructureAction::PagePaste:
        case SequencerPreparedPageStructureAction::StepPaste:
        case SequencerPreparedPageStructureAction::FocusedStepReset:
            finalPage = seq::activeContentPageForStep(plan.finalFocus);
            break;
        case SequencerPreparedPageStructureAction::StepSelectionReset:
        case SequencerPreparedPageStructureAction::PageSelectionReset:
            break;
        case SequencerPreparedPageStructureAction::
            PageSelectionDeleteOrDeepReset:
            if (flag(plan, kFlagRootContext)) {
                finalPage = seq::activeContentPageForStep(plan.finalFocus);
            }
            break;
    }
    sequencer.page.set(finalPage);
    sequencer.focusedStep.set(plan.finalFocus);
}

FLASHMEM bool executeMappedMutation(
    Plan& plan,
    seq::SequencerState& sequencer,
    const SequencerHistoryDomainServices& history,
    MutationAccumulator& mutation
) noexcept {
    if (!initializeProspectiveGraph(plan, sequencer, mutation) ||
        !extendChildPreservingLogicalContent(plan, sequencer, mutation) ||
        !resizeRootBeforeMappedGraphReclaim(plan, sequencer, mutation) ||
        !releaseMappedGraphTargets(plan, sequencer, mutation) ||
        !precompactIfRequired(plan, history, mutation) ||
        !graphCapacityAvailable(plan, sequencer) ||
        !writeMappedFlatTargets(plan, sequencer, mutation) ||
        !appendMappedGraphPayloads(plan, sequencer, mutation)) {
        return false;
    }
    return true;
}

FLASHMEM bool executeDeleteMutation(
    Plan& plan,
    seq::SequencerState& sequencer,
    const SequencerHistoryDomainServices& history,
    MutationAccumulator& mutation
) noexcept {
    const auto result = seq::deleteSequencerRootPagesUnversioned(
        sequencer, plan.pageMask);
    if (!result.accepted() || !result.changed()) return false;
    mutation.merge(result.domains);
    return precompactIfRequired(plan, history, mutation);
}

FLASHMEM SequencerPreparedPageStructureMutationOutcome mutatePlan(
    void* context,
    seq::SequencerState& sequencer,
    const SequencerHistoryDomainServices& history
) noexcept {
    if (context == nullptr) {
        return SequencerPreparedPageStructureMutationOutcome::Failed;
    }
    auto& plan = *static_cast<Plan*>(context);
    MutationAccumulator mutation;
    const bool deleteAction =
        plan.action == SequencerPreparedPageStructureAction::PageDelete ||
        (plan.action == SequencerPreparedPageStructureAction::
             PageSelectionDeleteOrDeepReset &&
         flag(plan, kFlagRootContext));
    const bool ok = deleteAction
        ? executeDeleteMutation(plan, sequencer, history, mutation)
        : executeMappedMutation(plan, sequencer, history, mutation);
    if (!ok) return SequencerPreparedPageStructureMutationOutcome::Failed;

    publishMutationRevisions(sequencer, mutation);
    publishReplayableFocus(plan, sequencer);
    return mutation.changed()
        ? SequencerPreparedPageStructureMutationOutcome::Changed
        : SequencerPreparedPageStructureMutationOutcome::NoChange;
}

FLASHMEM void finalizeCommittedPlan(
    void* context,
    seq::SequencerState& sequencer
) noexcept {
    if (context == nullptr) return;
    const auto& plan = *static_cast<const Plan*>(context);
    if (flag(plan, kFlagGraphDelta)) {
        seq::publishPreparedSequencerGraphContentPath(
            sequencer, plan.contentPath);
    }
}

}  // namespace

FLASHMEM SequencerPreparedPageStructureContentContext
SequencerPreparedPageStructureMutationPlan::context() const noexcept {
    return contentContextFor(contentPath);
}

FLASHMEM bool SequencerPreparedPageStructureMutationPlan::
    compactGraphOnSeal() const noexcept {
    return flag(*this, kFlagRequiresCompaction);
}

FLASHMEM Preflight buildSequencerPageCreateMutationPlan(
    const seq::SequencerState& sequencer,
    uint8_t expectedTrack,
    uint8_t targetPage,
    Plan& out
) noexcept {
    if (!beginPlan(
            sequencer,
            expectedTrack,
            SequencerPreparedPageStructureAction::PageCreate,
            out) ||
        !rootContextOnly(out) ||
        targetPage != pageCreateTargetFor(sequencer) ||
        targetPage != sequencer.activePageCount()) {
        return out.outcome;
    }
    if (targetPage == seq::SequencerState::PAGE_COUNT &&
        sequencer.activePageCount() == seq::SequencerState::PAGE_COUNT) {
        out.outcome = Preflight::NoChange;
        return out.outcome;
    }
    if (targetPage >= seq::SequencerState::PAGE_COUNT) return out.outcome;
    const uint8_t required = static_cast<uint8_t>(std::min<uint16_t>(
        seq::SequencerState::MAX_STEPS,
        static_cast<uint16_t>(targetPage + 1U) *
            seq::SequencerState::STEPS_PER_PAGE));
    if (required <= out.contentLength) {
        out.outcome = Preflight::NoChange;
        return out.outcome;
    }
    out.resultingContentLength = required;
    out.finalFocus = static_cast<uint8_t>(
        targetPage * seq::SequencerState::STEPS_PER_PAGE);
    if (!assignDefaultRange(out, out.contentLength, required)) {
        return out.outcome;
    }
    return finishMappedPlan(sequencer, out, true);
}

FLASHMEM Preflight buildSequencerPageSelectionPasteMutationPlan(
    const seq::SequencerState& sequencer,
    const core::state::StructureClipboardState& clipboard,
    SequencerPreparedPageStructureTarget target,
    Plan& out
) noexcept {
    const uint8_t expectedTrack =
        sequencerPreparedPageStructureTargetTrack(target);
    const uint8_t cursorPage =
        sequencerPreparedPageStructureTargetPage(target);
    if (!beginPlan(
            sequencer,
            expectedTrack,
            SequencerPreparedPageStructureAction::PageSelectionPaste,
            out) ||
        !rootContextOnly(out) || cursorPage >= seq::SequencerState::PAGE_COUNT ||
        !setupClipboardPlan(out, clipboard)) {
        return out.outcome;
    }
    const auto& selection = clipboard.sequencerPageSelection;
    uint8_t firstDestination = seq::SequencerState::PAGE_COUNT;
    uint8_t required = out.contentLength;
    for (uint8_t index = 0U; index < selection.count; ++index) {
        const auto& page = selection.pages[index];
        const uint16_t destination = static_cast<uint16_t>(cursorPage) +
            static_cast<uint16_t>(page.sourcePage - selection.sourceFirstPage);
        if (destination >= seq::SequencerState::PAGE_COUNT) {
            return out.outcome;
        }
        firstDestination = std::min<uint8_t>(
            firstDestination, static_cast<uint8_t>(destination));
        required = static_cast<uint8_t>(std::max<uint16_t>(
            required,
            destination * seq::SequencerState::STEPS_PER_PAGE + page.count));
    }
    if (firstDestination >= seq::SequencerState::PAGE_COUNT ||
        required > seq::SequencerState::MAX_STEPS) {
        return out.outcome;
    }
    out.resultingContentLength = required;
    out.finalFocus = static_cast<uint8_t>(
        firstDestination * seq::SequencerState::STEPS_PER_PAGE);
    if (required > out.contentLength &&
        !assignDefaultRange(out, out.contentLength, required)) {
        return out.outcome;
    }
    for (uint8_t index = 0U; index < selection.count; ++index) {
        const auto& page = selection.pages[index];
        const auto destination = static_cast<uint8_t>(
            cursorPage + page.sourcePage - selection.sourceFirstPage);
        if (!buildPagePasteTargets(out, page, destination, index)) {
            return out.outcome;
        }
    }
    return finishMappedPlan(sequencer, out);
}

FLASHMEM Preflight buildSequencerPageClearMutationPlan(
    const seq::SequencerState& sequencer,
    uint8_t expectedTrack,
    uint8_t page,
    Plan& out
) noexcept {
    if (!beginPlan(
            sequencer,
            expectedTrack,
            SequencerPreparedPageStructureAction::PageClear,
            out) ||
        !rootContextOnly(out) ||
        sequencer.structureUi.previewAddPageSlot.get() ||
        page != sequencer.page.get() || page != sequencer.visiblePage() ||
        page >= sequencer.activePageCount()) {
        return out.outcome;
    }
    const uint8_t start = static_cast<uint8_t>(
        page * seq::SequencerState::STEPS_PER_PAGE);
    const uint8_t end = static_cast<uint8_t>(std::min<uint16_t>(
        out.contentLength,
        static_cast<uint16_t>(start) + seq::SequencerState::STEPS_PER_PAGE));
    out.finalFocus = start;
    if (!assignDefaultRange(out, start, end)) return out.outcome;
    return finishMappedPlan(sequencer, out);
}

FLASHMEM Preflight buildSequencerPageDeleteMutationPlan(
    const seq::SequencerState& sequencer,
    uint8_t expectedTrack,
    uint8_t page,
    Plan& out
) noexcept {
    if (!beginPlan(
            sequencer,
            expectedTrack,
            SequencerPreparedPageStructureAction::PageDelete,
            out) ||
        !rootContextOnly(out) ||
        sequencer.structureUi.previewAddPageSlot.get() ||
        page != sequencer.page.get() || page != sequencer.visiblePage() ||
        page >= sequencer.activePageCount()) {
        return out.outcome;
    }
    if (sequencer.activePageCount() <= 1U) {
        out.outcome = Preflight::NoChange;
        return out.outcome;
    }
    out.pageMask = static_cast<uint16_t>(uint16_t{1} << page);
    const uint8_t removed = static_cast<uint8_t>(std::min<uint16_t>(
        seq::SequencerState::STEPS_PER_PAGE,
        static_cast<uint16_t>(out.patternLength) -
            static_cast<uint16_t>(page) * seq::SequencerState::STEPS_PER_PAGE));
    out.resultingContentLength = static_cast<uint8_t>(
        out.contentLength - removed);
    out.finalFocus = static_cast<uint8_t>(std::min<uint16_t>(
        static_cast<uint16_t>(page) * seq::SequencerState::STEPS_PER_PAGE,
        static_cast<uint16_t>(out.resultingContentLength - 1U)));
    return finishDeletePlan(sequencer, out);
}

FLASHMEM Preflight buildSequencerPagePasteMutationPlan(
    const seq::SequencerState& sequencer,
    const core::state::StructureClipboardState& clipboard,
    SequencerPreparedPageStructureTarget target,
    Plan& out
) noexcept {
    const uint8_t expectedTrack =
        sequencerPreparedPageStructureTargetTrack(target);
    const uint8_t targetPage =
        sequencerPreparedPageStructureTargetPage(target);
    if (!beginPlan(
            sequencer,
            expectedTrack,
            SequencerPreparedPageStructureAction::PagePaste,
            out) ||
        !rootContextOnly(out) || !setupClipboardPlan(out, clipboard)) {
        return out.outcome;
    }
    const uint8_t derivedTarget = pagePasteTargetFor(sequencer);
    if ((sequencer.structureUi.previewAddPageSlot.get() &&
         (derivedTarget != sequencer.activePageCount() ||
          derivedTarget >= seq::SequencerState::PAGE_COUNT)) ||
        targetPage >= seq::SequencerState::PAGE_COUNT ||
        targetPage != derivedTarget) {
        return out.outcome;
    }
    const auto& page = clipboard.sequencerPage;
    const uint8_t targetStart = static_cast<uint8_t>(
        targetPage * seq::SequencerState::STEPS_PER_PAGE);
    uint16_t required = static_cast<uint16_t>(targetStart) + page.count;
    if (targetPage >= sequencer.activePageCount()) {
        required = static_cast<uint16_t>(targetStart) +
            seq::SequencerState::STEPS_PER_PAGE;
    }
    required = std::max<uint16_t>(required, out.contentLength);
    if (required > seq::SequencerState::MAX_STEPS) return out.outcome;
    out.resultingContentLength = static_cast<uint8_t>(required);
    out.finalFocus = targetStart;
    if (out.resultingContentLength > out.contentLength &&
        !assignDefaultRange(
            out, out.contentLength, out.resultingContentLength)) {
        return out.outcome;
    }
    if (!buildPagePasteTargets(out, page, targetPage, 0U)) {
        return out.outcome;
    }
    return finishMappedPlan(sequencer, out);
}

FLASHMEM Preflight buildSequencerStepPasteMutationPlan(
    const seq::SequencerState& sequencer,
    const core::state::StructureClipboardState& clipboard,
    SequencerPreparedStepPasteTarget target,
    Plan& out
) noexcept {
    const uint8_t expectedTrack =
        sequencerPreparedStepPasteTargetTrack(target);
    auto mode = sequencerPreparedStepPasteTargetMode(target);
    const uint8_t cursorStep =
        sequencerPreparedStepPasteTargetCursor(target);
    if (!beginPlan(
            sequencer,
            expectedTrack,
            SequencerPreparedPageStructureAction::StepPaste,
            out) ||
        !setupClipboardPlan(out, clipboard)) {
        return out.outcome;
    }
    const auto& steps = clipboard.sequencerSteps;
    if (steps.rootContext != rootContextOnly(out)) return out.outcome;
    mode = project::sanitizeProjectStepPasteMode(mode);
    const uint8_t maxStep = rootContextOnly(out)
        ? static_cast<uint8_t>(seq::SequencerState::MAX_STEPS - 1U)
        : static_cast<uint8_t>(
              GraphLimits::MAX_EXPANDED_NOTES_PER_ROOT_STEP - 1U);
    if (cursorStep > maxStep) return out.outcome;

    uint8_t firstTarget = seq::SequencerState::MAX_STEPS;
    uint8_t lastTarget = 0U;
    for (uint8_t index = 0U; index < steps.count; ++index) {
        uint8_t target = 0U;
        if (!seq::resolveStepPasteTarget(
                mode,
                cursorStep,
                steps.entries[index].offset,
                out.contentLength,
                maxStep,
                target)) {
            return out.outcome;
        }
        if (!assignTarget(out, target, index)) return out.outcome;
        firstTarget = std::min(firstTarget, target);
        lastTarget = std::max(lastTarget, target);
    }
    if (firstTarget >= seq::SequencerState::MAX_STEPS) return out.outcome;
    out.finalFocus = firstTarget;
    if (mode != project::ProjectStepPasteMode::WRAP) {
        out.resultingContentLength = seq::requiredStepPasteLength(mode, lastTarget);
        if (out.resultingContentLength < out.contentLength) {
            out.resultingContentLength = out.contentLength;
        }
    }
    if (out.resultingContentLength == 0U ||
        out.resultingContentLength > static_cast<uint8_t>(maxStep + 1U)) {
        return out.outcome;
    }
    if (rootContextOnly(out) &&
        out.resultingContentLength > out.contentLength &&
        !assignDefaultRange(
            out, out.contentLength, out.resultingContentLength)) {
        return out.outcome;
    }
    if (!rootContextOnly(out) &&
        out.resultingContentLength > out.contentLength) {
        out.flags |= kFlagChildResize;
    }
    return finishMappedPlan(
        sequencer,
        out,
        out.resultingContentLength != out.contentLength);
}

FLASHMEM Preflight buildSequencerFocusedStepResetMutationPlan(
    const seq::SequencerState& sequencer,
    SequencerPreparedFocusedStepResetTarget target,
    Plan& out
) noexcept {
    const uint8_t expectedTrack =
        sequencerPreparedFocusedStepResetTargetTrack(target);
    const uint8_t step =
        sequencerPreparedFocusedStepResetTargetStep(target);
    const auto depth = sequencerPreparedFocusedStepResetTargetDepth(target);
    if (!beginPlan(
            sequencer,
            expectedTrack,
            SequencerPreparedPageStructureAction::FocusedStepReset,
            out) ||
        step >= out.contentLength || step != out.initialFocus ||
        !validResetDepth(depth)) {
        return out.outcome;
    }
    out.resetDepth = depth;
    out.finalFocus = step;
    if (!assignTarget(out, step, Plan::TARGET_DEFAULT)) return out.outcome;
    return finishMappedPlan(sequencer, out);
}

FLASHMEM Preflight buildSequencerStepSelectionResetMutationPlan(
    const seq::SequencerState& sequencer,
    const oc::note::sequencer::StepBitMask128& selectedMask,
    SequencerPreparedStepSelectionResetTarget target,
    Plan& out
) noexcept {
    const uint8_t expectedTrack =
        sequencerPreparedStepSelectionResetTargetTrack(target);
    const auto depth =
        sequencerPreparedStepSelectionResetTargetDepth(target);
    if (!beginPlan(
            sequencer,
            expectedTrack,
            SequencerPreparedPageStructureAction::StepSelectionReset,
            out) ||
        !validResetDepth(depth)) {
        return out.outcome;
    }
    out.resetDepth = depth;
    for (uint16_t step = 0U; step < out.contentLength; ++step) {
        if (selectedMask.test(static_cast<uint8_t>(step)) &&
            !assignTarget(
                out,
                static_cast<uint8_t>(step),
                Plan::TARGET_DEFAULT)) {
            return out.outcome;
        }
    }
    if (out.targetCount == 0U) {
        out.outcome = Preflight::NoChange;
        return out.outcome;
    }
    return finishMappedPlan(sequencer, out);
}

FLASHMEM Preflight buildSequencerPageSelectionResetMutationPlan(
    const seq::SequencerState& sequencer,
    uint8_t expectedTrack,
    uint16_t selectedPageMask,
    Plan& out
) noexcept {
    if (!beginPlan(
            sequencer,
            expectedTrack,
            SequencerPreparedPageStructureAction::PageSelectionReset,
            out)) {
        return out.outcome;
    }
    const uint8_t pageCount = pageCountForLength(out.contentLength);
    const uint16_t activeMask = pageCount >= 16U
        ? std::numeric_limits<uint16_t>::max()
        : static_cast<uint16_t>((uint16_t{1} << pageCount) - 1U);
    out.pageMask = static_cast<uint16_t>(selectedPageMask & activeMask);
    out.resetDepth = rootContextOnly(out)
        ? StepResetDepth::Deep
        : StepResetDepth::Shallow;
    for (uint16_t step = 0U; step < out.contentLength; ++step) {
        const uint8_t page = static_cast<uint8_t>(
            step / seq::SequencerState::STEPS_PER_PAGE);
        if (pageMaskContains(out.pageMask, page) &&
            !assignTarget(
                out,
                static_cast<uint8_t>(step),
                Plan::TARGET_DEFAULT)) {
            return out.outcome;
        }
    }
    if (out.targetCount == 0U) {
        out.outcome = Preflight::NoChange;
        return out.outcome;
    }
    return finishMappedPlan(sequencer, out);
}

FLASHMEM Preflight
buildSequencerPageSelectionDeleteOrDeepResetMutationPlan(
    const seq::SequencerState& sequencer,
    uint8_t expectedTrack,
    uint16_t selectedPageMask,
    Plan& out
) noexcept {
    if (!beginPlan(
            sequencer,
            expectedTrack,
            SequencerPreparedPageStructureAction::
                PageSelectionDeleteOrDeepReset,
            out)) {
        return out.outcome;
    }
    const uint8_t pageCount = pageCountForLength(out.contentLength);
    const uint16_t activeMask = pageCount >= 16U
        ? std::numeric_limits<uint16_t>::max()
        : static_cast<uint16_t>((uint16_t{1} << pageCount) - 1U);
    out.pageMask = static_cast<uint16_t>(selectedPageMask & activeMask);
    const uint8_t selectedCount = countPages(out.pageMask, pageCount);
    if (selectedCount == 0U ||
        (rootContextOnly(out) && selectedCount >= pageCount)) {
        out.outcome = Preflight::NoChange;
        return out.outcome;
    }

    out.resetDepth = StepResetDepth::Deep;
    if (rootContextOnly(out)) {
        uint8_t removed = 0U;
        uint8_t firstSelected = pageCount;
        for (uint8_t page = 0U; page < pageCount; ++page) {
            if (!pageMaskContains(out.pageMask, page)) continue;
            firstSelected = std::min(firstSelected, page);
            const uint8_t start = static_cast<uint8_t>(
                page * seq::SequencerState::STEPS_PER_PAGE);
            removed = static_cast<uint8_t>(removed + std::min<uint16_t>(
                seq::SequencerState::STEPS_PER_PAGE,
                static_cast<uint16_t>(out.contentLength - start)));
        }
        out.resultingContentLength = static_cast<uint8_t>(
            out.contentLength - removed);
        out.finalFocus = static_cast<uint8_t>(std::min<uint16_t>(
            static_cast<uint16_t>(firstSelected) *
                seq::SequencerState::STEPS_PER_PAGE,
            static_cast<uint16_t>(out.resultingContentLength - 1U)));
        return finishDeletePlan(sequencer, out);
    }

    for (uint16_t step = 0U; step < out.contentLength; ++step) {
        const uint8_t page = static_cast<uint8_t>(
            step / seq::SequencerState::STEPS_PER_PAGE);
        if (pageMaskContains(out.pageMask, page) &&
            !assignTarget(
                out,
                static_cast<uint8_t>(step),
                Plan::TARGET_DEFAULT)) {
            return out.outcome;
        }
    }
    return finishMappedPlan(sequencer, out);
}

FLASHMEM SequencerPreparedPageStructureExecution
makeSequencerPreparedPageStructureExecution(Plan& plan) noexcept {
    const uint8_t afterPatternLength = rootContextOnly(plan)
        ? plan.resultingContentLength
        : plan.patternLength;
    return {
        .payloadPlan = plan.payloadPlan,
        .action = plan.action,
        .expectedTrack = plan.expectedTrack,
        .beforePageCount = pageCountForLength(plan.patternLength),
        .afterPageCount = pageCountForLength(afterPatternLength),
        .compactGraphOnSeal = plan.compactGraphOnSeal(),
        .mutationContext = &plan,
        .revalidate = &revalidateMutationPlan,
        .mutate = &mutatePlan,
        .finalizeCommitted = &finalizeCommittedPlan,
    };
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#elif defined(_MSC_VER)
__declspec(noinline)
#endif
FLASHMEM SequencerPreparedPageStructureResult
executeSequencerPreparedPageStructureMutationPlan(
    SequencerPreparedPageStructureTransaction& transaction,
    Plan& plan
) {
    const auto execution = makeSequencerPreparedPageStructureExecution(plan);
    return transaction.execute(execution);
}

}  // namespace core::handler
