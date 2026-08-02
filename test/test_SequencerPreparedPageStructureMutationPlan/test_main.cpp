#ifdef NDEBUG
#undef NDEBUG
#endif

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <new>
#include <utility>

#include "app/ExtmemAllocator.hpp"
#include "handler/sequencer/SequencerPreparedPageStructureMutationPlan.hpp"
#include "handler/sequencer/SequencerStructureStepOps.hpp"
#include "state/CoreState.hpp"
#include "state/sequencer/SequencerCcLaneDomain.hpp"
#include "state/sequencer/SequencerCcLanePatternOps.hpp"
#include "state/sequencer/SequencerContentViewOps.hpp"
#include "state/sequencer/SequencerGraphOps.hpp"
#include "state/sequencer/SequencerTrackBankOps.hpp"
#include "support/CoreStorages.hpp"
#include "support/NotificationTestUtils.hpp"
#include "support/SequencerHistoryTransactionAssertions.hpp"

namespace allocation_trace {

bool enabled = false;
std::size_t count = 0U;

void record() noexcept {
    if (enabled) ++count;
}

class Scope final {
public:
    Scope() noexcept {
        count = 0U;
        enabled = true;
    }
    ~Scope() { enabled = false; }

    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
};

}  // namespace allocation_trace

void* operator new(std::size_t bytes) {
    allocation_trace::record();
    if (void* memory = std::malloc(bytes)) return memory;
    throw std::bad_alloc{};
}

void* operator new[](std::size_t bytes) { return ::operator new(bytes); }
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { ::operator delete(memory); }
void operator delete(void* memory, std::size_t) noexcept {
    ::operator delete(memory);
}
void operator delete[](void* memory, std::size_t) noexcept {
    ::operator delete[](memory);
}

namespace {

namespace seq = core::state::sequencer;
namespace project = core::state::project;
namespace tx = test_support::sequencer_transaction;

using Action = core::handler::SequencerPreparedPageStructureAction;
using Execution = core::handler::SequencerPreparedPageStructureExecution;
using MutationOutcome =
    core::handler::SequencerPreparedPageStructureMutationOutcome;
using MutationPlan =
    core::handler::SequencerPreparedPageStructureMutationPlan;
using core::handler::makeSequencerPreparedFocusedStepResetTarget;
using core::handler::makeSequencerPreparedPageStructureTarget;
using core::handler::makeSequencerPreparedStepPasteTarget;
using core::handler::makeSequencerPreparedStepSelectionResetTarget;
using Preflight =
    core::handler::SequencerPreparedPageStructurePreflightOutcome;
using Result = core::handler::SequencerPreparedPageStructureResult;
using Services = core::handler::SequencerHistoryDomainServices;
using StepResetDepth = core::handler::StepResetDepth;
using Transaction = core::handler::SequencerPreparedPageStructureTransaction;
using PayloadPlan = seq::SequencerCoalescedPatternPayloadPlan;
using Owner = seq::SequencerPreparedPatternEditOwner;
using Graph = oc::note::sequencer::StepSequencerGraph;
using GraphLimits = oc::note::sequencer::StepSequencerGraphLimits;
using GraphNode = oc::note::sequencer::StepSequencerStepNode;

static_assert(sizeof(MutationPlan) <= 256U);
static_assert(sizeof(core::handler::SequencerPreparedPageStructureGraphBudget) ==
              8U);

struct UnchangedPrecompaction {
    static seq::SequencerPreparedPatternGraphPrecompactionOutcome apply(
        void*,
        Owner,
        uint8_t,
        uint8_t,
        seq::SequencerPreparedGraphContentPath&
    ) {
        return seq::SequencerPreparedPatternGraphPrecompactionOutcome::Unchanged;
    }
};

constexpr Services::Operations kUnchangedPrecompactionOperations{
    .precompactPreparedPatternEditGraph = &UnchangedPrecompaction::apply,
};

Services unchangedPrecompactionServices() {
    return Services::fromStaticOperations<
        kUnchangedPrecompactionOperations>(nullptr);
}

uint64_t byteHash(const void* data, std::size_t size) noexcept {
    const auto* bytes = static_cast<const uint8_t*>(data);
    uint64_t hash = 1469598103934665603ULL;
    for (std::size_t index = 0U; index < size; ++index) {
        hash ^= bytes[index];
        hash *= 1099511628211ULL;
    }
    return hash;
}

void setLength(seq::SequencerState& sequencer, uint8_t length) {
    sequencer.pattern.setContentLength(length);
    sequencer.page.set(0U);
    sequencer.focusedStep.set(0U);
}

void dirtyRootStep(seq::SequencerState& sequencer, uint8_t step = 0U) {
    sequencer.pattern.note[step] = 73U;
    auto enabled = sequencer.pattern.enabledMask.get();
    enabled.setBit(step, true);
    sequencer.pattern.enabledMask.set(enabled);
    sequencer.pattern.bumpStepDataRevision();
}

void fillPage(
    core::state::SequencerPageClipboard& page,
    uint8_t sourcePage,
    uint8_t firstNote = 67U
) {
    page = {};
    page.valid = true;
    page.sourcePage = sourcePage;
    page.count = seq::SequencerState::STEPS_PER_PAGE;
    page.enabledMask = 0x01U;
    for (uint8_t index = 0U; index < page.count; ++index) {
        page.note[index] = static_cast<uint8_t>(firstNote + index);
        page.velocity[index] = seq::SequencerState::DEFAULT_VELOCITY;
        page.gate[index] = seq::SequencerState::DEFAULT_GATE_PERCENT;
        page.nudge[index] = 0;
        page.probability[index] = seq::SequencerState::DEFAULT_PROBABILITY;
    }
}

void fillPageClipboard(core::state::StructureClipboardState& clipboard) {
    clipboard.kind.set(core::state::StructureClipboardKind::SEQUENCER_PAGE);
    clipboard.revision.set(11U);
    fillPage(clipboard.sequencerPage, 0U);
}

void fillPageSelectionClipboard(
    core::state::StructureClipboardState& clipboard
) {
    clipboard.kind.set(
        core::state::StructureClipboardKind::SEQUENCER_PAGE_SELECTION);
    clipboard.revision.set(12U);
    auto& selection = clipboard.sequencerPageSelection;
    selection = {};
    selection.valid = true;
    selection.sourceFirstPage = 1U;
    selection.count = 2U;
    fillPage(selection.pages[0U], 1U, 69U);
    fillPage(selection.pages[1U], 3U, 77U);
}

void fillStepsClipboard(
    core::state::StructureClipboardState& clipboard,
    uint8_t count = 1U,
    bool rootContext = true
) {
    clipboard.kind.set(core::state::StructureClipboardKind::SEQUENCER_STEPS);
    clipboard.revision.set(13U);
    auto& steps = clipboard.sequencerSteps;
    steps = {};
    steps.valid = true;
    steps.rootContext = rootContext;
    steps.count = count;
    steps.span = count;
    for (uint8_t index = 0U; index < count; ++index) {
        auto& entry = steps.entries[index];
        entry.valid = true;
        entry.offset = index;
        entry.enabled = index == 0U;
        entry.note = static_cast<uint8_t>(68U + index);
        entry.velocity = seq::SequencerState::DEFAULT_VELOCITY;
        entry.gate = seq::SequencerState::DEFAULT_GATE_PERCENT;
        entry.nudge = 0;
        entry.probability = seq::SequencerState::DEFAULT_PROBABILITY;
        entry.sourceNodeId = seq::rootStepNodeId(index);
    }
}

void attachScalarSourceGraph(
    core::state::StructureClipboardState& clipboard,
    uint8_t sourceStep = 0U,
    int8_t noteOffset = 5
) {
    auto graph = core::app::makeExtmemUnique<Graph>();
    assert(graph);
    assert(seq::initializeSequencerGraphRootUnversioned(*graph));
    auto& node = graph->stepNodes[seq::rootStepNodeId(sourceStep)];
    node.flags = static_cast<uint16_t>(
        node.flags | oc::note::sequencer::STEP_NODE_NOTE_OFFSET);
    node.noteOffset = noteOffset;
    clipboard.sequencerGraph = std::move(graph);
}

void attachMicroSequenceSourceGraph(
    core::state::StructureClipboardState& clipboard,
    uint8_t count
) {
    seq::SequencerPatternState source;
    assert(seq::ensureGraphRoot(source));
    for (uint8_t step = 0U; step < count; ++step) {
        const auto created = seq::createMicroSequence(
            source, seq::rootStepNodeId(step), 4U);
        assert(created.ok);
        const auto* graph = seq::graphView(source);
        assert(graph != nullptr);
        const auto* child = graph->sequence(created.id);
        assert(child != nullptr);
        assert(seq::setNodeNoteOffset(
            source, child->firstStepNode, static_cast<int8_t>(step + 1U)));
    }
    assert(source.graph);
    clipboard.sequencerGraph = std::move(source.graph);
}

void fillChildScalarClipboard(
    core::state::StructureClipboardState& clipboard,
    int8_t noteOffset = 9
) {
    fillStepsClipboard(clipboard, 1U, false);
    seq::SequencerPatternState source;
    const auto created = seq::createMicroSequence(
        source, seq::rootStepNodeId(0U), 1U);
    assert(created.ok);
    const auto* graph = seq::graphView(source);
    assert(graph != nullptr);
    const auto* sequence = graph->sequence(created.id);
    assert(sequence != nullptr);
    const uint16_t sourceNode = sequence->firstStepNode;
    assert(seq::setNodeNoteOffset(source, sourceNode, noteOffset));
    clipboard.sequencerSteps.entries[0U].sourceNodeId = sourceNode;
    clipboard.sequencerGraph = std::move(source.graph);
}

void fillSparseChildClipboard(
    core::state::StructureClipboardState& clipboard,
    uint8_t secondOffset
) {
    fillStepsClipboard(clipboard, 2U, false);
    clipboard.sequencerSteps.span = static_cast<uint8_t>(secondOffset + 1U);
    clipboard.sequencerSteps.entries[0U].offset = 0U;
    clipboard.sequencerSteps.entries[1U].offset = secondOffset;

    seq::SequencerPatternState source;
    const auto created = seq::createMicroSequence(
        source, seq::rootStepNodeId(0U), 2U);
    assert(created.ok);
    const auto* graph = seq::graphView(source);
    assert(graph != nullptr);
    const auto* sequence = graph->sequence(created.id);
    assert(sequence != nullptr);
    for (uint8_t index = 0U; index < 2U; ++index) {
        const uint16_t sourceNode = static_cast<uint16_t>(
            sequence->firstStepNode + index);
        assert(seq::setNodeNoteOffset(
            source,
            sourceNode,
            static_cast<int8_t>(50U + index)));
        clipboard.sequencerSteps.entries[index].sourceNodeId = sourceNode;
    }
    clipboard.sequencerGraph = std::move(source.graph);
}

bool sameStoredNode(const GraphNode& lhs, const GraphNode& rhs) noexcept {
    return std::memcmp(&lhs, &rhs, sizeof(GraphNode)) == 0;
}

void saturateMicroSequenceCapacityOutsideSpan(
    seq::SequencerPatternState& pattern,
    uint8_t excludedStart,
    uint8_t excludedEnd
) {
    bool exhausted = false;
    std::size_t createdCount = 0U;
    for (uint16_t step = 0U;
         step < seq::SequencerState::MAX_STEPS;
         ++step) {
        const auto root = static_cast<uint8_t>(step);
        if ((root >= excludedStart && root < excludedEnd) ||
            seq::stepNodeHasAnyChildContent(
                pattern, seq::rootStepNodeId(root))) {
            continue;
        }
        const auto created = seq::createMicroSequence(
            pattern, seq::rootStepNodeId(root), 4U);
        if (!created.ok) {
            exhausted = true;
            break;
        }
        ++createdCount;
    }
    assert(createdCount > 0U);
    assert(exhausted);
}

void assertReady(
    Preflight outcome,
    MutationPlan& plan,
    Action action
) {
    assert(outcome == Preflight::Ready);
    assert(plan.ready());
    assert(plan.action == action);
    assert(core::handler::makeSequencerPreparedPageStructureExecution(plan)
               .action == action);
    assert(plan.expectedTrack == 0U);
}

void test_exact_ten_builders_and_stable_keys() {
    {
        seq::SequencerState sequencer;
        setLength(sequencer, 8U);
        MutationPlan plan;
        assertReady(
            core::handler::buildSequencerPageCreateMutationPlan(
                sequencer, 0U, 1U, plan),
            plan,
            Action::PageCreate);
    }
    {
        seq::SequencerState sequencer;
        setLength(sequencer, 8U);
        core::state::StructureClipboardState clipboard;
        fillPageSelectionClipboard(clipboard);
        MutationPlan plan;
        assertReady(
            core::handler::buildSequencerPageSelectionPasteMutationPlan(
                sequencer, clipboard,
                makeSequencerPreparedPageStructureTarget(0U, 0U), plan),
            plan,
            Action::PageSelectionPaste);
    }
    {
        seq::SequencerState sequencer;
        setLength(sequencer, 8U);
        dirtyRootStep(sequencer);
        MutationPlan plan;
        assertReady(
            core::handler::buildSequencerPageClearMutationPlan(
                sequencer, 0U, 0U, plan),
            plan,
            Action::PageClear);
    }
    {
        seq::SequencerState sequencer;
        setLength(sequencer, 16U);
        MutationPlan plan;
        assertReady(
            core::handler::buildSequencerPageDeleteMutationPlan(
                sequencer, 0U, 0U, plan),
            plan,
            Action::PageDelete);
    }
    {
        seq::SequencerState sequencer;
        setLength(sequencer, 8U);
        core::state::StructureClipboardState clipboard;
        fillPageClipboard(clipboard);
        MutationPlan plan;
        assertReady(
            core::handler::buildSequencerPagePasteMutationPlan(
                sequencer, clipboard,
                makeSequencerPreparedPageStructureTarget(0U, 0U), plan),
            plan,
            Action::PagePaste);
    }
    {
        seq::SequencerState sequencer;
        setLength(sequencer, 8U);
        core::state::StructureClipboardState clipboard;
        fillStepsClipboard(clipboard);
        MutationPlan plan;
        assertReady(
            core::handler::buildSequencerStepPasteMutationPlan(
                sequencer,
                clipboard,
                makeSequencerPreparedStepPasteTarget(
                    0U,
                    project::ProjectStepPasteMode::EXTEND,
                    0U),
                plan),
            plan,
            Action::StepPaste);
    }
    {
        seq::SequencerState sequencer;
        setLength(sequencer, 8U);
        dirtyRootStep(sequencer);
        MutationPlan plan;
        assertReady(
            core::handler::buildSequencerFocusedStepResetMutationPlan(
                sequencer,
                makeSequencerPreparedFocusedStepResetTarget(
                    0U, 0U, StepResetDepth::Shallow),
                plan),
            plan,
            Action::FocusedStepReset);
    }
    {
        seq::SequencerState sequencer;
        setLength(sequencer, 8U);
        dirtyRootStep(sequencer);
        oc::note::sequencer::StepBitMask128 selected{};
        selected.setBit(0U, true);
        MutationPlan plan;
        assertReady(
            core::handler::buildSequencerStepSelectionResetMutationPlan(
                sequencer,
                selected,
                makeSequencerPreparedStepSelectionResetTarget(
                    0U, StepResetDepth::Deep),
                plan),
            plan,
            Action::StepSelectionReset);
    }
    {
        seq::SequencerState sequencer;
        setLength(sequencer, 8U);
        dirtyRootStep(sequencer);
        MutationPlan plan;
        assertReady(
            core::handler::buildSequencerPageSelectionResetMutationPlan(
                sequencer, 0U, 0x0001U, plan),
            plan,
            Action::PageSelectionReset);
    }
    {
        seq::SequencerState sequencer;
        setLength(sequencer, 16U);
        MutationPlan plan;
        assertReady(
            core::handler::
                buildSequencerPageSelectionDeleteOrDeepResetMutationPlan(
                    sequencer, 0U, 0x0001U, plan),
            plan,
            Action::PageSelectionDeleteOrDeepReset);
    }

    std::cout << "[PASS] exact ten Page/Step builders freeze stable action keys\n";
}

void test_rejected_and_semantic_no_change_preflights() {
    {
        seq::SequencerState sequencer;
        setLength(sequencer, 8U);
        MutationPlan plan;
        // Structure Page creation is append-only: an already-existing Page is
        // a stale target, not a semantic create no-op.
        assert(core::handler::buildSequencerPageCreateMutationPlan(
                   sequencer, 0U, 0U, plan) == Preflight::Rejected);
    }
    {
        seq::SequencerState sequencer;
        setLength(sequencer, seq::SequencerState::MAX_STEPS);
        MutationPlan plan;
        assert(core::handler::buildSequencerPageCreateMutationPlan(
                   sequencer,
                   0U,
                   seq::SequencerState::PAGE_COUNT,
                   plan) == Preflight::NoChange);
        assert(plan.targetCount == 0U);
    }
    {
        seq::SequencerState sequencer;
        setLength(sequencer, 8U);
        MutationPlan plan;
        assert(core::handler::buildSequencerPageClearMutationPlan(
                   sequencer, 0U, 0U, plan) == Preflight::NoChange);
    }
    {
        seq::SequencerState sequencer;
        setLength(sequencer, 8U);
        oc::note::sequencer::StepBitMask128 selected{};
        MutationPlan plan;
        assert(core::handler::buildSequencerStepSelectionResetMutationPlan(
                   sequencer,
                   selected,
                   makeSequencerPreparedStepSelectionResetTarget(
                       0U, StepResetDepth::Deep),
                   plan) == Preflight::NoChange);
    }
    {
        seq::SequencerState sequencer;
        setLength(sequencer, 8U);
        MutationPlan plan;
        assert(core::handler::buildSequencerPageClearMutationPlan(
                   sequencer,
                   seq::SequencerTrackBankState::TRACK_COUNT,
                   0U,
                   plan) == Preflight::Rejected);
    }
    {
        seq::SequencerState sequencer;
        setLength(sequencer, 8U);
        core::state::StructureClipboardState malformed;
        malformed.kind.set(
            core::state::StructureClipboardKind::SEQUENCER_PAGE);
        malformed.sequencerPage.valid = true;
        malformed.sequencerPage.sourcePage = 0U;
        malformed.sequencerPage.count = 0U;
        MutationPlan plan;
        assert(core::handler::buildSequencerPagePasteMutationPlan(
                   sequencer, malformed,
                   makeSequencerPreparedPageStructureTarget(0U, 0U), plan) ==
               Preflight::Rejected);
    }
    {
        seq::SequencerState sequencer;
        setLength(sequencer, 8U);
        dirtyRootStep(sequencer);
        sequencer.structureUi.previewAddPageSlot.set(true);
        MutationPlan plan;
        assert(core::handler::buildSequencerPageClearMutationPlan(
                   sequencer, 0U, 0U, plan) == Preflight::Rejected);
    }
    {
        seq::SequencerState sequencer;
        setLength(sequencer, 16U);
        MutationPlan plan;
        assert(core::handler::buildSequencerPageDeleteMutationPlan(
                   sequencer, 0U, 1U, plan) == Preflight::Rejected);
    }
    {
        seq::SequencerState sequencer;
        setLength(sequencer, 8U);
        sequencer.structureUi.previewAddPageSlot.set(true);
        sequencer.structureUi.previewPageIndex.set(0U);
        MutationPlan plan;
        assert(core::handler::buildSequencerPageCreateMutationPlan(
                   sequencer, 0U, 0U, plan) == Preflight::Rejected);
    }
    {
        seq::SequencerState sequencer;
        setLength(sequencer, 16U);
        sequencer.structureUi.previewAddPageSlot.set(true);
        sequencer.structureUi.previewPageIndex.set(3U);
        MutationPlan plan;
        assert(core::handler::buildSequencerPageCreateMutationPlan(
                   sequencer, 0U, 3U, plan) == Preflight::Rejected);
    }
    {
        seq::SequencerState sequencer;
        setLength(sequencer, 16U);
        core::state::StructureClipboardState clipboard;
        fillPageClipboard(clipboard);
        MutationPlan plan;
        assert(core::handler::buildSequencerPagePasteMutationPlan(
                   sequencer, clipboard,
                   makeSequencerPreparedPageStructureTarget(0U, 1U), plan) ==
               Preflight::Rejected);
    }
    {
        seq::SequencerState sequencer;
        setLength(sequencer, seq::SequencerState::MAX_STEPS);
        sequencer.structureUi.previewAddPageSlot.set(true);
        sequencer.structureUi.previewPageIndex.set(
            seq::SequencerState::PAGE_COUNT);
        core::state::StructureClipboardState clipboard;
        fillPageClipboard(clipboard);
        const uint64_t before = byteHash(
            &sequencer.pattern, sizeof(sequencer.pattern));
        MutationPlan plan;
        assert(core::handler::buildSequencerPagePasteMutationPlan(
                   sequencer,
                   clipboard,
                   makeSequencerPreparedPageStructureTarget(
                       0U,
                       static_cast<uint8_t>(
                           seq::SequencerState::PAGE_COUNT - 1U)),
                   plan) == Preflight::Rejected);
        assert(plan.targetCount == 0U);
        assert(byteHash(&sequencer.pattern, sizeof(sequencer.pattern)) ==
               before);
    }
    {
        seq::SequencerState sequencer;
        setLength(sequencer, 8U);
        dirtyRootStep(sequencer);
        sequencer.focusedStep.set(1U);
        MutationPlan plan;
        assert(core::handler::buildSequencerFocusedStepResetMutationPlan(
                   sequencer,
                   makeSequencerPreparedFocusedStepResetTarget(
                       0U, 0U, StepResetDepth::Shallow),
                   plan) == Preflight::Rejected);
    }
    {
        seq::SequencerState sequencer;
        setLength(sequencer, 8U);
        sequencer.focusedStep.set(8U);
        sequencer.page.set(1U);
        MutationPlan plan;
        assert(core::handler::buildSequencerPageClearMutationPlan(
                   sequencer, 0U, 0U, plan) == Preflight::Rejected);
    }
    {
        seq::SequencerState sequencer;
        setLength(sequencer, 8U);
        sequencer.page.set(1U);
        MutationPlan plan;
        assert(core::handler::buildSequencerPageClearMutationPlan(
                   sequencer, 0U, 0U, plan) == Preflight::Rejected);
    }
    {
        seq::SequencerState sequencer;
        setLength(sequencer, 8U);
        dirtyRootStep(sequencer);
        MutationPlan plan;
        assert(core::handler::buildSequencerFocusedStepResetMutationPlan(
                   sequencer,
                   makeSequencerPreparedFocusedStepResetTarget(
                       0U,
                       0U,
                       static_cast<StepResetDepth>(0xFFU)),
                   plan) == Preflight::Rejected);
    }
    {
        seq::SequencerState sequencer;
        setLength(sequencer, 8U);
        dirtyRootStep(sequencer);
        oc::note::sequencer::StepBitMask128 selected{};
        selected.setBit(0U, true);
        MutationPlan plan;
        assert(core::handler::buildSequencerStepSelectionResetMutationPlan(
                   sequencer,
                   selected,
                   makeSequencerPreparedStepSelectionResetTarget(
                       0U, static_cast<StepResetDepth>(0xFFU)),
                   plan) == Preflight::Rejected);
    }
    {
        seq::SequencerState sequencer;
        setLength(sequencer, 8U);
        const auto child = seq::createMicroSequence(
            sequencer.pattern, seq::rootStepNodeId(0U), 4U);
        assert(child.ok);
        assert(seq::enterMicroSequenceContentView(
            sequencer, seq::rootStepNodeId(0U), child.id));
        sequencer.page.set(1U);
        MutationPlan plan;
        assert(core::handler::buildSequencerFocusedStepResetMutationPlan(
                   sequencer,
                   makeSequencerPreparedFocusedStepResetTarget(
                       0U, 0U, StepResetDepth::Shallow),
                   plan) == Preflight::Rejected);
    }
    {
        seq::SequencerState sequencer;
        setLength(sequencer, 8U);
        assert(seq::ensureGraphRoot(sequencer.pattern));
        auto& malformed = sequencer.pattern.graph->stepNodes[0U];
        malformed.flags = static_cast<uint16_t>(
            malformed.flags |
            oc::note::sequencer::STEP_NODE_CHILD_SEQUENCE);
        malformed.childSequenceId = GraphLimits::INVALID_ID;
        MutationPlan plan;
        assert(core::handler::buildSequencerPageClearMutationPlan(
                   sequencer, 0U, 0U, plan) == Preflight::Rejected);
    }

    std::cout << "[PASS] malformed inputs reject and semantic no-ops stay cold\n";
}

void test_pattern_clipboard_and_content_path_staleness() {
    {
        seq::SequencerState sequencer;
        setLength(sequencer, 8U);
        dirtyRootStep(sequencer);
        MutationPlan plan;
        assertReady(
            core::handler::buildSequencerPageClearMutationPlan(
                sequencer, 0U, 0U, plan),
            plan,
            Action::PageClear);
        const Execution execution =
            core::handler::makeSequencerPreparedPageStructureExecution(plan);
        assert(execution.revalidate(execution.mutationContext, sequencer));
        sequencer.pattern.bumpStepDataRevision();
        assert(!execution.revalidate(execution.mutationContext, sequencer));
    }
    {
        seq::SequencerState sequencer;
        setLength(sequencer, 8U);
        assert(seq::ensureGraphRoot(sequencer.pattern));
        core::state::StructureClipboardState clipboard;
        fillStepsClipboard(clipboard);
        attachScalarSourceGraph(clipboard);
        MutationPlan plan;
        assertReady(
            core::handler::buildSequencerStepPasteMutationPlan(
                sequencer,
                clipboard,
                makeSequencerPreparedStepPasteTarget(
                    0U,
                    project::ProjectStepPasteMode::EXTEND,
                    0U),
                plan),
            plan,
            Action::StepPaste);
        const Execution execution =
            core::handler::makeSequencerPreparedPageStructureExecution(plan);
        assert(execution.revalidate(execution.mutationContext, sequencer));
        clipboard.sequencerSteps.entries[0U].sourceNodeId =
            GraphLimits::INVALID_ID;
        assert(!execution.revalidate(execution.mutationContext, sequencer));
    }
    {
        seq::SequencerState sequencer;
        setLength(sequencer, 8U);
        core::state::StructureClipboardState clipboard;
        fillStepsClipboard(clipboard);
        MutationPlan plan;
        assertReady(
            core::handler::buildSequencerStepPasteMutationPlan(
                sequencer,
                clipboard,
                makeSequencerPreparedStepPasteTarget(
                    0U,
                    project::ProjectStepPasteMode::EXTEND,
                    0U),
                plan),
            plan,
            Action::StepPaste);
        const Execution execution =
            core::handler::makeSequencerPreparedPageStructureExecution(plan);
        assert(execution.revalidate(execution.mutationContext, sequencer));
        clipboard.revision.set(clipboard.revision.get() + 1U);
        assert(!execution.revalidate(execution.mutationContext, sequencer));
    }
    {
        seq::SequencerState sequencer;
        setLength(sequencer, 8U);
        const auto child = seq::createMicroSequence(
            sequencer.pattern, seq::rootStepNodeId(0U), 4U);
        assert(child.ok);
        const auto* graph = seq::graphView(sequencer.pattern);
        assert(graph != nullptr);
        const auto* sequence = graph->sequence(child.id);
        assert(sequence != nullptr);
        assert(seq::setNodeNoteOffset(
            sequencer.pattern, sequence->firstStepNode, 4));
        assert(seq::enterMicroSequenceContentView(
            sequencer, seq::rootStepNodeId(0U), child.id));
        MutationPlan plan;
        assertReady(
            core::handler::buildSequencerFocusedStepResetMutationPlan(
                sequencer,
                makeSequencerPreparedFocusedStepResetTarget(
                    0U, 0U, StepResetDepth::Shallow),
                plan),
            plan,
            Action::FocusedStepReset);
        const Execution execution =
            core::handler::makeSequencerPreparedPageStructureExecution(plan);
        assert(execution.revalidate(execution.mutationContext, sequencer));
        assert(seq::leaveContentView(sequencer));
        assert(!execution.revalidate(execution.mutationContext, sequencer));
    }

    std::cout << "[PASS] Pattern, clipboard and Graph content paths revalidate\n";
}

void test_graph_budget_is_aggregate_exact_and_malformed_source_rejects() {
    {
        seq::SequencerState sequencer;
        setLength(sequencer, 8U);
        core::state::StructureClipboardState clipboard;
        fillStepsClipboard(clipboard, 2U);
        attachMicroSequenceSourceGraph(clipboard, 2U);
        MutationPlan plan;
        assertReady(
            core::handler::buildSequencerStepPasteMutationPlan(
                sequencer,
                clipboard,
                makeSequencerPreparedStepPasteTarget(
                    0U,
                    project::ProjectStepPasteMode::EXTEND,
                    0U),
                plan),
            plan,
            Action::StepPaste);
        assert(plan.graphBudget.stepNodes == 32U);
        assert(plan.graphBudget.sequences == 2U);
        assert(plan.graphBudget.cycleSets == 0U);
        assert(plan.payloadPlan == PayloadPlan::FullWithProspectiveGraph);
        assert(plan.compactGraphOnSeal());
    }
    {
        seq::SequencerState sequencer;
        setLength(sequencer, 8U);
        core::state::StructureClipboardState clipboard;
        fillStepsClipboard(clipboard);
        attachMicroSequenceSourceGraph(clipboard, 1U);
        auto& source = *clipboard.sequencerGraph;
        source.stepNodes[0U].childSequenceId = GraphLimits::INVALID_ID;
        MutationPlan plan;
        assert(core::handler::buildSequencerStepPasteMutationPlan(
                   sequencer,
                   clipboard,
                   makeSequencerPreparedStepPasteTarget(
                       0U,
                       project::ProjectStepPasteMode::EXTEND,
                       0U),
                   plan) == Preflight::Rejected);
    }
    {
        seq::SequencerState sequencer;
        setLength(sequencer, 8U);
        core::state::StructureClipboardState clipboard;
        fillStepsClipboard(clipboard);
        attachScalarSourceGraph(clipboard);
        clipboard.sequencerSteps.entries[0U].sourceNodeId =
            GraphLimits::INVALID_ID;
        MutationPlan plan;
        assert(core::handler::buildSequencerStepPasteMutationPlan(
                   sequencer,
                   clipboard,
                   makeSequencerPreparedStepPasteTarget(
                       0U,
                       project::ProjectStepPasteMode::EXTEND,
                       0U),
                   plan) == Preflight::Rejected);
    }
    {
        seq::SequencerState sequencer;
        setLength(sequencer, 8U);
        core::state::StructureClipboardState clipboard;
        fillStepsClipboard(clipboard, 2U);
        attachMicroSequenceSourceGraph(clipboard, 1U);
        auto& source = *clipboard.sequencerGraph;
        const uint16_t sharedSequence = source.stepNodes[0U].childSequenceId;
        assert(sharedSequence != GraphLimits::INVALID_ID);
        source.stepNodes[1U].flags = static_cast<uint16_t>(
            source.stepNodes[1U].flags |
            oc::note::sequencer::STEP_NODE_CHILD_SEQUENCE);
        source.stepNodes[1U].childSequenceId = sharedSequence;
        assert(!seq::validInitializedSequencerGraph(source));

        const uint32_t stepRevision =
            sequencer.pattern.stepDataRevision.get();
        const uint32_t graphRevision =
            sequencer.pattern.graphRevision.get();
        const uint32_t ccRevision = sequencer.pattern.ccLaneRevision.get();
        const uint32_t timingRevision =
            sequencer.pattern.patternTimingRevision.get();
        const uint8_t note = sequencer.pattern.note[0U];
        MutationPlan plan;
        {
            allocation_trace::Scope trace;
            assert(core::handler::buildSequencerStepPasteMutationPlan(
                       sequencer,
                       clipboard,
                       makeSequencerPreparedStepPasteTarget(
                           0U,
                           project::ProjectStepPasteMode::EXTEND,
                           0U),
                       plan) == Preflight::Rejected);
            assert(allocation_trace::count == 0U);
        }
        assert(sequencer.pattern.stepDataRevision.get() == stepRevision);
        assert(sequencer.pattern.graphRevision.get() == graphRevision);
        assert(sequencer.pattern.ccLaneRevision.get() == ccRevision);
        assert(sequencer.pattern.patternTimingRevision.get() ==
               timingRevision);
        assert(sequencer.pattern.note[0U] == note);
        assert(sequencer.pattern.graph == nullptr);
    }

    std::cout << "[PASS] Graph copy charge is aggregate/exact and malformed-safe\n";
}

struct CoreHarness {
    test_support::CoreStorages storages;
    core::state::CoreState state;
    Services history;

    CoreHarness()
        : state(storages.settings),
          history(Services::fromCoreState(state)) {
        state.sequencer.pattern.setContentLength(8U);
        assert(seq::initializeTrackBankFromActive(
            state.sequencerTracks, state.sequencer));
        settle();
    }

    void settle() {
        test_support::drainNotifications();
        state.flushProjectMutationCoalescing();
        test_support::drainNotifications();
        state.flushProjectMutationCoalescing();
        state.acknowledgeProjectSessionSave(
            state.project.metadata.modifiedCounter);
    }

    void synchronizeActiveTrack() {
        assert(seq::storeActiveTrack(
            state.sequencerTracks, state.sequencer));
        settle();
    }
};

void test_page_selection_stale_and_aggregate_capacity_fail_atomically() {
    {
        CoreHarness harness;
        auto& sequencer = harness.state.sequencer;
        setLength(sequencer, 8U);
        harness.synchronizeActiveTrack();

        core::state::StructureClipboardState clipboard;
        fillPageSelectionClipboard(clipboard);
        const auto invariantBefore = tx::captureStateInvariant(harness.state);
        seq::SequencerHistoryPatternSnapshot musicalBefore;
        tx::captureMusicalSnapshot(harness.state, musicalBefore);

        Transaction transaction(
            sequencer, harness.history, Action::PageSelectionPaste);
        assert(transaction.openBoundary());
        MutationPlan plan;
        assertReady(
            core::handler::buildSequencerPageSelectionPasteMutationPlan(
                sequencer, clipboard,
                makeSequencerPreparedPageStructureTarget(0U, 0U), plan),
            plan,
            Action::PageSelectionPaste);
        clipboard.revision.set(clipboard.revision.get() + 1U);
        assert(transaction.execute(
                   core::handler::makeSequencerPreparedPageStructureExecution(
                       plan)) == Result::Failed);

        tx::assertMusicalSnapshot(harness.state, musicalBefore);
        tx::assertStateInvariant(harness.state, invariantBefore);
        assert(harness.state.sequencerHistory.undoCount() == 0U);
        assert(!harness.state.hasPendingSequencerPatternHistoryCoalescing());
    }

    {
        CoreHarness harness;
        auto& sequencer = harness.state.sequencer;
        setLength(sequencer, seq::SequencerState::MAX_STEPS);
        assert(seq::ensureGraphRoot(sequencer.pattern));

        for (uint16_t root = 0U;
             root < seq::SequencerState::MAX_STEPS &&
             sequencer.pattern.graph->stepNodeCount <
                 GraphLimits::MAX_STEP_NODES -
                     GraphLimits::MAX_EXPANDED_NOTES_PER_ROOT_STEP;
             ++root) {
            const bool destinationRoot =
                (root >= 32U && root < 40U) ||
                (root >= 48U && root < 56U);
            if (destinationRoot) continue;
            const auto created = seq::createMicroSequence(
                sequencer.pattern,
                seq::rootStepNodeId(static_cast<uint8_t>(root)),
                2U);
            assert(created.ok);
        }
        assert(sequencer.pattern.graph->stepNodeCount ==
               GraphLimits::MAX_STEP_NODES -
                   GraphLimits::MAX_EXPANDED_NOTES_PER_ROOT_STEP);

        core::state::StructureClipboardState clipboard;
        fillPageSelectionClipboard(clipboard);
        seq::SequencerPatternState source;
        const auto first = seq::createMicroSequence(
            source, seq::rootStepNodeId(8U), 2U);
        const auto second = seq::createMicroSequence(
            source, seq::rootStepNodeId(24U), 2U);
        assert(first.ok && second.ok);
        clipboard.sequencerGraph = std::move(source.graph);
        harness.synchronizeActiveTrack();

        const auto invariantBefore = tx::captureStateInvariant(harness.state);
        seq::SequencerHistoryPatternSnapshot musicalBefore;
        tx::captureMusicalSnapshot(harness.state, musicalBefore);
        const uint64_t graphHashBefore = byteHash(
            sequencer.pattern.graph.get(), sizeof(Graph));

        Transaction transaction(
            sequencer, harness.history, Action::PageSelectionPaste);
        assert(transaction.openBoundary());
        MutationPlan plan;
        assertReady(
            core::handler::buildSequencerPageSelectionPasteMutationPlan(
                sequencer, clipboard,
                makeSequencerPreparedPageStructureTarget(0U, 4U), plan),
            plan,
            Action::PageSelectionPaste);
        assert(plan.graphBudget.stepNodes ==
               2U * GraphLimits::MAX_EXPANDED_NOTES_PER_ROOT_STEP);
        assert(plan.graphBudget.sequences == 2U);
        assert(plan.graphBudget.cycleSets == 0U);
        assert(plan.payloadPlan == PayloadPlan::FullCurrentPayload);
        assert(plan.compactGraphOnSeal());
        assert(transaction.execute(
                   core::handler::makeSequencerPreparedPageStructureExecution(
                       plan)) == Result::Failed);

        assert(sequencer.pattern.graph != nullptr);
        assert(byteHash(sequencer.pattern.graph.get(), sizeof(Graph)) ==
               graphHashBefore);
        tx::assertMusicalSnapshot(harness.state, musicalBefore);
        tx::assertStateInvariant(harness.state, invariantBefore);
        assert(harness.state.sequencerHistory.undoCount() == 0U);
        assert(!harness.state.hasPendingSequencerPatternHistoryCoalescing());
    }

    std::cout
        << "[PASS] PageSelectionPaste stale/capacity failures are atomic\n";
}

void test_graph_paste_releases_canonical_default_targets_idempotently() {
    seq::SequencerState sequencer;
    setLength(sequencer, 8U);
    assert(seq::ensureGraphRoot(sequencer.pattern));
    const uint32_t revisionBefore = sequencer.pattern.graphRevision.get();

    core::state::StructureClipboardState clipboard;
    fillStepsClipboard(clipboard);
    attachScalarSourceGraph(clipboard);
    MutationPlan plan;
    assertReady(
        core::handler::buildSequencerStepPasteMutationPlan(
            sequencer,
            clipboard,
            makeSequencerPreparedStepPasteTarget(
                0U,
                project::ProjectStepPasteMode::EXTEND,
                0U),
            plan),
        plan,
        Action::StepPaste);
    assert(plan.payloadPlan == PayloadPlan::FullCurrentPayload);
    const Execution execution =
        core::handler::makeSequencerPreparedPageStructureExecution(plan);
    assert(execution.revalidate(execution.mutationContext, sequencer));
    assert(execution.mutate(
               execution.mutationContext, sequencer, Services{}) ==
           MutationOutcome::Changed);

    const auto* graph = seq::graphView(sequencer.pattern);
    assert(graph != nullptr);
    assert(graph->stepNodes[0U].noteOffset == 5);
    assert(sequencer.pattern.graphRevision.get() == revisionBefore + 1U);

    std::cout << "[PASS] enabled/default Graph targets release idempotently\n";
}

void test_mixed_graph_paste_skips_semantically_identical_payloads() {
    seq::SequencerState sequencer;
    setLength(sequencer, 8U);
    core::state::StructureClipboardState clipboard;
    fillStepsClipboard(clipboard, 2U);
    attachMicroSequenceSourceGraph(clipboard, 2U);
    assert(seq::copyStepNodePayloadFromGraph(
        sequencer.pattern,
        seq::rootStepNodeId(0U),
        *clipboard.sequencerGraph,
        seq::rootStepNodeId(0U)));
    const auto* graphBefore = seq::graphView(sequencer.pattern);
    assert(graphBefore != nullptr);
    assert(graphBefore->sequenceCount == 2U);
    assert(graphBefore->stepNodeCount == 144U);

    MutationPlan plan;
    assertReady(
        core::handler::buildSequencerStepPasteMutationPlan(
            sequencer,
            clipboard,
            makeSequencerPreparedStepPasteTarget(
                0U,
                project::ProjectStepPasteMode::EXTEND,
                0U),
            plan),
        plan,
        Action::StepPaste);
    assert(plan.graphBudget.stepNodes == 16U);
    assert(plan.graphBudget.sequences == 1U);
    assert(plan.compactGraphOnSeal());
    const Execution execution =
        core::handler::makeSequencerPreparedPageStructureExecution(plan);
    assert(execution.revalidate(execution.mutationContext, sequencer));
    const Services history = unchangedPrecompactionServices();
    assert(execution.mutate(
               execution.mutationContext, sequencer, history) ==
           MutationOutcome::Changed);

    const auto* graphAfter = seq::graphView(sequencer.pattern);
    assert(graphAfter != nullptr);
    assert(graphAfter->sequenceCount == 3U);
    assert(graphAfter->stepNodeCount == 160U);
    assert(seq::validInitializedSequencerGraph(*graphAfter));
    for (uint8_t step = 0U; step < 2U; ++step) {
        const auto comparison = seq::compareSequencerGraphPayloads(
            *graphAfter,
            seq::rootStepNodeId(step),
            *clipboard.sequencerGraph,
            seq::rootStepNodeId(step),
            0U);
        assert(comparison.ok());
        assert(comparison.same);
    }

    std::cout << "[PASS] mixed Graph paste skips already-identical children\n";
}

struct FailingCommitHistory {
    core::state::CoreState* state = nullptr;
    std::size_t commitCount = 0U;
    std::size_t abortCount = 0U;

    static seq::SequencerPatternHistoryCommitOutcome boundary(void* context) {
        auto& self = *static_cast<FailingCommitHistory*>(context);
        return self.state->commitSequencerPatternHistoryCoalescingOutcome();
    }

    static seq::SequencerPreparedPatternEditBeginOutcome begin(
        void* context,
        Owner owner,
        uint8_t key,
        PayloadPlan plan,
        seq::SequencerHistoryDescriptor descriptor,
        bool compactGraphOnSeal
    ) {
        auto& self = *static_cast<FailingCommitHistory*>(context);
        return self.state->beginOrContinueSequencerPreparedPatternEdit(
            owner, key, plan, descriptor, compactGraphOnSeal);
    }

    static bool ready(
        void* context,
        Owner owner,
        uint8_t key,
        uint8_t expectedTrack
    ) {
        auto& self = *static_cast<FailingCommitHistory*>(context);
        return self.state->sequencerPreparedPatternEditReady(
            owner, key, expectedTrack);
    }

    static seq::SequencerPreparedPatternEditSealOutcome seal(
        void* context,
        Owner owner,
        uint8_t key,
        bool changed,
        seq::SequencerHistoryDescriptor descriptor
    ) {
        auto& self = *static_cast<FailingCommitHistory*>(context);
        return self.state->sealSequencerPreparedPatternEdit(
            owner, key, changed, descriptor);
    }

    static seq::SequencerPreparedPatternEditCommitOutcome commit(
        void* context,
        Owner
    ) {
        auto& self = *static_cast<FailingCommitHistory*>(context);
        ++self.commitCount;
        return seq::SequencerPreparedPatternEditCommitOutcome::Failed;
    }

    static seq::SequencerPreparedPatternEditAbortOutcome abort(
        void* context,
        Owner owner,
        uint8_t key
    ) {
        auto& self = *static_cast<FailingCommitHistory*>(context);
        ++self.abortCount;
        return self.state->abortSequencerPreparedPatternEdit(owner, key);
    }
};

constexpr Services::Operations kFailingCommitHistoryOperations{
    .commitCoalescedPatternEdit = &FailingCommitHistory::boundary,
    .beginPreparedPatternEdit = &FailingCommitHistory::begin,
    .preparedPatternEditReady = &FailingCommitHistory::ready,
    .sealPreparedPatternEdit = &FailingCommitHistory::seal,
    .commitPreparedPatternEdit = &FailingCommitHistory::commit,
    .abortPreparedPatternEdit = &FailingCommitHistory::abort,
};

Services failingCommitServices(FailingCommitHistory& history) {
    return Services::fromStaticOperations<
        kFailingCommitHistoryOperations>(&history);
}

void runChildOffsetExtensionCase(
    bool cycleStates,
    int8_t offset,
    project::ProjectStepPasteMode mode,
    uint8_t cursor,
    uint8_t expectedLength
) {
    CoreHarness harness;
    auto& sequencer = harness.state.sequencer;
    constexpr uint8_t oldLength = 4U;
    uint16_t containerId = GraphLimits::INVALID_ID;
    if (cycleStates) {
        const auto created = seq::createCycleStateSet(
            sequencer.pattern, seq::rootStepNodeId(0U), oldLength);
        assert(created.ok);
        containerId = created.id;
        assert(seq::setCycleStateSetOffset(
            sequencer.pattern, containerId, offset));
        assert(seq::enterCycleStatesContentView(
            sequencer, seq::rootStepNodeId(0U), containerId));
    } else {
        const auto created = seq::createMicroSequence(
            sequencer.pattern, seq::rootStepNodeId(0U), oldLength);
        assert(created.ok);
        containerId = created.id;
        assert(seq::setMicroSequenceOffset(
            sequencer.pattern, containerId, offset));
        assert(seq::enterMicroSequenceContentView(
            sequencer, seq::rootStepNodeId(0U), containerId));
    }

    for (uint8_t logical = 0U; logical < oldLength; ++logical) {
        const uint16_t node = seq::activeContentStepNodeId(
            sequencer, logical);
        assert(node != GraphLimits::INVALID_ID);
        assert(seq::setNodeNoteOffset(
            sequencer.pattern,
            node,
            static_cast<int8_t>(logical + 1U)));
    }
    const uint16_t nestedOwner = seq::activeContentStepNodeId(
        sequencer, cycleStates ? 1U : 2U);
    if (cycleStates) {
        assert(seq::createMicroSequence(
            sequencer.pattern, nestedOwner, 2U).ok);
    } else {
        assert(seq::createCycleStateSet(
            sequencer.pattern, nestedOwner, 2U).ok);
    }
    harness.synchronizeActiveTrack();

    auto* graph = sequencer.pattern.graph.get();
    assert(graph != nullptr);
    const void* const graphOwner = graph;
    const uint64_t beforeHash = byteHash(graph, sizeof(Graph));
    const uint32_t revisionBefore =
        sequencer.pattern.graphRevision.get();
    std::array<GraphNode, oldLength> oldLogical{};
    for (uint8_t logical = 0U; logical < oldLength; ++logical) {
        const uint16_t node = seq::activeContentStepNodeId(
            sequencer, logical);
        assert(node != GraphLimits::INVALID_ID);
        oldLogical[logical] = graph->stepNodes[node];
    }

    core::state::StructureClipboardState clipboard;
    fillChildScalarClipboard(clipboard);
    Transaction transaction(sequencer, harness.history, Action::StepPaste);
    assert(transaction.openBoundary());
    MutationPlan plan;
    assertReady(
        core::handler::buildSequencerStepPasteMutationPlan(
            sequencer,
            clipboard,
            makeSequencerPreparedStepPasteTarget(0U, mode, cursor),
            plan),
        plan,
        Action::StepPaste);
    assert(plan.context() == (cycleStates
        ? core::handler::
              SequencerPreparedPageStructureContentContext::CycleStates
        : core::handler::
              SequencerPreparedPageStructureContentContext::MicroSequence));
    assert(plan.contentLength == oldLength);
    assert(plan.resultingContentLength == expectedLength);
    assert(plan.payloadPlan == PayloadPlan::FullCurrentPayload);
    assert(!plan.compactGraphOnSeal());
    assert(transaction.execute(
               core::handler::makeSequencerPreparedPageStructureExecution(
                   plan)) == Result::Committed);

    assert(sequencer.pattern.graph.get() == graphOwner);
    assert(seq::activeContentLength(sequencer) == expectedLength);
    assert(sequencer.pattern.graphRevision.get() == revisionBefore + 1U);
    graph = sequencer.pattern.graph.get();
    for (uint8_t logical = 0U; logical < oldLength; ++logical) {
        const uint16_t node = seq::activeContentStepNodeId(
            sequencer, logical);
        assert(node != GraphLimits::INVALID_ID);
        assert(sameStoredNode(graph->stepNodes[node], oldLogical[logical]));
    }
    GraphNode disabled{};
    disabled.flags = oc::note::sequencer::STEP_NODE_ENABLED_OVERRIDE;
    for (uint8_t logical = oldLength; logical < expectedLength; ++logical) {
        const uint16_t node = seq::activeContentStepNodeId(
            sequencer, logical);
        assert(node != GraphLimits::INVALID_ID);
        if (logical == cursor) {
            assert(graph->stepNodes[node].noteOffset == 9);
            assert(graph->stepNodes[node].has(
                oc::note::sequencer::STEP_NODE_NOTE_OFFSET));
        } else {
            assert(sameStoredNode(graph->stepNodes[node], disabled));
        }
    }
    const uint64_t afterHash = byteHash(graph, sizeof(Graph));

    assert(harness.state.undoSequencerHistory());
    assert(sequencer.pattern.graph != nullptr);
    assert(seq::activeContentLength(sequencer) == oldLength);
    assert(byteHash(sequencer.pattern.graph.get(), sizeof(Graph)) ==
           beforeHash);
    assert(harness.state.redoSequencerHistory());
    assert(sequencer.pattern.graph != nullptr);
    assert(seq::activeContentLength(sequencer) == expectedLength);
    assert(byteHash(sequencer.pattern.graph.get(), sizeof(Graph)) ==
           afterHash);
}

void test_child_extensions_preserve_logical_content_across_offsets() {
    runChildOffsetExtensionCase(
        false,
        3,
        project::ProjectStepPasteMode::EXTEND,
        4U,
        5U);
    runChildOffsetExtensionCase(
        true,
        -1,
        project::ProjectStepPasteMode::PAGE,
        8U,
        16U);

    std::cout << "[PASS] child EXTEND/PAGE preserve logical payloads at +/- offsets\n";
}

void runChildOffsetReplacementAndExtensionCase(
    bool cycleStates,
    int8_t offset,
    project::ProjectStepPasteMode mode,
    uint8_t secondOffset,
    uint8_t expectedLength
) {
    CoreHarness harness;
    auto& sequencer = harness.state.sequencer;
    constexpr uint8_t oldLength = 4U;
    if (cycleStates) {
        const auto created = seq::createCycleStateSet(
            sequencer.pattern, seq::rootStepNodeId(0U), oldLength);
        assert(created.ok);
        assert(seq::setCycleStateSetOffset(
            sequencer.pattern, created.id, offset));
        assert(seq::enterCycleStatesContentView(
            sequencer, seq::rootStepNodeId(0U), created.id));
    } else {
        const auto created = seq::createMicroSequence(
            sequencer.pattern, seq::rootStepNodeId(0U), oldLength);
        assert(created.ok);
        assert(seq::setMicroSequenceOffset(
            sequencer.pattern, created.id, offset));
        assert(seq::enterMicroSequenceContentView(
            sequencer, seq::rootStepNodeId(0U), created.id));
    }
    for (uint8_t logical = 0U; logical < oldLength; ++logical) {
        assert(seq::setNodeNoteOffset(
            sequencer.pattern,
            seq::activeContentStepNodeId(sequencer, logical),
            static_cast<int8_t>(logical + 1U)));
    }
    const uint16_t replacedNode = seq::activeContentStepNodeId(
        sequencer, 0U);
    if (cycleStates) {
        assert(seq::createMicroSequence(
            sequencer.pattern, replacedNode, 2U).ok);
    } else {
        assert(seq::createCycleStateSet(
            sequencer.pattern, replacedNode, 2U).ok);
    }
    harness.synchronizeActiveTrack();

    const uint64_t beforeHash = byteHash(
        sequencer.pattern.graph.get(), sizeof(Graph));
    std::array<GraphNode, oldLength - 1U> untouched{};
    for (uint8_t logical = 1U; logical < oldLength; ++logical) {
        untouched[logical - 1U] = sequencer.pattern.graph->stepNodes[
            seq::activeContentStepNodeId(sequencer, logical)];
    }

    core::state::StructureClipboardState clipboard;
    fillSparseChildClipboard(clipboard, secondOffset);
    Transaction transaction(sequencer, harness.history, Action::StepPaste);
    assert(transaction.openBoundary());
    MutationPlan plan;
    assertReady(
        core::handler::buildSequencerStepPasteMutationPlan(
            sequencer,
            clipboard,
            makeSequencerPreparedStepPasteTarget(0U, mode, 0U),
            plan),
        plan,
        Action::StepPaste);
    assert(plan.resultingContentLength == expectedLength);
    assert(plan.graphBudget.stepNodes == 0U);
    assert(plan.compactGraphOnSeal());
    assert(transaction.execute(
               core::handler::makeSequencerPreparedPageStructureExecution(
                   plan)) == Result::Committed);

    assert(seq::activeContentLength(sequencer) == expectedLength);
    assert(seq::validInitializedSequencerGraph(
        *sequencer.pattern.graph));
    const auto& replaced = sequencer.pattern.graph->stepNodes[
        seq::activeContentStepNodeId(sequencer, 0U)];
    assert(replaced.noteOffset == 50);
    assert(!replaced.has(oc::note::sequencer::STEP_NODE_CHILD_SEQUENCE));
    assert(!replaced.has(oc::note::sequencer::STEP_NODE_CYCLE_SET));
    const auto& appended = sequencer.pattern.graph->stepNodes[
        seq::activeContentStepNodeId(sequencer, secondOffset)];
    assert(appended.noteOffset == 51);
    for (uint8_t logical = 1U; logical < oldLength; ++logical) {
        const auto& actual = sequencer.pattern.graph->stepNodes[
            seq::activeContentStepNodeId(sequencer, logical)];
        assert(sameStoredNode(actual, untouched[logical - 1U]));
    }
    const uint64_t afterHash = byteHash(
        sequencer.pattern.graph.get(), sizeof(Graph));

    assert(harness.state.undoSequencerHistory());
    assert(seq::activeContentLength(sequencer) == oldLength);
    assert(byteHash(sequencer.pattern.graph.get(), sizeof(Graph)) ==
           beforeHash);
    assert(harness.state.redoSequencerHistory());
    assert(seq::activeContentLength(sequencer) == expectedLength);
    assert(byteHash(sequencer.pattern.graph.get(), sizeof(Graph)) ==
           afterHash);
}

void test_child_resize_analysis_uses_old_logical_projection() {
    runChildOffsetReplacementAndExtensionCase(
        false,
        1,
        project::ProjectStepPasteMode::EXTEND,
        4U,
        5U);
    runChildOffsetReplacementAndExtensionCase(
        true,
        -1,
        project::ProjectStepPasteMode::PAGE,
        8U,
        16U);

    std::cout << "[PASS] child resize analyzes/reclaims existing logical targets exactly\n";
}

void test_child_offset_extension_commit_failure_rolls_back_exactly() {
    CoreHarness harness;
    auto& sequencer = harness.state.sequencer;
    const auto created = seq::createMicroSequence(
        sequencer.pattern, seq::rootStepNodeId(0U), 4U);
    assert(created.ok);
    assert(seq::setMicroSequenceOffset(
        sequencer.pattern, created.id, -2));
    assert(seq::enterMicroSequenceContentView(
        sequencer, seq::rootStepNodeId(0U), created.id));
    for (uint8_t logical = 0U; logical < 4U; ++logical) {
        assert(seq::setNodeNoteOffset(
            sequencer.pattern,
            seq::activeContentStepNodeId(sequencer, logical),
            static_cast<int8_t>(logical + 11U)));
    }
    auto* const emptyCc = seq::ensureSequencerCcLaneBank(sequencer.pattern);
    assert(emptyCc != nullptr);
    emptyCc->revision = 0xA5A55A5AU;
    assert(seq::sequencerCcLaneCount(*emptyCc) == 0U);
    harness.synchronizeActiveTrack();

    const auto invariantBefore = tx::captureStateInvariant(harness.state);
    const void* const graphOwner = sequencer.pattern.graph.get();
    const void* const ccOwner = sequencer.pattern.ccLanes.get();
    const uint64_t graphHash = byteHash(
        sequencer.pattern.graph.get(), sizeof(Graph));
    const uint64_t ccHash = byteHash(
        sequencer.pattern.ccLanes.get(), sizeof(*sequencer.pattern.ccLanes));
    const uint32_t ccRevision = sequencer.pattern.ccLaneRevision.get();
    const uint32_t viewRevision = sequencer.contentView.revision.get();
    const uint8_t focus = sequencer.focusedStep.get();
    const uint8_t page = sequencer.page.get();

    core::state::StructureClipboardState clipboard;
    fillChildScalarClipboard(clipboard);
    FailingCommitHistory probe{.state = &harness.state};
    const Services history = failingCommitServices(probe);
    Transaction transaction(sequencer, history, Action::StepPaste);
    assert(transaction.openBoundary());
    MutationPlan plan;
    assertReady(
        core::handler::buildSequencerStepPasteMutationPlan(
            sequencer,
            clipboard,
            makeSequencerPreparedStepPasteTarget(
                0U,
                project::ProjectStepPasteMode::PAGE,
                8U),
            plan),
        plan,
        Action::StepPaste);
    assert(transaction.execute(
               core::handler::makeSequencerPreparedPageStructureExecution(
                   plan)) == Result::Failed);

    assert(probe.commitCount == 1U);
    assert(probe.abortCount == 1U);
    assert(sequencer.pattern.graph.get() == graphOwner);
    assert(byteHash(sequencer.pattern.graph.get(), sizeof(Graph)) == graphHash);
    assert(sequencer.pattern.ccLanes.get() == ccOwner);
    assert(byteHash(
               sequencer.pattern.ccLanes.get(),
               sizeof(*sequencer.pattern.ccLanes)) == ccHash);
    assert(sequencer.pattern.ccLaneRevision.get() == ccRevision);
    assert(seq::activeContentLength(sequencer) == 4U);
    assert(sequencer.contentView.revision.get() == viewRevision);
    assert(sequencer.focusedStep.get() == focus);
    assert(sequencer.page.get() == page);
    tx::assertStateInvariant(harness.state, invariantBefore);
    assert(!harness.state.hasPendingSequencerPatternHistoryCoalescing());

    std::cout << "[PASS] child offset extension abort restores exact Before state\n";
}

void test_root_extension_keeps_cold_cc_exact_under_flat_history() {
    CoreHarness harness;
    auto& pattern = harness.state.sequencer.pattern;
    auto* editorCc = seq::ensureSequencerCcLaneBank(pattern);
    assert(editorCc != nullptr);
    seq::SequencerCcLaneDraft draft{};
    draft.destination.controller = 74U;
    assert(seq::createSequencerCcLane(*editorCc, 0U, draft).changed());
    assert(seq::setSequencerCcLaneEvent(
        *editorCc, 0U, 127U, 101U).changed());
    pattern.bumpCcLaneRevision();
    harness.synchronizeActiveTrack();

    auto& bankPattern = harness.state.sequencerTracks.track(0U);
    assert(bankPattern.ccLanes != nullptr);
    const void* const editorOwner = pattern.ccLanes.get();
    const void* const bankOwner = bankPattern.ccLanes.get();
    const uint32_t editorRevision = pattern.ccLaneRevision.get();
    const uint32_t bankRevision = bankPattern.ccLaneRevision.get();
    const uint64_t editorHash = byteHash(editorCc, sizeof(*editorCc));
    const uint64_t bankHash = byteHash(
        bankPattern.ccLanes.get(), sizeof(*bankPattern.ccLanes));

    auto assertColdCcExact = [&]() {
        assert(pattern.ccLanes.get() == editorOwner);
        assert(bankPattern.ccLanes.get() == bankOwner);
        assert(pattern.ccLaneRevision.get() == editorRevision);
        assert(bankPattern.ccLaneRevision.get() == bankRevision);
        assert(byteHash(pattern.ccLanes.get(), sizeof(*pattern.ccLanes)) ==
               editorHash);
        assert(byteHash(
                   bankPattern.ccLanes.get(),
                   sizeof(*bankPattern.ccLanes)) == bankHash);
        assert(pattern.ccLanes->lanes[0U].activeMask.test(127U));
        assert(pattern.ccLanes->lanes[0U].values[127U] == 101U);
    };

    Transaction transaction(
        harness.state.sequencer, harness.history, Action::PageCreate);
    assert(transaction.openBoundary());
    MutationPlan plan;
    assertReady(
        core::handler::buildSequencerPageCreateMutationPlan(
            harness.state.sequencer, 0U, 1U, plan),
        plan,
        Action::PageCreate);
    assert(plan.payloadPlan == PayloadPlan::FlatOnly);
    assert(transaction.execute(
               core::handler::makeSequencerPreparedPageStructureExecution(
                   plan)) == Result::Committed);
    assert(pattern.length.get() == 16U);
    assertColdCcExact();

    assert(harness.state.undoSequencerHistory());
    assert(pattern.length.get() == 8U);
    assertColdCcExact();
    assert(harness.state.redoSequencerHistory());
    assert(pattern.length.get() == 16U);
    assertColdCcExact();

    std::cout << "[PASS] root extension keeps cold CC byte/pointer exact via FlatOnly\n";
}

void test_page_delete_keeps_empty_cc_owner_exact_under_flat_history() {
    CoreHarness harness;
    auto& sequencer = harness.state.sequencer;
    setLength(sequencer, 16U);
    auto* const emptyCc = seq::ensureSequencerCcLaneBank(sequencer.pattern);
    assert(emptyCc != nullptr);
    assert(seq::sequencerCcLaneCount(*emptyCc) == 0U);
    harness.synchronizeActiveTrack();

    auto& pattern = sequencer.pattern;
    auto& bankPattern = harness.state.sequencerTracks.track(0U);
    const void* const editorOwner = pattern.ccLanes.get();
    const uint32_t editorRevision = pattern.ccLaneRevision.get();
    const uint32_t bankRevision = bankPattern.ccLaneRevision.get();
    const uint64_t editorHash = byteHash(emptyCc, sizeof(*emptyCc));
    assert(editorOwner != nullptr);
    assert(bankPattern.ccLanes == nullptr);

    auto assertEmptyCcExact = [&]() {
        assert(pattern.ccLanes.get() == editorOwner);
        assert(seq::sequencerCcLaneCount(*pattern.ccLanes) == 0U);
        assert(byteHash(pattern.ccLanes.get(), sizeof(*pattern.ccLanes)) ==
               editorHash);
        assert(pattern.ccLaneRevision.get() == editorRevision);
        assert(bankPattern.ccLanes == nullptr);
        assert(bankPattern.ccLaneRevision.get() == bankRevision);
    };

    Transaction transaction(sequencer, harness.history, Action::PageDelete);
    assert(transaction.openBoundary());
    MutationPlan plan;
    assertReady(
        core::handler::buildSequencerPageDeleteMutationPlan(
            sequencer, 0U, 0U, plan),
        plan,
        Action::PageDelete);
    assert(plan.payloadPlan == PayloadPlan::FlatOnly);
    assert(transaction.execute(
               core::handler::makeSequencerPreparedPageStructureExecution(
                   plan)) == Result::Committed);
    assert(pattern.length.get() == 8U);
    assertEmptyCcExact();

    assert(harness.state.undoSequencerHistory());
    assert(pattern.length.get() == 16U);
    assertEmptyCcExact();
    assert(harness.state.redoSequencerHistory());
    assert(pattern.length.get() == 8U);
    assertEmptyCcExact();

    std::cout << "[PASS] PageDelete keeps empty CC owner exact via FlatOnly\n";
}

void test_full_graph_page_history_preserves_empty_cc_owners() {
    CoreHarness harness;
    auto& sequencer = harness.state.sequencer;
    auto& pattern = sequencer.pattern;
    assert(seq::ensureGraphRoot(pattern));
    assert(seq::setNodeNoteOffset(
        pattern, seq::rootStepNodeId(0U), 7));

    auto* const editorCc = seq::ensureSequencerCcLaneBank(pattern);
    assert(editorCc != nullptr);
    editorCc->revision = 0x12345678U;
    assert(seq::sequencerCcLaneCount(*editorCc) == 0U);
    harness.synchronizeActiveTrack();

    auto& bankPattern = harness.state.sequencerTracks.track(0U);
    bankPattern.ccLanes =
        core::app::makeExtmemUnique<seq::SequencerCcLaneBank>();
    assert(bankPattern.ccLanes != nullptr);
    bankPattern.ccLanes->revision = 0x87654321U;
    assert(seq::sequencerCcLaneCount(*bankPattern.ccLanes) == 0U);

    const void* const editorOwner = pattern.ccLanes.get();
    const void* const bankOwner = bankPattern.ccLanes.get();
    const uint64_t editorHash = byteHash(editorOwner, sizeof(*editorCc));
    const uint64_t bankHash = byteHash(
        bankOwner, sizeof(*bankPattern.ccLanes));
    const uint32_t editorRevision = pattern.ccLaneRevision.get();
    const uint32_t bankRevision = bankPattern.ccLaneRevision.get();

    auto assertEmptyOwnersExact = [&]() {
        assert(pattern.ccLanes.get() == editorOwner);
        assert(bankPattern.ccLanes.get() == bankOwner);
        assert(byteHash(pattern.ccLanes.get(), sizeof(*pattern.ccLanes)) ==
               editorHash);
        assert(byteHash(
                   bankPattern.ccLanes.get(), sizeof(*bankPattern.ccLanes)) ==
               bankHash);
        assert(pattern.ccLaneRevision.get() == editorRevision);
        assert(bankPattern.ccLaneRevision.get() == bankRevision);
    };

    Transaction transaction(sequencer, harness.history, Action::PageClear);
    assert(transaction.openBoundary());
    MutationPlan plan;
    assertReady(
        core::handler::buildSequencerPageClearMutationPlan(
            sequencer, 0U, 0U, plan),
        plan,
        Action::PageClear);
    assert(plan.payloadPlan == PayloadPlan::FullCurrentPayload);
    assert(transaction.execute(
               core::handler::makeSequencerPreparedPageStructureExecution(
                   plan)) == Result::Committed);
    assertEmptyOwnersExact();

    assert(harness.state.undoSequencerHistory());
    assertEmptyOwnersExact();
    assert(harness.state.redoSequencerHistory());
    assertEmptyOwnersExact();

    std::cout <<
        "[PASS] Full Page Graph history preserves empty CC owners exactly\n";
}

void test_page_extension_reclaims_matching_cold_descendants_near_capacity() {
    CoreHarness harness;
    auto& sequencer = harness.state.sequencer;
    const auto cold = seq::createMicroSequence(
        sequencer.pattern, seq::rootStepNodeId(8U), 4U);
    assert(cold.ok);
    const auto* graph = seq::graphView(sequencer.pattern);
    assert(graph != nullptr);
    const auto* coldSequence = graph->sequence(cold.id);
    assert(coldSequence != nullptr);
    assert(seq::setNodeNoteOffset(
        sequencer.pattern, coldSequence->firstStepNode, 1));
    saturateMicroSequenceCapacityOutsideSpan(
        sequencer.pattern, 8U, 16U);
    harness.synchronizeActiveTrack();

    core::state::StructureClipboardState clipboard;
    fillPageClipboard(clipboard);
    attachMicroSequenceSourceGraph(clipboard, 1U);
    sequencer.structureUi.previewAddPageSlot.set(true);
    sequencer.structureUi.previewPageIndex.set(1U);

    const uint64_t beforeHash = byteHash(
        sequencer.pattern.graph.get(), sizeof(Graph));
    Transaction transaction(sequencer, harness.history, Action::PagePaste);
    assert(transaction.openBoundary());
    MutationPlan plan;
    assertReady(
        core::handler::buildSequencerPagePasteMutationPlan(
            sequencer, clipboard,
            makeSequencerPreparedPageStructureTarget(0U, 1U), plan),
        plan,
        Action::PagePaste);
    assert(plan.resultingContentLength == 16U);
    assert(plan.payloadPlan == PayloadPlan::FullCurrentPayload);
    assert(plan.compactGraphOnSeal());
    assert(plan.graphBudget.stepNodes != 0U);
    assert(plan.graphBudget.sequences != 0U);
    assert(transaction.execute(
               core::handler::makeSequencerPreparedPageStructureExecution(
                   plan)) == Result::Committed);

    graph = seq::graphView(sequencer.pattern);
    assert(graph != nullptr);
    assert(seq::validInitializedSequencerGraph(*graph));
    const auto comparison = seq::compareSequencerGraphPayloads(
        *graph,
        seq::rootStepNodeId(8U),
        *clipboard.sequencerGraph,
        seq::rootStepNodeId(0U),
        0U);
    assert(comparison.ok());
    assert(comparison.same);
    const uint64_t afterHash = byteHash(graph, sizeof(Graph));

    assert(harness.state.undoSequencerHistory());
    assert(sequencer.pattern.length.get() == 8U);
    assert(byteHash(sequencer.pattern.graph.get(), sizeof(Graph)) ==
           beforeHash);
    assert(harness.state.redoSequencerHistory());
    assert(sequencer.pattern.length.get() == 16U);
    assert(byteHash(sequencer.pattern.graph.get(), sizeof(Graph)) ==
           afterHash);

    std::cout << "[PASS] Page extension reclaims/rebuilds matching cold descendants\n";
}

void test_step_extension_capacity_failure_restores_matching_cold_target() {
    CoreHarness harness;
    auto& sequencer = harness.state.sequencer;
    const auto cold = seq::createMicroSequence(
        sequencer.pattern, seq::rootStepNodeId(10U), 4U);
    assert(cold.ok);
    const auto* graph = seq::graphView(sequencer.pattern);
    assert(graph != nullptr);
    const auto* coldSequence = graph->sequence(cold.id);
    assert(coldSequence != nullptr);
    assert(seq::setNodeNoteOffset(
        sequencer.pattern, coldSequence->firstStepNode, 1));
    saturateMicroSequenceCapacityOutsideSpan(
        sequencer.pattern, 8U, 12U);
    harness.synchronizeActiveTrack();

    core::state::StructureClipboardState clipboard;
    fillStepsClipboard(clipboard, 2U);
    attachMicroSequenceSourceGraph(clipboard, 2U);
    const auto invariantBefore = tx::captureStateInvariant(harness.state);
    const void* const graphOwner = sequencer.pattern.graph.get();
    const uint64_t graphHash = byteHash(graphOwner, sizeof(Graph));

    Transaction transaction(sequencer, harness.history, Action::StepPaste);
    assert(transaction.openBoundary());
    MutationPlan plan;
    assertReady(
        core::handler::buildSequencerStepPasteMutationPlan(
            sequencer,
            clipboard,
            makeSequencerPreparedStepPasteTarget(
                0U,
                project::ProjectStepPasteMode::EXTEND,
                10U),
            plan),
        plan,
        Action::StepPaste);
    assert(plan.resultingContentLength == 12U);
    assert(plan.payloadPlan == PayloadPlan::FullCurrentPayload);
    assert(plan.compactGraphOnSeal());
    assert(plan.targetToSource[10U] == 0U);
    assert(plan.targetToSource[11U] == 1U);
    assert(plan.graphBudget.sequences >= 2U);
    assert(transaction.execute(
               core::handler::makeSequencerPreparedPageStructureExecution(
                   plan)) == Result::Failed);

    assert(sequencer.pattern.graph.get() == graphOwner);
    assert(byteHash(sequencer.pattern.graph.get(), sizeof(Graph)) ==
           graphHash);
    assert(sequencer.pattern.length.get() == 8U);
    tx::assertStateInvariant(harness.state, invariantBefore);
    assert(!harness.state.hasPendingSequencerPatternHistoryCoalescing());

    std::cout << "[PASS] Step extension capacity failure restores cold Graph exactly\n";
}

void test_root_extension_from_graphless_and_disabled_destinations() {
    for (uint8_t variant = 0U; variant < 2U; ++variant) {
        const bool disabledOwner = variant != 0U;
        CoreHarness harness;
        auto& sequencer = harness.state.sequencer;
        const void* disabledOwnerAddress = nullptr;
        if (disabledOwner) {
            sequencer.pattern.graph = core::app::makeExtmemUnique<Graph>();
            assert(sequencer.pattern.graph != nullptr);
            assert(seq::isCanonicalDisabledSequencerGraph(
                *sequencer.pattern.graph));
            disabledOwnerAddress = sequencer.pattern.graph.get();
            sequencer.pattern.bumpGraphRevision();
            harness.synchronizeActiveTrack();
        }

        core::state::StructureClipboardState clipboard;
        fillStepsClipboard(clipboard);
        attachScalarSourceGraph(clipboard);
        Transaction transaction(
            sequencer, harness.history, Action::StepPaste);
        assert(transaction.openBoundary());
        MutationPlan plan;
        assertReady(
            core::handler::buildSequencerStepPasteMutationPlan(
                sequencer,
                clipboard,
                makeSequencerPreparedStepPasteTarget(
                    0U,
                    project::ProjectStepPasteMode::EXTEND,
                    8U),
                plan),
            plan,
            Action::StepPaste);
        assert(plan.resultingContentLength == 9U);
        assert(plan.payloadPlan == PayloadPlan::FullWithProspectiveGraph);
        assert(!plan.compactGraphOnSeal());
        assert(transaction.execute(
                   core::handler::
                       makeSequencerPreparedPageStructureExecution(plan)) ==
               Result::Committed);

        const auto* graph = seq::graphView(sequencer.pattern);
        assert(graph != nullptr);
        if (disabledOwner) {
            assert(sequencer.pattern.graph.get() == disabledOwnerAddress);
        }
        assert(graph->stepNodes[8U].has(
            oc::note::sequencer::STEP_NODE_NOTE_OFFSET));
        assert(graph->stepNodes[8U].noteOffset == 5);
        assert(sequencer.pattern.length.get() == 9U);
        assert(harness.state.undoSequencerHistory());
        assert(sequencer.pattern.length.get() == 8U);
        assert(seq::graphView(sequencer.pattern) == nullptr);
        assert(harness.state.redoSequencerHistory());
        assert(sequencer.pattern.length.get() == 9U);
        graph = seq::graphView(sequencer.pattern);
        assert(graph != nullptr);
        assert(graph->stepNodes[8U].noteOffset == 5);
    }

    std::cout << "[PASS] root extension enables graphless/disabled destinations exactly\n";
}

void test_prospective_graph_commit_undo_and_redo_are_exact() {
    CoreHarness harness;
    harness.state.sequencer.focusedStep.set(3U);
    core::state::StructureClipboardState clipboard;
    fillStepsClipboard(clipboard);
    attachScalarSourceGraph(clipboard);

    Transaction transaction(
        harness.state.sequencer, harness.history, Action::StepPaste);
    assert(transaction.openBoundary());
    MutationPlan plan;
    assertReady(
        core::handler::buildSequencerStepPasteMutationPlan(
            harness.state.sequencer,
            clipboard,
            makeSequencerPreparedStepPasteTarget(
                0U,
                project::ProjectStepPasteMode::EXTEND,
                0U),
            plan),
        plan,
        Action::StepPaste);
    assert(plan.payloadPlan == PayloadPlan::FullWithProspectiveGraph);
    assert(!plan.compactGraphOnSeal());
    assert(transaction.execute(
               core::handler::makeSequencerPreparedPageStructureExecution(
                   plan)) == Result::Committed);

    const auto* graph = seq::graphView(harness.state.sequencer.pattern);
    assert(graph != nullptr);
    assert(graph->stepNodes[0U].has(
        oc::note::sequencer::STEP_NODE_NOTE_OFFSET));
    assert(graph->stepNodes[0U].noteOffset == 5);
    assert(harness.state.sequencer.focusedStep.get() == 0U);
    assert(harness.state.sequencerTracks.track(0U).graph != nullptr);

    assert(harness.state.undoSequencerHistory());
    assert(harness.state.sequencer.pattern.graph == nullptr);
    assert(harness.state.sequencerTracks.track(0U).graph == nullptr);
    assert(harness.state.sequencer.focusedStep.get() == 3U);
    assert(harness.state.redoSequencerHistory());
    graph = seq::graphView(harness.state.sequencer.pattern);
    assert(graph != nullptr);
    assert(graph->stepNodes[0U].noteOffset == 5);
    assert(harness.state.sequencer.focusedStep.get() == 0U);

    std::cout << "[PASS] prospective Graph commit/undo/redo preserves ownership\n";
}

void test_page_delete_reclaims_descendants_from_cold_root_tail() {
    CoreHarness harness;
    auto& sequencer = harness.state.sequencer;
    setLength(sequencer, 16U);
    const auto coldChild = seq::createMicroSequence(
        sequencer.pattern, seq::rootStepNodeId(100U), 4U);
    assert(coldChild.ok);
    const auto* graph = seq::graphView(sequencer.pattern);
    assert(graph != nullptr);
    const auto* child = graph->sequence(coldChild.id);
    assert(child != nullptr);
    assert(seq::setNodeNoteOffset(
        sequencer.pattern, child->firstStepNode, 6));
    harness.synchronizeActiveTrack();
    const uint64_t beforeHash = byteHash(
        sequencer.pattern.graph.get(), sizeof(Graph));

    Transaction transaction(sequencer, harness.history, Action::PageDelete);
    assert(transaction.openBoundary());
    MutationPlan plan;
    assertReady(
        core::handler::buildSequencerPageDeleteMutationPlan(
            sequencer, 0U, 0U, plan),
        plan,
        Action::PageDelete);
    assert(plan.compactGraphOnSeal());
    assert(plan.payloadPlan == PayloadPlan::FullCurrentPayload);
    assert(transaction.execute(
               core::handler::makeSequencerPreparedPageStructureExecution(
                   plan)) == Result::Committed);

    assert(sequencer.pattern.length.get() == 8U);
    assert(seq::validInitializedSequencerGraph(*sequencer.pattern.graph));
    assert(sequencer.pattern.graph->stepNodeCount ==
           seq::SequencerState::MAX_STEPS);
    assert(!sequencer.pattern.graph->stepNodes[100U].has(
        oc::note::sequencer::STEP_NODE_CHILD_SEQUENCE));
    const uint64_t afterHash = byteHash(
        sequencer.pattern.graph.get(), sizeof(Graph));

    assert(harness.state.undoSequencerHistory());
    assert(sequencer.pattern.length.get() == 16U);
    assert(byteHash(sequencer.pattern.graph.get(), sizeof(Graph)) ==
           beforeHash);
    assert(harness.state.redoSequencerHistory());
    assert(sequencer.pattern.length.get() == 8U);
    assert(byteHash(sequencer.pattern.graph.get(), sizeof(Graph)) ==
           afterHash);

    std::cout << "[PASS] PageDelete compacts descendants from cold root tail\n";
}

void test_post_reclaim_capacity_failure_rolls_back_exactly() {
    CoreHarness harness;
    auto& sequencer = harness.state.sequencer;
    setLength(sequencer, seq::SequencerState::MAX_STEPS);
    const auto retained = seq::createMicroSequence(
        sequencer.pattern, seq::rootStepNodeId(127U), 4U);
    assert(retained.ok);
    harness.synchronizeActiveTrack();

    core::state::StructureClipboardState clipboard;
    fillStepsClipboard(clipboard, 24U);
    attachMicroSequenceSourceGraph(clipboard, 24U);

    const auto invariantBefore = tx::captureStateInvariant(harness.state);
    seq::SequencerHistoryPatternSnapshot musicalBefore;
    tx::captureMusicalSnapshot(harness.state, musicalBefore);
    assert(sequencer.pattern.graph);
    const uint64_t graphHashBefore = byteHash(
        sequencer.pattern.graph.get(), sizeof(Graph));

    Transaction transaction(sequencer, harness.history, Action::StepPaste);
    assert(transaction.openBoundary());
    MutationPlan plan;
    assertReady(
        core::handler::buildSequencerStepPasteMutationPlan(
            sequencer,
            clipboard,
            makeSequencerPreparedStepPasteTarget(
                0U,
                project::ProjectStepPasteMode::EXTEND,
                0U),
            plan),
        plan,
        Action::StepPaste);
    assert(plan.graphBudget.stepNodes == 384U);
    assert(plan.graphBudget.sequences == 24U);
    assert(plan.payloadPlan == PayloadPlan::FullCurrentPayload);
    assert(plan.compactGraphOnSeal());
    assert(transaction.execute(
               core::handler::makeSequencerPreparedPageStructureExecution(
                   plan)) == Result::Failed);

    assert(sequencer.pattern.graph);
    assert(byteHash(sequencer.pattern.graph.get(), sizeof(Graph)) ==
           graphHashBefore);
    tx::assertMusicalSnapshot(harness.state, musicalBefore);
    tx::assertStateInvariant(harness.state, invariantBefore);
    assert(!harness.state.hasPendingSequencerPatternHistoryCoalescing());

    std::cout << "[PASS] insufficient post-reclaim Graph capacity rolls back exactly\n";
}

struct CommitObservation {
    seq::SequencerState* sequencer = nullptr;
    uint32_t contentRevisionAtCommit = 0U;
    uint8_t focusAtCommit = 0U;
    std::size_t commitCount = 0U;

    static seq::SequencerPatternHistoryCommitOutcome boundary(void*) {
        return seq::SequencerPatternHistoryCommitOutcome::NoPending;
    }

    static seq::SequencerPreparedPatternEditBeginOutcome begin(
        void*,
        Owner,
        uint8_t,
        PayloadPlan,
        seq::SequencerHistoryDescriptor,
        bool
    ) {
        return seq::SequencerPreparedPatternEditBeginOutcome::Started;
    }

    static bool ready(void*, Owner, uint8_t, uint8_t) { return true; }

    static seq::SequencerPreparedPatternGraphPrecompactionOutcome precompact(
        void*,
        Owner,
        uint8_t,
        uint8_t,
        seq::SequencerPreparedGraphContentPath&
    ) {
        return seq::SequencerPreparedPatternGraphPrecompactionOutcome::Unchanged;
    }

    static seq::SequencerPreparedPatternEditSealOutcome seal(
        void*,
        Owner,
        uint8_t,
        bool changed,
        seq::SequencerHistoryDescriptor
    ) {
        assert(changed);
        return seq::SequencerPreparedPatternEditSealOutcome::Sealed;
    }

    static seq::SequencerPreparedPatternEditCommitOutcome commit(
        void* context,
        Owner
    ) {
        auto& self = *static_cast<CommitObservation*>(context);
        ++self.commitCount;
        self.contentRevisionAtCommit =
            self.sequencer->contentView.revision.get();
        self.focusAtCommit = self.sequencer->focusedStep.get();
        return seq::SequencerPreparedPatternEditCommitOutcome::Committed;
    }

    static seq::SequencerPreparedPatternEditAbortOutcome abort(
        void*,
        Owner,
        uint8_t
    ) {
        return seq::SequencerPreparedPatternEditAbortOutcome::Aborted;
    }
};

constexpr Services::Operations kCommitObservationOperations{
    .commitCoalescedPatternEdit = &CommitObservation::boundary,
    .beginPreparedPatternEdit = &CommitObservation::begin,
    .preparedPatternEditReady = &CommitObservation::ready,
    .precompactPreparedPatternEditGraph = &CommitObservation::precompact,
    .sealPreparedPatternEdit = &CommitObservation::seal,
    .commitPreparedPatternEdit = &CommitObservation::commit,
    .abortPreparedPatternEdit = &CommitObservation::abort,
};

void test_focus_is_replayable_and_detached_view_publication_is_postcommit() {
    seq::SequencerState sequencer;
    setLength(sequencer, 8U);
    sequencer.focusedStep.set(3U);
    assert(seq::ensureGraphRoot(sequencer.pattern));
    assert(seq::setNodeNoteOffset(
        sequencer.pattern, seq::rootStepNodeId(0U), 7));

    CommitObservation observation{.sequencer = &sequencer};
    const Services history = Services::fromStaticOperations<
        kCommitObservationOperations>(&observation);
    Transaction transaction(sequencer, history, Action::PageClear);
    assert(transaction.openBoundary());
    MutationPlan plan;
    assertReady(
        core::handler::buildSequencerPageClearMutationPlan(
            sequencer, 0U, 0U, plan),
        plan,
        Action::PageClear);
    const uint32_t contentRevisionBefore = sequencer.contentView.revision.get();
    assert(transaction.execute(
               core::handler::makeSequencerPreparedPageStructureExecution(
                   plan)) == Result::Committed);

    assert(observation.commitCount == 1U);
    assert(observation.focusAtCommit == 0U);
    assert(observation.contentRevisionAtCommit == contentRevisionBefore);
    assert(sequencer.contentView.revision.get() == contentRevisionBefore + 1U);

    std::cout << "[PASS] replayable focus precedes seal; view publication follows commit\n";
}

void test_builder_revalidation_and_flat_mutation_allocate_nothing() {
    seq::SequencerState sequencer;
    setLength(sequencer, 8U);
    dirtyRootStep(sequencer);
    MutationPlan plan;
    MutationOutcome mutation = MutationOutcome::Failed;
    {
        allocation_trace::Scope trace;
        assert(core::handler::buildSequencerPageClearMutationPlan(
                   sequencer, 0U, 0U, plan) == Preflight::Ready);
        const Execution execution =
            core::handler::makeSequencerPreparedPageStructureExecution(plan);
        assert(execution.revalidate(execution.mutationContext, sequencer));
        mutation = execution.mutate(
            execution.mutationContext, sequencer, Services{});
        assert(allocation_trace::count == 0U);
    }
    assert(mutation == MutationOutcome::Changed);
    assert(sequencer.pattern.note[0U] == seq::SequencerState::DEFAULT_NOTE);

    std::cout << "[PASS] plan build/revalidate/mutation are runtime-allocation free\n";
}

void test_reused_plan_is_reconstructed_before_early_rejection() {
    seq::SequencerState sequencer;
    setLength(sequencer, 8U);

    MutationPlan plan;
    plan.targetToSource.fill(17U);
    plan.contentPath.valid = true;
    plan.contentPath.compacted = true;
    plan.contentPath.stackDepth =
        seq::SequencerContentViewState::MAX_CHILD_DEPTH;
    for (auto& frame : plan.contentPath.frames) {
        frame.kind = seq::SequencerContentViewKind::MICRO_SEQUENCE;
        frame.ownerRootStep = 12U;
        frame.ownerLocalStep = 3U;
        frame.pageSnapshot = 2U;
        frame.focusSnapshot = 19U;
        frame.ownerNodeId = 7U;
        frame.sequenceId = 8U;
        frame.cycleSetId = 9U;
        frame.length = 4U;
    }
    plan.clipboard = reinterpret_cast<const core::state::StructureClipboardState*>(
        uintptr_t{1U});
    plan.sourceGraphIdentity = reinterpret_cast<const Graph*>(uintptr_t{1U});
    plan.stepDataRevision = 1U;
    plan.graphRevision = 2U;
    plan.ccLaneRevision = 3U;
    plan.timingRevision = 4U;
    plan.clipboardRevision = 5U;
    plan.graphBudget = {.stepNodes = 6U, .sequences = 7U, .cycleSets = 8U};
    plan.pageMask = 0xAAAAU;
    plan.action = Action::StepPaste;
    plan.outcome = Preflight::Ready;
    plan.payloadPlan = PayloadPlan::FullCurrentPayload;
    plan.expectedTrack = 0U;
    plan.patternLength = 99U;
    plan.contentLength = 98U;
    plan.resultingContentLength = 97U;
    plan.initialPage = 9U;
    plan.initialFocus = 72U;
    plan.finalFocus = 71U;
    plan.targetCount = 70U;
    plan.resetDepth = StepResetDepth::Deep;
    plan.flags = 0xFFU;

    constexpr uint8_t invalidTrack = seq::SequencerTrackBankState::TRACK_COUNT;
    assert(core::handler::buildSequencerPageCreateMutationPlan(
               sequencer, invalidTrack, 0U, plan) == Preflight::Rejected);

    for (const auto source : plan.targetToSource) {
        assert(source == MutationPlan::TARGET_UNTOUCHED);
    }
    assert(!plan.contentPath.valid);
    assert(!plan.contentPath.compacted);
    assert(plan.contentPath.stackDepth == 0U);
    for (const auto& frame : plan.contentPath.frames) {
        assert(frame.kind == seq::SequencerContentViewKind::ROOT);
        assert(frame.ownerRootStep == 0U);
        assert(frame.ownerLocalStep == 0U);
        assert(frame.pageSnapshot == 0U);
        assert(frame.focusSnapshot == 0U);
        assert(frame.ownerNodeId == GraphLimits::INVALID_ID);
        assert(frame.sequenceId == GraphLimits::INVALID_ID);
        assert(frame.cycleSetId == GraphLimits::INVALID_ID);
        assert(frame.length == 0U);
    }
    assert(plan.clipboard == nullptr);
    assert(plan.sourceGraphIdentity == nullptr);
    assert(plan.stepDataRevision == 0U);
    assert(plan.graphRevision == 0U);
    assert(plan.ccLaneRevision == 0U);
    assert(plan.timingRevision == 0U);
    assert(plan.clipboardRevision == 0U);
    assert(plan.graphBudget.stepNodes == 0U);
    assert(plan.graphBudget.sequences == 0U);
    assert(plan.graphBudget.cycleSets == 0U);
    assert(plan.pageMask == 0U);
    assert(plan.action == Action::PageCreate);
    assert(plan.outcome == Preflight::Rejected);
    assert(plan.payloadPlan == PayloadPlan::FlatOnly);
    assert(plan.expectedTrack == invalidTrack);
    assert(plan.patternLength == 0U);
    assert(plan.contentLength == 0U);
    assert(plan.resultingContentLength == 0U);
    assert(plan.initialPage == 0U);
    assert(plan.initialFocus == 0U);
    assert(plan.finalFocus == 0U);
    assert(plan.targetCount == 0U);
    assert(plan.resetDepth == StepResetDepth::Shallow);
    assert(plan.flags == 0U);

    std::cout << "[PASS] reused plan is reconstructed before early rejection\n";
}

}  // namespace

int main() {
    test_exact_ten_builders_and_stable_keys();
    test_rejected_and_semantic_no_change_preflights();
    test_pattern_clipboard_and_content_path_staleness();
    test_graph_budget_is_aggregate_exact_and_malformed_source_rejects();
    test_page_selection_stale_and_aggregate_capacity_fail_atomically();
    test_graph_paste_releases_canonical_default_targets_idempotently();
    test_mixed_graph_paste_skips_semantically_identical_payloads();
    test_child_extensions_preserve_logical_content_across_offsets();
    test_child_resize_analysis_uses_old_logical_projection();
    test_child_offset_extension_commit_failure_rolls_back_exactly();
    test_root_extension_keeps_cold_cc_exact_under_flat_history();
    test_page_delete_keeps_empty_cc_owner_exact_under_flat_history();
    test_full_graph_page_history_preserves_empty_cc_owners();
    test_page_extension_reclaims_matching_cold_descendants_near_capacity();
    test_step_extension_capacity_failure_restores_matching_cold_target();
    test_root_extension_from_graphless_and_disabled_destinations();
    test_prospective_graph_commit_undo_and_redo_are_exact();
    test_page_delete_reclaims_descendants_from_cold_root_tail();
    test_post_reclaim_capacity_failure_rolls_back_exactly();
    test_focus_is_replayable_and_detached_view_publication_is_postcommit();
    test_builder_revalidation_and_flat_mutation_allocate_nothing();
    test_reused_plan_is_reconstructed_before_early_rejection();
    std::cout << "All SequencerPreparedPageStructureMutationPlan tests passed "
              << "(plan bytes=" << sizeof(MutationPlan) << ").\n";
    return 0;
}
