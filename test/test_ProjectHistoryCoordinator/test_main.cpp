#include <cassert>
#include <cstring>
#include <iostream>
#include <utility>

#include "state/CoreState.hpp"
#include "state/modulation/ProjectControlMacroOps.hpp"
#include "state/project/ProjectTrackDomainServices.hpp"
#include "state/sequencer/SequencerCcLanePatternOps.hpp"
#include "state/sequencer/SequencerSnapshotOps.hpp"
#include "support/CoreStorages.hpp"

namespace {

namespace macro = core::state::macro;
namespace modulation = core::state::modulation;
namespace project = core::state::project;
namespace seq = core::state::sequencer;

struct Harness {
    test_support::CoreStorages storages;
    core::state::CoreState state;

    Harness()
        : state(
              storages.settings
          ) {}
};

constexpr macro::MacroAutomationSlotAddress kMacro{
    .track = 0,
    .page = 0,
    .macro = 1,
};

void recordMacroDestination(
    core::state::CoreState& state,
    uint8_t cc,
    macro::MacroHistoryActionKind kind =
        macro::MacroHistoryActionKind::PASTE_DESTINATION
) {
    auto change = state.macroHistory.prepare(state.pages, kMacro, kind);
    assert(change);
    auto& page = state.pages.pageData(kMacro.track, kMacro.page);
    page.setMacroActive(kMacro.macro, true);
    page.cc[kMacro.macro] = cc;
    state.pages.updateActiveConfigs();
    assert(state.macroHistory.commitPrepared(state.pages, std::move(change)));
    state.macroHistory.endCoalescing();
}

void recordStepPitch(core::state::CoreState& state, uint8_t pitch) {
    seq::SequencerHistoryPatternSnapshot before;
    seq::SequencerHistoryPatternSnapshot after;
    assert(seq::captureHistorySnapshot(state.sequencer, before));
    const uint8_t previous = state.sequencer.pattern.note[0];
    assert(state.sequencer.setStepNoteAt(0, pitch));
    assert(seq::captureHistorySnapshot(state.sequencer, after));
    assert(state.recordSequencerPatternHistory(
        std::move(before),
        std::move(after),
        seq::SequencerHistoryDescriptor{
            .kind = seq::SequencerHistoryActionKind::StepPropertyEdit,
            .trackIndex = 0,
            .stepIndex = 0,
            .property = seq::StepProperty::NOTE,
            .hasValue = true,
            .beforeValue = previous,
            .afterValue = pitch,
        }
    ));
}

void recordCcLane(core::state::CoreState& state) {
    seq::SequencerHistoryPatternSnapshot before;
    seq::SequencerHistoryPatternSnapshot after;
    assert(seq::captureHistorySnapshot(state.sequencer, before));

    auto* bank = seq::ensureSequencerCcLaneBank(state.sequencer.pattern);
    assert(bank != nullptr);
    seq::SequencerCcLaneDraft draft{};
    draft.destination.controller = 74;
    draft.destination.minimum = 0;
    draft.destination.maximum = 127;
    draft.destination.routePolicy = seq::SequencerCcLaneRoutePolicy::INHERIT_TRACK;
    draft.initialValue = 64;
    assert(seq::createSequencerCcLane(*bank, 0, draft).changed());
    assert(seq::setSequencerCcLaneEvent(*bank, 0, 2, 91).changed());
    state.sequencer.pattern.ccLaneRevision.set(bank->revision);

    assert(seq::captureHistorySnapshot(state.sequencer, after));
    assert(state.recordSequencerPatternHistory(
        std::move(before),
        std::move(after),
        seq::SequencerHistoryDescriptor{
            .kind = seq::SequencerHistoryActionKind::CcLaneCreate,
            .trackIndex = 0,
            .laneIndex = 0,
            .stepIndex = 2,
        }
    ));
}

void recordModulator(core::state::CoreState& state) {
    modulation::ModulatorLfoDraft draft{};
    draft.name = "LFO 1";
    draft.parameters.periodTicks = modulation::PROJECT_CONTROL_TICKS_PER_BEAT;
    assert(state.macroHistory.createUnassignedLfo(state.pages, draft).changed());
}

void recordTrackDelay(
    core::state::CoreState& state,
    uint8_t track,
    int16_t delayMs
) {
    auto tracks = project::ProjectTrackDomainServices::fromCoreState(state);
    assert(tracks.setDelayMs(track, delayMs));
}

bool hasCcLane(const core::state::CoreState& state) {
    const auto* bank = seq::sequencerCcLaneView(state.sequencer.pattern);
    return bank != nullptr && bank->lanes[0].occupied &&
           bank->lanes[0].activeMask.test(2) && bank->lanes[0].values[2] == 91;
}

void assertUndoLabel(const core::state::CoreState& state, const char* expected) {
    const auto* entry = state.projectHistory.peekUndo();
    assert(entry != nullptr);
    assert(std::strcmp(
        project::ProjectHistoryCoordinator::actionLabel(*entry),
        expected
    ) == 0);
}

void assertRedoLabel(const core::state::CoreState& state, const char* expected) {
    const auto* entry = state.projectHistory.peekRedo();
    assert(entry != nullptr);
    assert(std::strcmp(
        project::ProjectHistoryCoordinator::actionLabel(*entry),
        expected
    ) == 0);
}

void test_cross_domain_timeline_is_exact_and_semantic() {
    Harness h;
    const uint8_t initialPitch = h.state.sequencer.pattern.note[0];

    recordMacroDestination(h.state, 74);
    recordStepPitch(h.state, 72);
    recordCcLane(h.state);
    recordModulator(h.state);

    assert(h.state.projectHistory.undoCount() == 4U);
    assertUndoLabel(h.state, "Create Modulator");

    assert(h.state.undoProjectHistory());
    assert(h.state.pages.control.authored.modulation.sourceCount == 0U);
    assertUndoLabel(h.state, "Create CC Lane");
    assert(h.state.undoProjectHistory());
    assert(!hasCcLane(h.state));
    assertUndoLabel(h.state, "Step Property");
    assert(h.state.undoProjectHistory());
    assert(h.state.sequencer.pattern.note[0] == initialPitch);
    assertUndoLabel(h.state, "Paste Destination");
    assert(h.state.undoProjectHistory());
    assert(!h.state.pages.pageData(0, 0).isMacroActive(kMacro.macro));
    assert(h.state.projectHistory.undoCount() == 0U);
    assert(h.state.projectHistory.redoCount() == 4U);

    assertRedoLabel(h.state, "Paste Destination");
    assert(h.state.redoProjectHistory());
    assert(h.state.pages.pageData(0, 0).isMacroActive(kMacro.macro));
    assert(h.state.pages.pageData(0, 0).cc[kMacro.macro] == 74U);
    assertRedoLabel(h.state, "Step Property");
    assert(h.state.redoProjectHistory());
    assert(h.state.sequencer.pattern.note[0] == 72U);
    assertRedoLabel(h.state, "Create CC Lane");
    assert(h.state.redoProjectHistory());
    assert(hasCcLane(h.state));
    assertRedoLabel(h.state, "Create Modulator");
    assert(h.state.redoProjectHistory());
    assert(h.state.pages.control.authored.modulation.sourceCount == 1U);
    assert(h.state.projectHistory.redoCount() == 0U);

    std::cout << "[PASS] global history alternates Macro, Step, CC and Modulation exactly\n";
}

void test_track_is_a_third_exact_global_history_domain() {
    Harness h;
    const uint8_t initialPitch = h.state.sequencer.pattern.note[0];

    recordMacroDestination(h.state, 81U);
    recordTrackDelay(h.state, 3U, -24);
    recordStepPitch(h.state, 73U);
    assert(h.state.projectHistory.undoCount() == 3U);
    assertUndoLabel(h.state, "Step Property");

    assert(h.state.undoProjectHistory());
    assert(h.state.sequencer.pattern.note[0] == initialPitch);
    assertUndoLabel(h.state, "Track Delay");
    assert(h.state.undoProjectHistory());
    assert(h.state.projectTracks.authored.delayMs[3] == 0);
    assertUndoLabel(h.state, "Paste Destination");
    assert(h.state.undoProjectHistory());

    assertRedoLabel(h.state, "Paste Destination");
    assert(h.state.redoProjectHistory());
    assertRedoLabel(h.state, "Track Delay");
    assert(h.state.redoProjectHistory());
    assert(h.state.projectTracks.authored.delayMs[3] == -24);
    assertRedoLabel(h.state, "Step Property");
    assert(h.state.redoProjectHistory());
    assert(h.state.sequencer.pattern.note[0] == 73U);

    std::cout << "[PASS] Track participates in the exact global chronology\n";
}

void test_track_mutation_cuts_macro_and_sequencer_redo_branches() {
    Harness h;
    recordMacroDestination(h.state, 70U);
    recordStepPitch(h.state, 66U);
    assert(h.state.undoProjectHistory());
    assert(h.state.sequencerHistory.redoCount() == 1U);

    recordTrackDelay(h.state, 1U, 18);
    assert(h.state.projectHistory.redoCount() == 0U);
    assert(h.state.macroHistory.redoCount() == 0U);
    assert(h.state.sequencerHistory.redoCount() == 0U);
    assert(h.state.projectTrackHistory.redoCount() == 0U);

    assert(h.state.undoProjectHistory());
    assert(h.state.projectTracks.authored.delayMs[1] == 0);
    assert(h.state.undoProjectHistory());
    assert(!h.state.pages.pageData(0, 0).isMacroActive(kMacro.macro));

    std::cout << "[PASS] a Track command cuts every domain Redo branch\n";
}

void test_track_boundary_publishes_runtime_revisions() {
    Harness h;
    auto tracks = project::ProjectTrackDomainServices::fromCoreState(h.state);
    const uint32_t modifiedBefore = h.state.project.metadata.modifiedCounter;
    const uint32_t macroRevisionBefore = h.state.configRevision.get();
    const uint32_t sequencerRevisionBefore =
        h.state.sequencerRuntimeProjectRevision.get();

    assert(tracks.setMidiChannel(0U, 12U));
    assert(h.state.projectTracks.authored.midiChannels[0] == 12U);
    assert(h.state.configRevision.get() != macroRevisionBefore);
    assert(h.state.sequencerRuntimeProjectRevision.get() !=
           sequencerRevisionBefore);
    assert(h.state.project.metadata.modifiedCounter == modifiedBefore + 1U);

    assert(tracks.setMuted(0U, true));
    assert(project::projectTrackMuted(h.state.projectTracks, 0U));
    assert(h.state.undoProjectHistory());
    assert(!project::projectTrackMuted(h.state.projectTracks, 0U));
    assert(h.state.undoProjectHistory());
    assert(h.state.projectTracks.authored.midiChannels[0] == 0U);

    std::cout << "[PASS] Track authority publishes runtime revisions at commit/Undo\n";
}

void test_pending_track_gesture_blocks_global_history_boundary() {
    Harness h;
    recordMacroDestination(h.state, 79U);
    auto tracks = project::ProjectTrackDomainServices::fromCoreState(h.state);
    assert(tracks.beginGesture(
        project::ProjectTrackHistoryActionKind::Delay,
        0U
    ));
    assert(tracks.setDelayMs(0U, 31));
    assert(h.state.hasPendingProjectTransaction());
    assert(!h.state.prepareProjectHistoryInteraction());
    assert(!h.state.undoProjectHistory());
    assert(tracks.cancelGesture());
    assert(h.state.projectTracks.authored.delayMs[0] == 0);
    assert(!h.state.hasPendingProjectTransaction());
    assert(h.state.undoProjectHistory());

    std::cout << "[PASS] pending Track gestures are fail-closed global transactions\n";
}

void test_new_cross_domain_mutation_cuts_every_redo_branch() {
    Harness h;
    recordMacroDestination(h.state, 71);
    recordStepPitch(h.state, 67);

    assert(h.state.undoProjectHistory());
    assert(h.state.sequencerHistory.redoCount() == 1U);
    assert(h.state.projectHistory.redoCount() == 1U);

    recordMacroDestination(h.state, 72);
    assert(h.state.projectHistory.redoCount() == 0U);
    assert(h.state.macroHistory.redoCount() == 0U);
    assert(h.state.sequencerHistory.redoCount() == 0U);
    assert(!h.state.redoProjectHistory());

    assert(h.state.undoProjectHistory());
    assert(h.state.pages.pageData(0, 0).cc[kMacro.macro] == 71U);
    assert(h.state.undoProjectHistory());
    assert(!h.state.pages.pageData(0, 0).isMacroActive(kMacro.macro));

    std::cout << "[PASS] a new domain mutation cuts the complete global Redo branch\n";
}

void test_macro_domain_eviction_establishes_an_exact_history_barrier() {
    Harness h;
    for (uint8_t index = 0U; index < 10U; ++index) {
        recordMacroDestination(h.state, static_cast<uint8_t>(20U + index));
    }

    assert(h.state.macroHistory.undoCount() == macro::MacroHistoryService::ENTRY_LIMIT);
    assert(h.state.projectHistory.undoCount() == macro::MacroHistoryService::ENTRY_LIMIT);
    for (uint8_t index = 0U; index < macro::MacroHistoryService::ENTRY_LIMIT; ++index) {
        assert(h.state.undoProjectHistory());
    }
    assert(!h.state.undoProjectHistory());
    assert(h.state.pages.pageData(0, 0).cc[kMacro.macro] == 21U);

    std::cout << "[PASS] local capacity eviction cannot leave a dangling global action\n";
}

void test_domain_clear_is_a_global_history_boundary() {
    Harness h;
    recordMacroDestination(h.state, 75);
    recordStepPitch(h.state, 69);

    assert(h.state.projectHistory.undoCount() == 2U);
    assert(h.state.clearSequencerHistory());
    assert(h.state.sequencerHistory.undoCount() == 0U);
    assert(h.state.macroHistory.undoCount() == 0U);
    assert(h.state.projectHistory.undoCount() == 0U);
    assert(!h.state.undoProjectHistory());
    assert(h.state.pages.pageData(0, 0).isMacroActive(kMacro.macro));

    std::cout << "[PASS] clearing one domain cuts the global history boundary\n";
}

void test_global_history_is_fail_closed_during_modulator_audition() {
    Harness h;
    recordMacroDestination(h.state, 74U);
    recordMacroDestination(h.state, 75U);
    assert(h.state.undoProjectHistory());
    assert(h.state.pages.pageData(0U, 0U).cc[kMacro.macro] == 74U);

    modulation::ModulatorLfoDraft source{};
    source.name = "Audition LFO";
    source.parameters.periodTicks = modulation::PROJECT_CONTROL_TICKS_PER_BEAT;
    source.parameters.shape = modulation::ModulatorLfoShape::SINE;
    source.parameters.retrigger = modulation::ModulatorRetriggerPolicy::TRANSPORT;
    source.parameters.timing = modulation::ModulatorTimingMode::SYNC;
    modulation::ModulationBindingDraft binding{};
    binding.destination = modulation::projectControlDestination(kMacro);
    binding.amountQ15 = 8192;
    binding.application = modulation::ModulationApplication::AROUND_BASE;

    const auto begun = h.state.macroHistory.beginLfoModulatorAudition(
        h.state.pages,
        kMacro,
        source,
        binding
    );
    assert(begun.changed());
    const uint8_t sourceCount =
        h.state.pages.control.authored.modulation.sourceCount;
    const std::size_t undoCount = h.state.projectHistory.undoCount();
    const std::size_t redoCount = h.state.projectHistory.redoCount();

    assert(!h.state.prepareProjectHistoryInteraction());
    assert(!h.state.undoProjectHistory());
    assert(!h.state.redoProjectHistory());
    assert(h.state.projectHistory.undoCount() == undoCount);
    assert(h.state.projectHistory.redoCount() == redoCount);
    assert(h.state.pages.control.authored.modulation.sourceCount == sourceCount);
    assert(h.state.pages.pageData(0U, 0U).cc[kMacro.macro] == 74U);

    h.state.pages.control.audition.mode =
        static_cast<modulation::ProjectModulatorSourceSessionMode>(0xFFU);
    assert(!h.state.prepareProjectHistoryInteraction());
    assert(!h.state.undoProjectHistory());
    assert(!h.state.redoProjectHistory());
    assert(h.state.projectHistory.undoCount() == undoCount);
    assert(h.state.projectHistory.redoCount() == redoCount);
    assert(h.state.pages.control.authored.modulation.sourceCount == sourceCount);

    std::cout
        << "[PASS] global Undo/Redo is fail-closed during valid or malformed audition\n";
}

void test_sequencer_scope_eviction_prunes_a_cross_domain_middle_entry() {
    Harness h;
    recordMacroDestination(h.state, 77);
    for (uint8_t index = 0U;
         index < seq::SequencerHistoryService::PATTERN_ENTRY_LIMIT + 2U;
         ++index) {
        recordStepPitch(h.state, static_cast<uint8_t>(40U + index));
    }

    assert(h.state.sequencerHistory.undoCount(
        seq::SequencerHistoryScope::PatternOnly
    ) == seq::SequencerHistoryService::PATTERN_ENTRY_LIMIT);
    assert(h.state.projectHistory.undoCount() ==
           seq::SequencerHistoryService::PATTERN_ENTRY_LIMIT);

    for (uint8_t index = 0U;
         index < seq::SequencerHistoryService::PATTERN_ENTRY_LIMIT;
         ++index) {
        assertUndoLabel(h.state, "Step Property");
        assert(h.state.undoProjectHistory());
    }
    // The two oldest Step payloads were evicted together with their global
    // references. Their barrier also makes the older Macro action unreachable.
    assert(h.state.sequencer.pattern.note[0] == 41U);
    assert(!h.state.undoProjectHistory());
    assert(h.state.pages.pageData(0, 0).isMacroActive(kMacro.macro));

    std::cout << "[PASS] Sequencer scope eviction creates an exact-history barrier\n";
}

void test_redo_eviction_keeps_only_the_reachable_prefix() {
    project::ProjectHistoryCoordinator history;
    const auto& sink = history.eventSink();
    constexpr uintptr_t kFirst = 0x101U;
    constexpr uintptr_t kSecond = 0x102U;
    constexpr uintptr_t kThird = 0x103U;
    const auto kind = static_cast<uint8_t>(
        macro::MacroHistoryActionKind::PASTE_DESTINATION
    );

    sink.notifyCommitted(project::ProjectHistoryDomain::Macro, kFirst, kind);
    sink.notifyCommitted(project::ProjectHistoryDomain::Macro, kSecond, kind);
    sink.notifyCommitted(project::ProjectHistoryDomain::Macro, kThird, kind);
    sink.notifyApplied(
        project::ProjectHistoryDomain::Macro,
        kThird,
        project::ProjectHistoryDirection::Undo
    );
    sink.notifyApplied(
        project::ProjectHistoryDomain::Macro,
        kSecond,
        project::ProjectHistoryDirection::Undo
    );
    assert(history.undoCount() == 1U);
    assert(history.redoCount() == 2U);

    sink.notifyEvicted(project::ProjectHistoryDomain::Macro, kThird);
    assert(history.undoCount() == 1U);
    assert(history.redoCount() == 1U);
    assert(history.peekRedo() != nullptr);
    assert(history.peekRedo()->identity == kSecond);

    std::cout << "[PASS] Redo eviction preserves only the still-reachable prefix\n";
}

void test_empty_and_typed_labels_follow_history_revision() {
    project::ProjectHistoryCoordinator history;
    char label[48]{};

    history.formatUndoLabel(label, sizeof(label));
    assert(std::strcmp(label, "Undo -") == 0);
    history.formatRedoLabel(label, sizeof(label));
    assert(std::strcmp(label, "Redo -") == 0);
    assert(history.revision.get() == 0U);

    constexpr uintptr_t kAutomationState = 0x201U;
    history.eventSink().notifyCommitted(
        project::ProjectHistoryDomain::Macro,
        kAutomationState,
        static_cast<uint8_t>(macro::MacroHistoryActionKind::AUTOMATION_STATE)
    );
    assert(history.revision.get() == 1U);
    history.formatUndoLabel(label, sizeof(label));
    assert(std::strcmp(label, "Undo Automation State") == 0);

    history.eventSink().notifyApplied(
        project::ProjectHistoryDomain::Macro,
        kAutomationState,
        project::ProjectHistoryDirection::Undo
    );
    assert(history.revision.get() == 2U);
    history.formatRedoLabel(label, sizeof(label));
    assert(std::strcmp(label, "Redo Automation State") == 0);

    std::cout << "[PASS] empty and typed labels track the public revision\n";
}

}  // namespace

int main() {
    test_cross_domain_timeline_is_exact_and_semantic();
    test_track_is_a_third_exact_global_history_domain();
    test_track_mutation_cuts_macro_and_sequencer_redo_branches();
    test_track_boundary_publishes_runtime_revisions();
    test_pending_track_gesture_blocks_global_history_boundary();
    test_new_cross_domain_mutation_cuts_every_redo_branch();
    test_macro_domain_eviction_establishes_an_exact_history_barrier();
    test_domain_clear_is_a_global_history_boundary();
    test_global_history_is_fail_closed_during_modulator_audition();
    test_sequencer_scope_eviction_prunes_a_cross_domain_middle_entry();
    test_redo_eviction_keeps_only_the_reachable_prefix();
    test_empty_and_typed_labels_follow_history_revision();

    std::cout << "\nAll ProjectHistoryCoordinator tests passed.\n";
    return 0;
}
