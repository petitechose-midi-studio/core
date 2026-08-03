#ifdef NDEBUG
#undef NDEBUG
#endif

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>

#include <oc/time/Time.hpp>

#include "../../src/app/ExtmemAllocator.hpp"
#include "../../src/handler/macro/MacroDirectTrackStructureTransaction.hpp"
#include "../../src/handler/macro/MacroStructureDomainServices.hpp"
#include "../../src/handler/macro/MacroStructureWorkflow.hpp"
#include "../../src/state/CoreState.hpp"
#include "../../src/state/macro/MacroWorkflow.hpp"
#include "../../src/state/sequencer/SequencerCcLanePatternOps.hpp"
#include "../../src/state/sequencer/SequencerGraphOps.hpp"
#include "../../src/state/sequencer/SequencerSnapshotOps.hpp"
#include "../../src/state/sequencer/SequencerTrackActivationQueue.hpp"
#include "../support/CoreStorages.hpp"
#include "../support/NotificationTestUtils.hpp"
#include "../support/ProjectControlTestUtils.hpp"

#if !defined(MS_CORE_ENABLE_EXTMEM_FAILURE_INJECTION)
#error "This test requires native EXTMEM failure injection"
#endif

namespace {

using Action = core::handler::SequencerPreparedTrackStructureAction;
using Result = core::handler::SequencerPreparedTrackStructureResult;
using Status = core::handler::SequencerPreparedTrackStructureStatus;
using test_support::CoreStorages;
using test_support::drainNotifications;

constexpr uint64_t kHashOffset = 14695981039346656037ULL;
constexpr uint64_t kHashPrime = 1099511628211ULL;

uint32_t g_now_ms = 1000U;

uint32_t mockTimeMs() {
    return g_now_ms;
}

void mixBytes(uint64_t& hash, const void* bytes, std::size_t size) {
    const auto* cursor = static_cast<const uint8_t*>(bytes);
    for (std::size_t index = 0U; index < size; ++index) {
        hash ^= cursor[index];
        hash *= kHashPrime;
    }
}

template <typename T>
uint64_t fingerprint(const T& value) {
    uint64_t hash = kHashOffset;
    mixBytes(hash, &value, sizeof(value));
    return hash;
}

uint64_t fingerprintPattern(
    const core::state::sequencer::SequencerPatternState& pattern
) {
    core::state::sequencer::SequencerPatternSnapshot snapshot{};
    core::state::sequencer::captureSnapshot(pattern, snapshot);
    uint64_t hash = fingerprint(snapshot);
    const uintptr_t graphOwner = reinterpret_cast<uintptr_t>(
        pattern.graph.get()
    );
    const uintptr_t ccOwner = reinterpret_cast<uintptr_t>(
        pattern.ccLanes.get()
    );
    mixBytes(hash, &graphOwner, sizeof(graphOwner));
    mixBytes(hash, &ccOwner, sizeof(ccOwner));
    if (pattern.graph) {
        mixBytes(hash, pattern.graph.get(), sizeof(*pattern.graph));
    }
    if (pattern.ccLanes) {
        mixBytes(hash, pattern.ccLanes.get(), sizeof(*pattern.ccLanes));
    }
    return hash;
}

uint64_t fingerprintSequencer(const core::state::CoreState& state) {
    uint64_t hash = kHashOffset;
    const uint16_t mask = state.sequencerTracks.currentEnabledMask();
    const uint8_t active = state.sequencerTracks.activeTrackIndex();
    const uint8_t focus = state.sequencer.focusedStep.get();
    const uint8_t page = state.sequencer.page.get();
    mixBytes(hash, &mask, sizeof(mask));
    mixBytes(hash, &active, sizeof(active));
    mixBytes(hash, &focus, sizeof(focus));
    mixBytes(hash, &page, sizeof(page));
    const uint64_t editor = fingerprintPattern(state.sequencer.pattern);
    mixBytes(hash, &editor, sizeof(editor));
    for (uint8_t track = 0U;
         track < core::state::sequencer::SequencerTrackBankState::TRACK_COUNT;
         ++track) {
        const uint64_t bank = fingerprintPattern(
            state.sequencerTracks.track(track)
        );
        mixBytes(hash, &bank, sizeof(bank));
    }
    return hash;
}

struct FailureProof {
    uint64_t macroTracks = 0U;
    uint64_t control = 0U;
    uint64_t manual = 0U;
    uint64_t sequencer = 0U;
    uint64_t projectTracks = 0U;
    uint32_t configRevision = 0U;
    uint32_t controlRevision = 0U;
    uint32_t automationRevision = 0U;
    uint32_t runtimeRevision = 0U;
    uint32_t modifiedCounter = 0U;
    uint32_t projectNavigationRevision = 0U;
    uint16_t sharedMask = 0U;
    uint8_t sharedActive = 0U;
    uint8_t sequencerUndo = 0U;
    uint8_t projectUndo = 0U;
    uint8_t previewTrack = 0U;
    bool previewAdd = false;
    bool dirty = false;

    [[nodiscard]] bool operator==(const FailureProof& other) const {
        return macroTracks == other.macroTracks &&
               control == other.control && manual == other.manual &&
               sequencer == other.sequencer &&
               projectTracks == other.projectTracks &&
               configRevision == other.configRevision &&
               controlRevision == other.controlRevision &&
               automationRevision == other.automationRevision &&
               runtimeRevision == other.runtimeRevision &&
               modifiedCounter == other.modifiedCounter &&
               projectNavigationRevision ==
                   other.projectNavigationRevision &&
               sharedMask == other.sharedMask &&
               sharedActive == other.sharedActive &&
               sequencerUndo == other.sequencerUndo &&
               projectUndo == other.projectUndo &&
               previewTrack == other.previewTrack &&
               previewAdd == other.previewAdd && dirty == other.dirty;
    }
};

FailureProof captureFailureProof(const core::state::CoreState& state) {
    return {
        .macroTracks = fingerprint(state.pages.tracks),
        .control = fingerprint(state.pages.control.authored),
        .manual = fingerprint(state.macroUi.manualOverrides),
        .sequencer = fingerprintSequencer(state),
        .projectTracks = fingerprint(state.projectTracks.authored),
        .configRevision = state.configRevision.get(),
        .controlRevision = state.pages.control.authoredRevision,
        .automationRevision = state.macroUi.automationEditRevision.get(),
        .runtimeRevision = state.macroUi.runtimeProjectionRevision.get(),
        .modifiedCounter = state.project.metadata.modifiedCounter,
        .projectNavigationRevision =
            state.projectNavigation.contentRevision.get(),
        .sharedMask = state.sharedTrackEnabledMask.get(),
        .sharedActive = state.sharedTrackActive.get(),
        .sequencerUndo = state.sequencerHistory.undoCount(),
        .projectUndo = state.projectHistory.undoCount(),
        .previewTrack = state.trackNavigation.previewTrackIndex.get(),
        .previewAdd = state.trackNavigation.previewAddSlot.get(),
        .dirty = state.project.metadata.dirty,
    };
}

struct CommitCounters {
    uint32_t configRevision = 0U;
    uint32_t controlRevision = 0U;
    uint32_t automationRevision = 0U;
    uint32_t runtimeRevision = 0U;
    uint32_t modifiedCounter = 0U;
    uint32_t projectNavigationRevision = 0U;
    uint8_t sequencerUndo = 0U;
    uint8_t projectUndo = 0U;
};

CommitCounters captureCommitCounters(const core::state::CoreState& state) {
    return {
        .configRevision = state.configRevision.get(),
        .controlRevision = state.pages.control.authoredRevision,
        .automationRevision = state.macroUi.automationEditRevision.get(),
        .runtimeRevision = state.macroUi.runtimeProjectionRevision.get(),
        .modifiedCounter = state.project.metadata.modifiedCounter,
        .projectNavigationRevision =
            state.projectNavigation.contentRevision.get(),
        .sequencerUndo = state.sequencerHistory.undoCount(),
        .projectUndo = state.projectHistory.undoCount(),
    };
}

void assertCommittedCounters(
    const core::state::CoreState& state,
    const CommitCounters& before,
    bool controlChanged
) {
    assert(state.configRevision.get() ==
           core::state::macro::nextMacroConfigRevision(
               before.configRevision
           ));
    assert(state.pages.control.authoredRevision ==
           before.controlRevision + (controlChanged ? 1U : 0U));
    assert(state.macroUi.automationEditRevision.get() ==
           before.automationRevision);
    assert(state.macroUi.runtimeProjectionRevision.get() ==
           before.runtimeRevision);
    assert(state.project.metadata.modifiedCounter ==
           before.modifiedCounter + 1U);
    assert(state.project.metadata.dirty);
    assert(state.sequencerHistory.undoCount() ==
           static_cast<uint8_t>(before.sequencerUndo + 1U));
    assert(state.projectHistory.undoCount() ==
           static_cast<uint8_t>(before.projectUndo + 1U));
    assert(state.projectNavigation.contentRevision.get() ==
           before.projectNavigationRevision);
}

core::state::macro::MacroAutomationSlotAddress address(
    uint8_t track,
    uint8_t page = 0U,
    uint8_t macro = 0U
) {
    return {.track = track, .page = page, .macro = macro};
}

void configureAutomation(
    core::state::modulation::ProjectControlState& control,
    const core::state::macro::MacroAutomationSlotAddress& target
) {
    core::state::macro::MacroAutomationLane lane;
    assert(core::state::macro::macroAutomationAppendPoint(
        lane,
        0.0f,
        0.2f
    ));
    assert(core::state::macro::macroAutomationAppendPoint(
        lane,
        1.0f,
        0.8f
    ));
    assert(test_support::project_control::assignAutomation(
        control,
        target,
        lane
    ));
}

enum class OwnerShape : uint8_t {
    Graphless = 0U,
    Graph,
    Cc,
    Both,
};

void seedOwnerShape(
    core::state::sequencer::SequencerPatternState& pattern,
    OwnerShape shape
) {
    if (shape == OwnerShape::Graph || shape == OwnerShape::Both) {
        assert(core::state::sequencer::ensureGraphRoot(pattern));
        pattern.graph->enabled = true;
    }
    if (shape == OwnerShape::Cc || shape == OwnerShape::Both) {
        auto* cc = core::state::sequencer::ensureSequencerCcLaneBank(
            pattern
        );
        assert(cc != nullptr);
        core::state::sequencer::SequencerCcLaneDraft draft{};
        draft.destination.controller = 74U;
        assert(core::state::sequencer::createSequencerCcLane(
                   *cc,
                   0U,
                   draft
               ).changed());
        assert(core::state::sequencer::setSequencerCcLaneEvent(
                   *cc,
                   0U,
                   0U,
                   91U
               ).changed());
        pattern.ccLaneRevision.set(cc->revision);
    }
}

void settle(core::state::CoreState& state) {
    drainNotifications();
    state.flush();
    drainNotifications();
}

void test_all_owner_shapes_commit_through_the_product_adapter() {
    constexpr std::array<OwnerShape, 4U> shapes{
        OwnerShape::Graphless,
        OwnerShape::Graph,
        OwnerShape::Cc,
        OwnerShape::Both,
    };
    for (const OwnerShape shape : shapes) {
        CoreStorages storage;
        core::state::CoreState state(storage.settings);
        seedOwnerShape(state.sequencer.pattern, shape);
        state.pages.tracks[0].pages[0].cc[0] = 99U;
        const Result result =
            core::handler::executeMacroResetTrackStructure(state, 0U);
        assert(result.status == Status::Committed);
        assert(state.pages.tracks[0].pages[0].cc[0] ==
               core::state::macro::defaultMacroCc(0U, 0U));
        settle(state);
    }
    std::cout << "[PASS] all Macro direct Graph/CC owner shapes commit\n";
}

template <std::size_t AllocationCount, typename Prepare, typename Execute>
void runFailureMatrix(Prepare&& prepare, Execute&& execute) {
    for (std::size_t ordinal = 1U;
         ordinal <= AllocationCount;
         ++ordinal) {
        CoreStorages storage;
        core::state::CoreState state(storage.settings);
        prepare(state);
        const FailureProof before = captureFailureProof(state);
        {
            core::app::testing::ScopedExtmemAllocationFailure failure(
                ordinal
            );
            const Result result = execute(state);
            if (result.status != Status::AllocationUnavailable) {
                std::cerr << "failure matrix mismatch count="
                          << AllocationCount << " ordinal=" << ordinal
                          << " status="
                          << static_cast<unsigned>(result.status)
                          << " attempt="
                          << core::app::testing::extmemAllocationAttempt
                          << '\n';
            }
            assert(result.status == Status::AllocationUnavailable);
            assert(!result.settled());
            assert(core::app::testing::extmemAllocationAttempt == ordinal);
            assert(captureFailureProof(state) == before);
        }
        assert(core::app::testing::extmemAllocationAttempt == 0U);
        settle(state);
    }

    CoreStorages storage;
    core::state::CoreState state(storage.settings);
    prepare(state);
    {
        core::app::testing::ScopedExtmemAllocationFailure failure(
            AllocationCount + 1U
        );
        const Result result = execute(state);
        assert(result.status == Status::Committed);
        assert(core::app::testing::extmemAllocationAttempt ==
               AllocationCount);
        assert(core::app::testing::extmemAllocationFailureOrdinal ==
               AllocationCount + 1U);
    }
    settle(state);
}

void test_product_t1_t2_failure_ordinals_are_exact() {
    runFailureMatrix<8U>(
        [](core::state::CoreState& state) {
            seedOwnerShape(state.sequencer.pattern, OwnerShape::Both);
            state.pages.tracks[0].pages[0].cc[0] = 91U;
        },
        [](core::state::CoreState& state) {
            return core::handler::executeMacroResetTrackStructure(
                state,
                0U
            );
        }
    );

    core::state::macro::MacroTrackData source;
    source.pages[0].cc[0] = 101U;
    runFailureMatrix<12U>(
        [](core::state::CoreState& state) {
            seedOwnerShape(state.sequencer.pattern, OwnerShape::Both);
            seedOwnerShape(
                state.sequencerTracks.track(1U),
                OwnerShape::Both
            );
        },
        [&source](core::state::CoreState& state) {
            return core::handler::executeMacroPasteTrackStructure(
                state,
                1U,
                source,
                nullptr
            );
        }
    );
    std::cout << "[PASS] product T1/T2 fail-Nth and armed success are exact\n";
}

void test_draft_and_activation_collisions_reject_before_allocation() {
    {
        CoreStorages storage;
        core::state::CoreState state(storage.settings);
        state.sequencer.stepContentDraft.active.set(true);
        const FailureProof before = captureFailureProof(state);
        const uint32_t draftRevision =
            state.sequencer.stepContentDraft.revision.get();
        {
            core::app::testing::ScopedExtmemAllocationFailure failure(1U);
            const Result result =
                core::handler::executeMacroCreateTrackStructure(state, 1U);
            assert(result.status == Status::DraftBlocked);
            assert(core::app::testing::extmemAllocationAttempt == 0U);
        }
        assert(captureFailureProof(state) == before);
        assert(state.sequencer.stepContentDraft.failure == core::state::
            sequencer::SequencerStepContentDraftFailure::TRANSITION_BLOCKED);
        assert(state.sequencer.stepContentDraft.blockedTransition == core::
            state::sequencer::
                SequencerStepContentDraftBlockedTransition::TRACK);
        assert(state.sequencer.stepContentDraft.revision.get() ==
               draftRevision + 1U);
        settle(state);
    }

    for (const bool playing : {false, true}) {
        CoreStorages storage;
        core::state::CoreState state(storage.settings);
        auto& queue = state.sequencerTrackActivations;
        core::state::sequencer::SequencerTrackActivationBatch batch{};
        assert(queue.prepare(
            0x0002U,
            0xFFFFU,
            playing,
            batch,
            core::state::sequencer::
                SequencerTrackActivationOrigin::TRACK_PASTE
        ));
        assert(queue.armPrepared(batch));
        queue.publishPrepared(batch);
        const FailureProof before = captureFailureProof(state);
        const uint16_t pendingBefore = queue.pendingTrackMask();
        const uint32_t revisionBefore = queue.telemetryRevision().get();
        {
            core::app::testing::ScopedExtmemAllocationFailure failure(1U);
            const Result result =
                core::handler::executeMacroCreateTrackStructure(state, 1U);
            assert(result.status == Status::Stale);
            assert(core::app::testing::extmemAllocationAttempt == 0U);
        }
        assert(captureFailureProof(state) == before);
        assert(queue.pendingTrackMask() == pendingBefore);
        assert(queue.telemetryRevision().get() == revisionBefore);
        settle(state);
    }
    std::cout
        << "[PASS] Draft and stopped/playing activation collisions are early\n";
}

void test_delete_preserves_cold_content_and_replays_globally() {
    CoreStorages storage;
    core::state::CoreState state(storage.settings);
    assert(state.setSharedTrackState(0x0003U, 0U));
    state.pages.tracks[0].pages[0].cc[0] = 77U;
    state.sequencer.pattern.note[0] = 65U;
    configureAutomation(state.pages.control, address(0U));
    assert(state.macroUi.manualOverrides.activate(address(0U), 0.9f) ==
           core::state::macro::MacroManualOverrideState::ActivateStatus::
               ACTIVATED);
    assert(state.macroUi.manualOverrides.activate(address(2U), 0.4f) ==
           core::state::macro::MacroManualOverrideState::ActivateStatus::
               ACTIVATED);
    const auto macroBefore = state.pages.tracks[0];
    const CommitCounters before = captureCommitCounters(state);

    const auto service =
        core::handler::MacroStructureDomainServices::fromCoreState(state);
    assert(service.deleteActiveTrack());
    assert(state.sharedTrackEnabledMask.get() == 0x0002U);
    assert(state.sharedTrackActive.get() == 1U);
    assert(std::memcmp(
               &state.pages.tracks[0],
               &macroBefore,
               sizeof(macroBefore)
           ) == 0);
    assert(state.sequencerTracks.track(0U).note[0] == 65U);
    assert(!test_support::project_control::readSlot(
                state.pages.control,
                address(0U)
            ).present());
    assert(!state.macroUi.manualOverrides.activeFor(address(0U)));
    assert(state.macroUi.manualOverrides.activeFor(address(2U)));
    assertCommittedCounters(state, before, true);

    assert(state.undoProjectHistory());
    assert(state.sharedTrackEnabledMask.get() == 0x0003U);
    assert(state.sharedTrackActive.get() == 0U);
    assert(test_support::project_control::readSlot(
               state.pages.control,
               address(0U)
           ).present());
    assert(state.redoProjectHistory());
    assert(state.sharedTrackEnabledMask.get() == 0x0002U);
    assert(state.sharedTrackActive.get() == 1U);
    assert(!test_support::project_control::readSlot(
                state.pages.control,
                address(0U)
            ).present());
    settle(state);
    std::cout << "[PASS] Macro Delete preserves cold content and global replay\n";
}

void test_reset_paste_and_create_apply_exact_scopes() {
    {
        CoreStorages storage;
        core::state::CoreState state(storage.settings);
        assert(state.setSharedTrackState(0x0003U, 0U));
        state.pages.tracks[1].activePage = 3U;
        state.pages.tracks[1].enabledPageMask = 0x0009U;
        state.pages.tracks[1].pages[3].cc[2] = 87U;
        state.sequencerTracks.track(1U).note[0] = 72U;
        const uint64_t sequencerBefore = fingerprintPattern(
            state.sequencerTracks.track(1U)
        );
        configureAutomation(state.pages.control, address(1U, 3U, 2U));
        assert(state.macroUi.manualOverrides.activate(
                   address(1U, 3U, 2U),
                   0.8f
               ) == core::state::macro::MacroManualOverrideState::
                        ActivateStatus::ACTIVATED);
        const CommitCounters before = captureCommitCounters(state);
        const auto service =
            core::handler::MacroStructureDomainServices::fromCoreState(state);
        assert(service.resetTrackContent(1U));
        assert(state.sharedTrackEnabledMask.get() == 0x0003U);
        assert(state.sharedTrackActive.get() == 0U);
        assert(state.pages.tracks[1].activePage == 0U);
        assert(state.pages.tracks[1].enabledPageMask == 0x0001U);
        assert(fingerprintPattern(state.sequencerTracks.track(1U)) ==
               sequencerBefore);
        assert(!test_support::project_control::readSlot(
                    state.pages.control,
                    address(1U, 3U, 2U)
                ).present());
        assert(!state.macroUi.manualOverrides.activeFor(
            address(1U, 3U, 2U)
        ));
        assertCommittedCounters(state, before, true);
        settle(state);
    }

    {
        CoreStorages storage;
        core::state::CoreState state(storage.settings);
        state.pages.tracks[0].activePage = 2U;
        state.pages.tracks[0].enabledPageMask = 0x0005U;
        state.pages.tracks[0].pages[2].cc[3] = 93U;
        configureAutomation(state.pages.control, address(0U, 2U, 3U));
        assert(state.structureClipboard.storeMacroTrack(
            state.pages.tracks[0],
            state.pages.control,
            0U
        ));
        const uint64_t clipboardTrack = fingerprint(
            state.structureClipboard.macroTrack
        );
        const uint64_t clipboardAutomation = fingerprint(
            *state.structureClipboard.macroAutomationSet
        );
        state.sequencerTracks.track(1U).note[0] = 74U;
        const uint8_t sequencerNoteBefore =
            state.sequencerTracks.track(1U).note[0];
        assert(state.macroUi.manualOverrides.activate(address(1U), 0.7f) ==
               core::state::macro::MacroManualOverrideState::ActivateStatus::
                   ACTIVATED);
        const CommitCounters before = captureCommitCounters(state);
        const auto service =
            core::handler::MacroStructureDomainServices::fromCoreState(state);
        assert(service.pasteTrack(
            1U,
            state.structureClipboard.macroTrack,
            state.structureClipboard.macroAutomationSet.get()
        ));
        assert(state.sharedTrackEnabledMask.get() == 0x0003U);
        assert(state.sharedTrackActive.get() == 1U);
        assert(std::memcmp(
                   &state.pages.tracks[1],
                   &state.structureClipboard.macroTrack,
                   sizeof(core::state::macro::MacroTrackData)
               ) == 0);
        assert(state.sequencer.pattern.note[0] == 74U);
        assert(fingerprint(state.structureClipboard.macroTrack) ==
               clipboardTrack);
        assert(fingerprint(*state.structureClipboard.macroAutomationSet) ==
               clipboardAutomation);
        assert(test_support::project_control::readSlot(
                   state.pages.control,
                   address(1U, 2U, 3U)
               ).present());
        assert(!state.macroUi.manualOverrides.activeFor(address(1U)));
        assertCommittedCounters(state, before, true);
        assert(state.undoProjectHistory());
        assert(state.sharedTrackEnabledMask.get() == 0x0001U);
        assert(state.sharedTrackActive.get() == 0U);
        assert(state.redoProjectHistory());
        assert(state.sharedTrackActive.get() == 1U);
        assert(state.sequencer.pattern.note[0] == sequencerNoteBefore);
        settle(state);
    }

    {
        CoreStorages storage;
        core::state::CoreState state(storage.settings);
        state.pages.tracks[2].activePage = 4U;
        state.pages.tracks[2].enabledPageMask = 0x0011U;
        state.pages.tracks[2].pages[4].cc[1] = 111U;
        state.sequencerTracks.track(2U).note[0] = 76U;
        configureAutomation(state.pages.control, address(2U, 4U, 1U));
        assert(state.macroUi.manualOverrides.activate(
                   address(2U, 4U, 1U),
                   0.6f
               ) == core::state::macro::MacroManualOverrideState::
                        ActivateStatus::ACTIVATED);
        const CommitCounters before = captureCommitCounters(state);
        const auto service =
            core::handler::MacroStructureDomainServices::fromCoreState(state);
        assert(service.createTrack(2U));
        assert(state.sharedTrackEnabledMask.get() == 0x0005U);
        assert(state.sharedTrackActive.get() == 2U);
        assert(state.pages.tracks[2].activePage == 0U);
        assert(state.pages.tracks[2].enabledPageMask == 0x0001U);
        assert(state.sequencer.pattern.note[0] == 76U);
        assert(!test_support::project_control::readSlot(
                    state.pages.control,
                    address(2U, 4U, 1U)
                ).present());
        assert(!state.macroUi.manualOverrides.activeFor(
            address(2U, 4U, 1U)
        ));
        assertCommittedCounters(state, before, true);
        settle(state);
    }
    std::cout << "[PASS] Macro Reset/Paste/Create scopes are exact\n";
}

void test_manual_only_nochange_has_no_durable_publication() {
    CoreStorages storage;
    core::state::CoreState state(storage.settings);
    assert(state.macroUi.manualOverrides.activate(address(0U), 0.95f) ==
           core::state::macro::MacroManualOverrideState::ActivateStatus::
               ACTIVATED);
    const auto* graphBefore = state.sequencer.pattern.graph.get();
    const auto* ccBefore = state.sequencer.pattern.ccLanes.get();
    const CommitCounters before = captureCommitCounters(state);
    const Result reset =
        core::handler::executeMacroResetTrackStructure(state, 0U);
    assert(reset.status == Status::NoChange);
    assert(!state.macroUi.manualOverrides.activeFor(address(0U)));
    assert(state.sequencer.pattern.graph.get() == graphBefore);
    assert(state.sequencer.pattern.ccLanes.get() == ccBefore);
    assert(captureCommitCounters(state).configRevision ==
           before.configRevision);
    assert(state.pages.control.authoredRevision == before.controlRevision);
    assert(state.project.metadata.modifiedCounter == before.modifiedCounter);
    assert(state.sequencerHistory.undoCount() == before.sequencerUndo);
    assert(state.projectHistory.undoCount() == before.projectUndo);
    assert(state.projectNavigation.contentRevision.get() ==
           before.projectNavigationRevision);

    assert(state.macroUi.manualOverrides.activate(address(0U), 0.75f) ==
           core::state::macro::MacroManualOverrideState::ActivateStatus::
               ACTIVATED);
    const auto identical = state.pages.tracks[0];
    const CommitCounters beforePaste = captureCommitCounters(state);
    const Result paste = core::handler::executeMacroPasteTrackStructure(
        state,
        0U,
        identical,
        nullptr
    );
    assert(paste.status == Status::NoChange);
    assert(!state.macroUi.manualOverrides.activeFor(address(0U)));
    assert(state.configRevision.get() == beforePaste.configRevision);
    assert(state.project.metadata.modifiedCounter ==
           beforePaste.modifiedCounter);
    assert(state.sequencerHistory.undoCount() == beforePaste.sequencerUndo);
    settle(state);
    std::cout << "[PASS] manual-only NoChange has no durable publication\n";
}

void test_workflow_failure_preserves_track_add_preview() {
    CoreStorages storage;
    core::state::CoreState state(storage.settings);
    core::handler::MacroStructureWorkflow workflow(
        {
            state.macroUi,
            state.pages,
            state.trackNavigation,
            state.sharedTrackActive,
            state.structureNavigationFocus,
            state.structureClipboard,
        },
        core::handler::MacroStructureDomainServices::fromCoreState(state)
    );
    state.structureNavigationFocus.set(
        core::state::StructureNavigationFocus::TRACK
    );
    assert(state.structureClipboard.storeMacroTrack(
        state.pages.tracks[0],
        state.pages.control,
        0U
    ));
    state.trackNavigation.previewTrackIndex.set(1U);
    state.trackNavigation.previewAddSlot.set(true);
    assert(workflow.beginHoldAction(
        core::state::StructureHoldAction::PASTE,
        true
    ));
    {
        core::app::testing::ScopedExtmemAllocationFailure failure(1U);
        assert(workflow.commitHoldAction(
            core::state::StructureHoldAction::PASTE
        ));
    }
    assert(state.trackNavigation.previewAddSlot.get());
    assert(state.trackNavigation.previewTrackIndex.get() == 1U);
    assert(state.sharedTrackEnabledMask.get() == 0x0001U);
    assert(state.sequencerHistory.undoCount() == 0U);

    state.trackNavigation.previewTrackIndex.set(2U);
    state.trackNavigation.previewAddSlot.set(true);
    {
        core::app::testing::ScopedExtmemAllocationFailure failure(1U);
        workflow.createPreviewedStructure();
    }
    assert(state.trackNavigation.previewAddSlot.get());
    assert(state.trackNavigation.previewTrackIndex.get() == 2U);
    assert(state.sharedTrackEnabledMask.get() == 0x0001U);

    workflow.createPreviewedStructure();
    assert(!state.trackNavigation.previewAddSlot.get());
    assert(state.sharedTrackActive.get() == 2U);
    settle(state);
    std::cout << "[PASS] workflow failure preserves Track add preview\n";
}

}  // namespace

int main() {
    oc::time::setProvider(mockTimeMs);
    test_all_owner_shapes_commit_through_the_product_adapter();
    test_product_t1_t2_failure_ordinals_are_exact();
    test_draft_and_activation_collisions_reject_before_allocation();
    test_delete_preserves_cold_content_and_replays_globally();
    test_reset_paste_and_create_apply_exact_scopes();
    test_manual_only_nochange_has_no_durable_publication();
    test_workflow_failure_preserves_track_add_preview();
    std::cout << "\nAll MacroDirectTrackStructureTransaction tests passed.\n";
    return 0;
}
