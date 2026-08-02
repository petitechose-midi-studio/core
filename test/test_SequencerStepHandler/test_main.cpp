#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include <array>
#include <utility>

#include <config/App.hpp>
#include <config/Timing.hpp>
#include <iostream>
#include <oc/api/ButtonAPI.hpp>
#include <oc/api/EncoderAPI.hpp>
#include <oc/context/OverlayManager.hpp>
#include <oc/core/event/EventBus.hpp>
#include <oc/core/event/Events.hpp>
#include <oc/core/input/InputBinding.hpp>

#include "../../src/app/ExtmemAllocator.hpp"
#include "../../src/handler/common/SharedTrackDomainServices.hpp"
#include "../../src/handler/sequencer/SequencerDirectTrackStructureTransaction.hpp"
#include "../../src/handler/sequencer/SequencerHistoryDomainServices.hpp"
#include "../../src/handler/sequencer/SequencerPatternEditorHandler.hpp"
#include "../../src/handler/sequencer/SequencerPatternQuickControlsHandler.hpp"
#include "../../src/handler/sequencer/SequencerStepEditHandler.hpp"
#include "../../src/handler/sequencer/SequencerStepHandler.hpp"
#include "../../src/handler/sequencer/SequencerStructureNavigationWorkflow.hpp"
#include "../../src/handler/transport/TransportHandler.hpp"
#include "../../src/state/CoreState.hpp"
#include "../../src/state/sequencer/SequencerCcLaneDomain.hpp"
#include "../../src/state/sequencer/SequencerContentViewOps.hpp"
#include "../../src/state/sequencer/SequencerGraphOps.hpp"
#include "../../src/state/sequencer/SequencerPatternRegionOps.hpp"
#include "../../src/state/sequencer/SequencerSnapshotOps.hpp"
#include "../../src/state/sequencer/SequencerStepContentDraftOps.hpp"
#include "../../src/state/sequencer/SequencerTrackBankOps.hpp"
#include "../support/CoreStorages.hpp"
#include "../support/InputTestHardware.hpp"
#include "../support/NotificationTestUtils.hpp"
#include "../support/ProjectControlTestUtils.hpp"
#include "../support/SequencerHistoryTransactionAssertions.hpp"

namespace {

uint32_t g_now_ms = 0;

uint32_t mockTimeMs() { return g_now_ms; }

uint64_t byteHash(const void* data, std::size_t size) noexcept {
    const auto* bytes = static_cast<const uint8_t*>(data);
    uint64_t hash = 1469598103934665603ULL;
    for (std::size_t index = 0U; index < size; ++index) {
        hash ^= bytes[index];
        hash *= 1099511628211ULL;
    }
    return hash;
}

void configureProjectTrackFixture(core::state::CoreState& state, uint8_t track, uint8_t midiChannel,
                                  bool muted = false) {
    assert(track < core::state::project::PROJECT_TRACK_COUNT);
    state.projectTracks.authored.midiChannels[track] = midiChannel;
    const uint16_t bit = static_cast<uint16_t>(1U << track);
    if (muted) {
        state.projectTracks.authored.mutedMask |= bit;
    } else {
        state.projectTracks.authored.mutedMask &= static_cast<uint16_t>(~bit);
    }
}

using test_support::TestButtonHardware;
using test_support::TestEncoderHardware;
namespace tx = test_support::sequencer_transaction;
namespace seq = core::state::sequencer;

using HistoryServices = core::handler::SequencerHistoryDomainServices;

struct FailingPageCommitHistory {
    core::state::CoreState* state = nullptr;
    std::size_t commitCount = 0U;
    std::size_t abortCount = 0U;

    static seq::SequencerPatternHistoryCommitOutcome boundary(void* context) {
        auto& self = *static_cast<FailingPageCommitHistory*>(context);
        return self.state->commitSequencerPatternHistoryCoalescingOutcome();
    }

    static seq::SequencerPreparedPatternEditBeginOutcome begin(
        void* context,
        seq::SequencerPreparedPatternEditOwner owner,
        uint8_t key,
        seq::SequencerCoalescedPatternPayloadPlan payloadPlan,
        seq::SequencerHistoryDescriptor descriptor,
        bool compactGraphOnSeal
    ) {
        auto& self = *static_cast<FailingPageCommitHistory*>(context);
        return self.state->beginOrContinueSequencerPreparedPatternEdit(
            owner, key, payloadPlan, descriptor, compactGraphOnSeal);
    }

    static bool ready(
        void* context,
        seq::SequencerPreparedPatternEditOwner owner,
        uint8_t key,
        uint8_t expectedTrack
    ) {
        auto& self = *static_cast<FailingPageCommitHistory*>(context);
        return self.state->sequencerPreparedPatternEditReady(owner, key, expectedTrack);
    }

    static seq::SequencerPreparedPatternEditSealOutcome seal(
        void* context,
        seq::SequencerPreparedPatternEditOwner owner,
        uint8_t key,
        bool changed,
        seq::SequencerHistoryDescriptor descriptor
    ) {
        auto& self = *static_cast<FailingPageCommitHistory*>(context);
        return self.state->sealSequencerPreparedPatternEdit(
            owner, key, changed, descriptor);
    }

    static seq::SequencerPreparedPatternEditCommitOutcome commit(
        void* context,
        seq::SequencerPreparedPatternEditOwner
    ) {
        auto& self = *static_cast<FailingPageCommitHistory*>(context);
        ++self.commitCount;
        return seq::SequencerPreparedPatternEditCommitOutcome::Failed;
    }

    static seq::SequencerPreparedPatternEditAbortOutcome abort(
        void* context,
        seq::SequencerPreparedPatternEditOwner owner,
        uint8_t key
    ) {
        auto& self = *static_cast<FailingPageCommitHistory*>(context);
        ++self.abortCount;
        return self.state->abortSequencerPreparedPatternEdit(owner, key);
    }
};

constexpr HistoryServices::Operations kFailingPageCommitHistoryOperations{
    .commitCoalescedPatternEdit = &FailingPageCommitHistory::boundary,
    .beginPreparedPatternEdit = &FailingPageCommitHistory::begin,
    .preparedPatternEditReady = &FailingPageCommitHistory::ready,
    .sealPreparedPatternEdit = &FailingPageCommitHistory::seal,
    .commitPreparedPatternEdit = &FailingPageCommitHistory::commit,
    .abortPreparedPatternEdit = &FailingPageCommitHistory::abort,
};

enum class DirectTrackIntentDrift : uint8_t {
    TrackSelectionCursor = 0U,
    PageSelectionMask,
    StepSelectionClipboardRevision,
    ClipboardRevision,
    ClipboardOwner,
    TrackPasteGeneration,
    TrackPasteCommitConsumed,
};

struct DirectTrackIntentDriftHistory {
    core::state::CoreState* state = nullptr;
    DirectTrackIntentDrift drift =
        DirectTrackIntentDrift::TrackSelectionCursor;
    core::app::ExtmemUniquePtr<
        oc::note::sequencer::StepSequencerGraph
    > replacementGraph;

    static seq::SequencerTrackStructureChronologyResult boundary(
        void* context
    ) {
        auto& self = *static_cast<DirectTrackIntentDriftHistory*>(context);
        const auto result =
            self.state->openSequencerTrackStructureChronologyBoundary();
        if (result.status !=
            seq::SequencerTrackStructureChronologyStatus::Opened) {
            return result;
        }
        switch (self.drift) {
            case DirectTrackIntentDrift::TrackSelectionCursor:
                self.state->trackNavigation.selection.cursorIndex.set(10U);
                break;
            case DirectTrackIntentDrift::PageSelectionMask:
                self.state->sequencer.structureUi.pageSelection.
                    selectedMask.set(0x0040U);
                break;
            case DirectTrackIntentDrift::StepSelectionClipboardRevision:
                self.state->sequencer.structureUi.stepSelection.
                    clipboardRevision.set(83U);
                break;
            case DirectTrackIntentDrift::ClipboardRevision:
                self.state->structureClipboard.revision.set(
                    self.state->structureClipboard.revision.get() + 1U
                );
                break;
            case DirectTrackIntentDrift::ClipboardOwner:
                self.state->structureClipboard.sequencerGraph =
                    std::move(self.replacementGraph);
                break;
            case DirectTrackIntentDrift::TrackPasteGeneration:
                ++self.state->sequencer.structureUi.trackPaste.
                    interactionGeneration;
                break;
            case DirectTrackIntentDrift::TrackPasteCommitConsumed:
                self.state->sequencer.structureUi.trackPaste.commitConsumed =
                    !self.state->sequencer.structureUi.trackPaste.
                        commitConsumed;
                break;
        }
        return result;
    }
};

constexpr HistoryServices::Operations kDirectTrackIntentDriftHistoryOperations{
    .openTrackStructureChronologyBoundary =
        &DirectTrackIntentDriftHistory::boundary,
};

enum class TrackHoldBoundaryDrift : uint8_t {
    ActiveTrack = 0U,
    SelectionMask,
};

struct TrackHoldBoundaryDriftHistory {
    core::state::CoreState* state = nullptr;
    TrackHoldBoundaryDrift drift = TrackHoldBoundaryDrift::ActiveTrack;
    uint8_t nextActiveTrack = 0U;
    uint16_t nextSelectionMask = 0U;
    std::size_t boundaryCount = 0U;

    static seq::SequencerPatternHistoryCommitOutcome boundary(void* context) {
        auto& self = *static_cast<TrackHoldBoundaryDriftHistory*>(context);
        ++self.boundaryCount;
        const auto outcome =
            self.state->commitSequencerPatternHistoryCoalescingOutcome();
        if (outcome == seq::SequencerPatternHistoryCommitOutcome::Failed) {
            return outcome;
        }
        switch (self.drift) {
            case TrackHoldBoundaryDrift::ActiveTrack:
                assert(self.state->setSharedTrackState(
                    self.state->sharedTrackEnabledMask.get(),
                    self.nextActiveTrack
                ));
                break;
            case TrackHoldBoundaryDrift::SelectionMask:
                self.state->trackNavigation.selection.selectedMask.set(
                    self.nextSelectionMask
                );
                break;
        }
        return outcome;
    }
};

constexpr HistoryServices::Operations kTrackHoldBoundaryDriftHistoryOperations{
    .commitCoalescedPatternEdit = &TrackHoldBoundaryDriftHistory::boundary,
};

struct DirectTrackChronologyCounter {
    core::state::CoreState* state = nullptr;
    std::size_t boundaryCount = 0U;

    static seq::SequencerTrackStructureChronologyResult boundary(
        void* context
    ) {
        auto& self = *static_cast<DirectTrackChronologyCounter*>(context);
        ++self.boundaryCount;
        return self.state->openSequencerTrackStructureChronologyBoundary();
    }
};

constexpr HistoryServices::Operations kDirectTrackChronologyCounterOperations{
    .openTrackStructureChronologyBoundary =
        &DirectTrackChronologyCounter::boundary,
};

struct SequencerStepHarness {
    static constexpr oc::type::ScopeID SEQUENCER_SCOPE = 501;
    static constexpr oc::type::ScopeID PATTERN_EDITOR_SCOPE = 502;
    static constexpr oc::type::ScopeID STEP_EDITOR_SCOPE = 503;
    static constexpr oc::type::ScopeID PRESET_LIBRARY_SCOPE = 504;

    test_support::CoreStorages storages;
    core::state::CoreState state;
    oc::state::Signal<core::state::StructureNavigationFocus,
                      core::state::kStructureNavigationFocusMaxSubscribers>
        navigationFocus;

    oc::core::event::EventBus eventBus;
    oc::core::input::InputBinding inputBinding;
    TestButtonHardware buttonHw;
    TestEncoderHardware encoderHw;
    oc::api::ButtonAPI buttons;
    oc::api::EncoderAPI encoders;
    oc::context::OverlayManager<core::ui::OverlayType> overlays;
    core::state::sequencer::SequencerPatternRandomizeSession patternRandomize;
    core::handler::SequencerPatternEditorHandler patternEditorHandler;
    core::handler::SequencerStepHandler handler;
    core::handler::TransportHandler transportHandler;
    core::handler::SequencerPatternQuickControlsHandler quickControlsHandler;
    // Construct last so this integration probe cannot consume binding slots
    // ahead of the handler under test.
    core::handler::SequencerStepEditHandler stepEditHandler;

    SequencerStepHarness()
        : state(storages.settings), navigationFocus(core::state::StructureNavigationFocus::PAGE),
          inputBinding(eventBus, mockTimeMs, Config::Input::CONFIG),
          buttons(inputBinding, buttonHw), encoders(inputBinding, encoderHw),
          overlays(state.overlays, buttons),
          patternEditorHandler(
              core::handler::SequencerPatternEditorHandler::StateRefs{
                  state.sequencer,
                  state.sequencerTracks,
                  patternRandomize,
                  core::handler::SequencerHistoryDomainServices::fromCoreState(state),
              },
              overlays, encoders, buttons, SEQUENCER_SCOPE, PATTERN_EDITOR_SCOPE),
          handler(
              core::handler::SequencerStepHandler::StateRefs{
                  state.sequencer,
                  state.sequencerTracks,
                  navigationFocus,
                  state.trackNavigation,
                  state.projectNavigation,
                  state.projectTracks,
                  core::state::project::ProjectTrackDomainServices::fromCoreState(state),
                  state.structureClipboard,
                  core::handler::SharedTrackDomainServices::fromCoreState(state),
                  core::handler::SequencerHistoryDomainServices::fromCoreState(state),
                  state.pages,
                  &state.sequencerTrackActivations,
                  &state.statusBar,
              },
              encoders, buttons, SEQUENCER_SCOPE),
          transportHandler(core::handler::TransportHandler::StateRefs{state.statusBar}, buttons),
          quickControlsHandler(
              core::handler::SequencerPatternQuickControlsHandler::StateRefs{
                  state.overlays,
                  state.sequencer,
                  state.trackNavigation,
                  navigationFocus,
                  core::handler::SequencerHistoryDomainServices::fromCoreState(state),
              },
              encoders, buttons, SEQUENCER_SCOPE),
          stepEditHandler(
              core::handler::SequencerStepEditHandler::StateRefs{
                  state.overlays,
                  state.sequencer,
                  state.sequencerTracks,
                  state.structureClipboard,
                  state.trackNavigation,
                  state.patternPitchSettings,
                  navigationFocus,
                  core::handler::SequencerHistoryDomainServices::fromCoreState(state),
                  {},
                  {},
              },
              overlays,
              encoders,
              buttons,
              SEQUENCER_SCOPE,
              STEP_EDITOR_SCOPE,
              PRESET_LIBRARY_SCOPE,
              mockTimeMs) {
        g_now_ms = 0;
        oc::time::setProvider(mockTimeMs);
        overlays.setActiveViewProvider([]() { return SEQUENCER_SCOPE; });
        overlays.registerCleanup(core::ui::OverlayType::SEQ_PATTERN_EDIT, PATTERN_EDITOR_SCOPE);
        overlays.registerCleanup(core::ui::OverlayType::SEQ_STEP_EDIT, STEP_EDITOR_SCOPE);
        overlays.registerCleanup(
            core::ui::OverlayType::PRESET_LIBRARY,
            PRESET_LIBRARY_SCOPE
        );
        handler.attachPatternEditorHandler(patternEditorHandler);
        handler.attachStepEditHandler(stepEditHandler);
        handler.update(g_now_ms);
    }

    void tick(uint32_t nowMs) {
        g_now_ms = nowMs;
        inputBinding.processTick();
        handler.update(g_now_ms);
        patternEditorHandler.update(g_now_ms);
        stepEditHandler.update(g_now_ms);
    }

    void press(Config::ButtonID id) {
        const auto buttonId = static_cast<oc::type::ButtonID>(id);
        buttonHw.setPressed(buttonId, true);
        eventBus.emit(oc::core::event::ButtonPressEvent(buttonId, true));
    }

    void release(Config::ButtonID id) {
        const auto buttonId = static_cast<oc::type::ButtonID>(id);
        buttonHw.setPressed(buttonId, false);
        eventBus.emit(oc::core::event::ButtonReleaseEvent(buttonId));
    }

    void tap(Config::ButtonID id) {
        press(id);
        release(id);
    }

    void advance(uint32_t ms) {
        g_now_ms += ms;
        inputBinding.processTick();
        handler.update(g_now_ms);
        patternEditorHandler.update(g_now_ms);
        stepEditHandler.update(g_now_ms);
    }

    void turn(Config::EncoderID id, float value) {
        const auto encoderId = static_cast<oc::type::EncoderID>(id);
        encoderHw.setPosition(encoderId, value);
        eventBus.emit(oc::core::event::EncoderChangedEvent(encoderId, value));
    }
};

core::handler::SequencerStructureEditWorkflow makeStructureEditWorkflow(
    SequencerStepHarness& harness,
    HistoryServices history
) {
    return core::handler::SequencerStructureEditWorkflow({
        harness.state.sequencer,
        harness.state.sequencerTracks,
        harness.navigationFocus,
        harness.state.trackNavigation,
        harness.state.projectNavigation,
        harness.state.projectTracks,
        core::state::project::ProjectTrackDomainServices::fromCoreState(
            harness.state),
        harness.state.structureClipboard,
        core::handler::SharedTrackDomainServices::fromCoreState(harness.state),
        history,
        harness.state.pages,
        &harness.state.sequencerTrackActivations,
        &harness.state.statusBar,
    });
}

bool rootStepHasMicroSequence(const SequencerStepHarness& h, uint8_t step) {
    const auto* graph = core::state::sequencer::graphView(h.state.sequencer.pattern);
    if (graph == nullptr) return false;
    const auto nodeId = core::state::sequencer::rootStepNodeId(step);
    if (nodeId >= graph->stepNodeCount) return false;
    return graph->stepNodes[nodeId].has(oc::note::sequencer::STEP_NODE_CHILD_SEQUENCE);
}

const oc::note::sequencer::StepSequencerStepNode* rootStepNode(const SequencerStepHarness& h,
                                                               uint8_t step) {
    const auto* graph = core::state::sequencer::graphView(h.state.sequencer.pattern);
    if (graph == nullptr) return nullptr;
    return graph->stepNode(core::state::sequencer::rootStepNodeId(step));
}

void createRootMicroSequence(SequencerStepHarness& h, uint8_t step) {
    const auto nodeId = core::state::sequencer::rootStepNodeId(step);
    const auto result =
        core::state::sequencer::createMicroSequence(h.state.sequencer.pattern, nodeId, 2);
    assert(result.ok);
}

template <typename T>
uint64_t objectHash(const T* value) noexcept {
    return value == nullptr ? 0U : byteHash(value, sizeof(T));
}

struct PreparedEditorUiInvariant {
    uint8_t navigationFocus = 0U;
    uint8_t page = 0U;
    uint8_t focusedStep = 0U;
    uint8_t previewPageIndex = 0U;
    uint8_t holdAction = 0U;
    uint32_t holdStartedAtMs = 0U;

    bool pageSelectionActive = false;
    bool pageSelectionPlacing = false;
    uint8_t pageSelectionScope = 0U;
    uint8_t pageSelectionCursor = 0U;
    uint16_t pageSelectionSelectedMask = 0U;
    uint16_t pageSelectionDestinationMask = 0U;
    uint16_t pageSelectionOverwriteMask = 0U;
    bool pageSelectionPasteBlocked = false;
    uint32_t pageSelectionClipboardRevision = 0U;

    bool stepSelectionActive = false;
    bool stepSelectionPlacing = false;
    uint8_t stepSelectionCursor = 0U;
    uint64_t stepSelectionMaskHash = 0U;
    bool stepPastePreviewActive = false;
    uint8_t stepPastePreview = 0U;
    uint32_t stepSelectionClipboardRevision = 0U;

    uint8_t contentViewKind = 0U;
    uint8_t contentViewParentStep = 0U;
    uint16_t contentViewOwnerNodeId = 0U;
    uint16_t contentViewSequenceId = 0U;
    uint16_t contentViewCycleSetId = 0U;
    uint8_t contentViewLength = 0U;
    uint8_t contentViewDepth = 0U;
    uint32_t contentViewRevision = 0U;
    uint8_t contentViewRootPage = 0U;
    uint8_t contentViewRootFocus = 0U;
    uint8_t contentViewStackDepth = 0U;
    uint64_t contentViewFramesHash = 0U;
};

PreparedEditorUiInvariant capturePreparedEditorUiInvariant(
    const SequencerStepHarness& h
) {
    const auto& sequencer = h.state.sequencer;
    const auto& ui = sequencer.structureUi;
    const auto& pageSelection = ui.pageSelection;
    const auto& stepSelection = ui.stepSelection;
    const auto& contentView = sequencer.contentView;
    const auto stepMask = stepSelection.selectedMask.get();

    PreparedEditorUiInvariant out{};
    out.navigationFocus = static_cast<uint8_t>(h.navigationFocus.get());
    out.page = sequencer.page.get();
    out.focusedStep = sequencer.focusedStep.get();
    out.previewPageIndex = ui.previewPageIndex.get();
    out.holdAction = static_cast<uint8_t>(ui.pageHold.action.get());
    out.holdStartedAtMs = ui.pageHold.startedAtMs.get();

    out.pageSelectionActive = pageSelection.active.get();
    out.pageSelectionPlacing = pageSelection.placing.get();
    out.pageSelectionScope = static_cast<uint8_t>(pageSelection.scope.get());
    out.pageSelectionCursor = pageSelection.cursorIndex.get();
    out.pageSelectionSelectedMask = pageSelection.selectedMask.get();
    out.pageSelectionDestinationMask = pageSelection.destinationMask.get();
    out.pageSelectionOverwriteMask = pageSelection.overwriteMask.get();
    out.pageSelectionPasteBlocked = pageSelection.pasteBlocked.get();
    out.pageSelectionClipboardRevision = pageSelection.clipboardRevision.get();

    out.stepSelectionActive = stepSelection.active.get();
    out.stepSelectionPlacing = stepSelection.placing.get();
    out.stepSelectionCursor = stepSelection.cursorStep.get();
    out.stepSelectionMaskHash = byteHash(&stepMask, sizeof(stepMask));
    out.stepPastePreviewActive = stepSelection.pastePreviewActive.get();
    out.stepPastePreview = static_cast<uint8_t>(stepSelection.pastePreview.get());
    out.stepSelectionClipboardRevision = stepSelection.clipboardRevision.get();

    out.contentViewKind = static_cast<uint8_t>(contentView.kind.get());
    out.contentViewParentStep = contentView.parentStep.get();
    out.contentViewOwnerNodeId = contentView.ownerNodeId.get();
    out.contentViewSequenceId = contentView.sequenceId.get();
    out.contentViewCycleSetId = contentView.cycleSetId.get();
    out.contentViewLength = contentView.length.get();
    out.contentViewDepth = contentView.depth.get();
    out.contentViewRevision = contentView.revision.get();
    out.contentViewRootPage = contentView.rootPageSnapshot;
    out.contentViewRootFocus = contentView.rootFocusSnapshot;
    out.contentViewStackDepth = contentView.stackDepth;
    out.contentViewFramesHash =
        byteHash(contentView.frames.data(), sizeof(contentView.frames));
    return out;
}

void assertPageSelectionNavigationInvariant(
    const SequencerStepHarness& h,
    const PreparedEditorUiInvariant& expected
) {
    const auto actual = capturePreparedEditorUiInvariant(h);
    assert(actual.navigationFocus == expected.navigationFocus);
    assert(actual.page == expected.page);
    assert(actual.focusedStep == expected.focusedStep);
    assert(actual.previewPageIndex == expected.previewPageIndex);
    assert(actual.pageSelectionActive == expected.pageSelectionActive);
    assert(actual.pageSelectionPlacing == expected.pageSelectionPlacing);
    assert(actual.pageSelectionScope == expected.pageSelectionScope);
    assert(actual.pageSelectionCursor == expected.pageSelectionCursor);
    assert(actual.pageSelectionSelectedMask == expected.pageSelectionSelectedMask);
    assert(actual.pageSelectionDestinationMask == expected.pageSelectionDestinationMask);
    assert(actual.pageSelectionOverwriteMask == expected.pageSelectionOverwriteMask);
    assert(actual.pageSelectionPasteBlocked == expected.pageSelectionPasteBlocked);
    assert(actual.pageSelectionClipboardRevision ==
           expected.pageSelectionClipboardRevision);
}

struct PreparedClipboardInvariant {
    uint8_t kind = 0U;
    uint32_t revision = 0U;
    uint64_t pageHash = 0U;
    uint64_t stepHash = 0U;
    uint64_t pageSelectionHash = 0U;
    const void* graphOwner = nullptr;
    uint64_t graphHash = 0U;
    const void* ccOwner = nullptr;
    uint64_t ccHash = 0U;
};

PreparedClipboardInvariant capturePreparedClipboardInvariant(
    const core::state::StructureClipboardState& clipboard
) {
    PreparedClipboardInvariant out{};
    out.kind = static_cast<uint8_t>(clipboard.kind.get());
    out.revision = clipboard.revision.get();
    out.pageHash = byteHash(&clipboard.sequencerPage,
                            sizeof(clipboard.sequencerPage));
    out.stepHash = byteHash(&clipboard.sequencerSteps,
                            sizeof(clipboard.sequencerSteps));
    out.pageSelectionHash = byteHash(&clipboard.sequencerPageSelection,
                                     sizeof(clipboard.sequencerPageSelection));
    out.graphOwner = clipboard.sequencerGraph.get();
    out.graphHash = objectHash(clipboard.sequencerGraph.get());
    out.ccOwner = clipboard.sequencerCcLanes.get();
    out.ccHash = objectHash(clipboard.sequencerCcLanes.get());
    return out;
}

struct PreparedProductInvariant {
    uint8_t bankActiveTrack = 0U;
    uint16_t bankEnabledMask = 0U;
    uint8_t sharedActiveTrack = 0U;
    uint16_t sharedEnabledMask = 0U;
    uint32_t runtimeProjectRevision = 0U;

    uint16_t activationPendingMask = 0U;
    uint16_t activationRuntimeQueuedMask = 0U;
    uint16_t activationRuntimeCancelledMask = 0U;
    uint64_t activationRuntimeGenerationsHash = 0U;
    uint64_t activationTelemetryHash = 0U;
    uint64_t activationRealtimeHash = 0U;

    bool historyFeedbackVisible = false;
    uint32_t historyFeedbackRevision = 0U;
    uint64_t historyFeedbackLinesHash = 0U;
    uint32_t historyFeedbackHideAtMs = 0U;

    bool statusNoteIn = false;
    bool statusNoteOut = false;
    bool statusCcIn = false;
    bool statusCcOut = false;
    bool statusPlaying = false;
    uint64_t statusTempoHash = 0U;
    uint64_t statusTempoDisplayHash = 0U;
    bool statusSyncExternal = false;
    bool statusSyncInput = false;
    bool statusTempoLocked = false;
    bool statusTransportLocked = false;
    bool statusBeat = false;
    uint64_t statusTrackActivityHash = 0U;
};

PreparedProductInvariant capturePreparedProductInvariant(
    const SequencerStepHarness& h
) {
    PreparedProductInvariant out{};
    out.bankActiveTrack = h.state.sequencerTracks.activeTrackIndex();
    out.bankEnabledMask = h.state.sequencerTracks.currentEnabledMask();
    out.sharedActiveTrack = h.state.sharedTrackActive.get();
    out.sharedEnabledMask = h.state.sharedTrackEnabledMask.get();
    out.runtimeProjectRevision = h.state.sequencerRuntimeProjectRevision.get();

    const auto& activations = h.state.sequencerTrackActivations;
    out.activationPendingMask = activations.pendingTrackMask();
    const auto runtime = activations.captureRuntimePublication();
    out.activationRuntimeQueuedMask = runtime.queuedMask;
    out.activationRuntimeCancelledMask = runtime.cancelledMask;
    out.activationRuntimeGenerationsHash =
        byteHash(runtime.generations.data(), sizeof(runtime.generations));
    std::array<uint64_t, seq::SequencerTrackActivationQueue::TRACK_COUNT>
        telemetryCodes{};
    std::array<uint64_t, seq::SequencerTrackActivationQueue::TRACK_COUNT>
        realtimeCodes{};
    for (uint8_t track = 0U;
         track < static_cast<uint8_t>(telemetryCodes.size());
         ++track) {
        const auto telemetry = activations.telemetry(track);
        telemetryCodes[track] =
            static_cast<uint64_t>(telemetry.status) |
            (static_cast<uint64_t>(telemetry.origin) << 8U) |
            (static_cast<uint64_t>(telemetry.generation) << 16U);
        const auto realtime = activations.realtimeView(track);
        realtimeCodes[track] =
            static_cast<uint64_t>(realtime.disposition) |
            (static_cast<uint64_t>(realtime.requiresLocalLoopBoundary) << 8U) |
            (static_cast<uint64_t>(realtime.generation) << 16U);
    }
    out.activationTelemetryHash =
        byteHash(telemetryCodes.data(), sizeof(telemetryCodes));
    out.activationRealtimeHash =
        byteHash(realtimeCodes.data(), sizeof(realtimeCodes));

    const auto& feedback = h.state.sequencer.historyFeedback;
    out.historyFeedbackVisible = feedback.visible.get();
    out.historyFeedbackRevision = feedback.revision.get();
    std::array<uint64_t, 3U> feedbackLines{
        byteHash(feedback.line1.data(), sizeof(feedback.line1)),
        byteHash(feedback.line2.data(), sizeof(feedback.line2)),
        byteHash(feedback.line3.data(), sizeof(feedback.line3)),
    };
    out.historyFeedbackLinesHash =
        byteHash(feedbackLines.data(), sizeof(feedbackLines));
    out.historyFeedbackHideAtMs = feedback.hideAtMs;

    const auto& status = h.state.statusBar;
    out.statusNoteIn = status.noteInActive.get();
    out.statusNoteOut = status.noteOutActive.get();
    out.statusCcIn = status.ccInActive.get();
    out.statusCcOut = status.ccOutActive.get();
    out.statusPlaying = status.playing.get();
    const float tempo = status.tempo.get();
    const float tempoDisplay = status.tempoDisplay.get();
    out.statusTempoHash = byteHash(&tempo, sizeof(tempo));
    out.statusTempoDisplayHash = byteHash(&tempoDisplay, sizeof(tempoDisplay));
    out.statusSyncExternal = status.syncExternalSource.get();
    out.statusSyncInput = status.syncInputPulse.get();
    out.statusTempoLocked = status.tempoLocked.get();
    out.statusTransportLocked = status.transportLocked.get();
    out.statusBeat = status.beatPulse.get();
    std::array<uint8_t, core::state::StatusBarState::TRACK_COUNT> trackActivity{};
    for (uint8_t track = 0U; track < trackActivity.size(); ++track) {
        trackActivity[track] = status.trackNoteActivity[track].get();
    }
    out.statusTrackActivityHash =
        byteHash(trackActivity.data(), sizeof(trackActivity));
    return out;
}

struct PreparedActionInvariant {
    tx::StateInvariant state{};
    seq::SequencerHistoryPatternSnapshot musical{};
    uint64_t bankFlatHash = 0U;
    const void* bankGraphOwner = nullptr;
    uint64_t bankGraphHash = 0U;
    const void* bankCcOwner = nullptr;
    uint64_t bankCcHash = 0U;
    PreparedEditorUiInvariant ui{};
    PreparedClipboardInvariant clipboard{};
    PreparedProductInvariant product{};
};

PreparedActionInvariant capturePreparedActionInvariant(
    const SequencerStepHarness& h
) {
    PreparedActionInvariant out{};
    out.state = tx::captureStateInvariant(h.state);
    tx::captureMusicalSnapshot(h.state, out.musical);

    const auto activeTrack = h.state.sequencerTracks.activeTrackIndex();
    const auto& bankPattern = h.state.sequencerTracks.track(activeTrack);
    seq::SequencerPatternSnapshot bankFlat{};
    seq::captureSnapshot(bankPattern, bankFlat);
    out.bankFlatHash = byteHash(&bankFlat, sizeof(bankFlat));
    out.bankGraphOwner = bankPattern.graph.get();
    out.bankGraphHash = objectHash(bankPattern.graph.get());
    out.bankCcOwner = bankPattern.ccLanes.get();
    out.bankCcHash = objectHash(bankPattern.ccLanes.get());
    out.ui = capturePreparedEditorUiInvariant(h);
    out.clipboard = capturePreparedClipboardInvariant(h.state.structureClipboard);
    out.product = capturePreparedProductInvariant(h);
    return out;
}

void assertPreparedActionInvariant(
    const SequencerStepHarness& h,
    const PreparedActionInvariant& expected
) {
    tx::assertStateInvariant(h.state, expected.state);
    tx::assertMusicalSnapshot(h.state, expected.musical);

    const auto product = capturePreparedProductInvariant(h);
    assert(byteHash(&product, sizeof(product)) ==
           byteHash(&expected.product, sizeof(expected.product)));
    const auto activeTrack = expected.product.bankActiveTrack;
    const auto& bankPattern = h.state.sequencerTracks.track(activeTrack);
    seq::SequencerPatternSnapshot bankFlat{};
    seq::captureSnapshot(bankPattern, bankFlat);
    assert(byteHash(&bankFlat, sizeof(bankFlat)) == expected.bankFlatHash);
    assert(bankPattern.graph.get() == expected.bankGraphOwner);
    assert(objectHash(bankPattern.graph.get()) == expected.bankGraphHash);
    assert(bankPattern.ccLanes.get() == expected.bankCcOwner);
    assert(objectHash(bankPattern.ccLanes.get()) == expected.bankCcHash);

    const auto ui = capturePreparedEditorUiInvariant(h);
    assert(byteHash(&ui, sizeof(ui)) == byteHash(&expected.ui, sizeof(expected.ui)));
    const auto clipboard = capturePreparedClipboardInvariant(h.state.structureClipboard);
    assert(byteHash(&clipboard, sizeof(clipboard)) ==
           byteHash(&expected.clipboard, sizeof(expected.clipboard)));
    assert(!h.state.hasPendingSequencerPatternHistoryCoalescing());
}

bool nodeHasCycleStates(const SequencerStepHarness& h, uint16_t nodeId) {
    const auto* graph = core::state::sequencer::graphView(h.state.sequencer.pattern);
    if (graph == nullptr || nodeId >= graph->stepNodeCount) return false;
    return graph->stepNodes[nodeId].has(oc::note::sequencer::STEP_NODE_CYCLE_SET);
}

void configureActiveContentCycleDescendants(
    SequencerStepHarness& h,
    uint8_t step,
    int8_t parentOffset,
    int8_t firstStateOffset,
    int8_t secondStateOffset
) {
    auto& pattern = h.state.sequencer.pattern;
    const auto parentNode = seq::activeContentStepNodeId(h.state.sequencer, step);
    assert(parentNode != oc::note::sequencer::StepSequencerGraphLimits::INVALID_ID);
    assert(seq::setNodeNoteOffset(pattern, parentNode, parentOffset));
    const auto cycle = seq::createCycleStateSet(pattern, parentNode, 2U);
    assert(cycle.ok);
    const auto* graph = seq::graphView(pattern);
    assert(graph != nullptr);
    const auto* cycleSet = graph->cycleSet(cycle.id);
    assert(cycleSet != nullptr);
    assert(cycleSet->length == 2U);
    assert(seq::setNodeNoteOffset(pattern, cycleSet->firstStateNode, firstStateOffset));
    assert(seq::setNodeNoteOffset(
        pattern,
        static_cast<uint16_t>(cycleSet->firstStateNode + 1U),
        secondStateOffset));
}

void assertActiveContentCycleDescendants(
    const SequencerStepHarness& h,
    uint8_t step,
    int8_t firstStateOffset,
    int8_t secondStateOffset
) {
    const auto* graph = seq::graphView(h.state.sequencer.pattern);
    assert(graph != nullptr);
    const auto parentNode = seq::activeContentStepNodeId(h.state.sequencer, step);
    assert(parentNode != oc::note::sequencer::StepSequencerGraphLimits::INVALID_ID);
    const auto* parent = graph->stepNode(parentNode);
    assert(parent != nullptr);
    assert(parent->has(oc::note::sequencer::STEP_NODE_CYCLE_SET));
    const auto* cycleSet = graph->cycleSet(parent->cycleSetId);
    assert(cycleSet != nullptr);
    assert(cycleSet->length == 2U);
    const auto* first = graph->stepNode(cycleSet->firstStateNode);
    const auto* second = graph->stepNode(
        static_cast<uint16_t>(cycleSet->firstStateNode + 1U));
    assert(first != nullptr);
    assert(second != nullptr);
    assert(first->has(oc::note::sequencer::STEP_NODE_NOTE_OFFSET));
    assert(second->has(oc::note::sequencer::STEP_NODE_NOTE_OFFSET));
    assert(first->noteOffset == firstStateOffset);
    assert(second->noteOffset == secondStateOffset);
}

struct GraphReachability {
    using Limits = oc::note::sequencer::StepSequencerGraphLimits;

    std::array<bool, Limits::MAX_STEP_NODES> stepNodes{};
    std::array<bool, Limits::MAX_SEQUENCES> sequences{};
    std::array<bool, Limits::MAX_CYCLE_SETS> cycleSets{};
};

void visitReachableGraphNode(const oc::note::sequencer::StepSequencerGraph& graph, uint16_t nodeId,
                             GraphReachability& reachable);

void markReservedGraphNode(const oc::note::sequencer::StepSequencerGraph& graph, uint16_t nodeId,
                           GraphReachability& reachable) {
    using namespace oc::note::sequencer;

    assert(nodeId < graph.stepNodeCount);
    assert(nodeId < reachable.stepNodes.size());
    assert(!reachable.stepNodes[nodeId]);
    reachable.stepNodes[nodeId] = true;
    const auto& node = graph.stepNodes[nodeId];
    assert(!node.has(STEP_NODE_CHILD_SEQUENCE));
    assert(!node.has(STEP_NODE_CYCLE_SET));
    assert(node.childSequenceId == StepSequencerGraphLimits::INVALID_ID);
    assert(node.cycleSetId == StepSequencerGraphLimits::INVALID_ID);
}

void visitReachableGraphSequence(const oc::note::sequencer::StepSequencerGraph& graph,
                                 uint16_t sequenceId, GraphReachability& reachable) {
    assert(sequenceId < graph.sequenceCount);
    assert(sequenceId < reachable.sequences.size());
    if (reachable.sequences[sequenceId]) return;
    reachable.sequences[sequenceId] = true;

    const auto* sequence = graph.sequence(sequenceId);
    assert(sequence != nullptr);
    for (uint8_t index = 0; index < sequence->length; ++index) {
        visitReachableGraphNode(graph, static_cast<uint16_t>(sequence->firstStepNode + index),
                                reachable);
    }
    const uint8_t reservedCapacity =
        sequence->kind == oc::note::sequencer::StepSequencerSequenceKind::RootPattern
            ? sequence->length
            : oc::note::sequencer::StepSequencerGraphLimits::MAX_EXPANDED_NOTES_PER_ROOT_STEP;
    assert(static_cast<uint32_t>(sequence->firstStepNode) + reservedCapacity <=
           graph.stepNodeCount);
    for (uint8_t index = sequence->length; index < reservedCapacity; ++index) {
        markReservedGraphNode(graph, static_cast<uint16_t>(sequence->firstStepNode + index),
                              reachable);
    }
}

void visitReachableGraphCycleSet(const oc::note::sequencer::StepSequencerGraph& graph,
                                 uint16_t cycleSetId, GraphReachability& reachable) {
    assert(cycleSetId < graph.cycleSetCount);
    assert(cycleSetId < reachable.cycleSets.size());
    if (reachable.cycleSets[cycleSetId]) return;
    reachable.cycleSets[cycleSetId] = true;

    const auto* cycleSet = graph.cycleSet(cycleSetId);
    assert(cycleSet != nullptr);
    for (uint8_t index = 0; index < cycleSet->length; ++index) {
        visitReachableGraphNode(graph, static_cast<uint16_t>(cycleSet->firstStateNode + index),
                                reachable);
    }
    constexpr uint8_t reservedCapacity =
        oc::note::sequencer::StepSequencerGraphLimits::MAX_CYCLE_STATES_PER_SET;
    assert(static_cast<uint32_t>(cycleSet->firstStateNode) + reservedCapacity <=
           graph.stepNodeCount);
    for (uint8_t index = cycleSet->length; index < reservedCapacity; ++index) {
        markReservedGraphNode(graph, static_cast<uint16_t>(cycleSet->firstStateNode + index),
                              reachable);
    }
}

void visitReachableGraphNode(const oc::note::sequencer::StepSequencerGraph& graph, uint16_t nodeId,
                             GraphReachability& reachable) {
    using namespace oc::note::sequencer;

    assert(nodeId < graph.stepNodeCount);
    assert(nodeId < reachable.stepNodes.size());
    if (reachable.stepNodes[nodeId]) return;
    reachable.stepNodes[nodeId] = true;

    const auto& node = graph.stepNodes[nodeId];
    if (node.has(STEP_NODE_CHILD_SEQUENCE)) {
        visitReachableGraphSequence(graph, node.childSequenceId, reachable);
    } else {
        assert(node.childSequenceId == StepSequencerGraphLimits::INVALID_ID);
    }
    if (node.has(STEP_NODE_CYCLE_SET)) {
        visitReachableGraphCycleSet(graph, node.cycleSetId, reachable);
    } else {
        assert(node.cycleSetId == StepSequencerGraphLimits::INVALID_ID);
    }
}

void assertGraphHasNoOrphans(const oc::note::sequencer::StepSequencerGraph& graph) {
    assert(graph.enabled);
    assert(graph.stepNodeCount <= graph.stepNodes.size());
    assert(graph.sequenceCount <= graph.sequences.size());
    assert(graph.cycleSetCount <= graph.cycleSets.size());

    GraphReachability reachable;
    visitReachableGraphSequence(graph, graph.rootSequenceId, reachable);
    for (uint16_t index = 0; index < graph.stepNodeCount; ++index) {
        assert(reachable.stepNodes[index]);
    }
    for (uint16_t index = 0; index < graph.sequenceCount; ++index) {
        assert(reachable.sequences[index]);
    }
    for (uint16_t index = 0; index < graph.cycleSetCount; ++index) {
        assert(reachable.cycleSets[index]);
    }
}

void test_child_creation_draft_apply_and_back_decisions() {
    SequencerStepHarness h;
    h.state.sequencer.pattern.setContentLength(8);

    auto opened = core::state::sequencer::openOrCreateActiveContentChild(
        h.state.sequencer, 2, core::state::sequencer::StepContentChildKind::MICRO_SEQUENCE,
        core::state::sequencer::DEFAULT_MICRO_SEQUENCE_LENGTH);
    assert(opened.opened && opened.draft);
    assert(!rootStepHasMicroSequence(h, 2));
    assert(core::state::sequencer::setActiveContentStepFromNormalized(
        h.state.sequencer, 0, core::state::sequencer::StepProperty::NOTE, 1.0f,
        h.state.sequencer.pattern.pitchEditMode, {}));
    h.tap(Config::ButtonID::BOTTOM_RIGHT);
    assert(!h.state.sequencer.stepContentDraft.active.get());
    assert(rootStepHasMicroSequence(h, 2));
    assert(core::state::sequencer::isMicroSequenceContentView(h.state.sequencer));
    assert(h.state.sequencerHistory.undoCount() == 1);
    h.tap(Config::ButtonID::LEFT_TOP);
    assert(core::state::sequencer::isRootContentView(h.state.sequencer));

    opened = core::state::sequencer::openOrCreateActiveContentChild(
        h.state.sequencer, 3, core::state::sequencer::StepContentChildKind::CYCLE_STATES,
        core::state::sequencer::DEFAULT_CYCLE_STATE_COUNT);
    assert(opened.opened && opened.draft);
    assert(!h.state.sequencer.stepContentDraft.modified());
    h.tap(Config::ButtonID::LEFT_TOP);
    assert(!h.state.sequencer.stepContentDraft.active.get());
    assert(core::state::sequencer::isRootContentView(h.state.sequencer));
    assert(!nodeHasCycleStates(h, core::state::sequencer::rootStepNodeId(3)));
    assert(h.state.sequencerHistory.undoCount() == 1);

    opened = core::state::sequencer::openOrCreateActiveContentChild(
        h.state.sequencer, 3, core::state::sequencer::StepContentChildKind::CYCLE_STATES,
        core::state::sequencer::DEFAULT_CYCLE_STATE_COUNT);
    assert(opened.opened && opened.draft);
    assert(core::state::sequencer::setActiveContentStepFromNormalized(
        h.state.sequencer, 0, core::state::sequencer::StepProperty::VELOCITY, 1.0f,
        h.state.sequencer.pattern.pitchEditMode, {}));
    h.tap(Config::ButtonID::LEFT_TOP);
    assert(h.state.sequencer.stepContentDraft.exitPromptVisible.get());
    assert(h.state.sequencer.stepContentDraft.exitChoice.get() ==
           core::state::sequencer::SequencerStepContentDraftExitChoice::SAVE);
    h.tap(Config::ButtonID::BOTTOM_RIGHT);
    assert(h.state.sequencer.stepContentDraft.active.get());
    assert(h.state.sequencer.stepContentDraft.exitPromptVisible.get());
    assert(h.state.sequencerHistory.undoCount() == 1);
    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.advance(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    h.release(Config::ButtonID::BOTTOM_RIGHT);
    assert(h.state.sequencer.stepContentDraft.active.get());
    assert(h.state.sequencer.stepContentDraft.exitPromptVisible.get());
    assert(h.state.sequencerHistory.undoCount() == 1);
    h.turn(Config::EncoderID::NAV, -1.0f);
    assert(h.state.sequencer.stepContentDraft.exitChoice.get() ==
           core::state::sequencer::SequencerStepContentDraftExitChoice::DISCARD);
    h.tap(Config::ButtonID::NAV);
    assert(!h.state.sequencer.stepContentDraft.active.get());
    assert(core::state::sequencer::isRootContentView(h.state.sequencer));
    assert(!nodeHasCycleStates(h, core::state::sequencer::rootStepNodeId(3)));
    assert(h.state.sequencerHistory.undoCount() == 1);

    std::cout << "[PASS] test_child_creation_draft_apply_and_back_decisions\n";
}

void focusTrackNavigation(SequencerStepHarness& h) {
    h.navigationFocus.set(core::state::StructureNavigationFocus::TRACK);
    h.state.trackNavigation.syncPreviewTrack(h.state.sequencerTracks.activeTrackIndex());
}

using TrackGraph = oc::note::sequencer::StepSequencerGraph;
using TrackCcBank = seq::SequencerCcLaneBank;
using TrackBank = seq::SequencerTrackBankState;

constexpr uint16_t kDirectTrackSparseMask = 0x0025U;
constexpr uint8_t kDirectTrackOldActive = 2U;
constexpr uint8_t kDirectTrackIncoming = 5U;
constexpr uint8_t kDirectTrackCreateTarget = 6U;

enum class DirectTrackFixtureKind : uint8_t {
    Create = 0U,
    RemoveCurrent,
};

struct TrackColdOwners {
    const TrackGraph* graph = nullptr;
    const TrackCcBank* cc = nullptr;
};

TrackColdOwners trackColdOwners(const seq::SequencerPatternState& pattern) {
    return {pattern.graph.get(), pattern.ccLanes.get()};
}

uint64_t trackFlatHash(const seq::SequencerPatternState& pattern) {
    seq::SequencerPatternSnapshot flat{};
    seq::captureSnapshot(pattern, flat);
    const std::array<uint32_t, 6U> revisions{
        pattern.stepDataRevision.get(),
        pattern.patternVariationRevision.get(),
        pattern.patternScaleRevision.get(),
        pattern.patternTimingRevision.get(),
        pattern.graphRevision.get(),
        pattern.ccLaneRevision.get(),
    };
    uint64_t hash = byteHash(&flat, sizeof(flat));
    hash ^= byteHash(revisions.data(), sizeof(revisions));
    hash *= 1099511628211ULL;
    return hash;
}

uint64_t trackMusicalHash(const seq::SequencerPatternState& pattern) {
    seq::SequencerPatternSnapshot flat{};
    seq::captureSnapshot(pattern, flat);
    flat.stepDataRevision = 0U;
    flat.patternVariationRevision = 0U;
    flat.patternScaleRevision = 0U;
    flat.patternTimingRevision = 0U;
    flat.graphRevision = 0U;
    return byteHash(&flat, sizeof(flat));
}

void installTrackColdOwners(seq::SequencerPatternState& pattern, uint8_t tag) {
    pattern.graph = core::app::makeExtmemUnique<TrackGraph>();
    pattern.ccLanes = core::app::makeExtmemUnique<TrackCcBank>();
    assert(pattern.graph);
    assert(pattern.ccLanes);
    pattern.graph->enabled = true;
    pattern.note[0U] = static_cast<uint8_t>(48U + tag);
    pattern.velocity[0U] = static_cast<uint8_t>(80U + tag);
    pattern.setEnabled(0U, true);
    pattern.stepDataRevision.set(100U + tag);
    pattern.graphRevision.set(200U + tag);
    seq::SequencerCcLaneDraft draft{};
    draft.destination.controller = static_cast<uint8_t>(70U + tag);
    assert(seq::createSequencerCcLane(*pattern.ccLanes, 0U, draft).changed());
    assert(seq::setSequencerCcLaneEvent(
               *pattern.ccLanes,
               0U,
               0U,
               static_cast<uint8_t>(90U + tag)
           ).changed());
    pattern.ccLaneRevision.set(pattern.ccLanes->revision);
}

struct TrackPatternPhysicalInvariant {
    static constexpr std::size_t COUNT = TrackBank::TRACK_COUNT + 1U;
    std::array<const void*, COUNT> graphOwners{};
    std::array<const void*, COUNT> ccOwners{};
    std::array<uint64_t, COUNT> flatHashes{};
    std::array<uint64_t, COUNT> graphHashes{};
    std::array<uint64_t, COUNT> ccHashes{};
};

void captureTrackPatternPhysical(
    const seq::SequencerPatternState& pattern,
    std::size_t index,
    TrackPatternPhysicalInvariant& out
) {
    out.graphOwners[index] = pattern.graph.get();
    out.ccOwners[index] = pattern.ccLanes.get();
    out.flatHashes[index] = trackFlatHash(pattern);
    out.graphHashes[index] = objectHash(pattern.graph.get());
    out.ccHashes[index] = objectHash(pattern.ccLanes.get());
}

TrackPatternPhysicalInvariant captureTrackPatternPhysicalInvariant(
    const SequencerStepHarness& h
) {
    TrackPatternPhysicalInvariant out{};
    captureTrackPatternPhysical(h.state.sequencer.pattern, 0U, out);
    for (uint8_t track = 0U; track < TrackBank::TRACK_COUNT; ++track) {
        captureTrackPatternPhysical(
            h.state.sequencerTracks.track(track),
            static_cast<std::size_t>(track) + 1U,
            out
        );
    }
    return out;
}

struct TrackUiInvariant {
    bool previewAdd = false;
    uint8_t previewTrack = 0U;
    uint8_t holdAction = 0U;
    uint32_t holdStartedAtMs = 0U;
    bool selectionActive = false;
    bool selectionPlacing = false;
    uint8_t selectionScope = 0U;
    uint8_t selectionCursor = 0U;
    uint16_t selectionMask = 0U;
    uint16_t destinationMask = 0U;
    uint16_t overwriteMask = 0U;
    bool pasteBlocked = false;
    uint32_t clipboardRevision = 0U;
};

TrackUiInvariant captureTrackUiInvariant(const SequencerStepHarness& h) {
    const auto& ui = h.state.trackNavigation;
    return {
        .previewAdd = ui.previewAddSlot.get(),
        .previewTrack = ui.previewTrackIndex.get(),
        .holdAction = static_cast<uint8_t>(ui.hold.action.get()),
        .holdStartedAtMs = ui.hold.startedAtMs.get(),
        .selectionActive = ui.selection.active.get(),
        .selectionPlacing = ui.selection.placing.get(),
        .selectionScope = static_cast<uint8_t>(ui.selection.scope.get()),
        .selectionCursor = ui.selection.cursorIndex.get(),
        .selectionMask = ui.selection.selectedMask.get(),
        .destinationMask = ui.selection.destinationMask.get(),
        .overwriteMask = ui.selection.overwriteMask.get(),
        .pasteBlocked = ui.selection.pasteBlocked.get(),
        .clipboardRevision = ui.selection.clipboardRevision.get(),
    };
}

struct TrackTransientUiInvariant {
    bool contextVisible = false;
    uint8_t contextFocus = 0U;
    uint32_t contextRevision = 0U;
    uint64_t stepEditHash = 0U;
    uint64_t ccLaneUiHash = 0U;
    uint64_t stepPropertySelectorHash = 0U;
    uint64_t stepInlineFeedbackHash = 0U;
    uint64_t quickControlsHash = 0U;
    uint64_t contentViewHash = 0U;
    uint64_t stepDraftHash = 0U;
    uint64_t trackPasteHash = 0U;
};

TrackTransientUiInvariant captureTrackTransientUiInvariant(
    const SequencerStepHarness& h
) {
    const auto& sequencer = h.state.sequencer;
    return {
        .contextVisible = sequencer.contextSelector.visible,
        .contextFocus = static_cast<uint8_t>(
            sequencer.contextSelector.previewFocus),
        .contextRevision = sequencer.contextSelector.revision.get(),
        .stepEditHash = byteHash(&sequencer.stepEdit, sizeof(sequencer.stepEdit)),
        .ccLaneUiHash = byteHash(&sequencer.ccLaneUi, sizeof(sequencer.ccLaneUi)),
        .stepPropertySelectorHash = byteHash(
            &sequencer.stepPropertyInlineSelector,
            sizeof(sequencer.stepPropertyInlineSelector)),
        .stepInlineFeedbackHash = byteHash(
            &sequencer.stepInlineFeedback,
            sizeof(sequencer.stepInlineFeedback)),
        .quickControlsHash = byteHash(
            &sequencer.patternQuickControls,
            sizeof(sequencer.patternQuickControls)),
        .contentViewHash = byteHash(
            &sequencer.contentView,
            sizeof(sequencer.contentView)),
        .stepDraftHash = byteHash(
            &sequencer.stepContentDraft,
            sizeof(sequencer.stepContentDraft)),
        .trackPasteHash = byteHash(
            &sequencer.structureUi.trackPaste,
            sizeof(sequencer.structureUi.trackPaste)),
    };
}

struct TrackMacroInvariant {
    uint64_t tracksHash = 0U;
    uint64_t activeConfigsHash = 0U;
    uint64_t controlAuthoredHash = 0U;
    uint64_t manualOverridesHash = 0U;
    uint64_t contextSelectorHash = 0U;
    std::array<float, core::state::macro::MACRO_COUNT> runtimeValues{};
    uint16_t enabledTrackMask = 0U;
    uint16_t enabledPageMask = 0U;
    uint16_t manualMask = 0U;
    uint8_t activeTrack = 0U;
    uint8_t activePage = 0U;
    uint32_t manualRevision = 0U;
    uint32_t rejectedActivationCount = 0U;
    uint32_t controlAuthoredRevision = 0U;
    uint32_t configRevision = 0U;
    uint32_t automationEditRevision = 0U;
    uint32_t runtimeProjectionRevision = 0U;
    uint32_t runtimeOwnerRevision = 0U;
};

TrackMacroInvariant captureTrackMacroInvariant(const SequencerStepHarness& h) {
    TrackMacroInvariant out{};
    out.tracksHash = byteHash(
        h.state.pages.tracks.data(), sizeof(h.state.pages.tracks));
    out.activeConfigsHash = byteHash(
        h.state.pages.activeConfigs.data(), sizeof(h.state.pages.activeConfigs));
    out.controlAuthoredHash = byteHash(
        &h.state.pages.control.authored,
        sizeof(h.state.pages.control.authored));
    out.manualOverridesHash = byteHash(
        &h.state.macroUi.manualOverrides,
        sizeof(h.state.macroUi.manualOverrides));
    out.contextSelectorHash = byteHash(
        &h.state.macroUi.contextSelector,
        sizeof(h.state.macroUi.contextSelector));
    for (uint8_t macro = 0U;
         macro < core::state::macro::MACRO_COUNT;
         ++macro) {
        out.runtimeValues[macro] = h.state.macros.slots[macro].value.get();
    }
    out.enabledTrackMask = h.state.pages.currentTrackEnabledMask();
    out.enabledPageMask = h.state.pages.currentEnabledPageMask();
    out.manualMask = h.state.macroUi.automationManualOverrideMask.get();
    out.activeTrack = h.state.pages.currentActiveTrack();
    out.activePage = h.state.pages.currentActivePage();
    out.manualRevision = h.state.macroUi.manualOverrides.revision;
    out.rejectedActivationCount =
        h.state.macroUi.manualOverrides.rejectedActivationCount;
    out.controlAuthoredRevision = h.state.pages.control.authoredRevision;
    out.configRevision = h.state.configRevision.get();
    out.automationEditRevision = h.state.macroUi.automationEditRevision.get();
    out.runtimeProjectionRevision =
        h.state.macroUi.runtimeProjectionRevision.get();
    out.runtimeOwnerRevision = h.state.macroRuntimeOwnerRevision.get();
    return out;
}

struct TrackTransactionInvariant {
    TrackPatternPhysicalInvariant patterns{};
    TrackUiInvariant trackUi{};
    TrackTransientUiInvariant transientUi{};
    TrackMacroInvariant macros{};
    PreparedEditorUiInvariant editorUi{};
    PreparedClipboardInvariant clipboard{};
    PreparedProductInvariant product{};
    tx::StateInvariant publication{};
    uint64_t projectTracksHash = 0U;
};

TrackTransactionInvariant captureTrackTransactionInvariant(
    const SequencerStepHarness& h
) {
    return {
        .patterns = captureTrackPatternPhysicalInvariant(h),
        .trackUi = captureTrackUiInvariant(h),
        .transientUi = captureTrackTransientUiInvariant(h),
        .macros = captureTrackMacroInvariant(h),
        .editorUi = capturePreparedEditorUiInvariant(h),
        .clipboard = capturePreparedClipboardInvariant(h.state.structureClipboard),
        .product = capturePreparedProductInvariant(h),
        .publication = tx::captureStateInvariant(h.state),
        .projectTracksHash = byteHash(
            &h.state.projectTracks.authored,
            sizeof(h.state.projectTracks.authored)),
    };
}

void assertTrackTransactionInvariant(
    const SequencerStepHarness& h,
    const TrackTransactionInvariant& expected,
    bool expectedPatternCoalescing = false
) {
    const auto actual = captureTrackTransactionInvariant(h);
    assert(actual.patterns.graphOwners == expected.patterns.graphOwners);
    assert(actual.patterns.ccOwners == expected.patterns.ccOwners);
    assert(actual.patterns.flatHashes == expected.patterns.flatHashes);
    assert(actual.patterns.graphHashes == expected.patterns.graphHashes);
    assert(actual.patterns.ccHashes == expected.patterns.ccHashes);
    assert(actual.trackUi.previewAdd == expected.trackUi.previewAdd);
    assert(actual.trackUi.previewTrack == expected.trackUi.previewTrack);
    assert(actual.trackUi.holdAction == expected.trackUi.holdAction);
    assert(actual.trackUi.holdStartedAtMs ==
           expected.trackUi.holdStartedAtMs);
    assert(actual.trackUi.selectionActive ==
           expected.trackUi.selectionActive);
    assert(actual.trackUi.selectionPlacing ==
           expected.trackUi.selectionPlacing);
    assert(actual.trackUi.selectionScope ==
           expected.trackUi.selectionScope);
    assert(actual.trackUi.selectionCursor ==
           expected.trackUi.selectionCursor);
    assert(actual.trackUi.selectionMask == expected.trackUi.selectionMask);
    assert(actual.trackUi.destinationMask ==
           expected.trackUi.destinationMask);
    assert(actual.trackUi.overwriteMask == expected.trackUi.overwriteMask);
    assert(actual.trackUi.pasteBlocked == expected.trackUi.pasteBlocked);
    assert(actual.trackUi.clipboardRevision ==
           expected.trackUi.clipboardRevision);
    assert(actual.transientUi.contextVisible ==
           expected.transientUi.contextVisible);
    assert(actual.transientUi.contextFocus ==
           expected.transientUi.contextFocus);
    assert(actual.transientUi.contextRevision ==
           expected.transientUi.contextRevision);
    assert(actual.transientUi.stepEditHash ==
           expected.transientUi.stepEditHash);
    assert(actual.transientUi.ccLaneUiHash ==
           expected.transientUi.ccLaneUiHash);
    assert(actual.transientUi.stepPropertySelectorHash ==
           expected.transientUi.stepPropertySelectorHash);
    assert(actual.transientUi.stepInlineFeedbackHash ==
           expected.transientUi.stepInlineFeedbackHash);
    assert(actual.transientUi.quickControlsHash ==
           expected.transientUi.quickControlsHash);
    assert(actual.transientUi.contentViewHash ==
           expected.transientUi.contentViewHash);
    assert(actual.transientUi.stepDraftHash ==
           expected.transientUi.stepDraftHash);
    assert(actual.transientUi.trackPasteHash ==
           expected.transientUi.trackPasteHash);
    assert(byteHash(&actual.macros, sizeof(actual.macros)) ==
           byteHash(&expected.macros, sizeof(expected.macros)));
    assert(byteHash(&actual.editorUi, sizeof(actual.editorUi)) ==
           byteHash(&expected.editorUi, sizeof(expected.editorUi)));
    assert(byteHash(&actual.clipboard, sizeof(actual.clipboard)) ==
           byteHash(&expected.clipboard, sizeof(expected.clipboard)));
    assert(byteHash(&actual.product, sizeof(actual.product)) ==
           byteHash(&expected.product, sizeof(expected.product)));
    tx::assertStateInvariant(h.state, expected.publication);
    assert(actual.projectTracksHash == expected.projectTracksHash);
    assert(h.state.hasPendingSequencerPatternHistoryCoalescing() ==
           expectedPatternCoalescing);
}

struct CanonicalTrackLogicalProof {
    std::array<uint64_t, TrackBank::TRACK_COUNT> flatHashes{};
    std::array<uint64_t, TrackBank::TRACK_COUNT> graphHashes{};
    std::array<uint64_t, TrackBank::TRACK_COUNT> ccHashes{};
    std::array<bool, TrackBank::TRACK_COUNT> hasGraph{};
    std::array<bool, TrackBank::TRACK_COUNT> hasCc{};
    TrackMacroInvariant macros{};
    uint16_t trackMask = 0U;
    uint16_t sharedMask = 0U;
    uint8_t trackActive = 0U;
    uint8_t sharedActive = 0U;
    uint8_t focusedStep = 0U;
    uint8_t page = 0U;
    uint64_t projectTracksHash = 0U;
};

CanonicalTrackLogicalProof captureCanonicalTrackLogicalProof(
    const SequencerStepHarness& h
) {
    CanonicalTrackLogicalProof out{};
    const uint8_t active = h.state.sequencerTracks.activeTrackIndex();
    for (uint8_t track = 0U; track < TrackBank::TRACK_COUNT; ++track) {
        const auto& pattern = track == active
            ? h.state.sequencer.pattern
            : h.state.sequencerTracks.track(track);
        out.flatHashes[track] = trackMusicalHash(pattern);
        out.graphHashes[track] = objectHash(pattern.graph.get());
        out.ccHashes[track] = objectHash(pattern.ccLanes.get());
        out.hasGraph[track] = pattern.graph != nullptr;
        out.hasCc[track] = pattern.ccLanes != nullptr;
    }
    out.macros = captureTrackMacroInvariant(h);
    out.trackMask = h.state.sequencerTracks.currentEnabledMask();
    out.sharedMask = h.state.sharedTrackEnabledMask.get();
    out.trackActive = active;
    out.sharedActive = h.state.sharedTrackActive.get();
    out.focusedStep = h.state.sequencer.focusedStep.get();
    out.page = h.state.sequencer.page.get();
    out.projectTracksHash = byteHash(
        &h.state.projectTracks.authored,
        sizeof(h.state.projectTracks.authored));
    return out;
}

void assertCanonicalTrackLogicalProof(
    const SequencerStepHarness& h,
    const CanonicalTrackLogicalProof& expected
) {
    const auto actual = captureCanonicalTrackLogicalProof(h);
    assert(actual.flatHashes == expected.flatHashes);
    assert(actual.graphHashes == expected.graphHashes);
    assert(actual.ccHashes == expected.ccHashes);
    assert(actual.hasGraph == expected.hasGraph);
    assert(actual.hasCc == expected.hasCc);
    assert(byteHash(&actual.macros, sizeof(actual.macros)) ==
           byteHash(&expected.macros, sizeof(expected.macros)));
    assert(actual.trackMask == expected.trackMask);
    assert(actual.sharedMask == expected.sharedMask);
    assert(actual.trackActive == expected.trackActive);
    assert(actual.sharedActive == expected.sharedActive);
    assert(actual.focusedStep == expected.focusedStep);
    assert(actual.page == expected.page);
    assert(actual.projectTracksHash == expected.projectTracksHash);
}

void configureMacroTrackFixture(
    core::state::CoreState& state,
    uint8_t track,
    uint8_t page,
    float base,
    uint8_t manualMacro,
    float manualValue
) {
    auto& trackData = state.pages.tracks[track];
    trackData.activePage = page;
    trackData.enabledPageMask = static_cast<uint16_t>(
        0x0001U | static_cast<uint16_t>(1U << page));
    auto& pageData = trackData.pages[page];
    pageData.activeMacroMask = 0xFFU;
    for (uint8_t macro = 0U;
         macro < core::state::macro::MACRO_COUNT;
         ++macro) {
        pageData.values[macro] = base + static_cast<float>(macro) * 0.01f;
        pageData.cc[macro] = static_cast<uint8_t>(20U + track + macro);
    }
    using ActivateStatus =
        core::state::macro::MacroManualOverrideState::ActivateStatus;
    assert(state.macroUi.manualOverrides.activate(
               core::state::macro::MacroAutomationSlotAddress{
                   .track = track,
                   .page = page,
                   .macro = manualMacro,
               },
               manualValue
           ) == ActivateStatus::ACTIVATED);
}

void assertActiveMacroPresentation(const SequencerStepHarness& h) {
    const uint8_t track = h.state.pages.currentActiveTrack();
    const uint8_t page = h.state.pages.currentActivePage();
    const auto& pageData = h.state.pages.activePageData();
    uint16_t expectedManualMask = 0U;
    for (uint8_t macro = 0U;
         macro < core::state::macro::MACRO_COUNT;
         ++macro) {
        float expected = pageData.values[macro];
        float manual = 0.0f;
        if (h.state.macroUi.manualOverrides.valueFor(
                core::state::macro::MacroAutomationSlotAddress{
                    .track = track,
                    .page = page,
                    .macro = macro,
                },
                manual
            )) {
            expected = manual;
            expectedManualMask |= static_cast<uint16_t>(1U << macro);
        }
        assert(h.state.macros.slots[macro].value.get() == expected);
    }
    assert(h.state.macroUi.automationManualOverrideMask.get() ==
           expectedManualMask);
}

struct DirectTrackFixture {
    TrackColdOwners editor{};
    TrackColdOwners scratch{};
    TrackColdOwners incoming{};
    uint32_t selectorRevision = 0U;
};

DirectTrackFixture configureDirectTrackFixture(
    SequencerStepHarness& h,
    DirectTrackFixtureKind kind
) {
    auto& state = h.state;
    state.sequencerTracks.reset();
    assert(state.setSharedTrackState(
        kDirectTrackSparseMask,
        kDirectTrackOldActive));
    test_support::drainNotifications();

    const uint8_t target = kind == DirectTrackFixtureKind::Create
        ? kDirectTrackCreateTarget
        : kDirectTrackIncoming;
    state.sequencer.pattern.setContentLength(40U);
    state.sequencerTracks.track(kDirectTrackOldActive).setContentLength(11U);
    state.sequencerTracks.track(target).setContentLength(
        kind == DirectTrackFixtureKind::Create ? 24U : 5U);
    installTrackColdOwners(state.sequencer.pattern, 1U);
    installTrackColdOwners(
        state.sequencerTracks.track(kDirectTrackOldActive), 2U);
    installTrackColdOwners(state.sequencerTracks.track(target), 3U);

    configureMacroTrackFixture(
        state, kDirectTrackOldActive, 1U, 0.21f, 2U, 0.72f);
    configureMacroTrackFixture(
        state,
        target,
        kind == DirectTrackFixtureKind::Create ? 3U : 2U,
        kind == DirectTrackFixtureKind::Create ? 0.41f : 0.31f,
        kind == DirectTrackFixtureKind::Create ? 1U : 6U,
        kind == DirectTrackFixtureKind::Create ? 0.86f : 0.16f);
    state.pages.syncSharedTrackState(
        kDirectTrackSparseMask,
        kDirectTrackOldActive);
    auto shared = core::handler::SharedTrackDomainServices::fromCoreState(state);
    assert(shared.canReconcilePreparedSequencerActiveTrackPresentation());
    shared.reconcilePreparedSequencerActiveTrackPresentation();
    state.macroUi.contextSelector.show(
        core::state::StructureNavigationFocus::TRACK);

    state.sequencer.focusedStep.set(31U);
    state.sequencer.page.set(3U);
    focusTrackNavigation(h);
    state.trackNavigation.previewAddSlot.set(
        kind == DirectTrackFixtureKind::Create);
    state.trackNavigation.previewTrackIndex.set(
        kind == DirectTrackFixtureKind::Create
            ? kDirectTrackCreateTarget
            : kDirectTrackOldActive);
    state.trackNavigation.selection.active.set(false);
    state.trackNavigation.selection.placing.set(false);
    state.trackNavigation.selection.scope.set(
        core::state::StructureSelectionScope::TRACK);
    state.trackNavigation.selection.cursorIndex.set(9U);
    state.trackNavigation.selection.selectedMask.set(0x0080U);
    state.trackNavigation.selection.destinationMask.set(0x0100U);
    state.trackNavigation.selection.overwriteMask.set(0x0200U);
    state.trackNavigation.selection.pasteBlocked.set(true);
    state.trackNavigation.selection.clipboardRevision.set(71U);
    state.sequencer.structureUi.previewPageIndex.set(3U);
    state.sequencer.contextSelector.visible = true;
    state.sequencer.contextSelector.previewFocus =
        core::state::StructureNavigationFocus::TRACK;
    state.sequencer.contextSelector.revision.set(40U);
    state.sequencer.patternQuickControls.selecting.set(true);
    state.sequencer.stepInlineFeedback.show(
        3U,
        seq::StepProperty::VELOCITY,
        1000U);
    test_support::drainNotifications();
    const auto settledBoundary =
        state.openSequencerTrackStructureChronologyBoundary();
    assert(settledBoundary.status ==
           seq::SequencerTrackStructureChronologyStatus::Opened);
    assert(settledBoundary.predecessorPattern ==
           seq::SequencerPatternHistoryCommitOutcome::NoPending);
    test_support::drainNotifications();

    return {
        .editor = trackColdOwners(state.sequencer.pattern),
        .scratch = trackColdOwners(
            state.sequencerTracks.track(kDirectTrackOldActive)),
        .incoming = trackColdOwners(state.sequencerTracks.track(target)),
        .selectorRevision = state.sequencer.contextSelector.revision.get(),
    };
}

core::handler::SequencerDirectTrackStructureStateRefs directTrackRefs(
    SequencerStepHarness& h
) {
    return {
        h.state.sequencerTracks,
        h.state.sequencer,
        h.navigationFocus,
        h.state.trackNavigation,
        h.state.structureClipboard,
        h.state.pages,
        h.state.sequencerTrackActivations,
        core::handler::SharedTrackDomainServices::fromCoreState(h.state),
        HistoryServices::fromCoreState(h.state),
    };
}

void armTrackActivation(
    seq::SequencerTrackActivationQueue& queue,
    uint16_t trackMask,
    bool transportPlaying
) {
    seq::SequencerTrackActivationBatch batch{};
    assert(queue.prepare(
        trackMask,
        0xFFFFU,
        transportPlaying,
        batch,
        seq::SequencerTrackActivationOrigin::TRACK_PASTE));
    assert(queue.armPrepared(batch));
    queue.publishPrepared(batch);
}

seq::SequencerTrackActivationMutationGuard captureActivationQueueGuard(
    const seq::SequencerTrackActivationQueue& queue,
    uint16_t pendingTrackMask
) {
    // captureMutationGuard records every queue entry and every monotonic
    // counter. Its protected mask must exclude the deliberately pending
    // collision, so use an otherwise idle Track solely as the validity bit.
    uint16_t guardTrack = 0x8000U;
    if ((pendingTrackMask & guardTrack) != 0U) {
        guardTrack = 0x4000U;
    }
    assert((pendingTrackMask & guardTrack) == 0U);
    seq::SequencerTrackActivationMutationGuard guard{};
    assert(queue.captureMutationGuard(guardTrack, guard));
    assert(guard.valid());
    return guard;
}

void assertSameActivationQueueGuard(
    const seq::SequencerTrackActivationMutationGuard& actual,
    const seq::SequencerTrackActivationMutationGuard& expected
) {
    assert(actual.generations == expected.generations);
    assert(actual.operationIds == expected.operationIds);
    assert(actual.nextGeneration == expected.nextGeneration);
    assert(actual.nextOperationId == expected.nextOperationId);
    assert(actual.telemetryRevision == expected.telemetryRevision);
    assert(actual.packedEntries == expected.packedEntries);
    assert(actual.protectedTrackMask == expected.protectedTrackMask);
}

void preparePendingTrackPatternEdit(core::state::CoreState& state) {
    constexpr auto owner =
        seq::SequencerPreparedPatternEditOwner::PatternEditor;
    constexpr uint8_t key = 91U;
    const auto descriptor = seq::SequencerHistoryDescriptor{
        .kind = seq::SequencerHistoryActionKind::StepEdit,
        .stepIndex = 0U,
    };
    assert(state.beginOrContinueSequencerPreparedPatternEdit(
               owner,
               key,
               seq::SequencerCoalescedPatternPayloadPlan::FlatOnly,
               descriptor) ==
           seq::SequencerPreparedPatternEditBeginOutcome::Started);
    const uint8_t nextNote = state.sequencer.pattern.note[0U] == 73U
        ? 74U
        : 73U;
    assert(state.sequencer.setStepNoteAt(0U, nextNote));
    assert(state.sealSequencerPreparedPatternEdit(
               owner,
               key,
               true,
               descriptor) ==
           seq::SequencerPreparedPatternEditSealOutcome::Sealed);
    assert(state.hasPendingSequencerPatternHistoryCoalescing());
}

void assertSingleTrackStructurePublication(
    const SequencerStepHarness& h,
    const tx::StateInvariant& before
) {
    const auto after = tx::captureStateInvariant(h.state);
    assert(after.sequencerUndoCount == before.sequencerUndoCount + 1U);
    assert(after.sequencerRedoCount == 0U);
    assert(after.projectUndoCount == before.projectUndoCount + 1U);
    assert(after.projectRedoCount == 0U);
    assert(after.sequencerUndoIdentity != 0U);
    assert(after.sequencerUndoIdentity != before.sequencerUndoIdentity);
    assert(after.retainedBytes > before.retainedBytes);
    assert(after.modifiedCounter == before.modifiedCounter + 1U);
    assert(after.dirty);
    assert(after.sessionSavePending);
}

void openPatternEditor(SequencerStepHarness& h) {
    h.navigationFocus.set(core::state::StructureNavigationFocus::PAGE);
    h.tap(Config::ButtonID::NAV);
    assert(h.state.sequencer.patternEditor.active.get());
    assert(h.overlays.current() == core::ui::OverlayType::SEQ_PATTERN_EDIT);
}

void test_nav_context_selector_previews_and_applies_all_three_contexts() {
    SequencerStepHarness h;

    assert(h.navigationFocus.get() == core::state::StructureNavigationFocus::PAGE);
    h.press(Config::ButtonID::NAV);
    assert(h.state.sequencer.contextSelector.visible);
    h.turn(Config::EncoderID::NAV, -1.0f);
    assert(h.state.sequencer.contextSelector.previewFocus ==
           core::state::StructureNavigationFocus::TRACK);
    assert(h.navigationFocus.get() == core::state::StructureNavigationFocus::PAGE);
    h.release(Config::ButtonID::NAV);
    assert(h.navigationFocus.get() == core::state::StructureNavigationFocus::TRACK);

    h.press(Config::ButtonID::NAV);
    h.turn(Config::EncoderID::NAV, 1.0f);
    h.turn(Config::EncoderID::NAV, 1.0f);
    h.release(Config::ButtonID::NAV);
    assert(h.navigationFocus.get() == core::state::StructureNavigationFocus::STEP);

    h.press(Config::ButtonID::NAV);
    h.advance(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    assert(h.state.sequencer.structureUi.stepSelection.active.get());
    assert(!h.state.sequencer.contextSelector.visible);
    h.release(Config::ButtonID::NAV);
    assert(h.navigationFocus.get() == core::state::StructureNavigationFocus::STEP);
    assert(h.state.sequencer.structureUi.stepSelection.active.get());
    assert(!h.state.sequencer.structureUi.stepSelection.selected(0));
    h.tap(Config::ButtonID::LEFT_TOP);
    assert(!h.state.sequencer.structureUi.stepSelection.active.get());

    h.navigationFocus.set(core::state::StructureNavigationFocus::PAGE);
    h.tap(Config::ButtonID::NAV);
    assert(h.navigationFocus.get() == core::state::StructureNavigationFocus::PAGE);
    assert(!h.state.sequencer.contextSelector.visible);
    assert(h.state.sequencer.patternEditor.active.get());
    assert(h.overlays.current() == core::ui::OverlayType::SEQ_PATTERN_EDIT);

    std::cout << "[PASS] test_nav_context_selector_previews_and_applies_all_three_contexts\n";
}

void test_latched_track_editor_release_cannot_cross_into_page_editor() {
    SequencerStepHarness h;
    h.state.sequencer.pattern.setContentLength(8U);
    h.state.sequencerTracks.reset();
    h.state.setSharedTrackState(0x0001U, 0U);
    h.navigationFocus.set(core::state::StructureNavigationFocus::TRACK);
    h.state.trackNavigation.syncPreviewTrack(1U);
    h.state.trackNavigation.previewAddSlot.set(true);
    test_support::drainNotifications();

    h.press(Config::ButtonID::NAV);
    assert(h.state.sequencer.contextSelector.visible);

    // The physical release remains a latched OPEN_TRACK_EDITOR action. A
    // concurrent focus rewrite must fail closed rather than reinterpret it as
    // an OPEN_PATTERN_EDITOR action.
    h.navigationFocus.set(core::state::StructureNavigationFocus::PAGE);
    h.state.sequencer.structureUi.syncPreviewPage(0U);
    test_support::drainNotifications();

    {
        core::app::testing::ScopedExtmemAllocationFailure failure(1U);
        h.release(Config::ButtonID::NAV);
        assert(core::app::testing::extmemAllocationAttempt == 0U);
    }
    test_support::drainNotifications();

    assert(!h.state.sequencer.contextSelector.visible);
    assert(h.state.sequencer.pattern.length.get() == 8U);
    assert(h.state.sequencerHistory.undoCount() == 0U);
    assert(h.state.sharedTrackEnabledMask.get() == 0x0001U);
    assert(h.state.sharedTrackActive.get() == 0U);
    assert(!h.state.sequencer.patternEditor.active.get());
    assert(h.state.sequencer.structureUi.previewPageIndex.get() == 0U);

    std::cout
        << "[PASS] latched Track editor release cannot open Page editor after focus drift\n";
}

void test_page_navigation_is_cyclic_and_tap_opens_pattern_editor() {
    {
        SequencerStepHarness h;
        h.state.sequencer.pattern.setContentLength(24U);
        h.state.sequencer.page.set(0U);
        h.state.sequencer.focusedStep.set(0U);
        h.navigationFocus.set(core::state::StructureNavigationFocus::PAGE);
        test_support::drainNotifications();

        {
            core::app::testing::ScopedExtmemAllocationFailure failure(1U);
            h.turn(Config::EncoderID::NAV, -1.0f);
            assert(core::app::testing::extmemAllocationAttempt == 0U);
        }
        assert(h.state.sequencer.page.get() == 2U);
        assert(h.state.sequencer.focusedStep.get() == 16U);
        assert(h.state.sequencer.structureUi.previewPageIndex.get() == 2U);

        h.turn(Config::EncoderID::NAV, 1.0f);
        assert(h.state.sequencer.page.get() == 0U);
        assert(h.state.sequencer.focusedStep.get() == 0U);
        assert(h.state.sequencer.structureUi.previewPageIndex.get() == 0U);

        h.tap(Config::ButtonID::NAV);
        test_support::drainNotifications();

        assert(h.state.sequencer.pattern.length.get() == 24U);
        assert(h.state.sequencer.patternEditor.active.get());
        assert(h.overlays.current() == core::ui::OverlayType::SEQ_PATTERN_EDIT);
        assert(h.state.sequencerHistory.undoCount() == 0U);
    }

    {
        SequencerStepHarness h;
        h.state.sequencer.pattern.setContentLength(8U);
        h.state.sequencerTracks.reset();
        (void)h.state.setSharedTrackState(0x0001U, 0U);
        assert(h.state.sharedTrackEnabledMask.get() == 0x0001U);
        assert(h.state.sharedTrackActive.get() == 0U);
        h.navigationFocus.set(core::state::StructureNavigationFocus::PAGE);
        h.state.sequencer.structureUi.syncPreviewPage(0U);
        test_support::drainNotifications();

        h.press(Config::ButtonID::NAV);
        assert(h.state.sequencer.contextSelector.visible);
        h.navigationFocus.set(core::state::StructureNavigationFocus::TRACK);
        h.state.trackNavigation.syncPreviewTrack(1U);
        h.state.trackNavigation.previewAddSlot.set(true);
        test_support::drainNotifications();

        {
            core::app::testing::ScopedExtmemAllocationFailure failure(1U);
            h.release(Config::ButtonID::NAV);
            assert(core::app::testing::extmemAllocationAttempt == 0U);
        }
        test_support::drainNotifications();

        assert(!h.state.sequencer.contextSelector.visible);
        assert(h.state.sequencer.pattern.length.get() == 8U);
        assert(h.state.sharedTrackEnabledMask.get() == 0x0001U);
        assert(h.state.sharedTrackActive.get() == 0U);
        assert(h.state.sequencerHistory.undoCount() == 0U);
        assert(h.state.projectHistory.undoCount() == 0U);
        assert(h.state.trackNavigation.previewAddSlot.get());
        assert(h.state.trackNavigation.previewTrackIndex.get() == 1U);
        assert(!h.state.sequencer.patternEditor.active.get());
        assert(h.state.sequencer.structureUi.previewPageIndex.get() == 0U);
    }

    std::cout
        << "[PASS] Page NAV is cyclic and its latched editor cannot create a Track\n";
}

void test_latched_editor_target_drift_fails_closed() {
    {
        SequencerStepHarness h;
        h.state.sequencer.pattern.setContentLength(16U);
        h.navigationFocus.set(core::state::StructureNavigationFocus::PAGE);
        h.state.sequencer.structureUi.syncPreviewPage(0U);
        test_support::drainNotifications();

        h.press(Config::ButtonID::NAV);
        h.state.sequencer.structureUi.syncPreviewPage(1U);
        {
            core::app::testing::ScopedExtmemAllocationFailure failure(1U);
            h.release(Config::ButtonID::NAV);
            assert(core::app::testing::extmemAllocationAttempt == 0U);
        }
        test_support::drainNotifications();

        assert(h.state.sequencer.pattern.length.get() == 16U);
        assert(h.state.sequencerHistory.undoCount() == 0U);
        assert(h.state.projectHistory.undoCount() == 0U);
        assert(!h.state.sequencer.patternEditor.active.get());
        assert(h.state.sequencer.structureUi.previewPageIndex.get() == 1U);
    }

    {
        SequencerStepHarness h;
        h.state.sequencerTracks.reset();
        (void)h.state.setSharedTrackState(0x0001U, 0U);
        assert(h.state.sharedTrackEnabledMask.get() == 0x0001U);
        assert(h.state.sharedTrackActive.get() == 0U);
        h.navigationFocus.set(core::state::StructureNavigationFocus::TRACK);
        h.state.trackNavigation.syncPreviewTrack(1U);
        h.state.trackNavigation.previewAddSlot.set(true);
        test_support::drainNotifications();

        h.press(Config::ButtonID::NAV);
        h.state.trackNavigation.syncPreviewTrack(2U);
        h.state.trackNavigation.previewAddSlot.set(true);
        {
            core::app::testing::ScopedExtmemAllocationFailure failure(1U);
            h.release(Config::ButtonID::NAV);
            assert(core::app::testing::extmemAllocationAttempt == 0U);
        }
        test_support::drainNotifications();

        assert(h.state.sharedTrackEnabledMask.get() == 0x0001U);
        assert(h.state.sharedTrackActive.get() == 0U);
        assert(h.state.sequencerHistory.undoCount() == 0U);
        assert(h.state.projectHistory.undoCount() == 0U);
        assert(h.state.trackNavigation.previewAddSlot.get());
        assert(h.state.trackNavigation.previewTrackIndex.get() == 2U);
    }

    std::cout << "[PASS] latched Page/Track editor actions reject target drift\n";
}

void test_step_editor_uses_the_exact_latched_target() {
    {
        SequencerStepHarness h;
        h.state.sequencer.pattern.setContentLength(128U);
        h.state.sequencer.focusedStep.set(97U);
        h.navigationFocus.set(core::state::StructureNavigationFocus::STEP);
        test_support::drainNotifications();

        h.tap(Config::ButtonID::NAV);
        test_support::drainNotifications();

        assert(!h.state.sequencer.contextSelector.visible);
        assert(h.state.sequencer.stepEdit.visible.get());
        assert(h.state.sequencer.stepEdit.stepIndex.get() == 97U);
        assert(h.overlays.current() == core::ui::OverlayType::SEQ_STEP_EDIT);
        assert(h.state.sequencerHistory.undoCount() == 0U);
    }

    {
        SequencerStepHarness h;
        h.state.sequencer.pattern.setContentLength(128U);
        h.state.sequencer.focusedStep.set(97U);
        h.navigationFocus.set(core::state::StructureNavigationFocus::STEP);
        test_support::drainNotifications();

        h.press(Config::ButtonID::NAV);
        h.state.sequencer.focusedStep.set(96U);
        {
            core::app::testing::ScopedExtmemAllocationFailure failure(1U);
            h.release(Config::ButtonID::NAV);
            assert(core::app::testing::extmemAllocationAttempt == 0U);
        }
        test_support::drainNotifications();

        assert(!h.state.sequencer.contextSelector.visible);
        assert(!h.state.sequencer.stepEdit.visible.get());
        assert(h.state.sequencer.focusedStep.get() == 96U);
        assert(h.state.sequencerHistory.undoCount() == 0U);
        assert(h.state.projectHistory.undoCount() == 0U);
    }

    std::cout << "[PASS] Step editor uses its exact latched target\n";
}

void test_hidden_context_selector_cannot_complete_an_old_gesture() {
    {
        SequencerStepHarness h;
        h.navigationFocus.set(core::state::StructureNavigationFocus::PAGE);
        h.press(Config::ButtonID::NAV);
        assert(h.state.sequencer.contextSelector.visible);
        seq::resetTransientTrackState(h.state.sequencer);

        h.release(Config::ButtonID::NAV);
        test_support::drainNotifications();

        assert(!h.state.sequencer.patternEditor.active.get());
        assert(!h.state.sequencer.stepEdit.visible.get());
        assert(h.state.sequencerHistory.undoCount() == 0U);
        assert(h.state.projectHistory.undoCount() == 0U);
    }

    {
        SequencerStepHarness h;
        h.navigationFocus.set(core::state::StructureNavigationFocus::PAGE);
        h.press(Config::ButtonID::NAV);
        assert(h.state.sequencer.contextSelector.visible);
        seq::resetTransientTrackState(h.state.sequencer);

        h.advance(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
        h.release(Config::ButtonID::NAV);
        test_support::drainNotifications();

        assert(!h.state.sequencer.structureUi.pageSelection.active.get());
        assert(!h.state.sequencer.structureUi.stepSelection.active.get());
        assert(!h.state.trackNavigation.selection.active.get());
        assert(!h.state.sequencer.patternEditor.active.get());
        assert(!h.state.sequencer.stepEdit.visible.get());
        assert(h.state.sequencerHistory.undoCount() == 0U);
        assert(h.state.projectHistory.undoCount() == 0U);
    }

    {
        SequencerStepHarness h;
        h.state.sequencer.pattern.setContentLength(16U);
        h.state.sequencer.page.set(0U);
        h.state.sequencer.focusedStep.set(0U);
        h.state.sequencer.structureUi.syncPreviewPage(0U);
        h.navigationFocus.set(core::state::StructureNavigationFocus::PAGE);
        h.press(Config::ButtonID::NAV);
        assert(h.state.sequencer.contextSelector.visible);
        seq::resetTransientTrackState(h.state.sequencer);

        h.turn(Config::EncoderID::NAV, 1.0f);
        h.release(Config::ButtonID::NAV);
        test_support::drainNotifications();

        assert(h.navigationFocus.get() ==
               core::state::StructureNavigationFocus::PAGE);
        assert(h.state.sequencer.page.get() == 0U);
        assert(h.state.sequencer.focusedStep.get() == 0U);
        assert(!h.state.sequencer.patternEditor.active.get());
        assert(!h.state.sequencer.stepEdit.visible.get());
        assert(h.state.sequencerHistory.undoCount() == 0U);
        assert(h.state.projectHistory.undoCount() == 0U);
    }

    std::cout << "[PASS] hidden selector cannot complete an old NAV gesture\n";
}

void test_latched_nav_hold_cannot_cross_selection_context() {
    using Focus = core::state::StructureNavigationFocus;
    for (const auto route : std::array<std::pair<Focus, Focus>, 2U>{
             std::pair{Focus::TRACK, Focus::PAGE},
             std::pair{Focus::PAGE, Focus::TRACK},
         }) {
        SequencerStepHarness h;
        h.state.sequencerTracks.reset();
        (void)h.state.setSharedTrackState(0x0001U, 0U);
        assert(h.state.sharedTrackEnabledMask.get() == 0x0001U);
        assert(h.state.sharedTrackActive.get() == 0U);
        h.navigationFocus.set(route.first);
        h.state.trackNavigation.syncPreviewTrack(0U);
        h.state.trackNavigation.previewAddSlot.set(false);
        h.state.sequencer.structureUi.syncPreviewPage(0U);
        test_support::drainNotifications();

        h.press(Config::ButtonID::NAV);
        assert(h.state.sequencer.contextSelector.visible);
        h.navigationFocus.set(route.second);
        test_support::drainNotifications();
        {
            core::app::testing::ScopedExtmemAllocationFailure failure(1U);
            h.advance(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
            h.release(Config::ButtonID::NAV);
            assert(core::app::testing::extmemAllocationAttempt == 0U);
        }
        test_support::drainNotifications();

        assert(!h.state.sequencer.contextSelector.visible);
        assert(!h.state.trackNavigation.selection.active.get());
        assert(!h.state.sequencer.structureUi.pageSelection.active.get());
        assert(!h.state.sequencer.structureUi.stepSelection.active.get());
        assert(h.state.sequencerHistory.undoCount() == 0U);
        assert(h.state.projectHistory.undoCount() == 0U);
    }

    std::cout
        << "[PASS] latched NAV hold cannot cross Track/Page selection context\n";
}

void test_child_context_selector_cycles_pattern_and_step_only() {
    SequencerStepHarness h;
    const auto rootNode = core::state::sequencer::rootStepNodeId(0);
    const auto micro =
        core::state::sequencer::createMicroSequence(h.state.sequencer.pattern, rootNode, 2);
    assert(micro.ok);
    assert(core::state::sequencer::enterMicroSequenceContentView(h.state.sequencer, rootNode,
                                                                 micro.id));
    h.navigationFocus.set(core::state::StructureNavigationFocus::PAGE);

    h.press(Config::ButtonID::NAV);
    h.advance(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    assert(h.state.sequencer.structureUi.pageSelection.active.get());
    assert(!h.state.sequencer.structureUi.stepSelection.active.get());
    h.release(Config::ButtonID::NAV);
    assert(h.state.sequencer.structureUi.pageSelection.selectedMask.get() == 0U);
    h.tap(Config::ButtonID::LEFT_TOP);
    assert(!h.state.sequencer.structureUi.pageSelection.active.get());

    h.press(Config::ButtonID::NAV);
    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.sequencer.contextSelector.previewFocus ==
           core::state::StructureNavigationFocus::STEP);
    h.release(Config::ButtonID::NAV);
    assert(h.navigationFocus.get() == core::state::StructureNavigationFocus::STEP);

    h.press(Config::ButtonID::NAV);
    h.turn(Config::EncoderID::NAV, -1.0f);
    assert(h.state.sequencer.contextSelector.previewFocus ==
           core::state::StructureNavigationFocus::PAGE);
    h.release(Config::ButtonID::NAV);
    assert(h.navigationFocus.get() == core::state::StructureNavigationFocus::PAGE);

    std::cout << "[PASS] test_child_context_selector_cycles_pattern_and_step_only\n";
}

void test_track_selection_skips_gaps_and_mutes_atomically() {
    SequencerStepHarness h;
    h.state.sequencerTracks.reset();
    h.state.setSharedTrackState(0x0005U, 0U);
    h.navigationFocus.set(core::state::StructureNavigationFocus::TRACK);

    h.press(Config::ButtonID::NAV);
    h.advance(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    assert(h.state.trackNavigation.selection.active.get());
    assert(h.state.trackNavigation.selection.cursorIndex.get() == 0U);
    h.release(Config::ButtonID::NAV);
    assert(h.state.trackNavigation.selection.selectedMask.get() == 0U);

    h.tap(Config::ButtonID::NAV);
    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.trackNavigation.selection.cursorIndex.get() == 2U);
    h.tap(Config::ButtonID::NAV);
    assert(h.state.trackNavigation.selection.selectedMask.get() == 0x0005U);

    const uint8_t historyBefore = h.state.projectTrackHistory.undoCount();
    h.tap(Config::ButtonID::BOTTOM_LEFT);
    assert(h.state.projectTracks.authored.mutedMask == 0x0005U);
    assert(h.state.projectTrackHistory.undoCount() == historyBefore + 1U);
    assert(h.state.trackNavigation.selection.active.get());

    assert(h.state.undoProjectHistory());
    assert(h.state.projectTracks.authored.mutedMask == 0U);
    assert(h.state.redoProjectHistory());
    assert(h.state.projectTracks.authored.mutedMask == 0x0005U);

    h.tap(Config::ButtonID::BOTTOM_LEFT);
    assert(h.state.projectTracks.authored.mutedMask == 0U);
    h.tap(Config::ButtonID::LEFT_TOP);
    assert(h.state.trackNavigation.selection.active.get());
    assert(h.state.trackNavigation.selection.selectedMask.get() == 0U);
    h.tap(Config::ButtonID::LEFT_TOP);
    assert(!h.state.trackNavigation.selection.active.get());

    std::cout << "[PASS] test_track_selection_skips_gaps_and_mutes_atomically\n";
}

void test_track_selection_delete_is_undoable_and_keeps_one_track() {
    SequencerStepHarness h;
    h.state.sequencerTracks.reset();
    h.state.setSharedTrackState(0x0007U, 0U);
    h.navigationFocus.set(core::state::StructureNavigationFocus::TRACK);

    h.press(Config::ButtonID::NAV);
    h.advance(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    h.release(Config::ButtonID::NAV);
    h.turn(Config::EncoderID::NAV, 1.0f);
    h.tap(Config::ButtonID::NAV);
    assert(h.state.trackNavigation.selection.selectedMask.get() == 0x0002U);

    h.press(Config::ButtonID::BOTTOM_LEFT);
    h.advance(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    h.release(Config::ButtonID::BOTTOM_LEFT);

    assert(h.state.sequencerTracks.currentEnabledMask() == 0x0005U);
    assert(!h.state.trackNavigation.selection.active.get());
    assert(h.state.sequencerHistory.undoCount() == 1U);
    assert(h.state.undoProjectHistory());
    assert(h.state.sequencerTracks.currentEnabledMask() == 0x0007U);
    assert(h.state.redoProjectHistory());
    assert(h.state.sequencerTracks.currentEnabledMask() == 0x0005U);

    std::cout << "[PASS] test_track_selection_delete_is_undoable_and_keeps_one_track\n";
}

void test_track_selection_copy_is_global_from_sequencer_view() {
    SequencerStepHarness h;
    h.state.sequencerTracks.reset();
    h.state.setSharedTrackState(0x0005U, 0U);
    h.state.sequencer.pattern.setContentLength(8U);
    h.state.sequencer.pattern.note[0] = 79U;
    h.state.sequencer.pattern.velocity[0] = 103U;
    h.state.sequencer.pattern.setEnabled(0U, true);
    auto& sourcePatternTwo = h.state.sequencerTracks.track(2U);
    sourcePatternTwo.setContentLength(8U);
    sourcePatternTwo.note[0] = 67U;
    sourcePatternTwo.velocity[0] = 97U;
    sourcePatternTwo.setEnabled(0U, true);
    auto& untouchedPattern = h.state.sequencerTracks.track(5U);
    untouchedPattern.setContentLength(8U);
    untouchedPattern.note[0] = 55U;
    untouchedPattern.setEnabled(0U, true);

    auto& sourcePageZero = h.state.pages.pageData(0U, 0U);
    sourcePageZero.setMacroActive(2U, true);
    sourcePageZero.cc[2] = 22U;
    sourcePageZero.values[2] = 0.82f;
    auto& sourcePageTwo = h.state.pages.pageData(2U, 0U);
    sourcePageTwo.setMacroActive(5U, true);
    sourcePageTwo.cc[5] = 75U;
    sourcePageTwo.values[5] = 0.36f;
    h.state.pages.updateActiveConfigs();
    (void)test_support::project_control::addLocalLfo(
        h.state.pages.control, {.track = 0U, .page = 0U, .macro = 2U}, "Sequencer Track 1 LFO");
    (void)test_support::project_control::addLocalLfo(
        h.state.pages.control, {.track = 2U, .page = 0U, .macro = 5U}, "Sequencer Track 3 LFO");
    configureProjectTrackFixture(h.state, 4U, 9U);
    configureProjectTrackFixture(h.state, 6U, 13U, true);

    h.navigationFocus.set(core::state::StructureNavigationFocus::TRACK);
    h.press(Config::ButtonID::NAV);
    h.advance(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    h.release(Config::ButtonID::NAV);
    assert(h.state.trackNavigation.selection.active.get());
    h.tap(Config::ButtonID::NAV);
    assert(h.state.trackNavigation.selection.selectedMask.get() == 0x0001U);
    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.trackNavigation.selection.cursorIndex.get() == 2U);
    h.tap(Config::ButtonID::NAV);
    assert(h.state.trackNavigation.selection.selectedMask.get() == 0x0005U);

    h.tap(Config::ButtonID::BOTTOM_RIGHT);
    assert(h.state.structureClipboard.hasSequencerTrackSelection());
    assert(h.state.structureClipboard.sequencerTrackSelection->count == 2U);
    assert(h.state.trackNavigation.selection.placementActive());

    h.turn(Config::EncoderID::NAV, 1.0f);
    h.turn(Config::EncoderID::NAV, 1.0f);
    h.advance(0U);
    assert(h.state.trackNavigation.selection.cursorIndex.get() == 4U);
    assert(h.state.trackNavigation.selection.destinationMask.get() == 0x0050U);

    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.advance(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    h.release(Config::ButtonID::BOTTOM_RIGHT);

    assert(h.state.currentSharedTrackEnabledMask() == 0x0055U);
    assert(h.state.currentSharedActiveTrack() == 4U);
    assert(h.state.sequencer.pattern.note[0] == 79U);
    assert(h.state.sequencer.pattern.velocity[0] == 103U);
    assert(h.state.sequencerTracks.track(6U).note[0] == 67U);
    assert(h.state.sequencerTracks.track(6U).velocity[0] == 97U);
    assert(h.state.sequencerTracks.track(5U).note[0] == 55U);
    assert(h.state.pages.pageData(4U, 0U).isMacroActive(2U));
    assert(h.state.pages.pageData(4U, 0U).cc[2] == 22U);
    assert(h.state.pages.pageData(6U, 0U).isMacroActive(5U));
    assert(h.state.pages.pageData(6U, 0U).cc[5] == 75U);
    assert(test_support::project_control::outputBindingCountAt(
               h.state.pages.control, {.track = 4U, .page = 0U, .macro = 2U}) == 1U);
    assert(test_support::project_control::outputBindingCountAt(
               h.state.pages.control, {.track = 6U, .page = 0U, .macro = 5U}) == 1U);
    assert(h.state.projectTracks.authored.midiChannels[4U] == 9U);
    assert(h.state.projectTracks.authored.midiChannels[6U] == 13U);
    assert((h.state.projectTracks.authored.mutedMask & static_cast<uint16_t>(1U << 6U)) != 0U);
    assert(h.state.trackNavigation.selection.placementActive());

    assert(h.state.undoProjectHistory());
    assert(h.state.currentSharedTrackEnabledMask() == 0x0005U);
    assert(!h.state.pages.pageData(4U, 0U).isMacroActive(2U));
    assert(!h.state.pages.pageData(6U, 0U).isMacroActive(5U));
    assert(h.state.sequencerTracks.track(5U).note[0] == 55U);
    assert(test_support::project_control::outputBindingCountAt(
               h.state.pages.control, {.track = 4U, .page = 0U, .macro = 2U}) == 0U);
    assert(test_support::project_control::outputBindingCountAt(
               h.state.pages.control, {.track = 6U, .page = 0U, .macro = 5U}) == 0U);
    assert(h.state.redoProjectHistory());
    assert(h.state.currentSharedTrackEnabledMask() == 0x0055U);
    assert(h.state.pages.pageData(4U, 0U).isMacroActive(2U));
    assert(h.state.pages.pageData(6U, 0U).isMacroActive(5U));
    assert(test_support::project_control::outputBindingCountAt(
               h.state.pages.control, {.track = 4U, .page = 0U, .macro = 2U}) == 1U);
    assert(test_support::project_control::outputBindingCountAt(
               h.state.pages.control, {.track = 6U, .page = 0U, .macro = 5U}) == 1U);

    h.tap(Config::ButtonID::LEFT_TOP);
    assert(h.state.trackNavigation.selection.active.get());
    assert(!h.state.trackNavigation.selection.placementActive());
    h.tap(Config::ButtonID::LEFT_TOP);
    assert(!h.state.trackNavigation.selection.active.get());

    std::cout << "[PASS] sparse Track selection copies Sequencer, Macro and Modulators\n";
}

void test_page_selection_clear_and_delete_are_undoable() {
    SequencerStepHarness h;
    h.state.sequencer.pattern.setContentLength(24U);
    h.state.sequencer.pattern.note[0] = 72U;
    h.state.sequencer.pattern.note[8] = 84U;
    h.state.sequencer.pattern.setEnabled(0U, true);
    h.state.sequencer.pattern.setEnabled(8U, true);
    h.state.sequencer.page.set(0U);
    h.state.sequencer.focusedStep.set(0U);
    h.navigationFocus.set(core::state::StructureNavigationFocus::PAGE);
    assert(seq::storeActiveTrack(h.state.sequencerTracks, h.state.sequencer));

    h.press(Config::ButtonID::NAV);
    h.advance(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    assert(h.state.sequencer.structureUi.pageSelection.active.get());
    h.release(Config::ButtonID::NAV);
    h.tap(Config::ButtonID::NAV);
    h.turn(Config::EncoderID::NAV, 1.0f);
    h.tap(Config::ButtonID::NAV);
    assert(h.state.sequencer.structureUi.pageSelection.selectedMask.get() == 0x0003U);
    const uint8_t resetPage = h.state.sequencer.page.get();
    const uint8_t resetFocus = h.state.sequencer.focusedStep.get();

    h.tap(Config::ButtonID::BOTTOM_LEFT);
    assert(h.state.sequencer.pattern.note[0] ==
           core::state::sequencer::SequencerState::DEFAULT_NOTE);
    assert(h.state.sequencer.pattern.note[8] ==
           core::state::sequencer::SequencerState::DEFAULT_NOTE);
    assert(h.state.sequencer.structureUi.pageSelection.active.get());
    assert(h.state.sequencer.page.get() == resetPage);
    assert(h.state.sequencer.focusedStep.get() == resetFocus);
    assert(h.state.sequencerHistory.undoCount() == 1U);
    assert(h.state.sequencerHistory.undoCount(seq::SequencerHistoryScope::PatternOnly) == 1U);
    assert(h.state.undoProjectHistory());
    assert(h.state.sequencer.pattern.note[0] == 72U);
    assert(h.state.sequencer.pattern.note[8] == 84U);
    assert(h.state.sequencer.page.get() == resetPage);
    assert(h.state.sequencer.focusedStep.get() == resetFocus);
    assert(h.state.redoProjectHistory());
    assert(h.state.sequencer.pattern.note[0] == seq::SequencerState::DEFAULT_NOTE);
    assert(h.state.sequencer.pattern.note[8] == seq::SequencerState::DEFAULT_NOTE);
    assert(h.state.sequencer.page.get() == resetPage);
    assert(h.state.sequencer.focusedStep.get() == resetFocus);
    assert(h.state.undoProjectHistory());
    assert(h.state.sequencer.pattern.note[0] == 72U);
    assert(h.state.sequencer.pattern.note[8] == 84U);

    h.press(Config::ButtonID::BOTTOM_LEFT);
    h.advance(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    h.release(Config::ButtonID::BOTTOM_LEFT);
    assert(h.state.sequencer.pattern.length.get() == 8U);
    assert(!h.state.sequencer.structureUi.pageSelection.active.get());
    assert(h.state.sequencer.page.get() == 0U);
    assert(h.state.sequencer.focusedStep.get() == 0U);
    assert(h.state.sequencerHistory.undoCount() == 1U);
    assert(h.state.undoProjectHistory());
    assert(h.state.sequencer.pattern.length.get() == 24U);
    assert(h.state.sequencer.page.get() == resetPage);
    assert(h.state.sequencer.focusedStep.get() == resetFocus);
    assert(h.state.redoProjectHistory());
    assert(h.state.sequencer.pattern.length.get() == 8U);
    assert(h.state.sequencer.page.get() == 0U);
    assert(h.state.sequencer.focusedStep.get() == 0U);

    std::cout << "[PASS] test_page_selection_clear_and_delete_are_undoable\n";
}

void test_pattern_selection_paste_previews_collisions_and_creates_intermediate_pages() {
    SequencerStepHarness h;
    h.state.sequencer.pattern.setContentLength(24U);
    h.state.sequencer.pattern.note[0] = 72U;
    h.state.sequencer.pattern.velocity[0] = 91U;
    h.state.sequencer.pattern.setEnabled(0U, true);
    h.state.sequencer.pattern.note[16] = 84U;
    h.state.sequencer.pattern.velocity[16] = 111U;
    h.state.sequencer.pattern.setEnabled(16U, true);
    createRootMicroSequence(h, 0U);
    createRootMicroSequence(h, 16U);
    auto* cc = seq::ensureSequencerCcLaneBank(h.state.sequencer.pattern);
    assert(cc != nullptr);
    seq::SequencerCcLaneDraft ccDraft{};
    ccDraft.destination.controller = 74U;
    assert(seq::createSequencerCcLane(*cc, 0U, ccDraft).changed());
    assert(seq::setSequencerCcLaneEvent(*cc, 0U, 2U, 45U).changed());
    h.state.sequencer.pattern.bumpCcLaneRevision();
    assert(seq::setPatternPlaybackRegion(
        h.state.sequencer.pattern, {24U, 1U, 4U, 20U}));
    const void* const ccOwner = h.state.sequencer.pattern.ccLanes.get();
    const uint64_t ccHash = byteHash(
        ccOwner, sizeof(*h.state.sequencer.pattern.ccLanes));
    const uint32_t ccRevision =
        h.state.sequencer.pattern.ccLaneRevision.get();
    h.state.sequencer.page.set(0U);
    h.state.sequencer.focusedStep.set(0U);
    h.navigationFocus.set(core::state::StructureNavigationFocus::PAGE);

    h.press(Config::ButtonID::NAV);
    h.advance(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    h.release(Config::ButtonID::NAV);
    assert(h.state.sequencer.structureUi.pageSelection.active.get());

    h.tap(Config::ButtonID::NAV);
    h.turn(Config::EncoderID::NAV, 1.0f);
    h.turn(Config::EncoderID::NAV, 1.0f);
    h.tap(Config::ButtonID::NAV);
    assert(h.state.sequencer.structureUi.pageSelection.selectedMask.get() == 0x0005U);

    h.tap(Config::ButtonID::BOTTOM_RIGHT);
    auto& selection = h.state.sequencer.structureUi.pageSelection;
    assert(selection.placementActive());
    assert(h.state.structureClipboard.hasSequencerPageSelection());
    assert(h.state.structureClipboard.sequencerGraph != nullptr);
    assert(selection.cursorIndex.get() == 2U);
    assert(selection.destinationMask.get() == 0x0014U);
    assert(selection.overwriteMask.get() == 0x0004U);

    {
        auto discardedGraph = std::move(h.state.sequencer.pattern.graph);
    }
    h.state.sequencer.pattern.bumpGraphRevision();
    assert(h.state.sequencer.pattern.graph == nullptr);
    assert(seq::storeActiveTrack(
        h.state.sequencerTracks, h.state.sequencer));

    h.turn(Config::EncoderID::NAV, 1.0f);
    h.turn(Config::EncoderID::NAV, 1.0f);
    h.advance(0U);
    assert(selection.cursorIndex.get() == 4U);
    assert(selection.destinationMask.get() == 0x0050U);
    assert(selection.overwriteMask.get() == 0U);

    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.advance(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    h.release(Config::ButtonID::BOTTOM_RIGHT);

    assert(h.state.sequencer.activePageCount() == 7U);
    assert(h.state.sequencer.pattern.note[32] == 72U);
    assert(h.state.sequencer.pattern.velocity[32] == 91U);
    assert(h.state.sequencer.pattern.isEnabled(32U));
    assert(h.state.sequencer.pattern.note[48] == 84U);
    assert(h.state.sequencer.pattern.velocity[48] == 111U);
    assert(h.state.sequencer.pattern.isEnabled(48U));
    assert(h.state.sequencer.pattern.note[24] ==
           core::state::sequencer::SequencerState::DEFAULT_NOTE);
    assert(!h.state.sequencer.pattern.isEnabled(24U));
    assert(h.state.sequencer.pattern.note[40] ==
           core::state::sequencer::SequencerState::DEFAULT_NOTE);
    assert(!h.state.sequencer.pattern.isEnabled(40U));
    assert(rootStepHasMicroSequence(h, 32U));
    assert(rootStepHasMicroSequence(h, 48U));
    assert(h.state.sequencer.pattern.ccLanes != nullptr);
    assert(h.state.sequencer.pattern.ccLanes.get() == ccOwner);
    assert(byteHash(
               h.state.sequencer.pattern.ccLanes.get(),
               sizeof(*h.state.sequencer.pattern.ccLanes)) == ccHash);
    assert(h.state.sequencer.pattern.ccLanes->lanes[0U].activeMask.test(2U));
    assert(h.state.sequencer.pattern.ccLanes->lanes[0U].values[2U] == 45U);
    const auto committedRegion = seq::patternPlaybackRegion(
        h.state.sequencer.pattern);
    assert(committedRegion.contentLength == 56U);
    assert(committedRegion.playStart == 1U);
    assert(committedRegion.loopStart == 4U);
    assert(committedRegion.loopEnd == 20U);
    assert(h.state.sequencer.page.get() == 4U);
    assert(h.state.sequencer.focusedStep.get() == 32U);
    assert(h.state.sequencer.structureUi.previewPageIndex.get() == 4U);
    assert(h.state.sequencer.structureUi.pageHold.action.get() ==
           core::state::StructureHoldAction::NONE);
    assert(selection.active.get());
    assert(selection.placementActive());
    assert(selection.destinationMask.get() == 0x0050U);
    assert(selection.overwriteMask.get() == 0x0050U);

    const auto undoCount = h.state.sequencerHistory.undoCount();
    assert(undoCount == 1U);
    assert(h.state.sequencerHistory.undoCount(
               seq::SequencerHistoryScope::PatternOnly) == 1U);
    {
        core::app::testing::ScopedExtmemAllocationFailure failure(1U);
        h.press(Config::ButtonID::BOTTOM_RIGHT);
        assert(h.state.sequencer.structureUi.pageHold.action.get() ==
               core::state::StructureHoldAction::PASTE);
        h.advance(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
        assert(h.state.sequencer.structureUi.pageHold.action.get() ==
               core::state::StructureHoldAction::NONE);
        assert(core::app::testing::extmemAllocationAttempt == 0U);
        assert(core::app::testing::extmemAllocationFailureOrdinal == 1U);
    }
    h.release(Config::ButtonID::BOTTOM_RIGHT);
    assert(h.state.sequencerHistory.undoCount() == undoCount);
    assert(h.state.sequencer.page.get() == 4U);
    assert(h.state.sequencer.focusedStep.get() == 32U);
    assert(selection.destinationMask.get() == 0x0050U);
    assert(selection.overwriteMask.get() == 0x0050U);
    assert(h.state.sequencer.pattern.ccLanes.get() == ccOwner);
    assert(byteHash(
               h.state.sequencer.pattern.ccLanes.get(),
               sizeof(*h.state.sequencer.pattern.ccLanes)) == ccHash);
    assert(h.state.sequencer.pattern.ccLaneRevision.get() == ccRevision);

    h.tap(Config::ButtonID::LEFT_TOP);
    assert(selection.active.get());
    assert(!selection.placementActive());
    assert(selection.selectedMask.get() == 0U);
    h.tap(Config::ButtonID::LEFT_TOP);
    assert(!selection.active.get());

    assert(h.state.undoProjectHistory());
    assert(h.state.sequencer.activePageCount() == 3U);
    assert(h.state.sequencer.page.get() == 2U);
    assert(h.state.sequencer.focusedStep.get() == 16U);
    assert(h.state.sequencer.pattern.graph == nullptr);
    assert(h.state.sequencer.pattern.ccLanes != nullptr);
    assert(byteHash(
               h.state.sequencer.pattern.ccLanes.get(),
               sizeof(*h.state.sequencer.pattern.ccLanes)) == ccHash);
    assert(h.state.sequencer.pattern.ccLanes->lanes[0U].activeMask.test(2U));
    const auto undoRegion = seq::patternPlaybackRegion(h.state.sequencer.pattern);
    assert(undoRegion.contentLength == 24U);
    assert(undoRegion.playStart == 1U);
    assert(undoRegion.loopStart == 4U);
    assert(undoRegion.loopEnd == 20U);
    assert(h.state.redoProjectHistory());
    assert(h.state.sequencer.activePageCount() == 7U);
    assert(h.state.sequencer.page.get() == 4U);
    assert(h.state.sequencer.focusedStep.get() == 32U);
    assert(h.state.sequencer.pattern.note[32] == 72U);
    assert(h.state.sequencer.pattern.note[48] == 84U);
    assert(rootStepHasMicroSequence(h, 32U));
    assert(rootStepHasMicroSequence(h, 48U));
    assert(h.state.sequencer.pattern.ccLanes != nullptr);
    assert(byteHash(
               h.state.sequencer.pattern.ccLanes.get(),
               sizeof(*h.state.sequencer.pattern.ccLanes)) == ccHash);
    assert(h.state.sequencer.pattern.ccLanes->lanes[0U].activeMask.test(2U));

    std::cout << "[PASS] sparse Pattern selection previews collisions and fills page gaps\n";
}

void test_track_context_nav_crosses_sparse_slots_and_creates_without_structure() {
    SequencerStepHarness h;
    h.state.sequencerTracks.reset();
    h.state.setSharedTrackState(0x0005U, 0);
    h.navigationFocus.set(core::state::StructureNavigationFocus::TRACK);

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.sequencerTracks.activeTrackIndex() == 0);
    assert(h.state.trackNavigation.previewTrackIndex.get() == 1U);
    assert(h.state.trackNavigation.previewAddSlot.get());
    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.sequencerTracks.activeTrackIndex() == 2);
    assert(h.state.trackNavigation.previewTrackIndex.get() == 2U);
    assert(!h.state.trackNavigation.previewAddSlot.get());

    h.turn(Config::EncoderID::NAV, -1.0f);
    assert(h.state.trackNavigation.previewTrackIndex.get() == 1U);
    assert(h.state.trackNavigation.previewAddSlot.get());
    h.tap(Config::ButtonID::NAV);

    assert(h.state.sequencerTracks.currentEnabledMask() == 0x0007U);
    assert(h.state.sequencerTracks.activeTrackIndex() == 1U);
    assert(h.state.trackNavigation.previewTrackIndex.get() == 1U);
    assert(!h.state.trackNavigation.previewAddSlot.get());
    assert(h.state.sequencerHistory.undoCount() == 1U);

    std::cout
        << "[PASS] test_track_context_nav_crosses_sparse_slots_and_creates_without_structure\n";
}

void test_step_toggle_undo_redo_workflow() {
    SequencerStepHarness h;
    h.state.sequencer.pattern.setContentLength(8);
    h.state.sequencer.focusedStep.set(3);
    createRootMicroSequence(h, 0);
    assert(core::state::sequencer::storeActiveTrack(h.state.sequencerTracks, h.state.sequencer));

    assert(!h.state.sequencer.pattern.isEnabled(0));
    assert(h.state.sequencerHistory.undoCount() == 0);

    h.tap(Config::MACRO_BUTTONS[0]);
    assert(h.state.sequencer.pattern.isEnabled(0));
    assert(h.state.sequencer.focusedStep.get() == 0);
    assert(h.state.sequencerHistory.undoCount() == 1);
    assert(rootStepHasMicroSequence(h, 0));

    assert(h.state.undoProjectHistory());
    assert(!h.state.sequencer.pattern.isEnabled(0));
    assert(h.state.sequencer.focusedStep.get() == 3);
    assert(rootStepHasMicroSequence(h, 0));

    assert(h.state.sequencerHistory.undoCount() == 0);
    assert(h.state.sequencerHistory.redoCount() == 1);

    assert(h.state.redoProjectHistory());
    assert(h.state.sequencer.pattern.isEnabled(0));
    assert(h.state.sequencer.focusedStep.get() == 0);
    assert(rootStepHasMicroSequence(h, 0));

    assert(h.state.sequencerHistory.undoCount() == 1);
    assert(h.state.sequencerHistory.redoCount() == 0);

    std::cout << "[PASS] test_step_toggle_undo_redo_workflow\n";
}

void test_child_step_toggle_undo_redo_workflow() {
    SequencerStepHarness h;
    h.state.sequencer.pattern.setContentLength(8);
    createRootMicroSequence(h, 0);
    const auto* root = rootStepNode(h, 0);
    assert(root != nullptr);
    assert(core::state::sequencer::enterMicroSequenceContentView(
        h.state.sequencer, core::state::sequencer::rootStepNodeId(0), root->childSequenceId));
    h.state.sequencer.focusedStep.set(1);
    assert(core::state::sequencer::activeContentStepEnabled(h.state.sequencer, 0));

    h.tap(Config::MACRO_BUTTONS[0]);
    assert(!core::state::sequencer::activeContentStepEnabled(h.state.sequencer, 0));
    assert(h.state.sequencer.focusedStep.get() == 0);
    assert(h.state.sequencerHistory.undoCount() == 1);
    assert(!h.state.hasPendingSequencerPatternHistoryCoalescing());

    assert(h.state.undoProjectHistory());
    assert(core::state::sequencer::activeContentStepEnabled(h.state.sequencer, 0));
    assert(h.state.sequencer.focusedStep.get() == 1);
    assert(std::strcmp(h.state.sequencer.historyFeedback.line2.data(), "Step 01 State") == 0);
    assert(std::strcmp(h.state.sequencer.historyFeedback.line3.data(), "Off -> On") == 0);

    assert(h.state.redoProjectHistory());
    assert(!core::state::sequencer::activeContentStepEnabled(h.state.sequencer, 0));
    assert(h.state.sequencer.focusedStep.get() == 0);
    assert(std::strcmp(h.state.sequencer.historyFeedback.line3.data(), "On -> Off") == 0);

    std::cout << "[PASS] test_child_step_toggle_undo_redo_workflow\n";
}

void test_step_toggle_preflight_failure_is_atomic() {
    SequencerStepHarness h;
    h.state.sequencer.pattern.setContentLength(8);
    h.state.sequencer.focusedStep.set(3);
    assert(!h.state.sequencer.pattern.isEnabled(0));

    {
        core::app::testing::ScopedExtmemAllocationFailure failure(1U);
        h.tap(Config::MACRO_BUTTONS[0]);
        assert(core::app::testing::extmemAllocationAttempt == 1U);
        assert(core::app::testing::extmemAllocationFailureOrdinal == 0U);
    }

    assert(!h.state.sequencer.pattern.isEnabled(0));
    assert(h.state.sequencer.focusedStep.get() == 3);
    assert(h.state.sequencerHistory.undoCount() == 0);
    assert(!h.state.hasPendingSequencerPatternHistoryCoalescing());

    std::cout << "[PASS] test_step_toggle_preflight_failure_is_atomic\n";
}

void test_child_draft_toggle_stays_out_of_published_history() {
    SequencerStepHarness h;
    h.state.sequencer.pattern.setContentLength(8);

    const auto opened = core::state::sequencer::openOrCreateActiveContentChild(
        h.state.sequencer, 3, core::state::sequencer::StepContentChildKind::MICRO_SEQUENCE,
        core::state::sequencer::DEFAULT_MICRO_SEQUENCE_LENGTH);
    assert(opened.opened && opened.draft);
    assert(h.state.sequencer.stepContentDraft.active.get());
    assert(!h.state.sequencer.stepContentDraft.modified());
    assert(!rootStepHasMicroSequence(h, 3));
    assert(core::state::sequencer::graphView(h.state.sequencer.pattern) == nullptr);
    assert(core::state::sequencer::activeContentStepEnabled(h.state.sequencer, 0));

    {
        core::app::testing::ScopedExtmemAllocationFailure failure(1U);
        h.tap(Config::MACRO_BUTTONS[0]);
        assert(core::app::testing::extmemAllocationAttempt == 0U);
        assert(core::app::testing::extmemAllocationFailureOrdinal == 1U);
    }

    assert(h.state.sequencer.focusedStep.get() == 0);
    assert(!core::state::sequencer::activeContentStepEnabled(h.state.sequencer, 0));
    assert(h.state.sequencer.stepContentDraft.modified());
    assert(!rootStepHasMicroSequence(h, 3));
    assert(core::state::sequencer::graphView(h.state.sequencer.pattern) == nullptr);
    assert(h.state.sequencerHistory.undoCount() == 0);
    assert(!h.state.hasPendingSequencerPatternHistoryCoalescing());

    std::cout << "[PASS] test_child_draft_toggle_stays_out_of_published_history\n";
}

void test_pattern_editor_adds_only_the_next_page() {
    SequencerStepHarness h;
    h.state.sequencer.pattern.setContentLength(24);
    h.state.sequencer.page.set(2);
    h.state.sequencer.focusedStep.set(16);

    openPatternEditor(h);
    h.tap(Config::ButtonID::BOTTOM_RIGHT);

    assert(h.state.sequencer.patternEditor.active.get());
    assert(h.state.sequencer.pattern.length.get() == 32);
    assert(h.state.sequencer.page.get() == 3);
    assert(h.state.sequencer.focusedStep.get() == 24);

    for (uint8_t step = 24; step < 32; ++step) {
        assert(h.state.sequencer.pattern.note[step] ==
               core::state::sequencer::SequencerState::DEFAULT_NOTE);
        assert(h.state.sequencer.pattern.velocity[step] ==
               core::state::sequencer::SequencerState::DEFAULT_VELOCITY);
        assert(h.state.sequencer.pattern.gate[step] ==
               core::state::sequencer::SequencerState::DEFAULT_GATE_PERCENT);
        assert(h.state.sequencer.pattern.nudge[step] == 0);
        assert(h.state.sequencer.pattern.probability[step] ==
               core::state::sequencer::SequencerState::DEFAULT_PROBABILITY);
        assert(!h.state.sequencer.pattern.isEnabled(step));
    }

    std::cout << "[PASS] test_pattern_editor_adds_only_the_next_page\n";
}

void test_track_focus_bottom_left_mutes_without_clearing_payload() {
    SequencerStepHarness h;
    h.state.sequencerTracks.reset();
    h.state.setSharedTrackState(0x0003, 1);
    h.state.sequencer.pattern.note[0] = 82;
    h.state.sequencer.pattern.velocity[0] = 108;
    h.state.sequencer.pattern.setEnabled(0, true);
    focusTrackNavigation(h);

    const uint8_t sequencerUndoBefore = h.state.sequencerHistory.undoCount();
    const uint8_t trackUndoBefore = h.state.projectTrackHistory.undoCount();
    h.press(Config::ButtonID::BOTTOM_LEFT);
    h.release(Config::ButtonID::BOTTOM_LEFT);

    assert(h.state.sequencerTracks.currentEnabledMask() == 0x0003);
    assert(h.state.projectTracks.authored.mutedMask == 0x0002);
    assert(h.state.sequencer.pattern.note[0] == 82);
    assert(h.state.sequencer.pattern.velocity[0] == 108);
    assert(h.state.sequencer.pattern.isEnabled(0));
    assert(h.state.sequencerHistory.undoCount() == sequencerUndoBefore);
    assert(h.state.projectTrackHistory.undoCount() == trackUndoBefore + 1U);

    assert(h.state.undoProjectHistory());
    assert(h.state.projectTracks.authored.mutedMask == 0);
    assert(h.state.sequencer.pattern.note[0] == 82);
    assert(h.state.sequencer.pattern.isEnabled(0));

    assert(h.state.redoProjectHistory());
    assert(h.state.projectTracks.authored.mutedMask == 0x0002);

    h.press(Config::ButtonID::BOTTOM_LEFT);
    h.release(Config::ButtonID::BOTTOM_LEFT);
    assert(h.state.projectTracks.authored.mutedMask == 0);

    h.press(Config::ButtonID::BOTTOM_LEFT);
    h.release(Config::ButtonID::BOTTOM_LEFT);
    assert(h.state.projectTracks.authored.mutedMask == 0x0002);

    std::cout << "[PASS] test_track_focus_bottom_left_mutes_without_clearing_payload\n";
}

void test_sequencer_page_copy_and_long_press_paste() {
    SequencerStepHarness h;
    h.state.sequencer.pattern.setContentLength(16);
    h.state.sequencer.page.set(0);
    h.state.sequencer.focusedStep.set(0);
    h.state.sequencer.pattern.note[0] = 72;
    h.state.sequencer.pattern.velocity[0] = 99;
    h.state.sequencer.pattern.gate[0] = 80;
    h.state.sequencer.pattern.nudge[0] = 3;
    h.state.sequencer.pattern.probability[0] = 87;
    h.state.sequencer.pattern.setEnabled(0, true);
    createRootMicroSequence(h, 0);

    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.release(Config::ButtonID::BOTTOM_RIGHT);
    assert(h.state.structureClipboard.hasSequencerPage());
    assert(h.state.structureClipboard.sequencerPage.sourcePage == 0);

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.sequencer.page.get() == 1);

    h.press(Config::ButtonID::BOTTOM_RIGHT);
    assert(h.state.sequencer.structureUi.pageHold.action.get() ==
           core::state::StructureHoldAction::PASTE);
    h.advance(0);
    h.advance(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS / 2U);
    h.advance(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS / 2U + 1U);
    assert(h.state.sequencer.structureUi.pageHold.action.get() ==
           core::state::StructureHoldAction::NONE);
    assert(h.state.sequencer.pattern.note[8] == 72);
    h.release(Config::ButtonID::BOTTOM_RIGHT);

    assert(h.state.sequencer.pattern.note[8] == 72);
    assert(h.state.sequencer.pattern.velocity[8] == 99);
    assert(h.state.sequencer.pattern.gate[8] == 80);
    assert(h.state.sequencer.pattern.nudge[8] == 3);
    assert(h.state.sequencer.pattern.probability[8] == 87);
    assert(h.state.sequencer.pattern.isEnabled(8));
    assert(rootStepHasMicroSequence(h, 8));

    std::cout << "[PASS] test_sequencer_page_copy_and_long_press_paste\n";
}

void test_page_paste_existing_target_graph_oom_and_replay() {
    SequencerStepHarness h;
    auto& sequencer = h.state.sequencer;
    h.navigationFocus.set(core::state::StructureNavigationFocus::PAGE);
    sequencer.pattern.setContentLength(16U);
    sequencer.pattern.note[0U] = 72U;
    sequencer.pattern.velocity[0U] = 101U;
    sequencer.pattern.setEnabled(0U, true);
    createRootMicroSequence(h, 0U);
    auto* cc = seq::ensureSequencerCcLaneBank(sequencer.pattern);
    assert(cc != nullptr);
    seq::SequencerCcLaneDraft draft{};
    draft.destination.controller = 74U;
    assert(seq::createSequencerCcLane(*cc, 0U, draft).changed());
    assert(seq::setSequencerCcLaneEvent(*cc, 0U, 2U, 55U).changed());
    sequencer.pattern.bumpCcLaneRevision();
    assert(seq::setPatternPlaybackRegion(
        sequencer.pattern, {16U, 1U, 2U, 6U}));
    const void* const ccOwner = sequencer.pattern.ccLanes.get();
    const uint64_t ccHash = byteHash(
        ccOwner, sizeof(*sequencer.pattern.ccLanes));
    sequencer.page.set(0U);
    sequencer.focusedStep.set(0U);
    sequencer.structureUi.syncPreviewPage(0U);

    auto workflow = makeStructureEditWorkflow(
        h, HistoryServices::fromCoreState(h.state));
    workflow.copyCurrentStructure();
    assert(h.state.structureClipboard.hasSequencerPage());
    assert(h.state.structureClipboard.sequencerGraph != nullptr);
    const auto* clipboardGraph = h.state.structureClipboard.sequencerGraph.get();
    const auto clipboardRevision = h.state.structureClipboard.revision.get();

    {
        auto discardedGraph = std::move(sequencer.pattern.graph);
    }
    sequencer.pattern.bumpGraphRevision();
    assert(sequencer.pattern.graph == nullptr);
    sequencer.page.set(1U);
    sequencer.focusedStep.set(8U);
    sequencer.structureUi.syncPreviewPage(1U);
    assert(seq::storeActiveTrack(h.state.sequencerTracks, sequencer));
    workflow.beginHoldAction(core::state::StructureHoldAction::PASTE);
    const auto graphRevision = sequencer.pattern.graphRevision.get();
    const auto ccRevision = sequencer.pattern.ccLaneRevision.get();
    const auto timingRevision = sequencer.pattern.patternTimingRevision.get();

    {
        core::app::testing::ScopedExtmemAllocationFailure failure(1U);
        workflow.pasteCurrentStructure();
        assert(core::app::testing::extmemAllocationAttempt == 1U);
        assert(core::app::testing::extmemAllocationFailureOrdinal == 0U);
    }
    test_support::drainNotifications();
    assert(sequencer.pattern.length.get() == 16U);
    assert(sequencer.pattern.note[0U] == 72U);
    assert(sequencer.pattern.note[8U] == seq::SequencerState::DEFAULT_NOTE);
    assert(sequencer.pattern.graph == nullptr);
    assert(sequencer.pattern.graphRevision.get() == graphRevision);
    assert(sequencer.pattern.ccLanes != nullptr);
    assert(sequencer.pattern.ccLanes.get() == ccOwner);
    assert(byteHash(
               sequencer.pattern.ccLanes.get(),
               sizeof(*sequencer.pattern.ccLanes)) == ccHash);
    assert(sequencer.pattern.ccLanes->lanes[0U].activeMask.test(2U));
    assert(sequencer.pattern.ccLanes->lanes[0U].values[2U] == 55U);
    assert(sequencer.pattern.ccLaneRevision.get() == ccRevision);
    assert(sequencer.pattern.patternTimingRevision.get() == timingRevision);
    assert(sequencer.page.get() == 1U);
    assert(sequencer.focusedStep.get() == 8U);
    assert(sequencer.structureUi.previewPageIndex.get() == 1U);
    assert(sequencer.structureUi.pageHold.action.get() ==
           core::state::StructureHoldAction::PASTE);
    assert(h.state.structureClipboard.revision.get() == clipboardRevision);
    assert(h.state.structureClipboard.sequencerGraph.get() == clipboardGraph);
    assert(h.state.sequencerHistory.undoCount() == 0U);
    assert(!h.state.hasPendingSequencerPatternHistoryCoalescing());

    workflow.pasteCurrentStructure();

    assert(sequencer.pattern.length.get() == 16U);
    assert(sequencer.pattern.note[8U] == 72U);
    assert(sequencer.pattern.velocity[8U] == 101U);
    assert(sequencer.pattern.isEnabled(8U));
    assert(rootStepHasMicroSequence(h, 8U));
    assert(sequencer.pattern.ccLanes != nullptr);
    assert(sequencer.pattern.ccLanes.get() == ccOwner);
    assert(byteHash(
               sequencer.pattern.ccLanes.get(),
               sizeof(*sequencer.pattern.ccLanes)) == ccHash);
    assert(sequencer.pattern.ccLanes->lanes[0U].activeMask.test(2U));
    assert(sequencer.pattern.ccLanes->lanes[0U].values[2U] == 55U);
    const auto committedRegion = seq::patternPlaybackRegion(sequencer.pattern);
    assert(committedRegion.contentLength == 16U);
    assert(committedRegion.playStart == 1U);
    assert(committedRegion.loopStart == 2U);
    assert(committedRegion.loopEnd == 6U);
    assert(sequencer.page.get() == 1U);
    assert(sequencer.focusedStep.get() == 8U);
    assert(sequencer.structureUi.previewPageIndex.get() == 1U);
    assert(sequencer.structureUi.pageHold.action.get() ==
           core::state::StructureHoldAction::NONE);
    assert(h.state.sequencerHistory.undoCount() == 1U);
    assert(h.state.sequencerHistory.undoCount(
               seq::SequencerHistoryScope::PatternOnly) == 1U);

    assert(h.state.undoSequencerHistory());
    assert(sequencer.pattern.length.get() == 16U);
    assert(sequencer.pattern.graph == nullptr);
    assert(sequencer.pattern.ccLanes != nullptr);
    assert(byteHash(
               sequencer.pattern.ccLanes.get(),
               sizeof(*sequencer.pattern.ccLanes)) == ccHash);
    assert(sequencer.pattern.ccLanes->lanes[0U].activeMask.test(2U));
    assert(sequencer.page.get() == 1U);
    assert(sequencer.focusedStep.get() == 8U);
    const auto undoRegion = seq::patternPlaybackRegion(sequencer.pattern);
    assert(undoRegion.contentLength == 16U);
    assert(undoRegion.playStart == 1U);
    assert(undoRegion.loopStart == 2U);
    assert(undoRegion.loopEnd == 6U);

    assert(h.state.redoSequencerHistory());
    assert(sequencer.pattern.length.get() == 16U);
    assert(sequencer.pattern.note[8U] == 72U);
    assert(rootStepHasMicroSequence(h, 8U));
    assert(byteHash(
               sequencer.pattern.ccLanes.get(),
               sizeof(*sequencer.pattern.ccLanes)) == ccHash);
    assert(sequencer.page.get() == 1U);
    assert(sequencer.focusedStep.get() == 8U);

    const auto undoCount = h.state.sequencerHistory.undoCount();
    const void* const replayCcOwner = sequencer.pattern.ccLanes.get();
    const auto replayCcRevision = sequencer.pattern.ccLaneRevision.get();
    sequencer.focusedStep.set(13U);
    workflow.beginHoldAction(core::state::StructureHoldAction::PASTE);
    {
        core::app::testing::ScopedExtmemAllocationFailure failure(1U);
        workflow.pasteCurrentStructure();
        assert(core::app::testing::extmemAllocationAttempt == 0U);
        assert(core::app::testing::extmemAllocationFailureOrdinal == 1U);
    }
    assert(sequencer.page.get() == 1U);
    assert(sequencer.focusedStep.get() == 8U);
    assert(sequencer.structureUi.previewPageIndex.get() == 1U);
    assert(sequencer.structureUi.pageHold.action.get() ==
           core::state::StructureHoldAction::NONE);
    assert(sequencer.pattern.ccLanes.get() == replayCcOwner);
    assert(byteHash(
               sequencer.pattern.ccLanes.get(),
               sizeof(*sequencer.pattern.ccLanes)) == ccHash);
    assert(sequencer.pattern.ccLaneRevision.get() == replayCcRevision);
    assert(h.state.sequencerHistory.undoCount() == undoCount);
    assert(!h.state.hasPendingSequencerPatternHistoryCoalescing());

    std::cout << "[PASS] PagePaste existing-target Graph OOM/commit/replay/NoChange\n";
}

void test_page_paste_failures_restore_current_and_selection_ui() {
    {
        SequencerStepHarness h;
        auto& sequencer = h.state.sequencer;
        h.navigationFocus.set(core::state::StructureNavigationFocus::PAGE);
        sequencer.pattern.setContentLength(16U);
        sequencer.pattern.note[0U] = 76U;
        sequencer.pattern.setEnabled(0U, true);
        sequencer.page.set(0U);
        sequencer.focusedStep.set(0U);
        sequencer.structureUi.syncPreviewPage(0U);

        auto copyWorkflow = makeStructureEditWorkflow(
            h, HistoryServices::fromCoreState(h.state));
        copyWorkflow.copyCurrentStructure();
        assert(h.state.structureClipboard.hasSequencerPage());
        const auto clipboardRevision = h.state.structureClipboard.revision.get();
        sequencer.page.set(1U);
        sequencer.focusedStep.set(8U);
        sequencer.structureUi.syncPreviewPage(1U);
        assert(seq::storeActiveTrack(h.state.sequencerTracks, sequencer));

        FailingPageCommitHistory failing{.state = &h.state};
        auto failingWorkflow = makeStructureEditWorkflow(
            h,
            HistoryServices::fromStaticOperations<
                kFailingPageCommitHistoryOperations>(&failing));
        failingWorkflow.beginHoldAction(core::state::StructureHoldAction::PASTE);
        const auto holdStartedAt = sequencer.structureUi.pageHold.startedAtMs.get();

        failingWorkflow.pasteCurrentStructure();
        test_support::drainNotifications();

        assert(failing.commitCount == 1U);
        assert(failing.abortCount == 1U);
        assert(sequencer.pattern.length.get() == 16U);
        assert(sequencer.pattern.note[0U] == 76U);
        assert(sequencer.pattern.isEnabled(0U));
        assert(sequencer.pattern.note[8U] == seq::SequencerState::DEFAULT_NOTE);
        assert(h.state.sequencerTracks.track(0U).length.get() == 16U);
        assert(h.state.sequencerTracks.track(0U).note[0U] == 76U);
        assert(sequencer.page.get() == 1U);
        assert(sequencer.focusedStep.get() == 8U);
        assert(sequencer.structureUi.previewPageIndex.get() == 1U);
        assert(sequencer.structureUi.pageHold.action.get() ==
               core::state::StructureHoldAction::PASTE);
        assert(sequencer.structureUi.pageHold.startedAtMs.get() == holdStartedAt);
        assert(h.state.structureClipboard.hasSequencerPage());
        assert(h.state.structureClipboard.revision.get() == clipboardRevision);
        assert(h.state.sequencerHistory.undoCount() == 0U);
        assert(!h.state.hasPendingSequencerPatternHistoryCoalescing());
    }

    {
        SequencerStepHarness h;
        auto& sequencer = h.state.sequencer;
        h.navigationFocus.set(core::state::StructureNavigationFocus::PAGE);
        sequencer.pattern.setContentLength(16U);
        sequencer.pattern.note[0U] = 68U;
        sequencer.pattern.note[8U] = 80U;
        sequencer.pattern.setEnabled(0U, true);
        sequencer.pattern.setEnabled(8U, true);
        sequencer.page.set(0U);
        sequencer.focusedStep.set(0U);
        sequencer.structureUi.syncPreviewPage(0U);
        assert(seq::storeActiveTrack(h.state.sequencerTracks, sequencer));

        auto workflow = makeStructureEditWorkflow(
            h, HistoryServices::fromCoreState(h.state));
        auto& selection = sequencer.structureUi.pageSelection;
        selection.active.set(true);
        selection.placing.set(false);
        selection.scope.set(core::state::StructureSelectionScope::PAGE);
        selection.cursorIndex.set(0U);
        selection.selectedMask.set(0x0003U);
        workflow.copyStructureSelection();
        assert(selection.placementActive());
        assert(h.state.structureClipboard.hasSequencerPageSelection());
        selection.cursorIndex.set(2U);
        sequencer.structureUi.syncPreviewPage(2U);
        workflow.update(0U);
        workflow.beginHoldAction(core::state::StructureHoldAction::PASTE);

        const auto selectedMask = selection.selectedMask.get();
        const auto destinationMask = selection.destinationMask.get();
        const auto overwriteMask = selection.overwriteMask.get();
        const auto selectionClipboardRevision = selection.clipboardRevision.get();
        const auto clipboardRevision = h.state.structureClipboard.revision.get();
        const auto holdStartedAt = sequencer.structureUi.pageHold.startedAtMs.get();

        {
            core::app::testing::ScopedExtmemAllocationFailure failure(1U);
            workflow.pasteStructureSelection();
            assert(core::app::testing::extmemAllocationAttempt == 1U);
            assert(core::app::testing::extmemAllocationFailureOrdinal == 0U);
        }
        test_support::drainNotifications();
        assert(sequencer.pattern.length.get() == 16U);
        assert(sequencer.pattern.note[0U] == 68U);
        assert(sequencer.pattern.note[8U] == 80U);
        assert(sequencer.page.get() == 0U);
        assert(sequencer.focusedStep.get() == 0U);
        assert(sequencer.structureUi.previewPageIndex.get() == 2U);
        assert(selection.active.get());
        assert(selection.placementActive());
        assert(selection.cursorIndex.get() == 2U);
        assert(selection.selectedMask.get() == selectedMask);
        assert(selection.destinationMask.get() == destinationMask);
        assert(selection.overwriteMask.get() == overwriteMask);
        assert(selection.clipboardRevision.get() == selectionClipboardRevision);
        assert(sequencer.structureUi.pageHold.action.get() ==
               core::state::StructureHoldAction::PASTE);
        assert(sequencer.structureUi.pageHold.startedAtMs.get() == holdStartedAt);
        assert(h.state.sequencerHistory.undoCount() == 0U);
        assert(!h.state.hasPendingSequencerPatternHistoryCoalescing());

        FailingPageCommitHistory failing{.state = &h.state};
        auto failingWorkflow = makeStructureEditWorkflow(
            h,
            HistoryServices::fromStaticOperations<
                kFailingPageCommitHistoryOperations>(&failing));
        failingWorkflow.pasteStructureSelection();
        test_support::drainNotifications();

        assert(failing.commitCount == 1U);
        assert(failing.abortCount == 1U);
        assert(sequencer.pattern.length.get() == 16U);
        assert(sequencer.pattern.note[0U] == 68U);
        assert(sequencer.pattern.note[8U] == 80U);
        assert(h.state.sequencerTracks.track(0U).length.get() == 16U);
        assert(sequencer.page.get() == 0U);
        assert(sequencer.focusedStep.get() == 0U);
        assert(sequencer.structureUi.previewPageIndex.get() == 2U);
        assert(selection.active.get());
        assert(selection.placementActive());
        assert(selection.cursorIndex.get() == 2U);
        assert(selection.selectedMask.get() == selectedMask);
        assert(selection.destinationMask.get() == destinationMask);
        assert(selection.overwriteMask.get() == overwriteMask);
        assert(selection.clipboardRevision.get() == selectionClipboardRevision);
        assert(sequencer.structureUi.pageHold.action.get() ==
               core::state::StructureHoldAction::PASTE);
        assert(sequencer.structureUi.pageHold.startedAtMs.get() == holdStartedAt);
        assert(h.state.structureClipboard.hasSequencerPageSelection());
        assert(h.state.structureClipboard.revision.get() == clipboardRevision);
        assert(h.state.sequencerHistory.undoCount() == 0U);
        assert(!h.state.hasPendingSequencerPatternHistoryCoalescing());
    }

    std::cout << "[PASS] PagePaste failures restore current/selection UI exactly\n";
}

void test_child_content_clear_copy_and_paste_are_undoable() {
    SequencerStepHarness h;
    const auto rootNode = core::state::sequencer::rootStepNodeId(0);
    const auto micro =
        core::state::sequencer::createMicroSequence(h.state.sequencer.pattern, rootNode, 2);
    assert(micro.ok);
    assert(core::state::sequencer::enterMicroSequenceContentView(h.state.sequencer, rootNode,
                                                                 micro.id));
    h.state.sequencer.focusedStep.set(0);

    const auto childNode0 = core::state::sequencer::activeContentStepNodeId(h.state.sequencer, 0);
    const auto cycle =
        core::state::sequencer::createCycleStateSet(h.state.sequencer.pattern, childNode0, 2);
    assert(cycle.ok);
    assert(core::state::sequencer::setNodeNoteOffset(
        h.state.sequencer.pattern,
        h.state.sequencer.pattern.graph->cycleSets[cycle.id].firstStateNode, 5));

    const auto childNode1 = core::state::sequencer::activeContentStepNodeId(h.state.sequencer, 1);
    const auto replacedCycle =
        core::state::sequencer::createCycleStateSet(h.state.sequencer.pattern, childNode1, 4);
    assert(replacedCycle.ok);
    assert(core::state::sequencer::setNodeNoteOffset(
        h.state.sequencer.pattern,
        h.state.sequencer.pattern.graph->cycleSets[replacedCycle.id].firstStateNode, 9));
    h.state.sequencer.contentView.bump();

    const auto* graphBeforeClear = core::state::sequencer::graphView(h.state.sequencer.pattern);
    assert(graphBeforeClear != nullptr);
    assertGraphHasNoOrphans(*graphBeforeClear);
    const auto* graphOwner = graphBeforeClear;
    const uint16_t nodesBeforeClear = graphBeforeClear->stepNodeCount;
    const uint8_t sequencesBeforeClear = graphBeforeClear->sequenceCount;
    const uint8_t cyclesBeforeClear = graphBeforeClear->cycleSetCount;

    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.release(Config::ButtonID::BOTTOM_RIGHT);
    assert(h.state.structureClipboard.hasSequencerStepContent());

    const uint8_t undoBeforeClear = h.state.sequencerHistory.undoCount();
    {
        core::app::testing::ScopedExtmemAllocationFailure failure(5U);
        h.tap(Config::ButtonID::BOTTOM_LEFT);
        tx::assertMaxPlusOneStillArmed(4U);
    }
    const auto* graphAfterClear = core::state::sequencer::graphView(h.state.sequencer.pattern);
    assert(graphAfterClear != nullptr);
    assert(graphAfterClear == graphOwner);
    assert(!graphAfterClear->stepNodes[childNode0].has(oc::note::sequencer::STEP_NODE_CYCLE_SET));
    assert(graphAfterClear->stepNodes[childNode1].has(oc::note::sequencer::STEP_NODE_CYCLE_SET));
    assert(graphAfterClear->stepNodeCount ==
           nodesBeforeClear -
               oc::note::sequencer::StepSequencerGraphLimits::MAX_CYCLE_STATES_PER_SET);
    assert(graphAfterClear->sequenceCount == sequencesBeforeClear);
    assert(graphAfterClear->cycleSetCount == cyclesBeforeClear - 1U);
    assertGraphHasNoOrphans(*graphAfterClear);
    assert(h.state.sequencerHistory.undoCount() == undoBeforeClear + 1U);

    const uint16_t nodesBeforePaste = graphAfterClear->stepNodeCount;
    const uint8_t sequencesBeforePaste = graphAfterClear->sequenceCount;
    const uint8_t cyclesBeforePaste = graphAfterClear->cycleSetCount;

    h.press(Config::MACRO_BUTTONS[1]);
    h.release(Config::MACRO_BUTTONS[1]);
    assert(h.state.sequencer.focusedStep.get() == 1);
    {
        core::app::testing::ScopedExtmemAllocationFailure failure(5U);
        h.press(Config::ButtonID::BOTTOM_RIGHT);
        h.tick(0);
        h.tick(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
        h.release(Config::ButtonID::BOTTOM_RIGHT);
        tx::assertMaxPlusOneStillArmed(4U);
    }

    const auto* graphAfterPaste = core::state::sequencer::graphView(h.state.sequencer.pattern);
    assert(graphAfterPaste != nullptr);
    assert(graphAfterPaste == graphOwner);
    assert(graphAfterPaste->stepNodes[childNode1].has(oc::note::sequencer::STEP_NODE_CYCLE_SET));
    const auto* pastedCycle =
        graphAfterPaste->cycleSet(graphAfterPaste->stepNodes[childNode1].cycleSetId);
    assert(pastedCycle != nullptr);
    assert(pastedCycle->length == 2U);
    assert(graphAfterPaste->stepNodes[pastedCycle->firstStateNode].noteOffset == 5);
    assert(graphAfterPaste->stepNodeCount == nodesBeforePaste);
    assert(graphAfterPaste->sequenceCount == sequencesBeforePaste);
    assert(graphAfterPaste->cycleSetCount == cyclesBeforePaste);
    assertGraphHasNoOrphans(*graphAfterPaste);

    assert(h.state.undoProjectHistory());
    const auto* graphAfterUndo = core::state::sequencer::graphView(h.state.sequencer.pattern);
    assert(graphAfterUndo != nullptr);
    assert(graphAfterUndo->stepNodes[childNode1].has(oc::note::sequencer::STEP_NODE_CYCLE_SET));
    const auto* restoredCycle =
        graphAfterUndo->cycleSet(graphAfterUndo->stepNodes[childNode1].cycleSetId);
    assert(restoredCycle != nullptr);
    assert(restoredCycle->length == 4U);
    assert(graphAfterUndo->stepNodes[restoredCycle->firstStateNode].noteOffset == 9);
    assertGraphHasNoOrphans(*graphAfterUndo);

    std::cout << "[PASS] test_child_content_clear_copy_and_paste_are_undoable\n";
}

void test_child_content_clear_and_paste_preflight_failures_are_atomic() {
    for (std::size_t ordinal = 1U; ordinal <= 4U; ++ordinal) {
        SequencerStepHarness h;
        const auto rootNode = core::state::sequencer::rootStepNodeId(0);
        const auto micro =
            core::state::sequencer::createMicroSequence(h.state.sequencer.pattern, rootNode, 2);
        assert(micro.ok);
        assert(core::state::sequencer::enterMicroSequenceContentView(h.state.sequencer, rootNode,
                                                                     micro.id));
        h.state.sequencer.focusedStep.set(0);
        const auto childNode =
            core::state::sequencer::activeContentStepNodeId(h.state.sequencer, 0);
        assert(core::state::sequencer::createCycleStateSet(h.state.sequencer.pattern, childNode, 2)
                   .ok);
        h.state.sequencer.contentView.bump();

        const auto* graphOwner = core::state::sequencer::graphView(h.state.sequencer.pattern);
        assert(graphOwner != nullptr);
        assertGraphHasNoOrphans(*graphOwner);
        core::state::sequencer::SequencerHistoryPatternSnapshot musicalBefore;
        tx::captureMusicalSnapshot(h.state, musicalBefore);
        const auto invariantBefore = tx::captureStateInvariant(h.state);

        {
            core::app::testing::ScopedExtmemAllocationFailure failure(ordinal);
            h.tap(Config::ButtonID::BOTTOM_LEFT);
            tx::assertFailureConsumed(ordinal);
        }

        const auto* graphAfter = core::state::sequencer::graphView(h.state.sequencer.pattern);
        assert(graphAfter == graphOwner);
        assertGraphHasNoOrphans(*graphAfter);
        tx::assertMusicalSnapshot(h.state, musicalBefore);
        tx::assertStateInvariant(h.state, invariantBefore);
        assert(!h.state.hasPendingSequencerPatternHistoryCoalescing());
    }

    for (std::size_t ordinal = 1U; ordinal <= 4U; ++ordinal) {
        SequencerStepHarness h;
        const auto rootNode = core::state::sequencer::rootStepNodeId(0);
        const auto micro =
            core::state::sequencer::createMicroSequence(h.state.sequencer.pattern, rootNode, 2);
        assert(micro.ok);
        assert(core::state::sequencer::enterMicroSequenceContentView(h.state.sequencer, rootNode,
                                                                     micro.id));
        const auto sourceNode =
            core::state::sequencer::activeContentStepNodeId(h.state.sequencer, 0);
        assert(core::state::sequencer::createCycleStateSet(h.state.sequencer.pattern, sourceNode, 2)
                   .ok);
        const auto destinationNode =
            core::state::sequencer::activeContentStepNodeId(h.state.sequencer, 1);
        assert(core::state::sequencer::createCycleStateSet(h.state.sequencer.pattern,
                                                           destinationNode, 4)
                   .ok);
        h.state.sequencer.contentView.bump();
        h.state.sequencer.focusedStep.set(0);
        h.tap(Config::ButtonID::BOTTOM_RIGHT);
        assert(h.state.structureClipboard.hasSequencerStepContent());
        h.state.sequencer.focusedStep.set(1);

        const auto* graphOwner = core::state::sequencer::graphView(h.state.sequencer.pattern);
        assert(graphOwner != nullptr);
        assertGraphHasNoOrphans(*graphOwner);
        core::state::sequencer::SequencerHistoryPatternSnapshot musicalBefore;
        tx::captureMusicalSnapshot(h.state, musicalBefore);
        const auto invariantBefore = tx::captureStateInvariant(h.state);

        {
            core::app::testing::ScopedExtmemAllocationFailure failure(ordinal);
            h.press(Config::ButtonID::BOTTOM_RIGHT);
            h.tick(0);
            h.tick(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
            h.release(Config::ButtonID::BOTTOM_RIGHT);
            tx::assertFailureConsumed(ordinal);
        }

        const auto* graphAfter = core::state::sequencer::graphView(h.state.sequencer.pattern);
        assert(graphAfter == graphOwner);
        assertGraphHasNoOrphans(*graphAfter);
        tx::assertMusicalSnapshot(h.state, musicalBefore);
        tx::assertStateInvariant(h.state, invariantBefore);
        assert(!h.state.hasPendingSequencerPatternHistoryCoalescing());
    }

    std::cout << "[PASS] all child clear/paste preflight failures are atomic\n";
}

void test_graphless_child_content_paste_uses_prospective_compacted_owner() {
    const auto storeClipboard = [](core::state::StructureClipboardState& clipboard) {
        core::state::sequencer::SequencerPatternState source;
        source.setContentLength(8);
        const auto sourceRoot = core::state::sequencer::rootStepNodeId(0);
        const auto micro = core::state::sequencer::createMicroSequence(source, sourceRoot, 2);
        assert(micro.ok);
        const auto* sequence = source.graph->sequence(micro.id);
        assert(sequence != nullptr);
        assert(core::state::sequencer::setNodeNoteOffset(source, sequence->firstStepNode, 7));
        const auto cycle = core::state::sequencer::createCycleStateSet(source, sourceRoot, 4);
        assert(cycle.ok);
        const auto* cycleSet = source.graph->cycleSet(cycle.id);
        assert(cycleSet != nullptr);
        assert(core::state::sequencer::setNodeNoteOffset(source, cycleSet->firstStateNode, 9));
        const auto* sourceGraph = core::state::sequencer::graphView(source);
        assert(sourceGraph != nullptr);
        assert(clipboard.storeSequencerStepContent(
            *sourceGraph, sourceRoot, core::state::SequencerStepContentClipboardKind::ALL));
    };
    constexpr uint8_t targetStep = 4U;
    constexpr uint8_t transactionKey = targetStep | 0x80U;
    constexpr auto owner = core::state::sequencer::SequencerPreparedPatternEditOwner::StepContent;
    const auto descriptor = []() {
        return core::state::sequencer::SequencerHistoryDescriptor{
            .kind = core::state::sequencer::SequencerHistoryActionKind::StepEdit,
            .stepIndex = targetStep,
            .property = core::state::sequencer::StepProperty::NOTE,
            .hasValue = false,
        };
    };

    for (std::size_t ordinal = 1U; ordinal <= 4U; ++ordinal) {
        SequencerStepHarness h;
        h.state.sequencer.pattern.setContentLength(8);
        h.state.sequencer.focusedStep.set(targetStep);
        storeClipboard(h.state.structureClipboard);
        assert(core::state::sequencer::graphView(h.state.sequencer.pattern) == nullptr);
        core::state::sequencer::SequencerHistoryPatternSnapshot musicalBefore;
        tx::captureMusicalSnapshot(h.state, musicalBefore);
        const auto invariantBefore = tx::captureStateInvariant(h.state);

        {
            core::app::testing::ScopedExtmemAllocationFailure failure(ordinal);
            assert(h.state.beginOrContinueSequencerPreparedPatternEdit(
                       owner, transactionKey,
                       core::state::sequencer::SequencerCoalescedPatternPayloadPlan::
                           FullWithProspectiveGraph,
                       descriptor(), true) ==
                   core::state::sequencer::SequencerPreparedPatternEditBeginOutcome::Failed);
            tx::assertFailureConsumed(ordinal);
        }

        assert(core::state::sequencer::graphView(h.state.sequencer.pattern) == nullptr);
        tx::assertMusicalSnapshot(h.state, musicalBefore);
        tx::assertStateInvariant(h.state, invariantBefore);
        assert(!h.state.hasPendingSequencerPatternHistoryCoalescing());
    }

    SequencerStepHarness h;
    h.state.sequencer.pattern.setContentLength(8);
    h.state.sequencer.focusedStep.set(targetStep);
    storeClipboard(h.state.structureClipboard);
    assert(core::state::sequencer::graphView(h.state.sequencer.pattern) == nullptr);
    const oc::note::sequencer::StepSequencerGraph* prospectiveOwner = nullptr;
    {
        core::app::testing::ScopedExtmemAllocationFailure failure(5U);
        assert(h.state.beginOrContinueSequencerPreparedPatternEdit(
                   owner, transactionKey,
                   core::state::sequencer::SequencerCoalescedPatternPayloadPlan::
                       FullWithProspectiveGraph,
                   descriptor(), true) ==
               core::state::sequencer::SequencerPreparedPatternEditBeginOutcome::Started);
        prospectiveOwner = h.state.sequencer.pattern.graph.get();
        assert(prospectiveOwner != nullptr);
        assert(core::state::sequencer::graphView(h.state.sequencer.pattern) == nullptr);
        const bool changed =
            core::state::sequencer::pasteActiveContentChildrenFromClipboardPreservingGraphOwner(
                h.state.sequencer, targetStep, h.state.structureClipboard);
        assert(changed);
        assert(h.state.sequencer.pattern.graph.get() == prospectiveOwner);
        assert(h.state.sealSequencerPreparedPatternEdit(owner, transactionKey, changed,
                                                        descriptor()) ==
               core::state::sequencer::SequencerPreparedPatternEditSealOutcome::Sealed);
        assert(h.state.sequencer.pattern.graph.get() == prospectiveOwner);
        assert(h.state.commitSequencerPreparedPatternEdit(owner) ==
               core::state::sequencer::SequencerPreparedPatternEditCommitOutcome::Committed);
        // Every allocation ordinal below five failed before any write above.
        // The still-armed fifth ordinal therefore proves that the live owner
        // was installed prospectively and neither allocated nor replaced by
        // compaction after the first mutation.
        tx::assertMaxPlusOneStillArmed(4U);
    }

    const auto* graphOwner = core::state::sequencer::graphView(h.state.sequencer.pattern);
    assert(graphOwner == prospectiveOwner);
    assert(h.state.sequencer.pattern.graph.get() == graphOwner);
    assertGraphHasNoOrphans(*graphOwner);
    assert(graphOwner->stepNodeCount ==
           core::state::sequencer::SequencerPatternState::MAX_STEPS +
               oc::note::sequencer::StepSequencerGraphLimits::MAX_EXPANDED_NOTES_PER_ROOT_STEP +
               oc::note::sequencer::StepSequencerGraphLimits::MAX_CYCLE_STATES_PER_SET);
    assert(graphOwner->sequenceCount == 2U);
    assert(graphOwner->cycleSetCount == 1U);
    const auto* pastedRoot =
        graphOwner->stepNode(core::state::sequencer::rootStepNodeId(targetStep));
    assert(pastedRoot != nullptr);
    const auto* pastedSequence = graphOwner->sequence(pastedRoot->childSequenceId);
    const auto* pastedCycle = graphOwner->cycleSet(pastedRoot->cycleSetId);
    assert(pastedSequence != nullptr);
    assert(pastedCycle != nullptr);
    assert(pastedSequence->length == 2U);
    assert(pastedCycle->length == 4U);
    assert(graphOwner->stepNodes[pastedSequence->firstStepNode].noteOffset == 7);
    assert(graphOwner->stepNodes[pastedCycle->firstStateNode].noteOffset == 9);
    assert(h.state.sequencerHistory.undoCount() == 1U);
    assert(!h.state.hasPendingSequencerPatternHistoryCoalescing());

    core::state::sequencer::SequencerHistoryPatternSnapshot pastedSnapshot;
    tx::captureMusicalSnapshot(h.state, pastedSnapshot);
    assert(h.state.undoProjectHistory());
    assert(core::state::sequencer::graphView(h.state.sequencer.pattern) == nullptr);
    assert(h.state.redoProjectHistory());
    const auto* graphAfterRedo = core::state::sequencer::graphView(h.state.sequencer.pattern);
    assert(graphAfterRedo != nullptr);
    assertGraphHasNoOrphans(*graphAfterRedo);
    tx::assertMusicalSnapshot(h.state, pastedSnapshot);

    std::cout << "[PASS] graphless child paste keeps its prospective compacted owner\n";
}

void test_undo_removed_active_child_context_returns_to_root() {
    SequencerStepHarness h;
    h.state.sequencer.pattern.setContentLength(8);
    h.state.sequencer.focusedStep.set(0);

    core::state::sequencer::SequencerHistoryPatternSnapshot before;
    assert(core::state::sequencer::captureHistorySnapshot(h.state.sequencer, before));

    const auto rootNode = core::state::sequencer::rootStepNodeId(0);
    const auto micro =
        core::state::sequencer::createMicroSequence(h.state.sequencer.pattern, rootNode, 2);
    assert(micro.ok);

    core::state::sequencer::SequencerHistoryPatternSnapshot after;
    assert(core::state::sequencer::captureHistorySnapshot(h.state.sequencer, after));
    assert(h.state.recordSequencerPatternHistory(
        std::move(before), std::move(after),
        core::state::sequencer::SequencerHistoryDescriptor{
            .kind = core::state::sequencer::SequencerHistoryActionKind::StepEdit,
            .stepIndex = 0,
        }));

    assert(core::state::sequencer::enterMicroSequenceContentView(h.state.sequencer, rootNode,
                                                                 micro.id));
    assert(core::state::sequencer::isMicroSequenceContentView(h.state.sequencer));

    assert(h.state.undoSequencerHistory());
    assert(core::state::sequencer::isRootContentView(h.state.sequencer));
    assert(h.state.sequencer.contentView.depth.get() == 0);
    assert(h.state.sequencer.contentView.ownerNodeId.get() ==
           oc::note::sequencer::StepSequencerGraphLimits::INVALID_ID);

    std::cout << "[PASS] test_undo_removed_active_child_context_returns_to_root\n";
}

void test_sequencer_track_copy_and_long_press_paste_to_add_slot() {
    SequencerStepHarness h;
    h.state.sequencerTracks.reset();
    h.state.setSharedTrackState(0x0001, 0);
    configureProjectTrackFixture(h.state, 1, 8);
    h.state.sequencer.pattern.setContentLength(8);
    h.state.sequencer.pattern.note[0] = 79;
    h.state.sequencer.pattern.velocity[0] = 96;
    h.state.sequencer.pattern.gate[0] = 72;
    h.state.sequencer.pattern.setEnabled(0, true);
    createRootMicroSequence(h, 0);
    focusTrackNavigation(h);

    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.release(Config::ButtonID::BOTTOM_RIGHT);
    assert(h.state.structureClipboard.hasSequencerTrack());

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.trackNavigation.previewAddSlot.get());
    assert(h.state.trackNavigation.previewTrackIndex.get() == 1);

    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.tick(0);
    h.tick(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    h.release(Config::ButtonID::BOTTOM_RIGHT);

    assert(h.state.sequencerTracks.currentEnabledMask() == 0x0003);
    assert(h.state.sequencerTracks.activeTrackIndex() == 1);
    assert(h.state.projectTracks.authored.midiChannels[1] == 8);
    assert(h.state.sequencer.pattern.note[0] == 79);
    assert(h.state.sequencer.pattern.velocity[0] == 96);
    assert(h.state.sequencer.pattern.gate[0] == 72);
    assert(h.state.sequencer.pattern.isEnabled(0));
    assert(rootStepHasMicroSequence(h, 0));
    assert(h.state.sequencerHistory.undoCount(
               core::state::sequencer::SequencerHistoryScope::Structure) == 1);
    assert(h.state.structureClipboard.hasSequencerTrack());

    assert(h.state.undoSequencerHistory());
    assert(h.state.sequencerTracks.currentEnabledMask() == 0x0001);
    assert(h.state.projectTracks.authored.midiChannels[1] == 8);
    assert(h.state.structureClipboard.hasSequencerTrack());

    assert(h.state.redoSequencerHistory());
    assert(h.state.sequencerTracks.activeTrackIndex() == 1);
    assert(h.state.projectTracks.authored.midiChannels[1] == 8);
    assert(rootStepHasMicroSequence(h, 0));
    assert(h.state.structureClipboard.hasSequencerTrack());

    std::cout << "[PASS] test_sequencer_track_copy_and_long_press_paste_to_add_slot\n";
}

void test_sequencer_track_paste_preserves_occupied_destination_routing_and_mute() {
    SequencerStepHarness h;
    h.state.sequencerTracks.reset();
    h.state.setSharedTrackState(0x0003, 0);
    configureProjectTrackFixture(h.state, 1, 11, true);
    h.state.sequencer.pattern.note[0] = 76;
    h.state.sequencer.pattern.velocity[0] = 104;
    h.state.sequencer.pattern.setEnabled(0, true);
    focusTrackNavigation(h);

    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.release(Config::ButtonID::BOTTOM_RIGHT);
    assert(h.state.structureClipboard.hasSequencerTrack());

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.sequencerTracks.activeTrackIndex() == 1);
    assert(h.state.projectTracks.authored.midiChannels[h.state.currentSharedActiveTrack()] == 11);
    // Project Track remains authoritative while the guarded paste is open.
    assert(core::state::project::ProjectTrackDomainServices::fromCoreState(h.state).setMidiChannel(
        1, 13));
    assert(h.state.projectTracks.authored.midiChannels[1] == 13);

    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.tick(0);
    h.tick(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    h.release(Config::ButtonID::BOTTOM_RIGHT);

    assert(h.state.sequencerTracks.activeTrackIndex() == 1);
    assert(h.state.projectTracks.authored.midiChannels[1] == 13);
    assert(core::state::project::projectTrackMuted(h.state.projectTracks, 1));
    assert(h.state.sequencer.pattern.note[0] == 76);
    assert(h.state.sequencer.pattern.velocity[0] == 104);
    assert(h.state.sequencer.pattern.isEnabled(0));
    assert(h.state.sequencerHistory.undoCount(
               core::state::sequencer::SequencerHistoryScope::Structure) == 1);
    assert(h.state.structureClipboard.hasSequencerTrack());

    assert(h.state.undoSequencerHistory());
    assert(h.state.projectTracks.authored.midiChannels[h.state.currentSharedActiveTrack()] == 13);
    assert(core::state::project::projectTrackMuted(h.state.projectTracks, 1));
    assert(h.state.structureClipboard.hasSequencerTrack());

    assert(h.state.redoSequencerHistory());
    assert(h.state.projectTracks.authored.midiChannels[h.state.currentSharedActiveTrack()] == 13);
    assert(core::state::project::projectTrackMuted(h.state.projectTracks, 1));
    assert(h.state.sequencer.pattern.note[0] == 76);
    assert(h.state.structureClipboard.hasSequencerTrack());

    std::cout
        << "[PASS] test_sequencer_track_paste_preserves_occupied_destination_routing_and_mute\n";
}

void test_track_paste_global_undo_redo_restores_content_and_reports_outcome() {
    SequencerStepHarness h;
    h.state.sequencerTracks.reset();
    h.state.setSharedTrackState(0x0003, 0);
    focusTrackNavigation(h);

    configureProjectTrackFixture(h.state, 0, 2);
    h.state.sequencer.pattern.note[0] = 76;
    h.state.sequencer.pattern.velocity[0] = 104;
    h.state.sequencer.pattern.setEnabled(0, true);
    auto& destination = h.state.sequencerTracks.track(1);
    configureProjectTrackFixture(h.state, 1, 11, true);
    destination.note[0] = 42;
    destination.velocity[0] = 73;
    destination.setEnabled(0, true);

    h.tap(Config::ButtonID::BOTTOM_RIGHT);
    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.sequencerTracks.activeTrackIndex() == 1);
    assert(h.state.sequencer.pattern.note[0] == 42);

    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.advance(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    h.release(Config::ButtonID::BOTTOM_RIGHT);
    assert(h.state.sequencer.pattern.note[0] == 76);
    assert(h.state.projectTracks.authored.midiChannels[h.state.currentSharedActiveTrack()] == 11);
    assert(core::state::project::projectTrackMuted(h.state.projectTracks, 1));

    assert(h.state.undoProjectHistory());
    assert(h.state.sequencer.pattern.note[0] == 42);
    assert(h.state.sequencer.pattern.velocity[0] == 73);
    assert(h.state.projectTracks.authored.midiChannels[h.state.currentSharedActiveTrack()] == 11);
    assert(core::state::project::projectTrackMuted(h.state.projectTracks, 1));
    assert(std::strcmp(h.state.sequencer.historyFeedback.line2.data(), "Track Paste") == 0);
    assert(std::strcmp(h.state.sequencer.historyFeedback.line3.data(), "Pending cancelled") == 0);

    assert(h.state.redoProjectHistory());
    assert(h.state.sequencer.pattern.note[0] == 76);
    assert(h.state.projectTracks.authored.midiChannels[h.state.currentSharedActiveTrack()] == 11);
    assert(std::strcmp(h.state.sequencer.historyFeedback.line2.data(), "Track Paste") == 0);

    std::cout << "[PASS] test_track_paste_global_undo_redo_restores_content_and_reports_outcome\n";
}

void test_track_paste_clamps_focus_to_short_source_before_history_commit() {
    SequencerStepHarness h;
    h.state.sequencerTracks.reset();
    h.state.setSharedTrackState(0x0003, 0);
    focusTrackNavigation(h);
    h.state.sequencer.pattern.setContentLength(8);
    h.state.sequencer.pattern.note[0] = 68;
    h.state.sequencerTracks.track(1).setContentLength(128);
    configureProjectTrackFixture(h.state, 1, 7);

    h.tap(Config::ButtonID::BOTTOM_RIGHT);
    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.sequencerTracks.activeTrackIndex() == 1);
    h.state.sequencer.focusedStep.set(100);
    h.state.sequencer.page.set(12);

    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.advance(0);
    h.advance(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    h.release(Config::ButtonID::BOTTOM_RIGHT);

    assert(h.state.sequencer.pattern.length.get() == 8);
    assert(h.state.sequencer.focusedStep.get() == 7);
    assert(h.state.sequencer.page.get() == 0);
    assert(h.state.sequencer.pattern.note[0] == 68);
    assert(h.state.sequencerHistory.undoCount(
               core::state::sequencer::SequencerHistoryScope::Structure) == 1);

    assert(h.state.undoSequencerHistory());
    assert(h.state.sequencer.pattern.length.get() == 128);
    assert(h.state.sequencer.focusedStep.get() == 100);
    assert(h.state.sequencer.page.get() == 12);

    assert(h.state.redoSequencerHistory());
    assert(h.state.sequencer.pattern.length.get() == 8);
    assert(h.state.sequencer.focusedStep.get() == 7);
    assert(h.state.sequencer.page.get() == 0);

    std::cout << "[PASS] test_track_paste_clamps_focus_to_short_source_before_history_commit\n";
}

void test_track_paste_mid_hold_release_cancels_without_mutation_or_history() {
    SequencerStepHarness h;
    h.state.sequencerTracks.reset();
    h.state.setSharedTrackState(0x0003, 0);
    h.state.sequencer.pattern.note[0] = 81;
    h.state.sequencerTracks.track(1).note[0] = 44;
    configureProjectTrackFixture(h.state, 1, 6);
    focusTrackNavigation(h);
    h.tap(Config::ButtonID::BOTTOM_RIGHT);
    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.sequencer.pattern.note[0] == 44);

    const uint8_t undoBefore = h.state.sequencerHistory.undoCount(
        core::state::sequencer::SequencerHistoryScope::Structure);
    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.advance(Config::Timing::LATCH_THRESHOLD_MS);
    assert(h.state.sequencer.structureUi.trackPaste.guard.phase ==
           core::state::contextual::GuardedActionPhase::ARMED);
    h.advance(250);
    h.release(Config::ButtonID::BOTTOM_RIGHT);

    assert(h.state.sequencer.pattern.note[0] == 44);
    assert(h.state.sequencerTracks.track(1).note[0] == 44);
    assert(h.state.sequencerHistory.undoCount(
               core::state::sequencer::SequencerHistoryScope::Structure) == undoBefore);
    assert(h.state.sequencer.structureUi.trackPaste.feedback.status ==
           core::state::contextual::OperationFeedbackStatus::CANCELLED);
    h.advance(1);
    assert(h.state.sequencer.structureUi.trackPaste.feedback.status ==
           core::state::contextual::OperationFeedbackStatus::CANCELLED);
}

void test_track_paste_commits_once_at_absolute_long_threshold() {
    SequencerStepHarness h;
    h.state.sequencerTracks.reset();
    h.state.setSharedTrackState(0x0003, 0);
    h.state.sequencer.pattern.note[0] = 83;
    h.state.sequencerTracks.track(1).note[0] = 45;
    configureProjectTrackFixture(h.state, 1, 7);
    focusTrackNavigation(h);
    h.tap(Config::ButtonID::BOTTOM_RIGHT);
    h.turn(Config::EncoderID::NAV, 1.0f);

    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.advance(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS - 1U);
    assert(h.state.sequencer.pattern.note[0] == 45);
    assert(h.state.sequencerHistory.undoCount(
               core::state::sequencer::SequencerHistoryScope::Structure) == 0);
    h.advance(1);
    assert(h.state.sequencer.pattern.note[0] == 83);
    assert(h.state.sequencerHistory.undoCount(
               core::state::sequencer::SequencerHistoryScope::Structure) == 1);
    assert(h.state.sequencer.structureUi.trackPaste.guard.phase ==
           core::state::contextual::GuardedActionPhase::COMMITTED);
    h.advance(500);
    h.release(Config::ButtonID::BOTTOM_RIGHT);
    assert(h.state.sequencerHistory.undoCount(
               core::state::sequencer::SequencerHistoryScope::Structure) == 1);
}

void test_track_paste_left_top_cancels_and_consumes_later_release() {
    SequencerStepHarness h;
    h.state.sequencerTracks.reset();
    h.state.setSharedTrackState(0x0003, 0);
    h.state.sequencer.pattern.note[0] = 84;
    h.state.sequencerTracks.track(1).note[0] = 46;
    focusTrackNavigation(h);
    h.tap(Config::ButtonID::BOTTOM_RIGHT);
    h.turn(Config::EncoderID::NAV, 1.0f);

    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.advance(Config::Timing::LATCH_THRESHOLD_MS);
    h.tap(Config::ButtonID::LEFT_TOP);
    h.release(Config::ButtonID::BOTTOM_RIGHT);

    assert(h.state.sequencer.pattern.note[0] == 46);
    assert(h.state.sequencerHistory.undoCount(
               core::state::sequencer::SequencerHistoryScope::Structure) == 0);
    assert(h.navigationFocus.get() == core::state::StructureNavigationFocus::TRACK);
}

void test_track_paste_refreshes_route_during_hold_and_freezes_queued_plan() {
    SequencerStepHarness h;
    h.state.sequencerTracks.reset();
    h.state.setSharedTrackState(0x0003, 0);
    h.state.sequencer.pattern.note[0] = 85;
    h.state.sequencerTracks.track(1).note[0] = 47;
    configureProjectTrackFixture(h.state, 1, 3);
    focusTrackNavigation(h);
    h.tap(Config::ButtonID::BOTTOM_RIGHT);
    h.turn(Config::EncoderID::NAV, 1.0f);

    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.advance(Config::Timing::LATCH_THRESHOLD_MS);
    assert(core::state::project::ProjectTrackDomainServices::fromCoreState(h.state).setMidiChannel(
        1, 8));
    h.advance(1);
    assert(h.state.sequencer.structureUi.trackPaste.plan.entries[0].targetMidiChannel == 8);
    h.advance(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS - Config::Timing::LATCH_THRESHOLD_MS - 1U);
    assert(h.state.sequencer.pattern.note[0] == 85);
    assert(h.state.projectTracks.authored.midiChannels[h.state.currentSharedActiveTrack()] == 8);
    h.release(Config::ButtonID::BOTTOM_RIGHT);

    const auto frozenPlan = h.state.sequencer.structureUi.trackPaste.plan;
    const uint32_t frozenGeneration = h.state.sequencer.structureUi.trackPaste.activationGeneration;
    assert(frozenGeneration != 0);
    h.tap(Config::ButtonID::BOTTOM_RIGHT);  // Copy the destination after commit.
    h.advance(0);
    assert(core::state::sameSequencerTrackClipboardTransferPlan(
        h.state.sequencer.structureUi.trackPaste.plan, frozenPlan));
    assert(h.state.sequencer.structureUi.trackPaste.activationGeneration == frozenGeneration);
}

void test_deleted_track_slot_can_be_recreated_at_any_gap() {
    SequencerStepHarness h;
    h.state.sequencerTracks.reset();
    h.state.setSharedTrackState(0x0005, 0);
    configureProjectTrackFixture(h.state, 1, 8);
    configureProjectTrackFixture(h.state, 2, 2);
    h.state.sequencerTracks.track(2).note[0] = 83;
    h.state.sequencerTracks.track(2).setEnabled(0, true);
    focusTrackNavigation(h);

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.trackNavigation.previewAddSlot.get());
    assert(h.state.trackNavigation.previewTrackIndex.get() == 1);

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(!h.state.trackNavigation.previewAddSlot.get());
    assert(h.state.sequencerTracks.activeTrackIndex() == 2);

    h.turn(Config::EncoderID::NAV, -1.0f);
    assert(h.state.trackNavigation.previewAddSlot.get());
    assert(h.state.trackNavigation.previewTrackIndex.get() == 1);

    h.press(Config::ButtonID::NAV);
    h.release(Config::ButtonID::NAV);

    assert(!h.state.trackNavigation.previewAddSlot.get());
    assert(h.state.sequencerTracks.isTrackEnabled(1));
    assert(h.state.sequencerTracks.activeTrackIndex() == 1);
    assert(h.state.projectTracks.authored.midiChannels[1] == 8);
    assert(h.state.sequencer.pattern.note[0] ==
           core::state::sequencer::SequencerState::DEFAULT_NOTE);
    assert(!h.state.sequencer.pattern.isEnabled(0));
    assert(h.state.sequencerTracks.track(2).note[0] == 83);
    assert(h.state.sequencerTracks.track(2).isEnabled(0));
    assert(h.navigationFocus.get() == core::state::StructureNavigationFocus::TRACK);
    assert(h.state.sequencer.page.get() == 0);
    assert(h.state.sequencer.focusedStep.get() == 0);
    assert(h.state.sequencer.pattern.length.get() == 8);

    std::cout << "[PASS] test_deleted_track_slot_can_be_recreated_at_any_gap\n";
}

void test_created_page_is_undoable_and_redoable() {
    SequencerStepHarness h;
    h.state.sequencer.pattern.setContentLength(8);
    h.state.sequencer.page.set(0);
    h.state.sequencer.focusedStep.set(0);

    openPatternEditor(h);
    h.tap(Config::ButtonID::BOTTOM_RIGHT);

    assert(h.state.sequencer.pattern.length.get() == 16);
    assert(h.state.sequencer.page.get() == 1);
    assert(h.state.sequencerHistory.undoCount() == 1);
    assert(h.state.sequencerHistory.undoCount(
               core::state::sequencer::SequencerHistoryScope::PatternOnly) == 1);
    assert(h.state.sequencerHistory.undoCount(
               core::state::sequencer::SequencerHistoryScope::FullBank) == 0);

    assert(h.state.undoSequencerHistory());
    assert(h.state.sequencer.pattern.length.get() == 8);
    assert(h.state.sequencer.page.get() == 0);
    assert(h.state.sequencer.structureUi.previewPageIndex.get() == 0);
    assert(h.state.sequencerHistory.redoCount() == 1);
    assert(std::strcmp(h.state.sequencer.historyFeedback.line1.data(), "UNDO T01") == 0);
    assert(std::strcmp(h.state.sequencer.historyFeedback.line2.data(), "Page Structure") == 0);
    assert(std::strcmp(h.state.sequencer.historyFeedback.line3.data(), "2 pages -> 1 page") == 0);

    assert(h.state.redoSequencerHistory());
    assert(h.state.sequencer.pattern.length.get() == 16);
    assert(h.state.sequencer.page.get() == 1);
    assert(h.state.sequencer.structureUi.previewPageIndex.get() == 1);
    assert(std::strcmp(h.state.sequencer.historyFeedback.line1.data(), "REDO T01") == 0);
    assert(std::strcmp(h.state.sequencer.historyFeedback.line3.data(), "1 page -> 2 pages") == 0);

    std::cout << "[PASS] test_created_page_is_undoable_and_redoable\n";
}

void test_page_clear_prepared_workflow_commits_nochange_and_oom_is_atomic() {
    {
        SequencerStepHarness h;
        auto& sequencer = h.state.sequencer;
        sequencer.pattern.setContentLength(16U);
        sequencer.pattern.note[8U] = 79U;
        sequencer.pattern.velocity[8U] = 111U;
        sequencer.pattern.setEnabled(8U, true);
        sequencer.page.set(1U);
        sequencer.focusedStep.set(13U);
        sequencer.structureUi.syncPreviewPage(1U);
        assert(seq::storeActiveTrack(h.state.sequencerTracks, sequencer));

        h.tap(Config::ButtonID::BOTTOM_LEFT);

        assert(sequencer.pattern.note[8U] == seq::SequencerState::DEFAULT_NOTE);
        assert(sequencer.pattern.velocity[8U] == seq::SequencerState::DEFAULT_VELOCITY);
        assert(!sequencer.pattern.isEnabled(8U));
        assert(sequencer.page.get() == 1U);
        assert(sequencer.focusedStep.get() == 8U);
        assert(sequencer.structureUi.pageHold.action.get() ==
               core::state::StructureHoldAction::NONE);
        assert(h.state.sequencerHistory.undoCount() == 1U);
        assert(h.state.sequencerHistory.undoCount(seq::SequencerHistoryScope::PatternOnly) == 1U);

        assert(h.state.undoSequencerHistory());
        assert(sequencer.pattern.note[8U] == 79U);
        assert(sequencer.pattern.velocity[8U] == 111U);
        assert(sequencer.pattern.isEnabled(8U));
        assert(sequencer.page.get() == 1U);
        assert(sequencer.focusedStep.get() == 13U);
        assert(h.state.redoSequencerHistory());
        assert(sequencer.pattern.note[8U] == seq::SequencerState::DEFAULT_NOTE);
        assert(!sequencer.pattern.isEnabled(8U));
        assert(sequencer.page.get() == 1U);
        assert(sequencer.focusedStep.get() == 8U);
    }

    {
        SequencerStepHarness h;
        auto& sequencer = h.state.sequencer;
        sequencer.pattern.setContentLength(16U);
        sequencer.page.set(1U);
        sequencer.focusedStep.set(13U);
        sequencer.structureUi.syncPreviewPage(1U);
        assert(seq::storeActiveTrack(h.state.sequencerTracks, sequencer));

        h.tap(Config::ButtonID::BOTTOM_LEFT);

        assert(sequencer.page.get() == 1U);
        assert(sequencer.focusedStep.get() == 8U);
        assert(sequencer.structureUi.pageHold.action.get() ==
               core::state::StructureHoldAction::NONE);
        assert(h.state.sequencerHistory.undoCount() == 0U);
        assert(!h.state.hasPendingSequencerPatternHistoryCoalescing());
    }

    {
        SequencerStepHarness h;
        auto& sequencer = h.state.sequencer;
        sequencer.pattern.setContentLength(16U);
        sequencer.pattern.note[8U] = 76U;
        sequencer.pattern.setEnabled(8U, true);
        sequencer.page.set(1U);
        sequencer.focusedStep.set(12U);
        sequencer.structureUi.syncPreviewPage(1U);
        assert(seq::storeActiveTrack(h.state.sequencerTracks, sequencer));

        h.press(Config::ButtonID::BOTTOM_LEFT);
        assert(sequencer.structureUi.pageHold.action.get() ==
               core::state::StructureHoldAction::REMOVE);
        {
            core::app::testing::ScopedExtmemAllocationFailure failure(1U);
            h.release(Config::ButtonID::BOTTOM_LEFT);
            assert(core::app::testing::extmemAllocationAttempt == 1U);
            assert(core::app::testing::extmemAllocationFailureOrdinal == 0U);
        }

        assert(sequencer.pattern.length.get() == 16U);
        assert(sequencer.pattern.note[8U] == 76U);
        assert(sequencer.pattern.isEnabled(8U));
        assert(sequencer.page.get() == 1U);
        assert(sequencer.focusedStep.get() == 12U);
        assert(sequencer.structureUi.previewPageIndex.get() == 1U);
        assert(sequencer.structureUi.pageHold.action.get() ==
               core::state::StructureHoldAction::REMOVE);
        assert(h.state.sequencerHistory.undoCount() == 0U);
        assert(!h.state.hasPendingSequencerPatternHistoryCoalescing());
    }

    std::cout << "[PASS] prepared PageClear commit/NoChange/OOM settlements\n";
}

void test_page_delete_prepared_workflow_shifts_cc_and_replays() {
    SequencerStepHarness h;
    auto& sequencer = h.state.sequencer;
    sequencer.pattern.setContentLength(24U);
    sequencer.pattern.note[0U] = 60U;
    sequencer.pattern.note[8U] = 70U;
    sequencer.pattern.note[16U] = 80U;
    sequencer.pattern.setEnabled(0U, true);
    sequencer.pattern.setEnabled(8U, true);
    sequencer.pattern.setEnabled(16U, true);

    auto* cc = seq::ensureSequencerCcLaneBank(sequencer.pattern);
    assert(cc != nullptr);
    seq::SequencerCcLaneDraft draft{};
    draft.destination.controller = 74U;
    assert(seq::createSequencerCcLane(*cc, 0U, draft).changed());
    assert(seq::setSequencerCcLaneEvent(*cc, 0U, 2U, 11U).changed());
    assert(seq::setSequencerCcLaneEvent(*cc, 0U, 9U, 22U).changed());
    assert(seq::setSequencerCcLaneEvent(*cc, 0U, 18U, 33U).changed());
    sequencer.pattern.bumpCcLaneRevision();
    sequencer.page.set(1U);
    sequencer.focusedStep.set(10U);
    sequencer.structureUi.syncPreviewPage(1U);
    assert(seq::storeActiveTrack(h.state.sequencerTracks, sequencer));

    h.press(Config::ButtonID::BOTTOM_LEFT);
    assert(sequencer.structureUi.pageHold.action.get() ==
           core::state::StructureHoldAction::REMOVE);
    h.advance(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);

    assert(sequencer.pattern.length.get() == 16U);
    assert(sequencer.pattern.note[0U] == 60U);
    assert(sequencer.pattern.note[8U] == 80U);
    assert(sequencer.pattern.isEnabled(0U));
    assert(sequencer.pattern.isEnabled(8U));
    assert(sequencer.page.get() == 1U);
    assert(sequencer.focusedStep.get() == 8U);
    assert(sequencer.structureUi.previewPageIndex.get() == 1U);
    assert(sequencer.structureUi.pageHold.action.get() ==
           core::state::StructureHoldAction::NONE);
    assert(sequencer.pattern.ccLanes != nullptr);
    assert(sequencer.pattern.ccLanes->lanes[0U].activeMask.test(2U));
    assert(sequencer.pattern.ccLanes->lanes[0U].values[2U] == 11U);
    assert(!sequencer.pattern.ccLanes->lanes[0U].activeMask.test(9U));
    assert(sequencer.pattern.ccLanes->lanes[0U].activeMask.test(10U));
    assert(sequencer.pattern.ccLanes->lanes[0U].values[10U] == 33U);
    assert(h.state.sequencerHistory.undoCount() == 1U);
    assert(h.state.sequencerHistory.undoCount(seq::SequencerHistoryScope::PatternOnly) == 1U);
    h.release(Config::ButtonID::BOTTOM_LEFT);

    assert(h.state.undoSequencerHistory());
    assert(sequencer.pattern.length.get() == 24U);
    assert(sequencer.pattern.note[8U] == 70U);
    assert(sequencer.pattern.note[16U] == 80U);
    assert(sequencer.pattern.ccLanes->lanes[0U].activeMask.test(2U));
    assert(sequencer.pattern.ccLanes->lanes[0U].activeMask.test(9U));
    assert(sequencer.pattern.ccLanes->lanes[0U].values[9U] == 22U);
    assert(sequencer.pattern.ccLanes->lanes[0U].activeMask.test(18U));
    assert(sequencer.pattern.ccLanes->lanes[0U].values[18U] == 33U);
    assert(sequencer.page.get() == 1U);
    assert(sequencer.focusedStep.get() == 10U);

    assert(h.state.redoSequencerHistory());
    assert(sequencer.pattern.length.get() == 16U);
    assert(sequencer.pattern.note[8U] == 80U);
    assert(!sequencer.pattern.ccLanes->lanes[0U].activeMask.test(9U));
    assert(sequencer.pattern.ccLanes->lanes[0U].activeMask.test(10U));
    assert(sequencer.pattern.ccLanes->lanes[0U].values[10U] == 33U);
    assert(sequencer.page.get() == 1U);
    assert(sequencer.focusedStep.get() == 8U);

    std::cout << "[PASS] prepared PageDelete shifts CC and replays exactly\n";
}

void test_page_delete_oom_keeps_hold_until_latched_release() {
    SequencerStepHarness h;
    auto& sequencer = h.state.sequencer;
    sequencer.pattern.setContentLength(16U);
    sequencer.pattern.note[0U] = 70U;
    sequencer.pattern.note[8U] = 80U;
    sequencer.pattern.setEnabled(0U, true);
    sequencer.pattern.setEnabled(8U, true);
    sequencer.page.set(0U);
    sequencer.focusedStep.set(3U);
    sequencer.structureUi.syncPreviewPage(0U);
    assert(seq::storeActiveTrack(h.state.sequencerTracks, sequencer));

    h.press(Config::ButtonID::BOTTOM_LEFT);
    assert(sequencer.structureUi.pageHold.action.get() ==
           core::state::StructureHoldAction::REMOVE);
    {
        core::app::testing::ScopedExtmemAllocationFailure failure(1U);
        h.advance(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
        assert(core::app::testing::extmemAllocationAttempt == 1U);
        assert(core::app::testing::extmemAllocationFailureOrdinal == 0U);
    }

    assert(sequencer.pattern.length.get() == 16U);
    assert(sequencer.pattern.note[0U] == 70U);
    assert(sequencer.pattern.note[8U] == 80U);
    assert(sequencer.page.get() == 0U);
    assert(sequencer.focusedStep.get() == 3U);
    assert(sequencer.structureUi.pageHold.action.get() ==
           core::state::StructureHoldAction::REMOVE);
    assert(h.state.sequencerHistory.undoCount() == 0U);
    assert(!h.state.hasPendingSequencerPatternHistoryCoalescing());

    h.release(Config::ButtonID::BOTTOM_LEFT);
    assert(sequencer.structureUi.pageHold.action.get() ==
           core::state::StructureHoldAction::NONE);
    assert(sequencer.pattern.length.get() == 16U);
    assert(sequencer.pattern.note[0U] == 70U);
    assert(sequencer.pattern.note[8U] == 80U);
    assert(h.state.sequencerHistory.undoCount() == 0U);

    std::cout << "[PASS] PageDelete OOM hold clears only on latched release\n";
}

void test_page_delete_single_page_nochange_preserves_ui_until_release() {
    SequencerStepHarness h;
    auto& sequencer = h.state.sequencer;
    sequencer.pattern.setContentLength(8U);
    sequencer.pattern.note[0U] = 69U;
    sequencer.pattern.setEnabled(0U, true);
    sequencer.page.set(0U);
    sequencer.focusedStep.set(3U);
    sequencer.structureUi.syncPreviewPage(0U);
    assert(seq::storeActiveTrack(h.state.sequencerTracks, sequencer));

    core::handler::SequencerStructureEditWorkflow workflow({
        sequencer,
        h.state.sequencerTracks,
        h.navigationFocus,
        h.state.trackNavigation,
        h.state.projectNavigation,
        h.state.projectTracks,
        core::state::project::ProjectTrackDomainServices::fromCoreState(h.state),
        h.state.structureClipboard,
        core::handler::SharedTrackDomainServices::fromCoreState(h.state),
        HistoryServices::fromCoreState(h.state),
        h.state.pages,
        &h.state.sequencerTrackActivations,
        &h.state.statusBar,
    });
    workflow.beginHoldAction(core::state::StructureHoldAction::REMOVE);
    assert(sequencer.structureUi.pageHold.action.get() ==
           core::state::StructureHoldAction::REMOVE);
    workflow.applyCurrentStructureLongPress();

    assert(sequencer.pattern.length.get() == 8U);
    assert(sequencer.pattern.note[0U] == 69U);
    assert(sequencer.pattern.isEnabled(0U));
    assert(sequencer.page.get() == 0U);
    assert(sequencer.focusedStep.get() == 3U);
    assert(sequencer.structureUi.previewPageIndex.get() == 0U);
    assert(sequencer.structureUi.pageHold.action.get() ==
           core::state::StructureHoldAction::REMOVE);
    assert(h.state.sequencerHistory.undoCount() == 0U);
    assert(!h.state.hasPendingSequencerPatternHistoryCoalescing());

    workflow.clearHoldAction();
    assert(sequencer.structureUi.pageHold.action.get() ==
           core::state::StructureHoldAction::NONE);
    assert(sequencer.pattern.length.get() == 8U);
    assert(h.state.sequencerHistory.undoCount() == 0U);

    std::cout << "[PASS] single-Page delete NoChange settles only on release\n";
}

void test_page_clear_and_delete_failed_commits_restore_editor_state() {
    {
        SequencerStepHarness h;
        auto& sequencer = h.state.sequencer;
        sequencer.pattern.setContentLength(16U);
        sequencer.pattern.note[8U] = 77U;
        sequencer.pattern.velocity[8U] = 109U;
        sequencer.pattern.setEnabled(8U, true);
        auto* cc = seq::ensureSequencerCcLaneBank(sequencer.pattern);
        assert(cc != nullptr);
        seq::SequencerCcLaneDraft draft{};
        draft.destination.controller = 74U;
        assert(seq::createSequencerCcLane(*cc, 0U, draft).changed());
        assert(seq::setSequencerCcLaneEvent(*cc, 0U, 9U, 64U).changed());
        sequencer.pattern.bumpCcLaneRevision();
        sequencer.page.set(1U);
        sequencer.focusedStep.set(12U);
        sequencer.structureUi.syncPreviewPage(1U);
        assert(seq::storeActiveTrack(h.state.sequencerTracks, sequencer));

        FailingPageCommitHistory failing{.state = &h.state};
        const auto history = HistoryServices::fromStaticOperations<
            kFailingPageCommitHistoryOperations>(&failing);
        core::handler::SequencerStructureEditWorkflow workflow({
            sequencer,
            h.state.sequencerTracks,
            h.navigationFocus,
            h.state.trackNavigation,
            h.state.projectNavigation,
            h.state.projectTracks,
            core::state::project::ProjectTrackDomainServices::fromCoreState(h.state),
            h.state.structureClipboard,
            core::handler::SharedTrackDomainServices::fromCoreState(h.state),
            history,
            h.state.pages,
            &h.state.sequencerTrackActivations,
            &h.state.statusBar,
        });
        workflow.beginHoldAction(core::state::StructureHoldAction::REMOVE);
        const auto holdStartedAt = sequencer.structureUi.pageHold.startedAtMs.get();
        const auto graphRevision = sequencer.pattern.graphRevision.get();
        const auto ccRevision = sequencer.pattern.ccLaneRevision.get();
        const auto contentViewRevision = sequencer.contentView.revision.get();

        workflow.applyCurrentStructureShortPress();
        test_support::drainNotifications();

        assert(failing.commitCount == 1U);
        assert(failing.abortCount == 1U);
        assert(sequencer.pattern.length.get() == 16U);
        assert(sequencer.pattern.note[8U] == 77U);
        assert(sequencer.pattern.velocity[8U] == 109U);
        assert(sequencer.pattern.isEnabled(8U));
        assert(sequencer.pattern.graph == nullptr);
        assert(sequencer.pattern.graphRevision.get() == graphRevision);
        assert(sequencer.pattern.ccLanes != nullptr);
        assert(sequencer.pattern.ccLanes->lanes[0U].activeMask.test(9U));
        assert(sequencer.pattern.ccLanes->lanes[0U].values[9U] == 64U);
        assert(sequencer.pattern.ccLaneRevision.get() == ccRevision);
        assert(h.state.sequencerTracks.track(0U).note[8U] == 77U);
        assert(sequencer.page.get() == 1U);
        assert(sequencer.focusedStep.get() == 12U);
        assert(sequencer.structureUi.previewPageIndex.get() == 1U);
        assert(sequencer.structureUi.pageHold.action.get() ==
               core::state::StructureHoldAction::REMOVE);
        assert(sequencer.structureUi.pageHold.startedAtMs.get() == holdStartedAt);
        assert(!sequencer.structureUi.pageSelection.active.get());
        assert(!sequencer.structureUi.stepSelection.active.get());
        assert(sequencer.contentView.stackDepth == 0U);
        assert(sequencer.contentView.kind.get() ==
               seq::SequencerContentViewKind::ROOT);
        assert(sequencer.contentView.revision.get() == contentViewRevision);
        assert(h.state.sequencerHistory.undoCount() == 0U);
        assert(!h.state.hasPendingSequencerPatternHistoryCoalescing());
    }

    {
        SequencerStepHarness h;
        auto& sequencer = h.state.sequencer;
        sequencer.pattern.setContentLength(24U);
        sequencer.pattern.note[0U] = 61U;
        sequencer.pattern.note[8U] = 73U;
        sequencer.pattern.note[16U] = 85U;
        sequencer.pattern.setEnabled(0U, true);
        sequencer.pattern.setEnabled(8U, true);
        sequencer.pattern.setEnabled(16U, true);
        auto* cc = seq::ensureSequencerCcLaneBank(sequencer.pattern);
        assert(cc != nullptr);
        seq::SequencerCcLaneDraft draft{};
        draft.destination.controller = 71U;
        assert(seq::createSequencerCcLane(*cc, 0U, draft).changed());
        assert(seq::setSequencerCcLaneEvent(*cc, 0U, 2U, 11U).changed());
        assert(seq::setSequencerCcLaneEvent(*cc, 0U, 9U, 22U).changed());
        assert(seq::setSequencerCcLaneEvent(*cc, 0U, 18U, 33U).changed());
        sequencer.pattern.bumpCcLaneRevision();
        sequencer.page.set(1U);
        sequencer.focusedStep.set(10U);
        sequencer.structureUi.syncPreviewPage(1U);
        assert(seq::storeActiveTrack(h.state.sequencerTracks, sequencer));

        FailingPageCommitHistory failing{.state = &h.state};
        const auto history = HistoryServices::fromStaticOperations<
            kFailingPageCommitHistoryOperations>(&failing);
        core::handler::SequencerStructureEditWorkflow workflow({
            sequencer,
            h.state.sequencerTracks,
            h.navigationFocus,
            h.state.trackNavigation,
            h.state.projectNavigation,
            h.state.projectTracks,
            core::state::project::ProjectTrackDomainServices::fromCoreState(h.state),
            h.state.structureClipboard,
            core::handler::SharedTrackDomainServices::fromCoreState(h.state),
            history,
            h.state.pages,
            &h.state.sequencerTrackActivations,
            &h.state.statusBar,
        });
        workflow.beginHoldAction(core::state::StructureHoldAction::REMOVE);
        const auto holdStartedAt = sequencer.structureUi.pageHold.startedAtMs.get();
        const auto graphRevision = sequencer.pattern.graphRevision.get();
        const auto ccRevision = sequencer.pattern.ccLaneRevision.get();
        const auto contentViewRevision = sequencer.contentView.revision.get();

        workflow.applyCurrentStructureLongPress();
        test_support::drainNotifications();

        assert(failing.commitCount == 1U);
        assert(failing.abortCount == 1U);
        assert(sequencer.pattern.length.get() == 24U);
        assert(sequencer.pattern.note[0U] == 61U);
        assert(sequencer.pattern.note[8U] == 73U);
        assert(sequencer.pattern.note[16U] == 85U);
        assert(sequencer.pattern.isEnabled(0U));
        assert(sequencer.pattern.isEnabled(8U));
        assert(sequencer.pattern.isEnabled(16U));
        assert(sequencer.pattern.graph == nullptr);
        assert(sequencer.pattern.graphRevision.get() == graphRevision);
        assert(sequencer.pattern.ccLanes != nullptr);
        assert(sequencer.pattern.ccLanes->lanes[0U].activeMask.test(2U));
        assert(sequencer.pattern.ccLanes->lanes[0U].activeMask.test(9U));
        assert(sequencer.pattern.ccLanes->lanes[0U].activeMask.test(18U));
        assert(sequencer.pattern.ccLanes->lanes[0U].values[9U] == 22U);
        assert(sequencer.pattern.ccLaneRevision.get() == ccRevision);
        assert(h.state.sequencerTracks.track(0U).length.get() == 24U);
        assert(h.state.sequencerTracks.track(0U).note[8U] == 73U);
        assert(sequencer.page.get() == 1U);
        assert(sequencer.focusedStep.get() == 10U);
        assert(sequencer.structureUi.previewPageIndex.get() == 1U);
        assert(sequencer.structureUi.pageHold.action.get() ==
               core::state::StructureHoldAction::REMOVE);
        assert(sequencer.structureUi.pageHold.startedAtMs.get() == holdStartedAt);
        assert(!sequencer.structureUi.pageSelection.active.get());
        assert(!sequencer.structureUi.stepSelection.active.get());
        assert(sequencer.contentView.stackDepth == 0U);
        assert(sequencer.contentView.kind.get() ==
               seq::SequencerContentViewKind::ROOT);
        assert(sequencer.contentView.revision.get() == contentViewRevision);
        assert(h.state.sequencerHistory.undoCount() == 0U);
        assert(!h.state.hasPendingSequencerPatternHistoryCoalescing());
    }

    std::cout << "[PASS] failed PageClear/PageDelete commits restore editor state\n";
}

void test_direct_track_create_remove_are_atomic_and_replay_exactly() {
    using Status = core::handler::SequencerPreparedTrackStructureStatus;

    {
        SequencerStepHarness h;
        const auto fixture = configureDirectTrackFixture(
            h, DirectTrackFixtureKind::Create);
        const auto beforeLogical = captureCanonicalTrackLogicalProof(h);
        const auto beforePublication = tx::captureStateInvariant(h.state);
        const auto beforeMacros = captureTrackMacroInvariant(h);

        assert(fixture.editor.graph != fixture.scratch.graph);
        assert(fixture.editor.graph != fixture.incoming.graph);
        assert(fixture.scratch.graph != fixture.incoming.graph);
        assert(fixture.editor.cc != fixture.scratch.cc);
        assert(fixture.editor.cc != fixture.incoming.cc);
        assert(fixture.scratch.cc != fixture.incoming.cc);

        const auto result =
            core::handler::executeSequencerCreateTrackStructure(
                directTrackRefs(h));
        assert(result.status == Status::Committed);
        assert(result.committed());
        assert(result.settled());
        test_support::drainNotifications();

        assert(h.state.sequencerTracks.currentEnabledMask() == 0x0065U);
        assert(h.state.sharedTrackEnabledMask.get() == 0x0065U);
        assert(h.state.sequencerTracks.activeTrackIndex() ==
               kDirectTrackCreateTarget);
        assert(h.state.sharedTrackActive.get() == kDirectTrackCreateTarget);
        assert(h.state.sequencer.focusedStep.get() == 7U);
        assert(h.state.sequencer.page.get() == 0U);
        assert(!h.state.trackNavigation.previewAddSlot.get());
        assert(h.state.trackNavigation.previewTrackIndex.get() ==
               kDirectTrackCreateTarget);
        assert(h.state.sequencer.structureUi.previewPageIndex.get() == 0U);

        assert(trackColdOwners(
                   h.state.sequencerTracks.track(kDirectTrackOldActive)).graph ==
               fixture.editor.graph);
        assert(trackColdOwners(
                   h.state.sequencerTracks.track(kDirectTrackOldActive)).cc ==
               fixture.editor.cc);
        assert(h.state.sequencer.pattern.graph == nullptr);
        assert(h.state.sequencer.pattern.ccLanes == nullptr);
        assert(trackColdOwners(
                   h.state.sequencerTracks.track(kDirectTrackCreateTarget)).graph ==
               fixture.scratch.graph);
        assert(trackColdOwners(
                   h.state.sequencerTracks.track(kDirectTrackCreateTarget)).cc ==
               fixture.scratch.cc);

        assert(!h.state.sequencer.contextSelector.visible);
        assert(h.state.sequencer.contextSelector.previewFocus ==
               core::state::StructureNavigationFocus::PAGE);
        assert(h.state.sequencer.contextSelector.revision.get() ==
               fixture.selectorRevision + 1U);
        assert(!h.state.sequencer.patternQuickControls.selecting.get());
        assert(!h.state.sequencer.stepInlineFeedback.visible.get());

        assertActiveMacroPresentation(h);
        const auto afterMacros = captureTrackMacroInvariant(h);
        assert(afterMacros.activeTrack == kDirectTrackCreateTarget);
        assert(afterMacros.activePage == 3U);
        assert(afterMacros.manualRevision == beforeMacros.manualRevision);
        assert(afterMacros.rejectedActivationCount ==
               beforeMacros.rejectedActivationCount);
        assert(afterMacros.controlAuthoredRevision ==
               beforeMacros.controlAuthoredRevision);
        assert(afterMacros.configRevision == beforeMacros.configRevision);
        assert(afterMacros.automationEditRevision ==
               beforeMacros.automationEditRevision);
        assert(afterMacros.runtimeProjectionRevision ==
               beforeMacros.runtimeProjectionRevision);
        assert(afterMacros.runtimeOwnerRevision ==
               beforeMacros.runtimeOwnerRevision);
        assert(afterMacros.contextSelectorHash ==
               beforeMacros.contextSelectorHash);
        assertSingleTrackStructurePublication(h, beforePublication);

        const auto afterLogical = captureCanonicalTrackLogicalProof(h);
        assert(h.state.undoSequencerHistory());
        test_support::drainNotifications();
        assertCanonicalTrackLogicalProof(h, beforeLogical);
        assertActiveMacroPresentation(h);
        assert(h.state.redoSequencerHistory());
        test_support::drainNotifications();
        assertCanonicalTrackLogicalProof(h, afterLogical);
        assertActiveMacroPresentation(h);
    }

    {
        SequencerStepHarness h;
        const auto fixture = configureDirectTrackFixture(
            h, DirectTrackFixtureKind::RemoveCurrent);
        const auto beforeLogical = captureCanonicalTrackLogicalProof(h);
        const auto beforePublication = tx::captureStateInvariant(h.state);
        const auto beforeMacros = captureTrackMacroInvariant(h);
        auto workflow = makeStructureEditWorkflow(
            h, HistoryServices::fromCoreState(h.state));
        workflow.beginHoldAction(core::state::StructureHoldAction::REMOVE);
        assert(h.state.trackNavigation.hold.action.get() ==
               core::state::StructureHoldAction::REMOVE);

        workflow.applyCurrentStructureLongPress();
        test_support::drainNotifications();

        assert(h.state.sequencerTracks.currentEnabledMask() == 0x0021U);
        assert(h.state.sharedTrackEnabledMask.get() == 0x0021U);
        assert(h.state.sequencerTracks.activeTrackIndex() ==
               kDirectTrackIncoming);
        assert(h.state.sharedTrackActive.get() == kDirectTrackIncoming);
        assert(h.state.sequencer.focusedStep.get() == 4U);
        assert(h.state.sequencer.page.get() == 0U);
        assert(h.state.trackNavigation.hold.action.get() ==
               core::state::StructureHoldAction::NONE);
        assert(h.state.trackNavigation.hold.startedAtMs.get() == 0U);
        assert(!h.state.trackNavigation.previewAddSlot.get());
        assert(h.state.trackNavigation.previewTrackIndex.get() ==
               kDirectTrackIncoming);
        assert(h.state.sequencer.structureUi.previewPageIndex.get() == 0U);

        assert(trackColdOwners(
                   h.state.sequencerTracks.track(kDirectTrackOldActive)).graph ==
               fixture.editor.graph);
        assert(trackColdOwners(
                   h.state.sequencerTracks.track(kDirectTrackOldActive)).cc ==
               fixture.editor.cc);
        assert(trackColdOwners(h.state.sequencer.pattern).graph ==
               fixture.incoming.graph);
        assert(trackColdOwners(h.state.sequencer.pattern).cc ==
               fixture.incoming.cc);
        assert(trackColdOwners(
                   h.state.sequencerTracks.track(kDirectTrackIncoming)).graph ==
               fixture.scratch.graph);
        assert(trackColdOwners(
                   h.state.sequencerTracks.track(kDirectTrackIncoming)).cc ==
               fixture.scratch.cc);

        assert(!h.state.sequencer.contextSelector.visible);
        assert(h.state.sequencer.contextSelector.revision.get() ==
               fixture.selectorRevision + 1U);
        assert(!h.state.sequencer.patternQuickControls.selecting.get());
        assert(!h.state.sequencer.stepInlineFeedback.visible.get());

        assertActiveMacroPresentation(h);
        const auto afterMacros = captureTrackMacroInvariant(h);
        assert(afterMacros.activeTrack == kDirectTrackIncoming);
        assert(afterMacros.activePage == 2U);
        assert(afterMacros.manualRevision == beforeMacros.manualRevision);
        assert(afterMacros.rejectedActivationCount ==
               beforeMacros.rejectedActivationCount);
        assert(afterMacros.controlAuthoredRevision ==
               beforeMacros.controlAuthoredRevision);
        assert(afterMacros.configRevision == beforeMacros.configRevision);
        assert(afterMacros.automationEditRevision ==
               beforeMacros.automationEditRevision);
        assert(afterMacros.runtimeProjectionRevision ==
               beforeMacros.runtimeProjectionRevision);
        assert(afterMacros.runtimeOwnerRevision ==
               beforeMacros.runtimeOwnerRevision);
        assert(afterMacros.contextSelectorHash ==
               beforeMacros.contextSelectorHash);
        assertSingleTrackStructurePublication(h, beforePublication);

        const auto afterLogical = captureCanonicalTrackLogicalProof(h);
        assert(h.state.undoSequencerHistory());
        test_support::drainNotifications();
        assertCanonicalTrackLogicalProof(h, beforeLogical);
        assertActiveMacroPresentation(h);
        assert(h.state.redoSequencerHistory());
        test_support::drainNotifications();
        assertCanonicalTrackLogicalProof(h, afterLogical);
        assertActiveMacroPresentation(h);
    }

    std::cout
        << "[PASS] direct Track Create/Remove are atomic and replay exactly\n";
}

void test_direct_track_global_history_and_redo_branch_are_exact() {
    using ActionKind = seq::SequencerHistoryActionKind;
    using Domain = core::state::project::ProjectHistoryDomain;
    using Scope = seq::SequencerHistoryScope;
    using Status = core::handler::SequencerPreparedTrackStructureStatus;

    for (const auto kind : std::array<DirectTrackFixtureKind, 2U>{
             DirectTrackFixtureKind::Create,
             DirectTrackFixtureKind::RemoveCurrent,
         }) {
        SequencerStepHarness h;
        configureDirectTrackFixture(h, kind);
        const auto beforeLogical = captureCanonicalTrackLogicalProof(h);

        const auto commit = [&]() {
            auto refs = directTrackRefs(h);
            return kind == DirectTrackFixtureKind::Create
                ? core::handler::executeSequencerCreateTrackStructure(refs)
                : core::handler::executeSequencerRemoveCurrentTrackStructure(
                      refs,
                      kDirectTrackOldActive
                  );
        };
        const auto assertUndoDescriptor = [&]() {
            const auto* entry = h.state.projectHistory.peekUndo();
            assert(entry != nullptr);
            assert(entry->domain == Domain::Sequencer);
            assert(entry->actionKind ==
                   static_cast<uint8_t>(ActionKind::TrackStructure));
            assert(entry->identity ==
                   h.state.sequencerHistory.projectHistoryUndoIdentity());
        };
        const auto assertRedoDescriptor = [&]() {
            const auto* entry = h.state.projectHistory.peekRedo();
            assert(entry != nullptr);
            assert(entry->domain == Domain::Sequencer);
            assert(entry->actionKind ==
                   static_cast<uint8_t>(ActionKind::TrackStructure));
            assert(entry->identity ==
                   h.state.sequencerHistory.projectHistoryRedoIdentity());
        };

        const auto first = commit();
        assert(first.status == Status::Committed);
        test_support::drainNotifications();
        const auto afterLogical = captureCanonicalTrackLogicalProof(h);
        assert(h.state.sequencerHistory.undoCount() == 1U);
        assert(h.state.sequencerHistory.redoCount() == 0U);
        assert(h.state.sequencerHistory.undoCount(Scope::Structure) == 1U);
        assert(h.state.projectHistory.undoCount() == 1U);
        assert(h.state.projectHistory.redoCount() == 0U);
        assertUndoDescriptor();

        assert(h.state.undoProjectHistory());
        test_support::drainNotifications();
        assertCanonicalTrackLogicalProof(h, beforeLogical);
        assertActiveMacroPresentation(h);
        assert(h.state.sequencerHistory.undoCount() == 0U);
        assert(h.state.sequencerHistory.redoCount() == 1U);
        assert(h.state.sequencerHistory.redoCount(Scope::Structure) == 1U);
        assert(h.state.projectHistory.undoCount() == 0U);
        assert(h.state.projectHistory.redoCount() == 1U);
        assertRedoDescriptor();

        if (kind == DirectTrackFixtureKind::Create) {
            h.navigationFocus.set(
                core::state::StructureNavigationFocus::TRACK
            );
            h.state.trackNavigation.previewAddSlot.set(true);
            h.state.trackNavigation.previewTrackIndex.set(
                kDirectTrackCreateTarget
            );
            test_support::drainNotifications();
        }
        const auto second = commit();
        assert(second.status == Status::Committed);
        test_support::drainNotifications();
        assertCanonicalTrackLogicalProof(h, afterLogical);
        assertActiveMacroPresentation(h);
        assert(h.state.sequencerHistory.undoCount() == 1U);
        assert(h.state.sequencerHistory.redoCount() == 0U);
        assert(h.state.sequencerHistory.undoCount(Scope::Structure) == 1U);
        assert(h.state.sequencerHistory.redoCount(Scope::Structure) == 0U);
        assert(h.state.projectHistory.undoCount() == 1U);
        assert(h.state.projectHistory.redoCount() == 0U);
        assert(h.state.projectHistory.peekRedo() == nullptr);
        assert(h.state.macroHistory.redoCount() == 0U);
        assert(h.state.projectTrackHistory.redoCount() == 0U);
        assert(h.state.projectSettingsHistory.projectHistoryRedoIdentity() ==
               0U);
        assertUndoDescriptor();
        assert(!h.state.redoProjectHistory());

        assert(h.state.undoProjectHistory());
        test_support::drainNotifications();
        assertCanonicalTrackLogicalProof(h, beforeLogical);
        assertActiveMacroPresentation(h);
        assertRedoDescriptor();
        assert(h.state.redoProjectHistory());
        test_support::drainNotifications();
        assertCanonicalTrackLogicalProof(h, afterLogical);
        assertActiveMacroPresentation(h);
        assertUndoDescriptor();
    }

    std::cout
        << "[PASS] direct Track global history and redo truncation are exact\n";
}

void test_direct_track_fail_nth_failures_restore_exact_state() {
    using Status = core::handler::SequencerPreparedTrackStructureStatus;

    for (std::size_t ordinal = 1U; ordinal <= 7U; ++ordinal) {
        SequencerStepHarness h;
        configureDirectTrackFixture(h, DirectTrackFixtureKind::Create);
        const auto expected = captureTrackTransactionInvariant(h);
        {
            core::app::testing::ScopedExtmemAllocationFailure failure(ordinal);
            const auto result =
                core::handler::executeSequencerCreateTrackStructure(
                    directTrackRefs(h));
            assert(result.status == Status::AllocationUnavailable);
            tx::assertFailureConsumed(ordinal);
        }
        test_support::drainNotifications();
        assertTrackTransactionInvariant(h, expected);
        assert(h.state.trackNavigation.previewAddSlot.get());
        assert(h.state.trackNavigation.previewTrackIndex.get() ==
               kDirectTrackCreateTarget);
        tx::assertFailureInjectionReset();
    }

    {
        SequencerStepHarness h;
        configureDirectTrackFixture(h, DirectTrackFixtureKind::Create);
        {
            core::app::testing::ScopedExtmemAllocationFailure failure(8U);
            const auto result =
                core::handler::executeSequencerCreateTrackStructure(
                    directTrackRefs(h));
            assert(result.status == Status::Committed);
            tx::assertMaxPlusOneStillArmed(7U);
        }
        assert(h.state.sequencerTracks.currentEnabledMask() == 0x0065U);
        tx::assertFailureInjectionReset();
    }

    for (std::size_t ordinal = 1U; ordinal <= 9U; ++ordinal) {
        SequencerStepHarness h;
        configureDirectTrackFixture(
            h, DirectTrackFixtureKind::RemoveCurrent);
        auto workflow = makeStructureEditWorkflow(
            h, HistoryServices::fromCoreState(h.state));
        workflow.beginHoldAction(core::state::StructureHoldAction::REMOVE);
        auto expected = captureTrackTransactionInvariant(h);
        expected.trackUi.holdAction = static_cast<uint8_t>(
            core::state::StructureHoldAction::NONE);
        expected.trackUi.holdStartedAtMs = 0U;
        {
            core::app::testing::ScopedExtmemAllocationFailure failure(ordinal);
            workflow.applyCurrentStructureLongPress();
            tx::assertFailureConsumed(ordinal);
        }
        test_support::drainNotifications();
        assertTrackTransactionInvariant(h, expected);
        assert(!h.state.trackNavigation.previewAddSlot.get());
        assert(h.state.trackNavigation.previewTrackIndex.get() ==
               kDirectTrackOldActive);
        tx::assertFailureInjectionReset();
    }

    {
        SequencerStepHarness h;
        configureDirectTrackFixture(
            h, DirectTrackFixtureKind::RemoveCurrent);
        auto workflow = makeStructureEditWorkflow(
            h, HistoryServices::fromCoreState(h.state));
        workflow.beginHoldAction(core::state::StructureHoldAction::REMOVE);
        {
            core::app::testing::ScopedExtmemAllocationFailure failure(10U);
            workflow.applyCurrentStructureLongPress();
            tx::assertMaxPlusOneStillArmed(9U);
        }
        assert(h.state.sequencerTracks.currentEnabledMask() == 0x0021U);
        assert(h.state.trackNavigation.hold.action.get() ==
               core::state::StructureHoldAction::NONE);
        tx::assertFailureInjectionReset();
    }

    std::cout
        << "[PASS] direct Track Create/Remove fail-Nth matrices are exact\n";
}

void test_direct_track_activation_collisions_are_atomic() {
    using Status = core::handler::SequencerPreparedTrackStructureStatus;
    using ChronologyStatus =
        seq::SequencerTrackStructureChronologyStatus;

    for (const bool playing : std::array<bool, 2U>{false, true}) {
        for (const uint16_t collision : {
                 static_cast<uint16_t>(1U << kDirectTrackOldActive),
                 static_cast<uint16_t>(1U << kDirectTrackCreateTarget),
             }) {
            SequencerStepHarness h;
            configureDirectTrackFixture(h, DirectTrackFixtureKind::Create);
            h.state.statusBar.playing.set(playing);
            armTrackActivation(
                h.state.sequencerTrackActivations,
                collision,
                playing
            );
            const auto activationBefore = captureActivationQueueGuard(
                h.state.sequencerTrackActivations,
                collision
            );
            const auto expected = captureTrackTransactionInvariant(h);
            {
                core::app::testing::ScopedExtmemAllocationFailure failure(1U);
                const auto result =
                    core::handler::executeSequencerCreateTrackStructure(
                        directTrackRefs(h));
                assert(result.status == Status::Stale);
                assert(result.chronology.status == ChronologyStatus::Opened);
                tx::assertMaxPlusOneStillArmed(0U);
            }
            test_support::drainNotifications();
            assertTrackTransactionInvariant(h, expected);
            const auto activationAfter = captureActivationQueueGuard(
                h.state.sequencerTrackActivations,
                collision
            );
            assertSameActivationQueueGuard(
                activationAfter,
                activationBefore
            );
            assert(h.state.sequencerTrackActivations.mutationGuardMatches(
                activationBefore
            ));
            tx::assertFailureInjectionReset();
        }
    }

    for (const bool playing : std::array<bool, 2U>{false, true}) {
        for (const uint16_t collision : {
                 static_cast<uint16_t>(1U << kDirectTrackOldActive),
                 static_cast<uint16_t>(1U << kDirectTrackIncoming),
             }) {
            SequencerStepHarness h;
            configureDirectTrackFixture(
                h, DirectTrackFixtureKind::RemoveCurrent);
            h.state.statusBar.playing.set(playing);
            armTrackActivation(
                h.state.sequencerTrackActivations,
                collision,
                playing
            );
            const auto activationBefore = captureActivationQueueGuard(
                h.state.sequencerTrackActivations,
                collision
            );
            const auto expected = captureTrackTransactionInvariant(h);
            {
                core::app::testing::ScopedExtmemAllocationFailure failure(1U);
                const auto result =
                    core::handler::executeSequencerRemoveCurrentTrackStructure(
                        directTrackRefs(h),
                        kDirectTrackOldActive);
                assert(result.status == Status::Stale);
                assert(result.chronology.status == ChronologyStatus::Opened);
                tx::assertMaxPlusOneStillArmed(0U);
            }
            test_support::drainNotifications();
            assertTrackTransactionInvariant(h, expected);
            const auto activationAfter = captureActivationQueueGuard(
                h.state.sequencerTrackActivations,
                collision
            );
            assertSameActivationQueueGuard(
                activationAfter,
                activationBefore
            );
            assert(h.state.sequencerTrackActivations.mutationGuardMatches(
                activationBefore
            ));
            tx::assertFailureInjectionReset();
        }
    }

    std::cout
        << "[PASS] direct Track activation collisions are atomic\n";
}

void test_direct_track_pattern_chronology_is_single_and_ordered() {
    {
        SequencerStepHarness h;
        configureDirectTrackFixture(h, DirectTrackFixtureKind::Create);
        const uint8_t noteBefore = h.state.sequencer.pattern.note[0U];
        preparePendingTrackPatternEdit(h.state);
        const uint8_t editedNote = h.state.sequencer.pattern.note[0U];
        const uint8_t sequencerUndoBefore = h.state.sequencerHistory.undoCount();
        const uint8_t projectUndoBefore = h.state.projectHistory.undoCount();
        const uint32_t modifiedBefore = h.state.project.metadata.modifiedCounter;

        const auto result =
            core::handler::executeSequencerCreateTrackStructure(
                directTrackRefs(h));
        assert(result.committed());
        assert(result.chronology.predecessorPattern ==
               seq::SequencerPatternHistoryCommitOutcome::Committed);
        assert(!h.state.hasPendingSequencerPatternHistoryCoalescing());
        assert(h.state.sequencerHistory.undoCount() ==
               sequencerUndoBefore + 2U);
        assert(h.state.projectHistory.undoCount() == projectUndoBefore + 2U);
        assert(h.state.sequencerHistory.undoCount(
                   seq::SequencerHistoryScope::PatternOnly) == 1U);
        assert(h.state.sequencerHistory.undoCount(
                   seq::SequencerHistoryScope::Structure) == 1U);
        assert(h.state.project.metadata.modifiedCounter == modifiedBefore + 2U);

        assert(h.state.undoSequencerHistory());
        assert(h.state.sequencerTracks.activeTrackIndex() ==
               kDirectTrackOldActive);
        assert(h.state.sequencer.pattern.note[0U] == editedNote);
        assert(h.state.undoSequencerHistory());
        assert(h.state.sequencer.pattern.note[0U] == noteBefore);
        assert(h.state.redoSequencerHistory());
        assert(h.state.sequencer.pattern.note[0U] == editedNote);
        assert(h.state.redoSequencerHistory());
        assert(h.state.sequencerTracks.activeTrackIndex() ==
               kDirectTrackCreateTarget);
        assertActiveMacroPresentation(h);
    }

    {
        SequencerStepHarness h;
        configureDirectTrackFixture(
            h, DirectTrackFixtureKind::RemoveCurrent);
        const uint8_t noteBefore = h.state.sequencer.pattern.note[0U];
        preparePendingTrackPatternEdit(h.state);
        const uint8_t editedNote = h.state.sequencer.pattern.note[0U];
        const uint8_t sequencerUndoBefore = h.state.sequencerHistory.undoCount();
        const uint8_t projectUndoBefore = h.state.projectHistory.undoCount();
        const uint32_t modifiedBefore = h.state.project.metadata.modifiedCounter;
        auto workflow = makeStructureEditWorkflow(
            h, HistoryServices::fromCoreState(h.state));
        workflow.beginHoldAction(core::state::StructureHoldAction::REMOVE);
        workflow.applyCurrentStructureLongPress();

        assert(!h.state.hasPendingSequencerPatternHistoryCoalescing());
        assert(h.state.sequencerHistory.undoCount() ==
               sequencerUndoBefore + 2U);
        assert(h.state.projectHistory.undoCount() == projectUndoBefore + 2U);
        assert(h.state.sequencerHistory.undoCount(
                   seq::SequencerHistoryScope::PatternOnly) == 1U);
        assert(h.state.sequencerHistory.undoCount(
                   seq::SequencerHistoryScope::Structure) == 1U);
        assert(h.state.project.metadata.modifiedCounter == modifiedBefore + 2U);
        assert(h.state.trackNavigation.hold.action.get() ==
               core::state::StructureHoldAction::NONE);

        assert(h.state.undoSequencerHistory());
        assert(h.state.sequencerTracks.activeTrackIndex() ==
               kDirectTrackOldActive);
        assert(h.state.sequencer.pattern.note[0U] == editedNote);
        assert(h.state.undoSequencerHistory());
        assert(h.state.sequencer.pattern.note[0U] == noteBefore);
        assert(h.state.redoSequencerHistory());
        assert(h.state.sequencer.pattern.note[0U] == editedNote);
        assert(h.state.redoSequencerHistory());
        assert(h.state.sequencerTracks.activeTrackIndex() ==
               kDirectTrackIncoming);
        assertActiveMacroPresentation(h);
    }

    std::cout
        << "[PASS] direct Track Pattern chronology is single and ordered\n";
}

void test_direct_track_exact_intent_drift_is_stale_before_allocation() {
    using Status = core::handler::SequencerPreparedTrackStructureStatus;
    constexpr std::array<DirectTrackIntentDrift, 7U> drifts{
        DirectTrackIntentDrift::TrackSelectionCursor,
        DirectTrackIntentDrift::PageSelectionMask,
        DirectTrackIntentDrift::StepSelectionClipboardRevision,
        DirectTrackIntentDrift::ClipboardRevision,
        DirectTrackIntentDrift::ClipboardOwner,
        DirectTrackIntentDrift::TrackPasteGeneration,
        DirectTrackIntentDrift::TrackPasteCommitConsumed,
    };

    for (const auto drift : drifts) {
        SequencerStepHarness h;
        configureDirectTrackFixture(h, DirectTrackFixtureKind::Create);
        DirectTrackIntentDriftHistory history{
            .state = &h.state,
            .drift = drift,
        };
        if (drift == DirectTrackIntentDrift::ClipboardOwner) {
            history.replacementGraph =
                core::app::makeExtmemUnique<TrackGraph>();
            assert(history.replacementGraph);
        }
        const auto before = captureCanonicalTrackLogicalProof(h);
        const uint8_t sequencerUndoBefore =
            h.state.sequencerHistory.undoCount();
        const uint8_t projectUndoBefore = h.state.projectHistory.undoCount();
        auto refs = directTrackRefs(h);
        refs.history = HistoryServices::fromStaticOperations<
            kDirectTrackIntentDriftHistoryOperations>(&history);
        {
            core::app::testing::ScopedExtmemAllocationFailure failure(1U);
            const auto result =
                core::handler::executeSequencerCreateTrackStructure(refs);
            assert(result.status == Status::Stale);
            assert(result.chronology.status ==
                   seq::SequencerTrackStructureChronologyStatus::Opened);
            tx::assertMaxPlusOneStillArmed(0U);
        }
        test_support::drainNotifications();
        assertCanonicalTrackLogicalProof(h, before);
        assert(h.state.sequencerHistory.undoCount() ==
               sequencerUndoBefore);
        assert(h.state.projectHistory.undoCount() == projectUndoBefore);
        assert(h.state.trackNavigation.previewAddSlot.get());
        assert(h.state.trackNavigation.previewTrackIndex.get() ==
               kDirectTrackCreateTarget);
        tx::assertFailureInjectionReset();
    }

    std::cout
        << "[PASS] direct Track exact-intent drift is stale before allocation\n";
}

void test_direct_track_accepts_terminal_consumed_paste_state() {
    SequencerStepHarness h;
    configureDirectTrackFixture(h, DirectTrackFixtureKind::Create);
    auto& paste = h.state.sequencer.structureUi.trackPaste;
    paste.commitConsumed = true;
    paste.buttonOwned = false;
    paste.detailVisible = false;
    paste.guard = {};

    const auto result =
        core::handler::executeSequencerCreateTrackStructure(
            directTrackRefs(h)
        );
    assert(result.committed());
    assert(h.state.sequencerTracks.activeTrackIndex() ==
           kDirectTrackCreateTarget);

    std::cout
        << "[PASS] direct Track accepts terminal consumed-paste state\n";
}

void test_direct_track_draft_priority_precedes_adapter_validation() {
    using Status = core::handler::SequencerPreparedTrackStructureStatus;
    SequencerStepHarness h;
    configureDirectTrackFixture(h, DirectTrackFixtureKind::Create);
    assert(h.state.sequencer.stepContentDraft.begin(
        h.state.sequencer.pattern,
        seq::SequencerStepContentDraftKind::MICRO_SEQUENCE,
        0U
    ));
    h.state.trackNavigation.hold.begin(
        core::state::StructureHoldAction::REMOVE,
        17U
    );
    const uint32_t draftRevision =
        h.state.sequencer.stepContentDraft.revision.get();
    const uint32_t contentRevision =
        h.state.sequencer.contentView.revision.get();
    const uint8_t undoBefore = h.state.sequencerHistory.undoCount();
    {
        core::app::testing::ScopedExtmemAllocationFailure failure(1U);
        const auto result =
            core::handler::executeSequencerCreateTrackStructure(
                directTrackRefs(h)
            );
        assert(result.status == Status::DraftBlocked);
        tx::assertMaxPlusOneStillArmed(0U);
    }
    assert(h.state.sequencer.stepContentDraft.failure ==
           seq::SequencerStepContentDraftFailure::TRANSITION_BLOCKED);
    assert(h.state.sequencer.stepContentDraft.blockedTransition ==
           seq::SequencerStepContentDraftBlockedTransition::TRACK);
    assert(h.state.sequencer.stepContentDraft.revision.get() ==
           draftRevision + 1U);
    assert(h.state.sequencer.contentView.revision.get() ==
           contentRevision + 1U);
    assert(h.state.sequencerHistory.undoCount() == undoBefore);
    assert(h.state.sequencerTracks.currentEnabledMask() ==
           kDirectTrackSparseMask);
    tx::assertFailureInjectionReset();

    std::cout
        << "[PASS] direct Track Draft priority precedes adapter validation\n";
}

void test_direct_track_obvious_invalid_topology_skips_chronology() {
    using Status = core::handler::SequencerPreparedTrackStructureStatus;
    using ChronologyStatus =
        seq::SequencerTrackStructureChronologyStatus;
    using PatternOutcome =
        seq::SequencerPatternHistoryCommitOutcome;

    {
        SequencerStepHarness h;
        configureDirectTrackFixture(h, DirectTrackFixtureKind::Create);
        const uint16_t enabledMask = static_cast<uint16_t>(
            kDirectTrackSparseMask |
            static_cast<uint16_t>(1U << kDirectTrackCreateTarget)
        );
        assert(h.state.setSharedTrackState(
            enabledMask,
            kDirectTrackOldActive
        ));
        test_support::drainNotifications();
        h.state.trackNavigation.previewAddSlot.set(true);
        h.state.trackNavigation.previewTrackIndex.set(
            kDirectTrackCreateTarget
        );
        preparePendingTrackPatternEdit(h.state);
        test_support::drainNotifications();
        assert(h.state.hasPendingSequencerPatternHistoryCoalescing());
        const auto expected = captureTrackTransactionInvariant(h);
        {
            core::app::testing::ScopedExtmemAllocationFailure failure(1U);
            const auto result =
                core::handler::executeSequencerCreateTrackStructure(
                    directTrackRefs(h)
                );
            assert(result.status == Status::Invalid);
            assert(result.chronology.status == ChronologyStatus::Unavailable);
            assert(result.chronology.predecessorPattern ==
                   PatternOutcome::NoPending);
            tx::assertMaxPlusOneStillArmed(0U);
        }
        test_support::drainNotifications();
        assertTrackTransactionInvariant(h, expected, true);
        assert(h.state.hasPendingSequencerPatternHistoryCoalescing());
        tx::assertFailureInjectionReset();
    }

    {
        SequencerStepHarness h;
        configureDirectTrackFixture(
            h,
            DirectTrackFixtureKind::RemoveCurrent
        );
        const uint16_t onlyActive =
            static_cast<uint16_t>(1U << kDirectTrackOldActive);
        assert(h.state.setSharedTrackState(
            onlyActive,
            kDirectTrackOldActive
        ));
        test_support::drainNotifications();
        h.state.trackNavigation.previewAddSlot.set(false);
        h.state.trackNavigation.previewTrackIndex.set(
            kDirectTrackOldActive
        );
        preparePendingTrackPatternEdit(h.state);
        test_support::drainNotifications();
        assert(h.state.hasPendingSequencerPatternHistoryCoalescing());
        const auto expected = captureTrackTransactionInvariant(h);
        {
            core::app::testing::ScopedExtmemAllocationFailure failure(1U);
            const auto result =
                core::handler::executeSequencerRemoveCurrentTrackStructure(
                    directTrackRefs(h),
                    kDirectTrackOldActive
                );
            assert(result.status == Status::Invalid);
            assert(result.chronology.status == ChronologyStatus::Unavailable);
            assert(result.chronology.predecessorPattern ==
                   PatternOutcome::NoPending);
            tx::assertMaxPlusOneStillArmed(0U);
        }
        test_support::drainNotifications();
        assertTrackTransactionInvariant(h, expected, true);
        assert(h.state.hasPendingSequencerPatternHistoryCoalescing());
        tx::assertFailureInjectionReset();
    }

    std::cout
        << "[PASS] direct Track invalid topology skips chronology\n";
}

void test_direct_track_missing_presentation_capability_is_preflight_atomic() {
    using Status =
        core::handler::SequencerPreparedTrackStructureStatus;
    using ChronologyStatus =
        seq::SequencerTrackStructureChronologyStatus;

    for (const auto kind : std::array<DirectTrackFixtureKind, 2U>{
             DirectTrackFixtureKind::Create,
             DirectTrackFixtureKind::RemoveCurrent,
         }) {
        SequencerStepHarness h;
        configureDirectTrackFixture(h, kind);
        DirectTrackChronologyCounter chronology{.state = &h.state};
        auto refs = directTrackRefs(h);
        refs.history = HistoryServices::fromStaticOperations<
            kDirectTrackChronologyCounterOperations>(&chronology);
        refs.sharedTracks = core::handler::SharedTrackDomainServices{
            core::handler::SharedTrackDomainServices::StateRefs{
                h.state.sharedTrackActive,
                h.state.sharedTrackEnabledMask,
            },
        };
        assert(!refs.sharedTracks.
            canReconcilePreparedSequencerActiveTrackPresentation());
        const auto expected = captureTrackTransactionInvariant(h);

        core::handler::SequencerPreparedTrackStructureResult result{};
        {
            core::app::testing::ScopedExtmemAllocationFailure failure(1U);
            result = kind == DirectTrackFixtureKind::Create
                ? core::handler::executeSequencerCreateTrackStructure(refs)
                : core::handler::executeSequencerRemoveCurrentTrackStructure(
                      refs,
                      kDirectTrackOldActive
                  );
            tx::assertMaxPlusOneStillArmed(0U);
        }
        test_support::drainNotifications();

        assert(result.status == Status::PublicationUnavailable);
        assert(result.chronology.status == ChronologyStatus::Unavailable);
        assert(chronology.boundaryCount == 0U);
        assertTrackTransactionInvariant(h, expected);
        tx::assertFailureInjectionReset();
    }

    std::cout
        << "[PASS] missing direct Track presentation capability is preflight-atomic\n";
}

void test_track_remove_hold_latches_target_and_rejects_external_drift() {
    {
        SequencerStepHarness h;
        configureDirectTrackFixture(h, DirectTrackFixtureKind::RemoveCurrent);
        seq::resetTransientTrackState(h.state.sequencer);
        test_support::drainNotifications();

        h.press(Config::ButtonID::BOTTOM_LEFT);
        assert(h.state.trackNavigation.hold.action.get() ==
               core::state::StructureHoldAction::REMOVE);
        assert(h.state.trackNavigation.previewTrackIndex.get() ==
               kDirectTrackOldActive);

        h.turn(Config::EncoderID::NAV, 1.0f);
        assert(h.state.sequencerTracks.activeTrackIndex() ==
               kDirectTrackOldActive);
        assert(h.state.trackNavigation.previewTrackIndex.get() ==
               kDirectTrackOldActive);

        assert(h.state.setSharedTrackState(
            kDirectTrackSparseMask,
            kDirectTrackIncoming
        ));
        test_support::drainNotifications();
        assert(h.state.sequencerTracks.activeTrackIndex() ==
               kDirectTrackIncoming);
        assert(h.state.trackNavigation.previewTrackIndex.get() ==
               kDirectTrackOldActive);
        const uint8_t sequencerUndoBefore =
            h.state.sequencerHistory.undoCount();
        const uint8_t projectUndoBefore = h.state.projectHistory.undoCount();
        const uint16_t maskBefore =
            h.state.sequencerTracks.currentEnabledMask();

        h.advance(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
        assert(h.state.trackNavigation.hold.action.get() ==
               core::state::StructureHoldAction::NONE);
        assert(h.state.sequencerTracks.currentEnabledMask() == maskBefore);
        assert(h.state.sequencerTracks.activeTrackIndex() ==
               kDirectTrackIncoming);
        assert(h.state.trackNavigation.previewTrackIndex.get() ==
               kDirectTrackOldActive);
        assert(h.state.sequencerHistory.undoCount() == sequencerUndoBefore);
        assert(h.state.projectHistory.undoCount() == projectUndoBefore);

        const uint16_t mutedBefore = h.state.projectTracks.authored.mutedMask;
        h.release(Config::ButtonID::BOTTOM_LEFT);
        assert(h.state.projectTracks.authored.mutedMask == mutedBefore);
    }

    {
        SequencerStepHarness h;
        configureDirectTrackFixture(h, DirectTrackFixtureKind::Create);
        const auto create =
            core::handler::executeSequencerCreateTrackStructure(
                directTrackRefs(h)
            );
        assert(create.committed());
        assert(h.state.sequencerTracks.activeTrackIndex() ==
               kDirectTrackCreateTarget);

        auto workflow = makeStructureEditWorkflow(
            h,
            HistoryServices::fromCoreState(h.state)
        );
        workflow.beginHoldAction(core::state::StructureHoldAction::REMOVE);
        assert(h.state.trackNavigation.previewTrackIndex.get() ==
               kDirectTrackCreateTarget);
        assert(h.state.undoSequencerHistory());
        test_support::drainNotifications();
        assert(h.state.sequencerTracks.activeTrackIndex() ==
               kDirectTrackOldActive);
        assert(h.state.trackNavigation.previewTrackIndex.get() ==
               kDirectTrackOldActive);
        assert(h.state.trackNavigation.hold.action.get() ==
               core::state::StructureHoldAction::REMOVE);

        auto expected = captureTrackTransactionInvariant(h);
        expected.trackUi.holdAction = static_cast<uint8_t>(
            core::state::StructureHoldAction::NONE
        );
        expected.trackUi.holdStartedAtMs = 0U;
        {
            core::app::testing::ScopedExtmemAllocationFailure failure(1U);
            workflow.applyCurrentStructureLongPress();
            tx::assertMaxPlusOneStillArmed(0U);
        }
        test_support::drainNotifications();
        assertTrackTransactionInvariant(h, expected);
        assert(h.state.sequencerTracks.activeTrackIndex() ==
               kDirectTrackOldActive);
        assert(h.state.trackNavigation.previewTrackIndex.get() ==
               kDirectTrackOldActive);
        tx::assertFailureInjectionReset();
    }

    std::cout
        << "[PASS] Track Remove hold latches target and rejects external drift\n";
}

void test_track_remove_hold_does_not_block_acquired_nav_release() {
    SequencerStepHarness h;
    configureDirectTrackFixture(h, DirectTrackFixtureKind::RemoveCurrent);
    seq::resetTransientTrackState(h.state.sequencer);
    test_support::drainNotifications();

    h.press(Config::ButtonID::NAV);
    assert(h.state.sequencer.contextSelector.visible);
    h.press(Config::ButtonID::BOTTOM_LEFT);
    assert(h.state.trackNavigation.hold.action.get() ==
           core::state::StructureHoldAction::REMOVE);
    h.release(Config::ButtonID::NAV);
    assert(!h.state.sequencer.contextSelector.visible);
    h.release(Config::ButtonID::BOTTOM_LEFT);
    assert(h.state.trackNavigation.hold.action.get() ==
           core::state::StructureHoldAction::NONE);

    std::cout
        << "[PASS] Track Remove hold preserves acquired NAV release\n";
}

void test_track_remove_hold_rejects_new_nav_press_without_hiding_action() {
    {
        SequencerStepHarness h;
        configureDirectTrackFixture(h, DirectTrackFixtureKind::RemoveCurrent);
        seq::resetTransientTrackState(h.state.sequencer);
        test_support::drainNotifications();

        const uint16_t maskBefore =
            h.state.sequencerTracks.currentEnabledMask();
        const uint8_t undoBefore = h.state.sequencerHistory.undoCount();
        h.press(Config::ButtonID::BOTTOM_LEFT);
        h.advance(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS - 1U);
        h.press(Config::ButtonID::NAV);
        assert(!h.state.sequencer.contextSelector.visible);
        h.advance(1U);
        test_support::drainNotifications();

        assert(!h.state.sequencer.contextSelector.visible);
        assert(h.state.sequencerTracks.currentEnabledMask() != maskBefore);
        assert(h.state.sequencerHistory.undoCount() == undoBefore + 1U);
        h.release(Config::ButtonID::NAV);
        h.release(Config::ButtonID::BOTTOM_LEFT);
        assert(!h.state.sequencer.contextSelector.visible);
    }

    {
        SequencerStepHarness h;
        configureDirectTrackFixture(h, DirectTrackFixtureKind::RemoveCurrent);
        seq::resetTransientTrackState(h.state.sequencer);
        test_support::drainNotifications();

        const uint16_t mutedBefore =
            h.state.projectTracks.authored.mutedMask;
        h.press(Config::ButtonID::BOTTOM_LEFT);
        h.press(Config::ButtonID::NAV);
        assert(!h.state.sequencer.contextSelector.visible);
        h.release(Config::ButtonID::BOTTOM_LEFT);
        h.release(Config::ButtonID::NAV);
        assert(!h.state.sequencer.contextSelector.visible);
        assert(h.state.projectTracks.authored.mutedMask != mutedBefore);
    }

    std::cout
        << "[PASS] acquired Track Remove rejects a new NAV selector press\n";
}

void test_track_hold_boundary_drift_cannot_retarget_mutation() {
    {
        SequencerStepHarness h;
        configureDirectTrackFixture(h, DirectTrackFixtureKind::RemoveCurrent);
        seq::resetTransientTrackState(h.state.sequencer);
        test_support::drainNotifications();

        TrackHoldBoundaryDriftHistory drift{
            .state = &h.state,
            .drift = TrackHoldBoundaryDrift::ActiveTrack,
            .nextActiveTrack = kDirectTrackIncoming,
        };
        auto workflow = makeStructureEditWorkflow(
            h,
            HistoryServices::fromStaticOperations<
                kTrackHoldBoundaryDriftHistoryOperations>(&drift)
        );
        workflow.beginHoldAction(core::state::StructureHoldAction::REMOVE);
        const uint16_t mutedBefore =
            h.state.projectTracks.authored.mutedMask;
        const uint8_t undoBefore = h.state.sequencerHistory.undoCount();
        {
            core::app::testing::ScopedExtmemAllocationFailure failure(1U);
            workflow.applyLatchedCurrentTrackShortPress();
            tx::assertMaxPlusOneStillArmed(0U);
        }
        test_support::drainNotifications();

        assert(drift.boundaryCount == 1U);
        assert(h.state.sharedTrackActive.get() == kDirectTrackIncoming);
        assert(h.state.projectTracks.authored.mutedMask == mutedBefore);
        assert(h.state.sequencerHistory.undoCount() == undoBefore);
        assert(h.state.trackNavigation.hold.action.get() ==
               core::state::StructureHoldAction::NONE);
        tx::assertFailureInjectionReset();
    }

    {
        SequencerStepHarness h;
        configureDirectTrackFixture(h, DirectTrackFixtureKind::RemoveCurrent);
        seq::resetTransientTrackState(h.state.sequencer);
        auto& selection = h.state.trackNavigation.selection;
        selection.active.set(true);
        selection.scope.set(core::state::StructureSelectionScope::TRACK);
        selection.placing.set(false);
        selection.cursorIndex.set(kDirectTrackOldActive);
        selection.selectedMask.set(
            static_cast<uint16_t>(1U << kDirectTrackOldActive)
        );
        h.state.trackNavigation.syncPreviewTrack(kDirectTrackOldActive);
        test_support::drainNotifications();

        TrackHoldBoundaryDriftHistory drift{
            .state = &h.state,
            .drift = TrackHoldBoundaryDrift::SelectionMask,
            .nextSelectionMask =
                static_cast<uint16_t>(1U << kDirectTrackIncoming),
        };
        auto workflow = makeStructureEditWorkflow(
            h,
            HistoryServices::fromStaticOperations<
                kTrackHoldBoundaryDriftHistoryOperations>(&drift)
        );
        workflow.beginSelectionHoldAction(
            core::state::StructureHoldAction::REMOVE
        );
        const uint16_t enabledBefore =
            h.state.sequencerTracks.currentEnabledMask();
        const uint8_t undoBefore = h.state.sequencerHistory.undoCount();
        {
            core::app::testing::ScopedExtmemAllocationFailure failure(1U);
            workflow.applyLatchedTrackSelectionLongPress();
            tx::assertMaxPlusOneStillArmed(0U);
        }
        test_support::drainNotifications();

        assert(drift.boundaryCount == 1U);
        assert(selection.selectedMask.get() == drift.nextSelectionMask);
        assert(h.state.sequencerTracks.currentEnabledMask() == enabledBefore);
        assert(h.state.sequencerHistory.undoCount() == undoBefore);
        assert(h.state.trackNavigation.hold.action.get() ==
               core::state::StructureHoldAction::NONE);
        tx::assertFailureInjectionReset();
    }

    std::cout
        << "[PASS] Track hold boundary drift cannot retarget mute or delete\n";
}

void test_track_remove_hold_provenance_cannot_cross_context_or_selection() {
    using Focus = core::state::StructureNavigationFocus;
    const auto assertNextTrackTapWorks = [](SequencerStepHarness& h) {
        h.navigationFocus.set(Focus::TRACK);
        h.state.trackNavigation.selection.active.set(false);
        h.state.trackNavigation.selection.placing.set(false);
        h.state.trackNavigation.selection.scope.set(
            core::state::StructureSelectionScope::TRACK
        );
        h.state.trackNavigation.previewAddSlot.set(false);
        h.state.trackNavigation.syncPreviewTrack(
            h.state.sharedTrackActive.get()
        );
        h.advance(0U);
        test_support::drainNotifications();
        const uint16_t mutedBefore =
            h.state.projectTracks.authored.mutedMask;
        h.press(Config::ButtonID::BOTTOM_LEFT);
        h.release(Config::ButtonID::BOTTOM_LEFT);
        assert(h.state.projectTracks.authored.mutedMask != mutedBefore);
    };

    for (const auto focus : std::array<Focus, 2U>{Focus::PAGE, Focus::STEP}) {
        for (const bool longPress : std::array<bool, 2U>{false, true}) {
            SequencerStepHarness h;
            configureDirectTrackFixture(
                h,
                DirectTrackFixtureKind::RemoveCurrent
            );
            seq::resetTransientTrackState(h.state.sequencer);
            test_support::drainNotifications();

            h.press(Config::ButtonID::BOTTOM_LEFT);
            assert(h.state.trackNavigation.hold.action.get() ==
                   core::state::StructureHoldAction::REMOVE);
            h.navigationFocus.set(focus);
            if (focus == Focus::PAGE) {
                h.state.sequencer.pattern.setContentLength(8U);
                h.state.sequencer.page.set(0U);
                h.state.sequencer.structureUi.syncPreviewPage(0U);
            } else {
                h.state.sequencer.focusedStep.set(
                    seq::SequencerState::MAX_STEPS
                );
            }
            h.advance(0U);
            test_support::drainNotifications();

            auto expected = captureTrackTransactionInvariant(h);
            expected.trackUi.holdAction = static_cast<uint8_t>(
                core::state::StructureHoldAction::NONE
            );
            expected.trackUi.holdStartedAtMs = 0U;
            {
                core::app::testing::ScopedExtmemAllocationFailure failure(1U);
                if (longPress) {
                    h.advance(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
                } else {
                    h.release(Config::ButtonID::BOTTOM_LEFT);
                }
                tx::assertMaxPlusOneStillArmed(0U);
            }
            test_support::drainNotifications();
            assertTrackTransactionInvariant(h, expected);
            if (longPress) {
                h.release(Config::ButtonID::BOTTOM_LEFT);
                test_support::drainNotifications();
                assertTrackTransactionInvariant(h, expected);
            }
            tx::assertFailureInjectionReset();
            assertNextTrackTapWorks(h);
        }
    }

    for (const bool longPress : std::array<bool, 2U>{false, true}) {
        SequencerStepHarness h;
        configureDirectTrackFixture(
            h,
            DirectTrackFixtureKind::RemoveCurrent
        );
        seq::resetTransientTrackState(h.state.sequencer);
        auto& selection = h.state.trackNavigation.selection;
        selection.active.set(true);
        selection.placing.set(false);
        selection.selectedMask.set(0x0001U);
        test_support::drainNotifications();

        h.press(Config::ButtonID::BOTTOM_LEFT);
        assert(h.state.trackNavigation.hold.action.get() ==
               core::state::StructureHoldAction::REMOVE);
        selection.active.set(false);
        h.advance(0U);
        test_support::drainNotifications();

        auto expected = captureTrackTransactionInvariant(h);
        expected.trackUi.holdAction = static_cast<uint8_t>(
            core::state::StructureHoldAction::NONE
        );
        expected.trackUi.holdStartedAtMs = 0U;
        {
            core::app::testing::ScopedExtmemAllocationFailure failure(1U);
            if (longPress) {
                h.advance(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
            } else {
                h.release(Config::ButtonID::BOTTOM_LEFT);
            }
            tx::assertMaxPlusOneStillArmed(0U);
        }
        test_support::drainNotifications();
        assertTrackTransactionInvariant(h, expected);
        if (longPress) {
            h.release(Config::ButtonID::BOTTOM_LEFT);
            test_support::drainNotifications();
            assertTrackTransactionInvariant(h, expected);
        }
        tx::assertFailureInjectionReset();
        assertNextTrackTapWorks(h);
    }

    enum class InvalidInitialSelection : uint8_t {
        Scope = 0U,
        AddPreview,
    };
    for (const auto invalid :
         std::array<InvalidInitialSelection, 2U>{
             InvalidInitialSelection::Scope,
             InvalidInitialSelection::AddPreview,
         }) {
        for (const bool longPress : std::array<bool, 2U>{false, true}) {
            SequencerStepHarness h;
            configureDirectTrackFixture(
                h,
                DirectTrackFixtureKind::RemoveCurrent
            );
            seq::resetTransientTrackState(h.state.sequencer);
            auto& selection = h.state.trackNavigation.selection;
            selection.active.set(true);
            selection.scope.set(core::state::StructureSelectionScope::TRACK);
            selection.placing.set(false);
            selection.selectedMask.set(0x0001U);
            if (invalid == InvalidInitialSelection::Scope) {
                selection.scope.set(
                    core::state::StructureSelectionScope::PAGE
                );
            } else {
                h.state.trackNavigation.previewAddSlot.set(true);
            }
            h.advance(0U);
            test_support::drainNotifications();
            const auto expected = captureTrackTransactionInvariant(h);

            h.press(Config::ButtonID::BOTTOM_LEFT);
            assert(h.state.trackNavigation.hold.action.get() ==
                   core::state::StructureHoldAction::NONE);
            {
                core::app::testing::ScopedExtmemAllocationFailure failure(1U);
                if (longPress) {
                    h.advance(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
                    h.release(Config::ButtonID::BOTTOM_LEFT);
                } else {
                    h.release(Config::ButtonID::BOTTOM_LEFT);
                }
                tx::assertMaxPlusOneStillArmed(0U);
            }
            test_support::drainNotifications();
            assertTrackTransactionInvariant(h, expected);
            tx::assertFailureInjectionReset();

            selection.scope.set(core::state::StructureSelectionScope::TRACK);
            h.state.trackNavigation.previewAddSlot.set(false);
            assertNextTrackTapWorks(h);
        }
    }

    enum class SelectionDrift : uint8_t {
        FocusPage = 0U,
        FocusStep,
        ClipboardRevision,
        SelectedMask,
        DestinationMask,
        OverwriteMask,
        Cursor,
        Scope,
        Placing,
        PasteBlocked,
        PreviewTrack,
        PreviewAddTrack,
        EnabledMask,
        ActiveTrack,
    };
    for (const auto drift : std::array<SelectionDrift, 14U>{
             SelectionDrift::FocusPage,
             SelectionDrift::FocusStep,
             SelectionDrift::ClipboardRevision,
             SelectionDrift::SelectedMask,
             SelectionDrift::DestinationMask,
             SelectionDrift::OverwriteMask,
             SelectionDrift::Cursor,
             SelectionDrift::Scope,
             SelectionDrift::Placing,
             SelectionDrift::PasteBlocked,
             SelectionDrift::PreviewTrack,
             SelectionDrift::PreviewAddTrack,
             SelectionDrift::EnabledMask,
             SelectionDrift::ActiveTrack,
         }) {
        for (const bool longPress : std::array<bool, 2U>{false, true}) {
            SequencerStepHarness h;
            configureDirectTrackFixture(
                h,
                DirectTrackFixtureKind::RemoveCurrent
            );
            seq::resetTransientTrackState(h.state.sequencer);
            auto& selection = h.state.trackNavigation.selection;
            selection.active.set(true);
            selection.placing.set(false);
            selection.selectedMask.set(0x0001U);
            selection.cursorIndex.set(0U);
            test_support::drainNotifications();

            h.press(Config::ButtonID::BOTTOM_LEFT);
            assert(h.state.trackNavigation.hold.action.get() ==
                   core::state::StructureHoldAction::REMOVE);
            switch (drift) {
                case SelectionDrift::FocusPage:
                    h.navigationFocus.set(Focus::PAGE);
                    break;
                case SelectionDrift::FocusStep:
                    h.navigationFocus.set(Focus::STEP);
                    break;
                case SelectionDrift::ClipboardRevision:
                    selection.clipboardRevision.set(
                        selection.clipboardRevision.get() + 1U
                    );
                    break;
                case SelectionDrift::SelectedMask:
                    selection.selectedMask.set(0x0020U);
                    break;
                case SelectionDrift::DestinationMask:
                    selection.destinationMask.set(0x0004U);
                    break;
                case SelectionDrift::OverwriteMask:
                    selection.overwriteMask.set(0x0020U);
                    break;
                case SelectionDrift::Cursor:
                    selection.cursorIndex.set(5U);
                    break;
                case SelectionDrift::Scope:
                    selection.scope.set(
                        core::state::StructureSelectionScope::PAGE
                    );
                    break;
                case SelectionDrift::Placing:
                    selection.placing.set(true);
                    break;
                case SelectionDrift::PasteBlocked:
                    selection.pasteBlocked.set(!selection.pasteBlocked.get());
                    break;
                case SelectionDrift::PreviewTrack:
                    h.state.trackNavigation.previewTrackIndex.set(
                        kDirectTrackIncoming
                    );
                    break;
                case SelectionDrift::PreviewAddTrack:
                    h.state.trackNavigation.previewAddSlot.set(true);
                    break;
                case SelectionDrift::EnabledMask:
                    h.state.sharedTrackEnabledMask.set(
                        static_cast<uint16_t>(kDirectTrackSparseMask | 0x0002U)
                    );
                    break;
                case SelectionDrift::ActiveTrack:
                    h.state.sharedTrackActive.set(kDirectTrackIncoming);
                    break;
            }
            h.advance(0U);
            test_support::drainNotifications();

            auto expected = captureTrackTransactionInvariant(h);
            expected.trackUi.holdAction = static_cast<uint8_t>(
                core::state::StructureHoldAction::NONE
            );
            expected.trackUi.holdStartedAtMs = 0U;
            {
                core::app::testing::ScopedExtmemAllocationFailure failure(1U);
                if (longPress) {
                    h.advance(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
                } else {
                    h.release(Config::ButtonID::BOTTOM_LEFT);
                }
                tx::assertMaxPlusOneStillArmed(0U);
            }
            test_support::drainNotifications();
            assertTrackTransactionInvariant(h, expected);
            if (longPress) {
                h.release(Config::ButtonID::BOTTOM_LEFT);
                test_support::drainNotifications();
                assertTrackTransactionInvariant(h, expected);
            }
            tx::assertFailureInjectionReset();
            assertNextTrackTapWorks(h);
        }
    }

    for (const bool longPress : std::array<bool, 2U>{false, true}) {
        SequencerStepHarness h;
        configureDirectTrackFixture(
            h,
            DirectTrackFixtureKind::RemoveCurrent
        );
        seq::resetTransientTrackState(h.state.sequencer);
        test_support::drainNotifications();

        h.press(Config::ButtonID::BOTTOM_LEFT);
        assert(h.state.trackNavigation.hold.action.get() ==
               core::state::StructureHoldAction::REMOVE);
        auto& selection = h.state.trackNavigation.selection;
        selection.active.set(true);
        selection.placing.set(false);
        selection.selectedMask.set(0x0001U);
        h.advance(0U);
        test_support::drainNotifications();

        auto expected = captureTrackTransactionInvariant(h);
        expected.trackUi.holdAction = static_cast<uint8_t>(
            core::state::StructureHoldAction::NONE
        );
        expected.trackUi.holdStartedAtMs = 0U;
        {
            core::app::testing::ScopedExtmemAllocationFailure failure(1U);
            if (longPress) {
                h.advance(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
            } else {
                h.release(Config::ButtonID::BOTTOM_LEFT);
            }
            tx::assertMaxPlusOneStillArmed(0U);
        }
        test_support::drainNotifications();
        assertTrackTransactionInvariant(h, expected);
        if (longPress) {
            h.release(Config::ButtonID::BOTTOM_LEFT);
            test_support::drainNotifications();
            assertTrackTransactionInvariant(h, expected);
        }
        tx::assertFailureInjectionReset();
        assertNextTrackTapWorks(h);
    }

    {
        SequencerStepHarness h;
        configureDirectTrackFixture(
            h,
            DirectTrackFixtureKind::RemoveCurrent
        );
        seq::resetTransientTrackState(h.state.sequencer);
        test_support::drainNotifications();
        h.press(Config::ButtonID::BOTTOM_LEFT);
        assert(h.state.setSharedTrackState(
            kDirectTrackSparseMask,
            kDirectTrackIncoming
        ));
        h.advance(0U);
        test_support::drainNotifications();

        auto expected = captureTrackTransactionInvariant(h);
        expected.trackUi.holdAction = static_cast<uint8_t>(
            core::state::StructureHoldAction::NONE
        );
        expected.trackUi.holdStartedAtMs = 0U;
        {
            core::app::testing::ScopedExtmemAllocationFailure failure(1U);
            h.release(Config::ButtonID::BOTTOM_LEFT);
            tx::assertMaxPlusOneStillArmed(0U);
        }
        test_support::drainNotifications();
        assertTrackTransactionInvariant(h, expected);
        tx::assertFailureInjectionReset();
        assertNextTrackTapWorks(h);
    }

    std::cout
        << "[PASS] Track Remove hold provenance cannot cross context or selection\n";
}

void test_track_remove_hold_rejects_shared_hold_replacement() {
    enum class Replacement : uint8_t {
        Clear = 0U,
        Paste,
        NewRemove,
    };

    for (const bool selectionGesture : std::array<bool, 2U>{false, true}) {
        for (const bool longPress : std::array<bool, 2U>{false, true}) {
            for (const auto replacement : std::array<Replacement, 3U>{
                     Replacement::Clear,
                     Replacement::Paste,
                     Replacement::NewRemove,
                 }) {
                SequencerStepHarness h;
                configureDirectTrackFixture(
                    h,
                    DirectTrackFixtureKind::RemoveCurrent
                );
                seq::resetTransientTrackState(h.state.sequencer);
                auto& selection = h.state.trackNavigation.selection;
                if (selectionGesture) {
                    selection.active.set(true);
                    selection.scope.set(
                        core::state::StructureSelectionScope::TRACK
                    );
                    selection.placing.set(false);
                    selection.cursorIndex.set(kDirectTrackOldActive);
                    selection.selectedMask.set(static_cast<uint16_t>(
                        1U << kDirectTrackOldActive
                    ));
                }
                h.state.trackNavigation.syncPreviewTrack(
                    kDirectTrackOldActive
                );
                test_support::drainNotifications();

                h.press(Config::ButtonID::BOTTOM_LEFT);
                auto& hold = h.state.trackNavigation.hold;
                assert(hold.action.get() ==
                       core::state::StructureHoldAction::REMOVE);
                const uint32_t acquiredAt = hold.startedAtMs.get();
                auto expectedHoldAction =
                    core::state::StructureHoldAction::NONE;
                uint32_t expectedHoldStartedAt = 0U;
                switch (replacement) {
                    case Replacement::Clear:
                        hold.clear();
                        break;
                    case Replacement::Paste:
                        expectedHoldAction =
                            core::state::StructureHoldAction::PASTE;
                        expectedHoldStartedAt = acquiredAt + 1U;
                        hold.begin(
                            expectedHoldAction,
                            expectedHoldStartedAt
                        );
                        break;
                    case Replacement::NewRemove:
                        expectedHoldAction =
                            core::state::StructureHoldAction::REMOVE;
                        expectedHoldStartedAt = acquiredAt + 1U;
                        hold.begin(
                            expectedHoldAction,
                            expectedHoldStartedAt
                        );
                        break;
                }
                test_support::drainNotifications();

                const uint16_t mutedBefore =
                    h.state.projectTracks.authored.mutedMask;
                const uint16_t enabledBefore =
                    h.state.sequencerTracks.currentEnabledMask();
                const uint8_t sequencerUndoBefore =
                    h.state.sequencerHistory.undoCount();
                const uint8_t projectUndoBefore =
                    h.state.projectHistory.undoCount();
                {
                    core::app::testing::ScopedExtmemAllocationFailure failure(1U);
                    if (longPress) {
                        h.advance(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
                        h.release(Config::ButtonID::BOTTOM_LEFT);
                    } else {
                        h.release(Config::ButtonID::BOTTOM_LEFT);
                    }
                    tx::assertMaxPlusOneStillArmed(0U);
                }
                test_support::drainNotifications();

                assert(h.state.projectTracks.authored.mutedMask == mutedBefore);
                assert(h.state.sequencerTracks.currentEnabledMask() ==
                       enabledBefore);
                assert(h.state.sequencerHistory.undoCount() ==
                       sequencerUndoBefore);
                assert(h.state.projectHistory.undoCount() == projectUndoBefore);
                assert(hold.action.get() == expectedHoldAction);
                assert(hold.startedAtMs.get() == expectedHoldStartedAt);
                tx::assertFailureInjectionReset();
            }
        }
    }

    std::cout
        << "[PASS] Track Remove rejects cleared, replaced and re-armed shared holds\n";
}

void test_track_structure_replay_preserves_runtime_when_active_is_unchanged() {
    SequencerStepHarness h;
    h.state.sequencerTracks.reset();
    assert(h.state.setSharedTrackState(0x0007U, 0U));
    h.navigationFocus.set(core::state::StructureNavigationFocus::TRACK);
    test_support::drainNotifications();

    h.press(Config::ButtonID::NAV);
    h.advance(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    h.release(Config::ButtonID::NAV);
    h.turn(Config::EncoderID::NAV, 1.0f);
    h.tap(Config::ButtonID::NAV);
    h.press(Config::ButtonID::BOTTOM_LEFT);
    h.advance(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    h.release(Config::ButtonID::BOTTOM_LEFT);
    assert(h.state.sequencerTracks.currentEnabledMask() == 0x0005U);
    assert(h.state.sequencerTracks.activeTrackIndex() == 0U);

    std::array<float, core::state::macro::MACRO_COUNT> undoProjection{};
    for (uint8_t macro = 0U;
         macro < core::state::macro::MACRO_COUNT;
         ++macro) {
        undoProjection[macro] = 0.91f - static_cast<float>(macro) * 0.01f;
        h.state.macros.slots[macro].value.set(undoProjection[macro]);
    }
    h.state.macroUi.automationManualOverrideMask.set(0x00A5U);
    const uint32_t projectionRevision =
        h.state.macroUi.runtimeProjectionRevision.get();
    assert(h.state.undoSequencerHistory());
    assert(h.state.sequencerTracks.currentEnabledMask() == 0x0007U);
    assert(h.state.sequencerTracks.activeTrackIndex() == 0U);
    for (uint8_t macro = 0U;
         macro < core::state::macro::MACRO_COUNT;
         ++macro) {
        assert(h.state.macros.slots[macro].value.get() ==
               undoProjection[macro]);
    }
    assert(h.state.macroUi.automationManualOverrideMask.get() == 0x00A5U);
    assert(h.state.macroUi.runtimeProjectionRevision.get() ==
           projectionRevision);

    std::array<float, core::state::macro::MACRO_COUNT> redoProjection{};
    for (uint8_t macro = 0U;
         macro < core::state::macro::MACRO_COUNT;
         ++macro) {
        redoProjection[macro] = 0.11f + static_cast<float>(macro) * 0.01f;
        h.state.macros.slots[macro].value.set(redoProjection[macro]);
    }
    h.state.macroUi.automationManualOverrideMask.set(0x005AU);
    assert(h.state.redoSequencerHistory());
    assert(h.state.sequencerTracks.currentEnabledMask() == 0x0005U);
    assert(h.state.sequencerTracks.activeTrackIndex() == 0U);
    for (uint8_t macro = 0U;
         macro < core::state::macro::MACRO_COUNT;
         ++macro) {
        assert(h.state.macros.slots[macro].value.get() ==
               redoProjection[macro]);
    }
    assert(h.state.macroUi.automationManualOverrideMask.get() == 0x005AU);
    assert(h.state.macroUi.runtimeProjectionRevision.get() ==
           projectionRevision);

    std::cout
        << "[PASS] unchanged-active Track replay preserves runtime projection\n";
}

void test_created_track_is_undoable_and_redoable() {
    SequencerStepHarness h;
    h.state.sequencerTracks.reset();
    h.state.setSharedTrackState(0x0001, 0);
    focusTrackNavigation(h);

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.trackNavigation.previewAddSlot.get());
    assert(h.state.trackNavigation.previewTrackIndex.get() == 1);

    h.press(Config::ButtonID::NAV);
    h.release(Config::ButtonID::NAV);

    assert(h.state.sequencerTracks.currentEnabledMask() == 0x0003);
    assert(h.state.sequencerTracks.activeTrackIndex() == 1);
    assert(h.state.projectTracks.authored.midiChannels[h.state.currentSharedActiveTrack()] == 1);
    assert(h.state.sequencer.pattern.length.get() == 8);
    assert(h.state.sequencer.page.get() == 0);
    assert(h.state.sequencer.focusedStep.get() == 0);
    assert(h.navigationFocus.get() == core::state::StructureNavigationFocus::TRACK);
    assert(h.state.sequencerHistory.undoCount() == 1);
    assert(h.state.sequencerHistory.undoCount(
               core::state::sequencer::SequencerHistoryScope::Structure) == 1);
    assert(h.state.sequencerHistory.undoCount(
               core::state::sequencer::SequencerHistoryScope::FullBank) == 0);

    assert(h.state.undoSequencerHistory());
    assert(h.state.sequencerTracks.currentEnabledMask() == 0x0001);
    assert(h.state.sequencerTracks.activeTrackIndex() == 0);
    assert(!h.state.trackNavigation.previewAddSlot.get());
    assert(h.state.trackNavigation.previewTrackIndex.get() == 0);
    assert(h.state.projectTracks.authored.midiChannels[h.state.currentSharedActiveTrack()] == 0);
    assert(h.state.sequencerHistory.redoCount() == 1);
    assert(std::strcmp(h.state.sequencer.historyFeedback.line1.data(), "UNDO T02") == 0);
    assert(std::strcmp(h.state.sequencer.historyFeedback.line2.data(), "Track Structure") == 0);
    assert(std::strcmp(h.state.sequencer.historyFeedback.line3.data(), "2 tracks -> 1 track") == 0);

    assert(h.state.redoSequencerHistory());
    assert(h.state.sequencerTracks.currentEnabledMask() == 0x0003);
    assert(h.state.sequencerTracks.activeTrackIndex() == 1);
    assert(!h.state.trackNavigation.previewAddSlot.get());
    assert(h.state.trackNavigation.previewTrackIndex.get() == 1);
    assert(h.state.projectTracks.authored.midiChannels[h.state.currentSharedActiveTrack()] == 1);
    assert(std::strcmp(h.state.sequencer.historyFeedback.line1.data(), "REDO T02") == 0);
    assert(std::strcmp(h.state.sequencer.historyFeedback.line3.data(), "1 track -> 2 tracks") == 0);

    std::cout << "[PASS] test_created_track_is_undoable_and_redoable\n";
}

void test_track_creation_history_unavailable_is_atomic_and_keeps_add_slot_open() {
    test_support::CoreStorages storages;
    core::state::CoreState state(storages.settings);
    oc::state::Signal<core::state::StructureNavigationFocus,
                      core::state::kStructureNavigationFocusMaxSubscribers>
        navigationFocus{core::state::StructureNavigationFocus::TRACK};

    state.sequencerTracks.reset();
    state.setSharedTrackState(0x0001, 0);
    state.sequencerTracks.track(1).note[0] = 77;
    state.sequencerTracks.track(1).setEnabled(0, true);

    auto sharedTracks =
        core::handler::SharedTrackDomainServices::fromCoreState(state);
    core::handler::SequencerStructureNavigationWorkflow workflow({
        state.sequencer,
        state.sequencerTracks,
        navigationFocus,
        state.trackNavigation,
        sharedTracks,
    });
    workflow.moveByFocus(1.0f);
    assert(state.trackNavigation.previewAddSlot.get());
    assert(state.trackNavigation.previewTrackIndex.get() == 1);

    core::handler::SequencerStructureEditWorkflow edit({
        state.sequencer,
        state.sequencerTracks,
        navigationFocus,
        state.trackNavigation,
        state.projectNavigation,
        state.projectTracks,
        core::state::project::ProjectTrackDomainServices::fromCoreState(state),
        state.structureClipboard,
        sharedTracks,
        core::handler::SequencerHistoryDomainServices{},
        state.pages,
        &state.sequencerTrackActivations,
        &state.statusBar,
    });
    const auto result = edit.createPreviewedTrackStructure();

    assert(
        result.status == core::handler::
            SequencerPreparedTrackStructureStatus::HistoryUnavailable);
    assert(state.sequencerTracks.currentEnabledMask() == 0x0001);
    assert(state.sequencerTracks.activeTrackIndex() == 0);
    assert(state.sharedTrackEnabledMask.get() == 0x0001);
    assert(state.sharedTrackActive.get() == 0);
    assert(state.sequencerTracks.track(1).note[0] == 77);
    assert(state.sequencerTracks.track(1).isEnabled(0));
    assert(state.trackNavigation.previewAddSlot.get());
    assert(state.sequencerHistory.undoCount() == 0);

    std::cout
        << "[PASS] Track creation HistoryUnavailable is atomic and keeps add preview\n";
}

void test_step_selection_copy_paste_extends_sparse_root_steps() {
    SequencerStepHarness h;
    h.state.sequencer.pattern.setContentLength(8);
    h.state.sequencer.page.set(0);
    h.state.sequencer.focusedStep.set(0);
    h.navigationFocus.set(core::state::StructureNavigationFocus::STEP);

    h.state.sequencer.pattern.note[1] = 65;
    h.state.sequencer.pattern.velocity[1] = 91;
    h.state.sequencer.pattern.gate[1] = 130;
    h.state.sequencer.pattern.nudge[1] = -2;
    h.state.sequencer.pattern.probability[1] = 76;
    h.state.sequencer.pattern.setEnabled(1, true);

    h.state.sequencer.pattern.note[3] = 70;
    h.state.sequencer.pattern.velocity[3] = 112;
    h.state.sequencer.pattern.gate[3] = 180;
    h.state.sequencer.pattern.nudge[3] = 4;
    h.state.sequencer.pattern.probability[3] = 64;
    h.state.sequencer.pattern.setEnabled(3, true);
    createRootMicroSequence(h, 3);
    auto selectedChord = oc::note::sequencer::StepSequencerChordSpec::semantic(
        oc::note::sequencer::StepSequencerChordHarmony::Custom, 8U,
        oc::note::sequencer::StepSequencerChordVoicing::Open, 1U,
        oc::note::sequencer::StepSequencerChordIntervalBasis::ChromaticSemitones);
    constexpr std::array<uint8_t, 8> intervals{
        0U, 3U, 5U, 8U, 12U, 17U, 24U, 31U,
    };
    for (uint8_t voice = 7U; voice > 0U; --voice) {
        selectedChord.setCustomInterval(voice, intervals[voice]);
    }
    assert(core::state::sequencer::setNodeChordSpec(
        h.state.sequencer.pattern, core::state::sequencer::rootStepNodeId(3), selectedChord));
    assert(seq::storeActiveTrack(h.state.sequencerTracks, h.state.sequencer));

    h.press(Config::ButtonID::NAV);
    h.advance(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    assert(h.state.sequencer.structureUi.stepSelection.active.get());
    assert(h.navigationFocus.get() == core::state::StructureNavigationFocus::STEP);
    h.release(Config::ButtonID::NAV);
    assert(h.state.sequencer.structureUi.stepSelection.active.get());

    h.tap(Config::MACRO_BUTTONS[1]);
    h.tap(Config::MACRO_BUTTONS[3]);
    assert(h.state.sequencer.structureUi.stepSelection.selected(1));
    assert(h.state.sequencer.structureUi.stepSelection.selected(3));

    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.release(Config::ButtonID::BOTTOM_RIGHT);
    assert(h.state.structureClipboard.hasSequencerSteps());
    assert(h.state.structureClipboard.sequencerSteps.rootContext);
    assert(h.state.structureClipboard.sequencerSteps.count == 2);
    assert(h.state.structureClipboard.sequencerSteps.span == 3);
    assert(h.state.sequencer.structureUi.stepSelection.active.get());
    assert(h.state.sequencer.structureUi.stepSelection.placementActive());

    h.turn(Config::EncoderID::NAV, 1.0f);
    h.turn(Config::EncoderID::NAV, 1.0f);
    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.sequencer.structureUi.stepSelection.cursorStep.get() == 6);
    const uint8_t prePastePage = h.state.sequencer.page.get();
    const uint8_t prePasteFocus = h.state.sequencer.focusedStep.get();

    h.press(Config::ButtonID::BOTTOM_RIGHT);
    assert(h.state.sequencer.structureUi.pageHold.action.get() ==
           core::state::StructureHoldAction::PASTE);
    h.advance(0);
    assert(h.state.sequencer.structureUi.stepSelection.pastePreviewActive.get());
    assert(h.state.sequencer.structureUi.stepSelection.pastePreview.get() ==
           core::state::sequencer::SequencerStepPastePreview::GHOST);
    h.advance(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    h.release(Config::ButtonID::BOTTOM_RIGHT);

    assert(h.state.sequencer.structureUi.stepSelection.active.get());
    assert(h.state.sequencer.structureUi.stepSelection.placementActive());
    assert(h.state.sequencer.structureUi.pageHold.action.get() ==
           core::state::StructureHoldAction::NONE);
    assert(h.state.sequencer.pattern.length.get() == 9);
    assert(h.state.sequencer.focusedStep.get() == 6);
    assert(h.state.sequencer.pattern.note[6] == 65);
    assert(h.state.sequencer.pattern.velocity[6] == 91);
    assert(h.state.sequencer.pattern.gate[6] == 130);
    assert(h.state.sequencer.pattern.nudge[6] == -2);
    assert(h.state.sequencer.pattern.probability[6] == 76);
    assert(h.state.sequencer.pattern.isEnabled(6));
    assert(h.state.sequencer.pattern.note[8] == 70);
    assert(h.state.sequencer.pattern.velocity[8] == 112);
    assert(h.state.sequencer.pattern.gate[8] == 180);
    assert(h.state.sequencer.pattern.nudge[8] == 4);
    assert(h.state.sequencer.pattern.probability[8] == 64);
    assert(h.state.sequencer.pattern.isEnabled(8));
    assert(rootStepHasMicroSequence(h, 8));
    const auto* pastedChordNode = rootStepNode(h, 8);
    assert(pastedChordNode != nullptr);
    assert(pastedChordNode->has(oc::note::sequencer::STEP_NODE_CHORD_MODE));
    assert(pastedChordNode->chordMode == oc::note::sequencer::StepSequencerChordMode::Local);
    assert(oc::note::sequencer::chordSpecsEqual(pastedChordNode->chordSpec, selectedChord));
    assert(h.state.sequencerHistory.undoCount() == 1U);
    assert(h.state.sequencerHistory.undoCount(seq::SequencerHistoryScope::PatternOnly) == 1U);

    assert(h.state.undoSequencerHistory());
    assert(h.state.sequencer.pattern.length.get() == 8U);
    assert(h.state.sequencer.focusedStep.get() == prePasteFocus);
    assert(h.state.sequencer.page.get() == prePastePage);
    assert(!h.state.sequencer.pattern.isEnabled(6U));
    assert(h.state.sequencer.pattern.isEnabled(1U));
    assert(h.state.sequencer.pattern.isEnabled(3U));
    assert(rootStepHasMicroSequence(h, 3U));
    assert(h.state.redoSequencerHistory());
    assert(h.state.sequencer.pattern.length.get() == 9U);
    assert(h.state.sequencer.focusedStep.get() == 6U);
    assert(h.state.sequencer.page.get() == 0U);
    assert(h.state.sequencer.pattern.note[6U] == 65U);
    assert(h.state.sequencer.pattern.note[8U] == 70U);
    assert(rootStepHasMicroSequence(h, 8U));

    h.tap(Config::ButtonID::LEFT_TOP);
    assert(h.state.sequencer.structureUi.stepSelection.active.get());
    assert(!h.state.sequencer.structureUi.stepSelection.placementActive());
    assert(!h.state.sequencer.structureUi.stepSelection.anySelected());
    h.tap(Config::ButtonID::LEFT_TOP);
    assert(!h.state.sequencer.structureUi.stepSelection.active.get());

    std::cout << "[PASS] test_step_selection_copy_paste_extends_sparse_root_steps\n";
}

void test_step_selection_macro_long_press_consumes_release_without_toggling() {
    SequencerStepHarness h;
    h.state.sequencer.pattern.setContentLength(8);
    h.navigationFocus.set(core::state::StructureNavigationFocus::STEP);
    h.state.sequencer.structureUi.stepSelection.active.set(true);
    h.state.sequencer.structureUi.stepSelection.cursorStep.set(2);
    h.state.sequencer.structureUi.stepSelection.setSelected(2, true);
    h.state.sequencer.structureUi.stepSelection.setSelected(4, true);

    h.press(Config::MACRO_BUTTONS[2]);
    h.tick(0);
    h.tick(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    h.release(Config::MACRO_BUTTONS[2]);

    assert(h.state.sequencer.structureUi.stepSelection.active.get());
    assert(h.state.sequencer.structureUi.stepSelection.selected(2));
    assert(h.state.sequencer.structureUi.stepSelection.selected(4));

    h.tap(Config::MACRO_BUTTONS[2]);
    assert(!h.state.sequencer.structureUi.stepSelection.selected(2));
    assert(h.state.sequencer.structureUi.stepSelection.selected(4));

    std::cout << "[PASS] test_step_selection_macro_long_press_consumes_release_without_toggling\n";
}

void test_macro_press_on_future_page_does_not_wrap_to_existing_step() {
    SequencerStepHarness h;
    h.state.sequencer.pattern.setContentLength(8);
    h.state.sequencer.page.set(1);
    h.state.sequencer.focusedStep.set(0);
    h.navigationFocus.set(core::state::StructureNavigationFocus::PAGE);

    h.tap(Config::MACRO_BUTTONS[0]);

    assert(!h.state.sequencer.pattern.isEnabled(0));
    assert(h.state.sequencer.focusedStep.get() == 0);
    assert(h.state.sequencerHistory.undoCount() == 0);

    std::cout << "[PASS] test_macro_press_on_future_page_does_not_wrap_to_existing_step\n";
}

void test_step_focus_bottom_left_resets_focused_step_only() {
    SequencerStepHarness h;
    h.state.sequencer.pattern.setContentLength(16);
    h.state.sequencer.page.set(0);
    h.state.sequencer.focusedStep.set(3);
    h.navigationFocus.set(core::state::StructureNavigationFocus::STEP);

    h.state.sequencer.pattern.note[3] = 74;
    h.state.sequencer.pattern.velocity[3] = 105;
    h.state.sequencer.pattern.setEnabled(3, true);
    createRootMicroSequence(h, 3);
    oc::note::sequencer::StepSequencerChordSpec chord{};
    chord.voiceCount = 6;
    assert(core::state::sequencer::setNodeChordSpec(
        h.state.sequencer.pattern, core::state::sequencer::rootStepNodeId(3), chord));
    h.state.sequencer.pattern.note[8] = 81;
    h.state.sequencer.pattern.setEnabled(8, true);
    assert(seq::storeActiveTrack(h.state.sequencerTracks, h.state.sequencer));

    const uint8_t undoBefore = h.state.sequencerHistory.undoCount();
    h.press(Config::ButtonID::BOTTOM_LEFT);
    h.release(Config::ButtonID::BOTTOM_LEFT);

    assert(h.state.sequencer.pattern.length.get() == 16);
    assert(h.state.sequencer.focusedStep.get() == 3);
    assert(h.state.sequencer.page.get() == 0);
    assert(!h.state.sequencer.pattern.isEnabled(3));
    assert(h.state.sequencer.pattern.note[3] ==
           core::state::sequencer::SequencerState::DEFAULT_NOTE);
    assert(rootStepHasMicroSequence(h, 3));
    const auto* shallowResetNode = rootStepNode(h, 3);
    assert(shallowResetNode != nullptr);
    assert(!shallowResetNode->has(oc::note::sequencer::STEP_NODE_CHORD_MODE));
    assert(h.state.sequencer.pattern.isEnabled(8));
    assert(h.state.sequencer.pattern.note[8] == 81);
    assert(h.state.sequencerHistory.undoCount() == undoBefore + 1U);

    assert(h.state.undoSequencerHistory());
    assert(h.state.sequencer.pattern.isEnabled(3U));
    assert(h.state.sequencer.pattern.note[3U] == 74U);
    assert(rootStepHasMicroSequence(h, 3U));
    const auto* restoredNode = rootStepNode(h, 3U);
    assert(restoredNode != nullptr);
    assert(restoredNode->has(oc::note::sequencer::STEP_NODE_CHORD_MODE));
    assert(h.state.sequencer.focusedStep.get() == 3U);
    assert(h.state.sequencer.page.get() == 0U);
    assert(h.state.redoSequencerHistory());
    assert(!h.state.sequencer.pattern.isEnabled(3U));
    assert(rootStepHasMicroSequence(h, 3U));
    assert(!rootStepNode(h, 3U)->has(oc::note::sequencer::STEP_NODE_CHORD_MODE));
    assert(h.state.undoSequencerHistory());
    assert(h.state.sequencer.pattern.isEnabled(3U));
    assert(rootStepHasMicroSequence(h, 3U));

    h.press(Config::ButtonID::BOTTOM_LEFT);
    h.advance(0);
    h.advance(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    h.release(Config::ButtonID::BOTTOM_LEFT);

    assert(h.state.sequencer.pattern.length.get() == 16);
    assert(h.state.sequencer.focusedStep.get() == 3);
    assert(h.state.sequencer.page.get() == 0);
    assert(!rootStepHasMicroSequence(h, 3));
    assert(h.state.sequencerHistory.undoCount() == undoBefore + 1U);

    assert(h.state.undoSequencerHistory());
    assert(h.state.sequencer.pattern.isEnabled(3U));
    assert(h.state.sequencer.pattern.note[3U] == 74U);
    assert(rootStepHasMicroSequence(h, 3U));
    assert(h.state.sequencer.focusedStep.get() == 3U);
    assert(h.state.redoSequencerHistory());
    assert(!h.state.sequencer.pattern.isEnabled(3U));
    assert(!rootStepHasMicroSequence(h, 3U));
    assert(h.state.sequencer.focusedStep.get() == 3U);

    std::cout << "[PASS] test_step_focus_bottom_left_resets_focused_step_only\n";
}

void test_step_focus_copy_paste_copies_complete_step_without_selection() {
    SequencerStepHarness h;
    h.state.sequencer.pattern.setContentLength(8);
    h.state.sequencer.page.set(0);
    h.state.sequencer.focusedStep.set(1);
    h.navigationFocus.set(core::state::StructureNavigationFocus::STEP);

    h.state.sequencer.pattern.note[1] = 76;
    h.state.sequencer.pattern.velocity[1] = 112;
    h.state.sequencer.pattern.gate[1] = 180;
    h.state.sequencer.pattern.nudge[1] = 3;
    h.state.sequencer.pattern.setEnabled(1, true);
    createRootMicroSequence(h, 1);
    oc::note::sequencer::StepSequencerChordSpec chord{};
    chord.voiceCount = 7;
    assert(core::state::sequencer::setNodeChordSpec(
        h.state.sequencer.pattern, core::state::sequencer::rootStepNodeId(1), chord));
    assert(seq::storeActiveTrack(h.state.sequencerTracks, h.state.sequencer));

    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.release(Config::ButtonID::BOTTOM_RIGHT);

    assert(h.state.structureClipboard.hasSequencerSteps());
    assert(h.state.structureClipboard.sequencerSteps.rootContext);
    assert(h.state.structureClipboard.sequencerSteps.count == 1);
    assert(!h.state.sequencer.structureUi.stepSelection.active.get());

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.sequencer.focusedStep.get() == 2);
    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.advance(0);
    h.advance(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    h.release(Config::ButtonID::BOTTOM_RIGHT);

    assert(!h.state.sequencer.structureUi.stepSelection.active.get());
    assert(h.state.sequencer.focusedStep.get() == 2);
    assert(h.state.sequencer.pattern.isEnabled(2));
    assert(h.state.sequencer.pattern.note[2] == 76);
    assert(h.state.sequencer.pattern.velocity[2] == 112);
    assert(h.state.sequencer.pattern.gate[2] == 180);
    assert(h.state.sequencer.pattern.nudge[2] == 3);
    assert(rootStepHasMicroSequence(h, 2));
    const auto* pastedNode = rootStepNode(h, 2);
    assert(pastedNode != nullptr);
    assert(pastedNode->has(oc::note::sequencer::STEP_NODE_CHORD_MODE));
    assert(pastedNode->chordMode == oc::note::sequencer::StepSequencerChordMode::Local);
    assert(pastedNode->chordSpec.voiceCount == 7);
    assert(h.state.sequencerHistory.undoCount() == 1U);
    assert(h.state.sequencerHistory.undoCount(seq::SequencerHistoryScope::PatternOnly) == 1U);

    assert(h.state.undoSequencerHistory());
    assert(h.state.sequencer.focusedStep.get() == 2U);
    assert(h.state.sequencer.page.get() == 0U);
    assert(!h.state.sequencer.pattern.isEnabled(2U));
    assert(!rootStepHasMicroSequence(h, 2U));
    assert(h.state.redoSequencerHistory());
    assert(h.state.sequencer.focusedStep.get() == 2U);
    assert(h.state.sequencer.pattern.note[2U] == 76U);
    assert(rootStepHasMicroSequence(h, 2U));

    const auto historyAfterCommit = h.state.sequencerHistory.undoCount();
    const auto clipboardRevision = h.state.structureClipboard.revision.get();
    {
        core::app::testing::ScopedExtmemAllocationFailure failure(1U);
        h.press(Config::ButtonID::BOTTOM_RIGHT);
        h.advance(0U);
        h.advance(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
        assert(core::app::testing::extmemAllocationAttempt == 0U);
        assert(core::app::testing::extmemAllocationFailureOrdinal == 1U);
    }
    h.release(Config::ButtonID::BOTTOM_RIGHT);
    assert(h.state.sequencerHistory.undoCount() == historyAfterCommit);
    assert(h.state.structureClipboard.revision.get() == clipboardRevision);
    assert(h.state.sequencer.pattern.note[2U] == 76U);
    assert(rootStepHasMicroSequence(h, 2U));

    std::cout << "[PASS] test_step_focus_copy_paste_copies_complete_step_without_selection\n";
}

void test_step_selection_clear_is_undoable_and_keeps_selection_active() {
    SequencerStepHarness h;
    h.state.sequencer.pattern.setContentLength(8);
    h.navigationFocus.set(core::state::StructureNavigationFocus::STEP);
    h.state.sequencer.pattern.note[2] = 74;
    h.state.sequencer.pattern.velocity[2] = 105;
    h.state.sequencer.pattern.setEnabled(2, true);
    createRootMicroSequence(h, 2);
    assert(core::state::sequencer::storeActiveTrack(
        h.state.sequencerTracks, h.state.sequencer));

    h.state.sequencer.structureUi.stepSelection.active.set(true);
    h.state.sequencer.structureUi.stepSelection.cursorStep.set(2);
    h.state.sequencer.structureUi.stepSelection.setSelected(2, true);
    const uint8_t undoBefore = h.state.sequencerHistory.undoCount();

    h.press(Config::ButtonID::BOTTOM_LEFT);
    h.release(Config::ButtonID::BOTTOM_LEFT);
    assert(h.state.sequencer.structureUi.stepSelection.active.get());
    assert(!h.state.sequencer.pattern.isEnabled(2));
    assert(h.state.sequencer.pattern.note[2] ==
           core::state::sequencer::SequencerState::DEFAULT_NOTE);
    assert(rootStepHasMicroSequence(h, 2));
    assert(h.state.sequencerHistory.undoCount() == undoBefore + 1U);

    assert(h.state.undoSequencerHistory());
    assert(h.state.sequencer.pattern.isEnabled(2));
    assert(h.state.sequencer.pattern.note[2] == 74);
    assert(h.state.sequencer.pattern.velocity[2] == 105);
    assert(rootStepHasMicroSequence(h, 2));

    assert(h.state.redoSequencerHistory());
    assert(!h.state.sequencer.pattern.isEnabled(2U));
    assert(h.state.sequencer.pattern.note[2U] == seq::SequencerState::DEFAULT_NOTE);
    assert(rootStepHasMicroSequence(h, 2U));
    assert(h.state.undoSequencerHistory());
    assert(h.state.sequencer.pattern.isEnabled(2U));
    assert(rootStepHasMicroSequence(h, 2U));

    const uint8_t undoBeforeDeepReset = h.state.sequencerHistory.undoCount();

    h.press(Config::ButtonID::BOTTOM_LEFT);
    h.advance(0);
    h.advance(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    h.release(Config::ButtonID::BOTTOM_LEFT);

    assert(h.state.sequencer.structureUi.stepSelection.active.get());
    assert(!h.state.sequencer.pattern.isEnabled(2));
    assert(h.state.sequencer.pattern.note[2] ==
           core::state::sequencer::SequencerState::DEFAULT_NOTE);
    assert(!rootStepHasMicroSequence(h, 2));
    assert(h.state.sequencerHistory.undoCount() == undoBeforeDeepReset + 1U);

    assert(h.state.undoSequencerHistory());
    assert(h.state.sequencer.pattern.isEnabled(2));
    assert(h.state.sequencer.pattern.note[2] == 74);
    assert(h.state.sequencer.pattern.velocity[2] == 105);
    assert(rootStepHasMicroSequence(h, 2));

    assert(h.state.redoSequencerHistory());
    assert(!h.state.sequencer.pattern.isEnabled(2U));
    assert(!rootStepHasMicroSequence(h, 2U));

    std::cout << "[PASS] test_step_selection_clear_is_undoable_and_keeps_selection_active\n";
}

void test_step_selection_wrap_paste_overwrites_inside_pattern() {
    SequencerStepHarness h;
    h.state.sequencer.pattern.setContentLength(8);
    h.navigationFocus.set(core::state::StructureNavigationFocus::STEP);
    h.state.projectNavigation.stepPasteMode = core::state::project::ProjectStepPasteMode::WRAP;

    h.state.sequencer.pattern.note[1] = 61;
    h.state.sequencer.pattern.note[3] = 63;
    h.state.sequencer.pattern.setEnabled(1, true);
    h.state.sequencer.pattern.setEnabled(3, true);
    h.state.sequencer.pattern.note[7] = 79;
    h.state.sequencer.pattern.setEnabled(7, true);

    h.state.sequencer.structureUi.stepSelection.active.set(true);
    h.state.sequencer.structureUi.stepSelection.cursorStep.set(1);
    h.state.sequencer.structureUi.stepSelection.setSelected(1, true);
    h.state.sequencer.structureUi.stepSelection.setSelected(3, true);

    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.release(Config::ButtonID::BOTTOM_RIGHT);
    assert(h.state.structureClipboard.hasSequencerSteps());
    assert(h.state.sequencer.structureUi.stepSelection.placementActive());

    h.state.sequencer.structureUi.stepSelection.cursorStep.set(7);
    h.state.sequencer.focusedStep.set(7);
    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.advance(0);
    assert(h.state.sequencer.structureUi.stepSelection.pastePreviewActive.get());
    assert(h.state.sequencer.structureUi.stepSelection.pastePreview.get() ==
           core::state::sequencer::SequencerStepPastePreview::OVERWRITE);
    h.advance(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    h.release(Config::ButtonID::BOTTOM_RIGHT);

    assert(h.state.sequencer.pattern.length.get() == 8);
    assert(h.state.sequencer.pattern.note[7] == 61);
    assert(h.state.sequencer.pattern.isEnabled(7));
    assert(h.state.sequencer.pattern.note[1] == 63);
    assert(h.state.sequencer.pattern.isEnabled(1));
    assert(h.state.sequencer.structureUi.stepSelection.placementActive());

    std::cout << "[PASS] test_step_selection_wrap_paste_overwrites_inside_pattern\n";
}

void test_child_content_nav_enters_step_selection_and_pastes_child_steps() {
    SequencerStepHarness h;
    const auto rootNode = core::state::sequencer::rootStepNodeId(0);
    const auto micro =
        core::state::sequencer::createMicroSequence(h.state.sequencer.pattern, rootNode, 2);
    assert(micro.ok);
    assert(core::state::sequencer::enterMicroSequenceContentView(h.state.sequencer, rootNode,
                                                                 micro.id));
    h.navigationFocus.set(core::state::StructureNavigationFocus::STEP);
    h.state.sequencer.focusedStep.set(0);

    const auto childNode0 = core::state::sequencer::activeContentStepNodeId(h.state.sequencer, 0);
    assert(core::state::sequencer::setNodeNoteOffset(h.state.sequencer.pattern, childNode0, 4));
    const auto cycle =
        core::state::sequencer::createCycleStateSet(h.state.sequencer.pattern, childNode0, 2);
    assert(cycle.ok);

    h.handler.enterSelectionModeForCurrentFocus();
    assert(h.state.sequencer.structureUi.stepSelection.active.get());
    assert(h.navigationFocus.get() == core::state::StructureNavigationFocus::STEP);

    h.tap(Config::MACRO_BUTTONS[0]);
    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.release(Config::ButtonID::BOTTOM_RIGHT);
    assert(h.state.structureClipboard.hasSequencerSteps());
    assert(!h.state.structureClipboard.sequencerSteps.rootContext);

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.sequencer.structureUi.stepSelection.cursorStep.get() == 1);
    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.advance(0);
    h.advance(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    h.release(Config::ButtonID::BOTTOM_RIGHT);

    const auto childNode1 = core::state::sequencer::activeContentStepNodeId(h.state.sequencer, 1);
    const auto* graph = core::state::sequencer::graphView(h.state.sequencer.pattern);
    assert(graph != nullptr);
    assert(graph->stepNodes[childNode1].noteOffset == 4);
    assert(graph->stepNodes[childNode1].has(oc::note::sequencer::STEP_NODE_CYCLE_SET));

    std::cout << "[PASS] test_child_content_nav_enters_step_selection_and_pastes_child_steps\n";
}

void test_child_draft_owns_main_bottom_actions_until_single_apply() {
    SequencerStepHarness h;
    h.state.sequencer.pattern.setContentLength(8);

    const auto sourceRoot = core::state::sequencer::rootStepNodeId(0);
    const auto source =
        core::state::sequencer::createMicroSequence(h.state.sequencer.pattern, sourceRoot, 2);
    assert(source.ok);
    assert(core::state::sequencer::enterMicroSequenceContentView(h.state.sequencer, sourceRoot,
                                                                 source.id));
    h.navigationFocus.set(core::state::StructureNavigationFocus::STEP);
    const auto sourceNode = core::state::sequencer::activeContentStepNodeId(h.state.sequencer, 0);
    assert(core::state::sequencer::setNodeNoteOffset(h.state.sequencer.pattern, sourceNode, 9));
    h.tap(Config::ButtonID::BOTTOM_RIGHT);
    assert(h.state.structureClipboard.hasSequencerSteps());
    const uint32_t sourceClipboardRevision = h.state.structureClipboard.revision.get();
    h.tap(Config::ButtonID::LEFT_TOP);
    assert(core::state::sequencer::isRootContentView(h.state.sequencer));

    const auto opened = core::state::sequencer::openOrCreateActiveContentChild(
        h.state.sequencer, 3, core::state::sequencer::StepContentChildKind::MICRO_SEQUENCE,
        core::state::sequencer::DEFAULT_MICRO_SEQUENCE_LENGTH);
    assert(opened.opened && opened.draft);
    h.navigationFocus.set(core::state::StructureNavigationFocus::STEP);
    h.state.sequencer.focusedStep.set(0);
    const auto draftNode = core::state::sequencer::activeContentStepNodeId(h.state.sequencer, 0);
    assert(core::state::sequencer::setNodeNoteOffset(
        core::state::sequencer::authoringPattern(h.state.sequencer), draftNode, 3));
    core::state::sequencer::notifyStepContentDraftMutation(h.state.sequencer);
    assert(h.state.sequencer.stepContentDraft.modified());
    assert(h.state.sequencerHistory.undoCount() == 0);

    // Hidden Reset/Remove actions must not touch the unpublished draft.
    h.tap(Config::ButtonID::BOTTOM_LEFT);
    h.press(Config::ButtonID::BOTTOM_LEFT);
    h.advance(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    h.release(Config::ButtonID::BOTTOM_LEFT);
    const auto* draftGraph =
        core::state::sequencer::authoringPattern(h.state.sequencer).graph.get();
    assert(draftGraph != nullptr);
    assert(draftGraph->stepNodes[draftNode].noteOffset == 3);
    assert(h.state.sequencer.stepContentDraft.active.get());
    assert(!rootStepHasMicroSequence(h, 3));
    assert(h.state.sequencerHistory.undoCount() == 0);

    // A held Apply may not begin or execute the compatible hidden Paste. The
    // release publishes the draft exactly once and keeps its local value.
    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.advance(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    assert(h.state.sequencer.stepContentDraft.active.get());
    assert(core::state::sequencer::authoringPattern(h.state.sequencer)
               .graph->stepNodes[draftNode]
               .noteOffset == 3);
    h.release(Config::ButtonID::BOTTOM_RIGHT);

    assert(!h.state.sequencer.stepContentDraft.active.get());
    assert(rootStepHasMicroSequence(h, 3));
    assert(h.state.sequencerHistory.undoCount() == 1);
    const auto publishedNode =
        core::state::sequencer::activeContentStepNodeId(h.state.sequencer, 0);
    const auto* publishedGraph = core::state::sequencer::graphView(h.state.sequencer.pattern);
    assert(publishedGraph != nullptr);
    assert(publishedGraph->stepNodes[publishedNode].noteOffset == 3);
    assert(h.state.structureClipboard.revision.get() == sourceClipboardRevision);

    // Apply consumes only its physical release; the next ordinary Copy works.
    h.tap(Config::ButtonID::BOTTOM_RIGHT);
    assert(h.state.structureClipboard.revision.get() == sourceClipboardRevision + 1U);
    assert(h.state.sequencerHistory.undoCount() == 1);

    std::cout << "[PASS] test_child_draft_owns_main_bottom_actions_until_single_apply\n";
}

void test_child_step_focus_bottom_actions_use_local_step_payload() {
    SequencerStepHarness h;
    const auto rootNode = core::state::sequencer::rootStepNodeId(0);
    const auto micro =
        core::state::sequencer::createMicroSequence(h.state.sequencer.pattern, rootNode, 2);
    assert(micro.ok);
    assert(core::state::sequencer::enterMicroSequenceContentView(h.state.sequencer, rootNode,
                                                                 micro.id));
    h.navigationFocus.set(core::state::StructureNavigationFocus::STEP);
    h.state.sequencer.focusedStep.set(0);

    auto childNode0 = core::state::sequencer::activeContentStepNodeId(h.state.sequencer, 0);
    assert(core::state::sequencer::setNodeNoteOffset(h.state.sequencer.pattern, childNode0, 4));
    assert(
        core::state::sequencer::createCycleStateSet(h.state.sequencer.pattern, childNode0, 2).ok);

    h.tap(Config::ButtonID::BOTTOM_LEFT);
    const auto* graph = core::state::sequencer::graphView(h.state.sequencer.pattern);
    assert(graph != nullptr);
    assert(graph->stepNodes[childNode0].noteOffset == 0);
    assert(!graph->stepNodes[childNode0].has(oc::note::sequencer::STEP_NODE_NOTE_OFFSET));
    assert(nodeHasCycleStates(h, childNode0));

    assert(core::state::sequencer::setNodeNoteOffset(h.state.sequencer.pattern, childNode0, 5));
    h.tap(Config::ButtonID::BOTTOM_RIGHT);
    assert(h.state.structureClipboard.hasSequencerSteps());
    assert(!h.state.structureClipboard.sequencerSteps.rootContext);

    h.turn(Config::EncoderID::NAV, 1.0f);
    assert(h.state.sequencer.focusedStep.get() == 1);
    h.press(Config::ButtonID::BOTTOM_RIGHT);
    h.advance(0);
    h.advance(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    h.release(Config::ButtonID::BOTTOM_RIGHT);

    const auto childNode1 = core::state::sequencer::activeContentStepNodeId(h.state.sequencer, 1);
    graph = core::state::sequencer::graphView(h.state.sequencer.pattern);
    assert(graph != nullptr);
    assert(graph->stepNodes[childNode1].noteOffset == 5);
    assert(graph->stepNodes[childNode1].has(oc::note::sequencer::STEP_NODE_NOTE_OFFSET));
    assert(nodeHasCycleStates(h, childNode1));

    h.press(Config::ButtonID::BOTTOM_LEFT);
    h.advance(0);
    h.advance(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    h.release(Config::ButtonID::BOTTOM_LEFT);

    graph = core::state::sequencer::graphView(h.state.sequencer.pattern);
    assert(graph != nullptr);
    assert(!graph->stepNodes[childNode1].has(oc::note::sequencer::STEP_NODE_NOTE_OFFSET));
    assert(!nodeHasCycleStates(h, childNode1));

    std::cout << "[PASS] test_child_step_focus_bottom_actions_use_local_step_payload\n";
}

void test_child_page_selection_reset_shallow_commits_pattern_only_and_replays() {
    SequencerStepHarness h;
    auto& sequencer = h.state.sequencer;
    const auto rootNode = seq::rootStepNodeId(0U);
    const auto micro = seq::createMicroSequence(sequencer.pattern, rootNode, 2U);
    assert(micro.ok);
    assert(seq::enterMicroSequenceContentView(sequencer, rootNode, micro.id));
    configureActiveContentCycleDescendants(h, 0U, 7, 11, -4);

    sequencer.page.set(0U);
    sequencer.focusedStep.set(1U);
    h.navigationFocus.set(core::state::StructureNavigationFocus::PAGE);
    auto& selection = sequencer.structureUi.pageSelection;
    selection.active.set(true);
    selection.scope.set(core::state::StructureSelectionScope::PAGE);
    selection.cursorIndex.set(0U);
    selection.selectedMask.set(0x0001U);
    assert(seq::storeActiveTrack(h.state.sequencerTracks, sequencer));

    auto workflow = makeStructureEditWorkflow(
        h, HistoryServices::fromCoreState(h.state));
    h.tick(83U);
    workflow.beginHoldAction(core::state::StructureHoldAction::REMOVE);
    const auto uiBefore = capturePreparedEditorUiInvariant(h);
    workflow.applySelectionBottomLeftTap();
    test_support::drainNotifications();

    auto activeNode = seq::activeContentStepNodeId(sequencer, 0U);
    const auto* graph = seq::graphView(sequencer.pattern);
    assert(graph != nullptr);
    const auto* resetNode = graph->stepNode(activeNode);
    assert(resetNode != nullptr);
    assert(!resetNode->has(oc::note::sequencer::STEP_NODE_NOTE_OFFSET));
    assert(resetNode->noteOffset == 0);
    assertActiveContentCycleDescendants(h, 0U, 11, -4);
    assertPageSelectionNavigationInvariant(h, uiBefore);
    assert(core::state::sequencer::isMicroSequenceContentView(sequencer));
    assert(sequencer.contentView.stackDepth == 1U);
    assert(sequencer.page.get() == 0U);
    assert(sequencer.focusedStep.get() == 1U);
    assert(h.navigationFocus.get() == core::state::StructureNavigationFocus::PAGE);
    assert(selection.active.get());
    assert(!selection.placing.get());
    assert(selection.scope.get() == core::state::StructureSelectionScope::PAGE);
    assert(selection.cursorIndex.get() == 0U);
    assert(selection.selectedMask.get() == 0x0001U);
    assert(sequencer.structureUi.pageHold.action.get() ==
           core::state::StructureHoldAction::NONE);
    assert(h.state.sequencerHistory.undoCount() == 1U);
    assert(h.state.sequencerHistory.undoCount(seq::SequencerHistoryScope::PatternOnly) == 1U);
    assert(h.state.sequencerHistory.undoCount(seq::SequencerHistoryScope::Structure) == 0U);
    assert(h.state.sequencerHistory.undoCount(seq::SequencerHistoryScope::FullBank) == 0U);

    assert(h.state.undoSequencerHistory());
    test_support::drainNotifications();
    activeNode = seq::activeContentStepNodeId(sequencer, 0U);
    graph = seq::graphView(sequencer.pattern);
    assert(graph != nullptr);
    const auto* restoredNode = graph->stepNode(activeNode);
    assert(restoredNode != nullptr);
    assert(restoredNode->has(oc::note::sequencer::STEP_NODE_NOTE_OFFSET));
    assert(restoredNode->noteOffset == 7);
    assertActiveContentCycleDescendants(h, 0U, 11, -4);
    assertPageSelectionNavigationInvariant(h, uiBefore);
    assert(core::state::sequencer::isMicroSequenceContentView(sequencer));
    assert(sequencer.page.get() == 0U);
    assert(sequencer.focusedStep.get() == 1U);
    assert(h.navigationFocus.get() == core::state::StructureNavigationFocus::PAGE);
    assert(selection.active.get());
    assert(!selection.placing.get());
    assert(selection.cursorIndex.get() == 0U);
    assert(selection.selectedMask.get() == 0x0001U);
    assert(h.state.sequencerHistory.redoCount() == 1U);

    assert(h.state.redoSequencerHistory());
    test_support::drainNotifications();
    activeNode = seq::activeContentStepNodeId(sequencer, 0U);
    graph = seq::graphView(sequencer.pattern);
    assert(graph != nullptr);
    resetNode = graph->stepNode(activeNode);
    assert(resetNode != nullptr);
    assert(!resetNode->has(oc::note::sequencer::STEP_NODE_NOTE_OFFSET));
    assert(resetNode->noteOffset == 0);
    assertActiveContentCycleDescendants(h, 0U, 11, -4);
    assertPageSelectionNavigationInvariant(h, uiBefore);
    assert(core::state::sequencer::isMicroSequenceContentView(sequencer));
    assert(sequencer.page.get() == 0U);
    assert(sequencer.focusedStep.get() == 1U);
    assert(h.navigationFocus.get() == core::state::StructureNavigationFocus::PAGE);
    assert(selection.active.get());
    assert(!selection.placing.get());
    assert(selection.cursorIndex.get() == 0U);
    assert(selection.selectedMask.get() == 0x0001U);
    assert(sequencer.structureUi.pageHold.action.get() ==
           core::state::StructureHoldAction::NONE);

    std::cout << "[PASS] child PageSelectionReset shallow preserves descendants and replays\n";
}

void test_child_page_selection_deep_reset_removes_descendants_and_replays() {
    SequencerStepHarness h;
    auto& sequencer = h.state.sequencer;
    const auto rootNode = seq::rootStepNodeId(0U);
    const auto micro = seq::createMicroSequence(sequencer.pattern, rootNode, 2U);
    assert(micro.ok);
    assert(seq::enterMicroSequenceContentView(sequencer, rootNode, micro.id));
    configureActiveContentCycleDescendants(h, 0U, 9, 12, -5);

    sequencer.page.set(0U);
    sequencer.focusedStep.set(1U);
    h.navigationFocus.set(core::state::StructureNavigationFocus::PAGE);
    auto& selection = sequencer.structureUi.pageSelection;
    selection.active.set(true);
    selection.scope.set(core::state::StructureSelectionScope::PAGE);
    selection.cursorIndex.set(0U);
    selection.selectedMask.set(0x0001U);
    assert(seq::storeActiveTrack(h.state.sequencerTracks, sequencer));

    auto workflow = makeStructureEditWorkflow(
        h, HistoryServices::fromCoreState(h.state));
    h.tick(89U);
    workflow.beginHoldAction(core::state::StructureHoldAction::REMOVE);
    const auto uiBefore = capturePreparedEditorUiInvariant(h);
    workflow.applySelectionBottomLeftHold();
    test_support::drainNotifications();

    auto activeNode = seq::activeContentStepNodeId(sequencer, 0U);
    auto* graph = sequencer.pattern.graph.get();
    assert(graph != nullptr);
    const auto* resetNode = graph->stepNode(activeNode);
    assert(resetNode != nullptr);
    assert(!resetNode->has(oc::note::sequencer::STEP_NODE_NOTE_OFFSET));
    assert(!resetNode->has(oc::note::sequencer::STEP_NODE_CYCLE_SET));
    assert(resetNode->noteOffset == 0);
    assertGraphHasNoOrphans(*graph);
    assertPageSelectionNavigationInvariant(h, uiBefore);
    assert(core::state::sequencer::isMicroSequenceContentView(sequencer));
    assert(sequencer.contentView.stackDepth == 1U);
    assert(sequencer.page.get() == 0U);
    assert(sequencer.focusedStep.get() == 1U);
    assert(h.navigationFocus.get() == core::state::StructureNavigationFocus::PAGE);
    assert(selection.active.get());
    assert(!selection.placing.get());
    assert(selection.scope.get() == core::state::StructureSelectionScope::PAGE);
    assert(selection.cursorIndex.get() == 0U);
    assert(selection.selectedMask.get() == 0x0001U);
    assert(sequencer.structureUi.pageHold.action.get() ==
           core::state::StructureHoldAction::NONE);
    assert(h.state.sequencerHistory.undoCount() == 1U);
    assert(h.state.sequencerHistory.undoCount(seq::SequencerHistoryScope::PatternOnly) == 1U);
    assert(h.state.sequencerHistory.undoCount(seq::SequencerHistoryScope::Structure) == 0U);
    assert(h.state.sequencerHistory.undoCount(seq::SequencerHistoryScope::FullBank) == 0U);

    assert(h.state.undoSequencerHistory());
    test_support::drainNotifications();
    activeNode = seq::activeContentStepNodeId(sequencer, 0U);
    graph = sequencer.pattern.graph.get();
    assert(graph != nullptr);
    const auto* restoredNode = graph->stepNode(activeNode);
    assert(restoredNode != nullptr);
    assert(restoredNode->has(oc::note::sequencer::STEP_NODE_NOTE_OFFSET));
    assert(restoredNode->noteOffset == 9);
    assertActiveContentCycleDescendants(h, 0U, 12, -5);
    assertGraphHasNoOrphans(*graph);
    assertPageSelectionNavigationInvariant(h, uiBefore);
    assert(core::state::sequencer::isMicroSequenceContentView(sequencer));
    assert(sequencer.page.get() == 0U);
    assert(sequencer.focusedStep.get() == 1U);
    assert(h.navigationFocus.get() == core::state::StructureNavigationFocus::PAGE);
    assert(selection.active.get());
    assert(!selection.placing.get());
    assert(selection.cursorIndex.get() == 0U);
    assert(selection.selectedMask.get() == 0x0001U);
    assert(h.state.sequencerHistory.redoCount() == 1U);

    assert(h.state.redoSequencerHistory());
    test_support::drainNotifications();
    activeNode = seq::activeContentStepNodeId(sequencer, 0U);
    graph = sequencer.pattern.graph.get();
    assert(graph != nullptr);
    resetNode = graph->stepNode(activeNode);
    assert(resetNode != nullptr);
    assert(!resetNode->has(oc::note::sequencer::STEP_NODE_NOTE_OFFSET));
    assert(!resetNode->has(oc::note::sequencer::STEP_NODE_CYCLE_SET));
    assert(resetNode->noteOffset == 0);
    assertGraphHasNoOrphans(*graph);
    assertPageSelectionNavigationInvariant(h, uiBefore);
    assert(core::state::sequencer::isMicroSequenceContentView(sequencer));
    assert(sequencer.page.get() == 0U);
    assert(sequencer.focusedStep.get() == 1U);
    assert(h.navigationFocus.get() == core::state::StructureNavigationFocus::PAGE);
    assert(selection.active.get());
    assert(!selection.placing.get());
    assert(selection.cursorIndex.get() == 0U);
    assert(selection.selectedMask.get() == 0x0001U);
    assert(sequencer.structureUi.pageHold.action.get() ==
           core::state::StructureHoldAction::NONE);

    std::cout << "[PASS] child PageSelection deep reset removes descendants and replays\n";
}

void test_prepared_step_page_nochange_paths_are_allocation_free() {
    {
        SequencerStepHarness h;
        auto& sequencer = h.state.sequencer;
        sequencer.pattern.setContentLength(8U);
        sequencer.pattern.note[1U] = 73U;
        sequencer.pattern.setEnabled(1U, true);
        sequencer.focusedStep.set(1U);
        h.navigationFocus.set(core::state::StructureNavigationFocus::STEP);
        assert(seq::storeActiveTrack(h.state.sequencerTracks, sequencer));
        auto workflow = makeStructureEditWorkflow(h, HistoryServices::fromCoreState(h.state));
        workflow.copyCurrentStructure();
        assert(h.state.structureClipboard.hasSequencerSteps());
        h.tick(17U);
        workflow.beginHoldAction(core::state::StructureHoldAction::PASTE);
        test_support::drainNotifications();

        auto expected = capturePreparedActionInvariant(h);
        expected.ui.holdAction = static_cast<uint8_t>(core::state::StructureHoldAction::NONE);
        expected.ui.holdStartedAtMs = 0U;
        {
            core::app::testing::ScopedExtmemAllocationFailure failure(1U);
            workflow.pasteCurrentStructure();
            assert(core::app::testing::extmemAllocationAttempt == 0U);
            assert(core::app::testing::extmemAllocationFailureOrdinal == 1U);
        }
        test_support::drainNotifications();
        assertPreparedActionInvariant(h, expected);
    }

    {
        SequencerStepHarness h;
        auto& sequencer = h.state.sequencer;
        sequencer.pattern.setContentLength(8U);
        sequencer.page.set(0U);
        sequencer.focusedStep.set(3U);
        h.navigationFocus.set(core::state::StructureNavigationFocus::STEP);
        assert(seq::storeActiveTrack(h.state.sequencerTracks, sequencer));
        auto workflow = makeStructureEditWorkflow(h, HistoryServices::fromCoreState(h.state));
        h.tick(19U);
        workflow.beginHoldAction(core::state::StructureHoldAction::REMOVE);
        test_support::drainNotifications();
        const auto expected = capturePreparedActionInvariant(h);
        {
            core::app::testing::ScopedExtmemAllocationFailure failure(1U);
            workflow.applyCurrentStructureShortPress();
            assert(core::app::testing::extmemAllocationAttempt == 0U);
            assert(core::app::testing::extmemAllocationFailureOrdinal == 1U);
        }
        test_support::drainNotifications();
        assertPreparedActionInvariant(h, expected);
    }

    {
        SequencerStepHarness h;
        auto& sequencer = h.state.sequencer;
        sequencer.pattern.setContentLength(8U);
        sequencer.focusedStep.set(2U);
        h.navigationFocus.set(core::state::StructureNavigationFocus::STEP);
        auto& selection = sequencer.structureUi.stepSelection;
        selection.active.set(true);
        selection.cursorStep.set(2U);
        selection.setSelected(2U, true);
        assert(seq::storeActiveTrack(h.state.sequencerTracks, sequencer));
        auto workflow = makeStructureEditWorkflow(h, HistoryServices::fromCoreState(h.state));
        h.tick(23U);
        workflow.beginHoldAction(core::state::StructureHoldAction::REMOVE);
        test_support::drainNotifications();
        const auto expected = capturePreparedActionInvariant(h);
        {
            core::app::testing::ScopedExtmemAllocationFailure failure(1U);
            workflow.resetStepSelectionShallow();
            assert(core::app::testing::extmemAllocationAttempt == 0U);
            assert(core::app::testing::extmemAllocationFailureOrdinal == 1U);
        }
        test_support::drainNotifications();
        assertPreparedActionInvariant(h, expected);
    }

    {
        SequencerStepHarness h;
        auto& sequencer = h.state.sequencer;
        sequencer.pattern.setContentLength(8U);
        h.navigationFocus.set(core::state::StructureNavigationFocus::PAGE);
        auto& selection = sequencer.structureUi.pageSelection;
        selection.active.set(true);
        selection.scope.set(core::state::StructureSelectionScope::PAGE);
        selection.cursorIndex.set(0U);
        selection.selectedMask.set(0x0001U);
        assert(seq::storeActiveTrack(h.state.sequencerTracks, sequencer));
        auto workflow = makeStructureEditWorkflow(h, HistoryServices::fromCoreState(h.state));
        h.tick(29U);
        workflow.beginHoldAction(core::state::StructureHoldAction::REMOVE);
        test_support::drainNotifications();
        const auto expected = capturePreparedActionInvariant(h);
        {
            core::app::testing::ScopedExtmemAllocationFailure failure(1U);
            workflow.applySelectionBottomLeftTap();
            assert(core::app::testing::extmemAllocationAttempt == 0U);
            assert(core::app::testing::extmemAllocationFailureOrdinal == 1U);
        }
        test_support::drainNotifications();
        assertPreparedActionInvariant(h, expected);
    }

    {
        SequencerStepHarness h;
        auto& sequencer = h.state.sequencer;
        sequencer.pattern.setContentLength(8U);
        h.navigationFocus.set(core::state::StructureNavigationFocus::PAGE);
        auto& selection = sequencer.structureUi.pageSelection;
        selection.active.set(true);
        selection.scope.set(core::state::StructureSelectionScope::PAGE);
        selection.cursorIndex.set(0U);
        selection.selectedMask.set(0x0001U);
        assert(seq::storeActiveTrack(h.state.sequencerTracks, sequencer));
        auto workflow = makeStructureEditWorkflow(h, HistoryServices::fromCoreState(h.state));
        h.tick(31U);
        workflow.beginHoldAction(core::state::StructureHoldAction::REMOVE);
        test_support::drainNotifications();
        const auto expected = capturePreparedActionInvariant(h);
        {
            core::app::testing::ScopedExtmemAllocationFailure failure(1U);
            workflow.applySelectionBottomLeftHold();
            assert(core::app::testing::extmemAllocationAttempt == 0U);
            assert(core::app::testing::extmemAllocationFailureOrdinal == 1U);
        }
        test_support::drainNotifications();
        assertPreparedActionInvariant(h, expected);
    }

    std::cout << "[PASS] prepared Step/Page NoChange paths allocate nothing\n";
}

void test_prepared_step_page_oom_failures_restore_exact_state() {
    {
        SequencerStepHarness h;
        auto& sequencer = h.state.sequencer;
        sequencer.pattern.setContentLength(8U);
        sequencer.pattern.note[1U] = 75U;
        sequencer.pattern.velocity[1U] = 106U;
        sequencer.pattern.setEnabled(1U, true);
        createRootMicroSequence(h, 1U);
        sequencer.focusedStep.set(1U);
        h.navigationFocus.set(core::state::StructureNavigationFocus::STEP);
        assert(seq::storeActiveTrack(h.state.sequencerTracks, sequencer));
        auto workflow = makeStructureEditWorkflow(h, HistoryServices::fromCoreState(h.state));
        workflow.copyCurrentStructure();
        auto& selection = sequencer.structureUi.stepSelection;
        selection.active.set(true);
        selection.placing.set(true);
        selection.cursorStep.set(4U);
        selection.setSelected(1U, true);
        selection.clipboardRevision.set(h.state.structureClipboard.revision.get());
        workflow.beginStepPastePreview();
        h.tick(41U);
        workflow.beginHoldAction(core::state::StructureHoldAction::PASTE);
        test_support::drainNotifications();
        const auto expected = capturePreparedActionInvariant(h);
        {
            core::app::testing::ScopedExtmemAllocationFailure failure(1U);
            workflow.pasteStepSelection();
            assert(core::app::testing::extmemAllocationAttempt == 1U);
            assert(core::app::testing::extmemAllocationFailureOrdinal == 0U);
        }
        test_support::drainNotifications();
        assertPreparedActionInvariant(h, expected);
    }

    {
        SequencerStepHarness h;
        auto& sequencer = h.state.sequencer;
        sequencer.pattern.setContentLength(8U);
        sequencer.pattern.note[3U] = 77U;
        sequencer.pattern.setEnabled(3U, true);
        createRootMicroSequence(h, 3U);
        sequencer.focusedStep.set(3U);
        h.navigationFocus.set(core::state::StructureNavigationFocus::STEP);
        assert(seq::storeActiveTrack(h.state.sequencerTracks, sequencer));
        auto workflow = makeStructureEditWorkflow(h, HistoryServices::fromCoreState(h.state));
        h.tick(43U);
        workflow.beginHoldAction(core::state::StructureHoldAction::REMOVE);
        test_support::drainNotifications();
        const auto expected = capturePreparedActionInvariant(h);
        {
            core::app::testing::ScopedExtmemAllocationFailure failure(1U);
            workflow.applyCurrentStructureShortPress();
            assert(core::app::testing::extmemAllocationAttempt == 1U);
            assert(core::app::testing::extmemAllocationFailureOrdinal == 0U);
        }
        test_support::drainNotifications();
        assertPreparedActionInvariant(h, expected);
    }

    {
        SequencerStepHarness h;
        auto& sequencer = h.state.sequencer;
        sequencer.pattern.setContentLength(8U);
        sequencer.pattern.note[2U] = 79U;
        sequencer.pattern.setEnabled(2U, true);
        createRootMicroSequence(h, 2U);
        sequencer.focusedStep.set(2U);
        h.navigationFocus.set(core::state::StructureNavigationFocus::STEP);
        auto& selection = sequencer.structureUi.stepSelection;
        selection.active.set(true);
        selection.cursorStep.set(2U);
        selection.setSelected(2U, true);
        assert(seq::storeActiveTrack(h.state.sequencerTracks, sequencer));
        auto workflow = makeStructureEditWorkflow(h, HistoryServices::fromCoreState(h.state));
        h.tick(47U);
        workflow.beginHoldAction(core::state::StructureHoldAction::REMOVE);
        test_support::drainNotifications();
        const auto expected = capturePreparedActionInvariant(h);
        {
            core::app::testing::ScopedExtmemAllocationFailure failure(1U);
            workflow.resetStepSelectionDeep();
            assert(core::app::testing::extmemAllocationAttempt == 1U);
            assert(core::app::testing::extmemAllocationFailureOrdinal == 0U);
        }
        test_support::drainNotifications();
        assertPreparedActionInvariant(h, expected);
    }

    {
        SequencerStepHarness h;
        auto& sequencer = h.state.sequencer;
        const auto rootNode = seq::rootStepNodeId(0U);
        const auto micro = seq::createMicroSequence(sequencer.pattern, rootNode, 2U);
        assert(micro.ok);
        assert(seq::enterMicroSequenceContentView(sequencer, rootNode, micro.id));
        const auto childNode = seq::activeContentStepNodeId(sequencer, 0U);
        assert(seq::setNodeNoteOffset(sequencer.pattern, childNode, 7));
        sequencer.page.set(0U);
        sequencer.focusedStep.set(1U);
        h.navigationFocus.set(core::state::StructureNavigationFocus::PAGE);
        auto& selection = sequencer.structureUi.pageSelection;
        selection.active.set(true);
        selection.scope.set(core::state::StructureSelectionScope::PAGE);
        selection.cursorIndex.set(0U);
        selection.selectedMask.set(0x0001U);
        assert(seq::storeActiveTrack(h.state.sequencerTracks, sequencer));
        auto workflow = makeStructureEditWorkflow(h, HistoryServices::fromCoreState(h.state));
        h.tick(53U);
        workflow.beginHoldAction(core::state::StructureHoldAction::REMOVE);
        test_support::drainNotifications();
        const auto expected = capturePreparedActionInvariant(h);
        {
            core::app::testing::ScopedExtmemAllocationFailure failure(1U);
            workflow.applySelectionBottomLeftTap();
            assert(core::app::testing::extmemAllocationAttempt == 1U);
            assert(core::app::testing::extmemAllocationFailureOrdinal == 0U);
        }
        test_support::drainNotifications();
        assertPreparedActionInvariant(h, expected);
    }

    {
        SequencerStepHarness h;
        auto& sequencer = h.state.sequencer;
        sequencer.pattern.setContentLength(24U);
        sequencer.pattern.note[8U] = 81U;
        sequencer.pattern.setEnabled(8U, true);
        createRootMicroSequence(h, 8U);
        sequencer.page.set(1U);
        sequencer.focusedStep.set(10U);
        h.navigationFocus.set(core::state::StructureNavigationFocus::PAGE);
        auto& selection = sequencer.structureUi.pageSelection;
        selection.active.set(true);
        selection.scope.set(core::state::StructureSelectionScope::PAGE);
        selection.cursorIndex.set(1U);
        selection.selectedMask.set(0x0002U);
        assert(seq::storeActiveTrack(h.state.sequencerTracks, sequencer));
        auto workflow = makeStructureEditWorkflow(h, HistoryServices::fromCoreState(h.state));
        h.tick(59U);
        workflow.beginHoldAction(core::state::StructureHoldAction::REMOVE);
        test_support::drainNotifications();
        const auto expected = capturePreparedActionInvariant(h);
        {
            core::app::testing::ScopedExtmemAllocationFailure failure(1U);
            workflow.applySelectionBottomLeftHold();
            assert(core::app::testing::extmemAllocationAttempt == 1U);
            assert(core::app::testing::extmemAllocationFailureOrdinal == 0U);
        }
        test_support::drainNotifications();
        assertPreparedActionInvariant(h, expected);
    }

    std::cout << "[PASS] prepared Step/Page OOM failures restore exact state\n";
}

void test_prepared_step_page_failed_commits_restore_exact_state() {
    {
        SequencerStepHarness h;
        auto& sequencer = h.state.sequencer;
        sequencer.pattern.setContentLength(8U);
        sequencer.pattern.note[0U] = 74U;
        sequencer.pattern.velocity[0U] = 101U;
        sequencer.pattern.setEnabled(0U, true);
        sequencer.focusedStep.set(0U);
        h.navigationFocus.set(core::state::StructureNavigationFocus::STEP);
        assert(seq::storeActiveTrack(h.state.sequencerTracks, sequencer));

        FailingPageCommitHistory failing{.state = &h.state};
        auto workflow = makeStructureEditWorkflow(
            h,
            HistoryServices::fromStaticOperations<kFailingPageCommitHistoryOperations>(&failing));
        workflow.copyCurrentStructure();
        assert(h.state.structureClipboard.hasSequencerSteps());
        sequencer.focusedStep.set(1U);
        h.tick(61U);
        workflow.beginHoldAction(core::state::StructureHoldAction::PASTE);
        test_support::drainNotifications();
        const auto expected = capturePreparedActionInvariant(h);
        workflow.pasteCurrentStructure();
        test_support::drainNotifications();
        assert(failing.commitCount == 1U);
        assert(failing.abortCount == 1U);
        assertPreparedActionInvariant(h, expected);
    }

    {
        SequencerStepHarness h;
        auto& sequencer = h.state.sequencer;
        sequencer.pattern.setContentLength(8U);
        const auto rootNode = seq::rootStepNodeId(0U);
        const auto micro = seq::createMicroSequence(sequencer.pattern, rootNode, 16U);
        assert(micro.ok);
        assert(seq::enterMicroSequenceContentView(sequencer, rootNode, micro.id));
        assert(sequencer.pattern.length.get() == 8U);
        assert(seq::activeContentLength(sequencer) == 16U);
        const auto childNode = seq::activeContentStepNodeId(sequencer, 12U);
        assert(seq::setNodeNoteOffset(sequencer.pattern, childNode, 5));
        sequencer.page.set(1U);
        sequencer.focusedStep.set(12U);
        h.navigationFocus.set(core::state::StructureNavigationFocus::STEP);
        assert(seq::storeActiveTrack(h.state.sequencerTracks, sequencer));

        FailingPageCommitHistory failing{.state = &h.state};
        auto workflow = makeStructureEditWorkflow(
            h,
            HistoryServices::fromStaticOperations<kFailingPageCommitHistoryOperations>(&failing));
        h.tick(67U);
        workflow.beginHoldAction(core::state::StructureHoldAction::REMOVE);
        test_support::drainNotifications();
        const auto expected = capturePreparedActionInvariant(h);
        workflow.applyCurrentStructureLongPress();
        test_support::drainNotifications();
        assert(failing.commitCount == 1U);
        assert(failing.abortCount == 1U);
        assertPreparedActionInvariant(h, expected);
    }

    {
        SequencerStepHarness h;
        auto& sequencer = h.state.sequencer;
        sequencer.pattern.setContentLength(8U);
        sequencer.pattern.note[2U] = 82U;
        sequencer.pattern.velocity[2U] = 113U;
        sequencer.pattern.setEnabled(2U, true);
        createRootMicroSequence(h, 2U);
        sequencer.focusedStep.set(4U);
        h.navigationFocus.set(core::state::StructureNavigationFocus::STEP);
        auto& selection = sequencer.structureUi.stepSelection;
        selection.active.set(true);
        selection.cursorStep.set(2U);
        selection.setSelected(2U, true);
        assert(seq::storeActiveTrack(h.state.sequencerTracks, sequencer));

        FailingPageCommitHistory failing{.state = &h.state};
        auto workflow = makeStructureEditWorkflow(
            h,
            HistoryServices::fromStaticOperations<kFailingPageCommitHistoryOperations>(&failing));
        h.tick(71U);
        workflow.beginHoldAction(core::state::StructureHoldAction::REMOVE);
        test_support::drainNotifications();
        const auto expected = capturePreparedActionInvariant(h);
        workflow.resetStepSelectionShallow();
        test_support::drainNotifications();
        assert(failing.commitCount == 1U);
        assert(failing.abortCount == 1U);
        assertPreparedActionInvariant(h, expected);
    }

    {
        SequencerStepHarness h;
        auto& sequencer = h.state.sequencer;
        sequencer.pattern.setContentLength(16U);
        sequencer.pattern.note[0U] = 64U;
        sequencer.pattern.note[8U] = 84U;
        sequencer.pattern.setEnabled(0U, true);
        sequencer.pattern.setEnabled(8U, true);
        sequencer.page.set(1U);
        sequencer.focusedStep.set(11U);
        h.navigationFocus.set(core::state::StructureNavigationFocus::PAGE);
        auto& selection = sequencer.structureUi.pageSelection;
        selection.active.set(true);
        selection.scope.set(core::state::StructureSelectionScope::PAGE);
        selection.cursorIndex.set(1U);
        selection.selectedMask.set(0x0003U);
        assert(seq::storeActiveTrack(h.state.sequencerTracks, sequencer));

        FailingPageCommitHistory failing{.state = &h.state};
        auto workflow = makeStructureEditWorkflow(
            h,
            HistoryServices::fromStaticOperations<kFailingPageCommitHistoryOperations>(&failing));
        h.tick(73U);
        workflow.beginHoldAction(core::state::StructureHoldAction::REMOVE);
        test_support::drainNotifications();
        const auto expected = capturePreparedActionInvariant(h);
        workflow.applySelectionBottomLeftTap();
        test_support::drainNotifications();
        assert(failing.commitCount == 1U);
        assert(failing.abortCount == 1U);
        assertPreparedActionInvariant(h, expected);
    }

    {
        SequencerStepHarness h;
        auto& sequencer = h.state.sequencer;
        const auto rootNode = seq::rootStepNodeId(0U);
        const auto micro = seq::createMicroSequence(sequencer.pattern, rootNode, 2U);
        assert(micro.ok);
        assert(seq::enterMicroSequenceContentView(sequencer, rootNode, micro.id));
        const auto childNode = seq::activeContentStepNodeId(sequencer, 0U);
        assert(seq::setNodeNoteOffset(sequencer.pattern, childNode, 9));
        sequencer.page.set(0U);
        sequencer.focusedStep.set(1U);
        h.navigationFocus.set(core::state::StructureNavigationFocus::PAGE);
        auto& selection = sequencer.structureUi.pageSelection;
        selection.active.set(true);
        selection.scope.set(core::state::StructureSelectionScope::PAGE);
        selection.cursorIndex.set(0U);
        selection.selectedMask.set(0x0001U);
        assert(seq::storeActiveTrack(h.state.sequencerTracks, sequencer));

        FailingPageCommitHistory failing{.state = &h.state};
        auto workflow = makeStructureEditWorkflow(
            h,
            HistoryServices::fromStaticOperations<kFailingPageCommitHistoryOperations>(&failing));
        h.tick(79U);
        workflow.beginHoldAction(core::state::StructureHoldAction::REMOVE);
        test_support::drainNotifications();
        const auto expected = capturePreparedActionInvariant(h);
        workflow.applySelectionBottomLeftHold();
        test_support::drainNotifications();
        assert(failing.commitCount == 1U);
        assert(failing.abortCount == 1U);
        assertPreparedActionInvariant(h, expected);
    }

    std::cout << "[PASS] failed Step/Page commits restore music, UI and clipboard\n";
}

}  // namespace

int main() {
    test_child_creation_draft_apply_and_back_decisions();
    test_nav_context_selector_previews_and_applies_all_three_contexts();
    test_latched_track_editor_release_cannot_cross_into_page_editor();
    test_page_navigation_is_cyclic_and_tap_opens_pattern_editor();
    test_latched_editor_target_drift_fails_closed();
    test_step_editor_uses_the_exact_latched_target();
    test_hidden_context_selector_cannot_complete_an_old_gesture();
    test_latched_nav_hold_cannot_cross_selection_context();
    test_child_context_selector_cycles_pattern_and_step_only();
    test_track_selection_skips_gaps_and_mutes_atomically();
    test_track_selection_delete_is_undoable_and_keeps_one_track();
    test_track_selection_copy_is_global_from_sequencer_view();
    test_page_selection_clear_and_delete_are_undoable();
    test_pattern_selection_paste_previews_collisions_and_creates_intermediate_pages();
    test_track_context_nav_crosses_sparse_slots_and_creates_without_structure();
    test_step_toggle_undo_redo_workflow();
    test_child_step_toggle_undo_redo_workflow();
    test_step_toggle_preflight_failure_is_atomic();
    test_child_draft_toggle_stays_out_of_published_history();
    test_pattern_editor_adds_only_the_next_page();
    test_track_focus_bottom_left_mutes_without_clearing_payload();
    test_sequencer_page_copy_and_long_press_paste();
    test_page_paste_existing_target_graph_oom_and_replay();
    test_page_paste_failures_restore_current_and_selection_ui();
    test_child_content_clear_copy_and_paste_are_undoable();
    test_child_content_clear_and_paste_preflight_failures_are_atomic();
    test_graphless_child_content_paste_uses_prospective_compacted_owner();
    test_undo_removed_active_child_context_returns_to_root();
    test_sequencer_track_copy_and_long_press_paste_to_add_slot();
    test_sequencer_track_paste_preserves_occupied_destination_routing_and_mute();
    test_track_paste_global_undo_redo_restores_content_and_reports_outcome();
    test_track_paste_clamps_focus_to_short_source_before_history_commit();
    test_track_paste_mid_hold_release_cancels_without_mutation_or_history();
    test_track_paste_commits_once_at_absolute_long_threshold();
    test_track_paste_left_top_cancels_and_consumes_later_release();
    test_track_paste_refreshes_route_during_hold_and_freezes_queued_plan();
    test_deleted_track_slot_can_be_recreated_at_any_gap();
    test_created_page_is_undoable_and_redoable();
    test_page_clear_prepared_workflow_commits_nochange_and_oom_is_atomic();
    test_page_delete_prepared_workflow_shifts_cc_and_replays();
    test_page_delete_oom_keeps_hold_until_latched_release();
    test_page_delete_single_page_nochange_preserves_ui_until_release();
    test_page_clear_and_delete_failed_commits_restore_editor_state();
    test_direct_track_create_remove_are_atomic_and_replay_exactly();
    test_direct_track_global_history_and_redo_branch_are_exact();
    test_direct_track_fail_nth_failures_restore_exact_state();
    test_direct_track_activation_collisions_are_atomic();
    test_direct_track_pattern_chronology_is_single_and_ordered();
    test_direct_track_exact_intent_drift_is_stale_before_allocation();
    test_direct_track_accepts_terminal_consumed_paste_state();
    test_direct_track_draft_priority_precedes_adapter_validation();
    test_direct_track_obvious_invalid_topology_skips_chronology();
    test_direct_track_missing_presentation_capability_is_preflight_atomic();
    test_track_remove_hold_latches_target_and_rejects_external_drift();
    test_track_remove_hold_does_not_block_acquired_nav_release();
    test_track_remove_hold_rejects_new_nav_press_without_hiding_action();
    test_track_hold_boundary_drift_cannot_retarget_mutation();
    test_track_remove_hold_provenance_cannot_cross_context_or_selection();
    test_track_remove_hold_rejects_shared_hold_replacement();
    test_track_structure_replay_preserves_runtime_when_active_is_unchanged();
    test_created_track_is_undoable_and_redoable();
    test_track_creation_history_unavailable_is_atomic_and_keeps_add_slot_open();
    test_macro_press_on_future_page_does_not_wrap_to_existing_step();
    test_step_focus_bottom_left_resets_focused_step_only();
    test_step_focus_copy_paste_copies_complete_step_without_selection();
    test_step_selection_copy_paste_extends_sparse_root_steps();
    test_step_selection_macro_long_press_consumes_release_without_toggling();
    test_step_selection_clear_is_undoable_and_keeps_selection_active();
    test_step_selection_wrap_paste_overwrites_inside_pattern();
    test_child_content_nav_enters_step_selection_and_pastes_child_steps();
    test_child_draft_owns_main_bottom_actions_until_single_apply();
    test_child_step_focus_bottom_actions_use_local_step_payload();
    test_child_page_selection_reset_shallow_commits_pattern_only_and_replays();
    test_child_page_selection_deep_reset_removes_descendants_and_replays();
    test_prepared_step_page_nochange_paths_are_allocation_free();
    test_prepared_step_page_oom_failures_restore_exact_state();
    test_prepared_step_page_failed_commits_restore_exact_state();

    std::cout << "\nAll SequencerStepHandler tests passed.\n";
    return 0;
}
