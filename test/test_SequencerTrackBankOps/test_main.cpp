#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <new>
#include <oc/state/ChangeCoalescer.hpp>
#include <oc/state/StaticSignalWatcher.hpp>
#include "app/ExtmemAllocator.hpp"
#include "state/CoreState.hpp"
#include "../support/CoreStorages.hpp"
#include "state/sequencer/SequencerGraphOps.hpp"
#include "state/sequencer/SequencerHistory.hpp"
#include "state/sequencer/SequencerTrackBankOps.hpp"
#include "../support/NotificationTestUtils.hpp"

#if !defined(MS_CORE_ENABLE_EXTMEM_FAILURE_INJECTION)
#error "This test requires native EXTMEM failure injection"
#endif

static bool countHeap = false;
static unsigned heapAllocations = 0U;
void* operator new(std::size_t size) {
    if (countHeap) ++heapAllocations;
    if (void* memory = std::malloc(size ? size : 1U)) return memory;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

namespace {
namespace seq = core::state::sequencer;

void seedTransientTrackState(seq::SequencerState& active) {
    active.stepEdit.stepIndex.set(7U);
    active.stepEdit.focusedRow.set(3U);
    active.stepEdit.localVariationEditActive.set(true);

    active.contextSelector.visible = true;
    active.contextSelector.previewFocus = core::state::StructureNavigationFocus::TRACK;

    active.ccLaneUi.overlayVisible.set(true);
    active.ccLaneUi.mode = seq::SequencerCcLaneUiMode::LANE_GRID;
    active.ccLaneUi.selectorIndex = 2U;
    active.ccLaneUi.draftDirty = true;

    active.stepPropertyInlineSelector.selecting.set(true);
    active.stepPropertyInlineSelector.macroLocalVariationEditActive.set(true);
    active.stepPropertyInlineSelector.selectedIndex.set(4);
    active.stepPropertyInlineSelector.snapshotValid = true;
    active.stepPropertyInlineSelector.suppressOpeningRelease = true;

    active.stepInlineFeedback.show(5U, seq::StepProperty::VELOCITY, 100U);
    active.patternQuickControls.selecting.set(true);
    active.patternQuickControls.feedbackVisible.set(true);
    active.patternQuickControls.focusedItem.set(seq::PatternQuickControlItem::OFFSET);
    active.patternQuickControls.offsetSteps.set(3);
    active.patternQuickControls.hideAtMs = 900U;

    active.contentView.frames[0].kind = seq::SequencerContentViewKind::MICRO_SEQUENCE;
    active.contentView.frames[0].ownerRootStep = 5U;
    active.contentView.frames[0].ownerNodeId = 7U;
    active.contentView.frames[0].sequenceId = 9U;
    active.contentView.frames[0].length = 4U;
    active.contentView.stackDepth = 1U;

    active.stepContentDraft.kind.set(seq::SequencerStepContentDraftKind::CHORD);
    active.stepContentDraft.exitPromptVisible.set(true);
    active.stepContentDraft.exitChoice.set(seq::SequencerStepContentDraftExitChoice::DISCARD);
    active.stepContentDraft.pristineGraphRevision = 77U;
    active.stepContentDraft.ownerStep = 5U;
    active.stepContentDraft.failure = seq::SequencerStepContentDraftFailure::HISTORY_UNAVAILABLE;
    active.stepContentDraft.blockedTransition =
        seq::SequencerStepContentDraftBlockedTransition::HISTORY;
    active.stepContentDraft.scratch =
        core::app::makeExtmemUnique<seq::SequencerPatternState>();
    assert(active.stepContentDraft.scratch);
}

void assertTransientTrackStateReset(
    const seq::SequencerState& active,
    const seq::SequencerPatternState* expectedDraftScratch
) {
    assert(active.stepEdit.stepIndex.get() == 0U);
    assert(active.stepEdit.focusedRow.get() == 0U);
    assert(!active.stepEdit.localVariationEditActive.get());

    assert(!active.contextSelector.visible);
    assert(active.contextSelector.previewFocus ==
           core::state::StructureNavigationFocus::PAGE);

    assert(!active.ccLaneUi.overlayVisible.get());
    assert(active.ccLaneUi.mode == seq::SequencerCcLaneUiMode::CLOSED);
    assert(active.ccLaneUi.selectorIndex == 0U);
    assert(!active.ccLaneUi.draftDirty);

    assert(!active.stepPropertyInlineSelector.selecting.get());
    assert(!active.stepPropertyInlineSelector.macroLocalVariationEditActive.get());
    assert(active.stepPropertyInlineSelector.selectedIndex.get() == 0);
    assert(!active.stepPropertyInlineSelector.snapshotValid);
    assert(!active.stepPropertyInlineSelector.suppressOpeningRelease);

    assert(!active.stepInlineFeedback.visible.get());
    assert(!active.stepInlineFeedback.touchedMask.get().any());
    assert(active.stepInlineFeedback.property.get() == seq::StepProperty::NOTE);
    assert(!active.patternQuickControls.selecting.get());
    assert(!active.patternQuickControls.feedbackVisible.get());
    assert(active.patternQuickControls.focusedItem.get() ==
           seq::PatternQuickControlItem::LENGTH);
    assert(active.patternQuickControls.offsetSteps.get() == 0);
    assert(active.patternQuickControls.hideAtMs == 0U);

    assert(active.contentView.stackDepth == 0U);
    assert(active.contentView.currentFrame() == nullptr);

    assert(!active.stepContentDraft.active.get());
    assert(active.stepContentDraft.kind.get() == seq::SequencerStepContentDraftKind::NONE);
    assert(!active.stepContentDraft.exitPromptVisible.get());
    assert(active.stepContentDraft.exitChoice.get() ==
           seq::SequencerStepContentDraftExitChoice::SAVE);
    assert(active.stepContentDraft.pristineGraphRevision == 0U);
    assert(active.stepContentDraft.ownerStep == 0U);
    assert(active.stepContentDraft.failure == seq::SequencerStepContentDraftFailure::NONE);
    assert(active.stepContentDraft.blockedTransition ==
           seq::SequencerStepContentDraftBlockedTransition::NONE);
    assert(active.stepContentDraft.scratch.get() == expectedDraftScratch);
}



void test_navigation_preserves_every_musical_owner_and_revision() {
    auto bank = std::make_unique<seq::SequencerTrackBankState>();
    bank->syncSharedTrackState(0xFFFFU, 0U);
    std::array<seq::SequencerHistoryPatternSnapshot, 16> before;
    std::array<const void*, 16> graphs{}, ccLanes{};
    for (uint8_t i = 0U; i < 16U; ++i) {
        auto& pattern = bank->track(i);
        pattern.setStepNoteAt(0U, 60U + i);
        pattern.setContentLength(8U + i);
        assert(seq::ensureGraphRoot(pattern));
        pattern.ccLanes = core::app::makeExtmemUnique<seq::SequencerCcLaneBank>();
        assert(pattern.ccLanes);
        assert(seq::captureHistorySnapshot(pattern, bank->clip(i), 0U, before[i]));
        graphs[i] = pattern.graph.get();
        ccLanes[i] = pattern.ccLanes.get();
    }
    seq::SequencerState active{bank->track(0U), bank->clip(0U)};
    seedTransientTrackState(active);
    const auto* scratch = active.stepContentDraft.scratch.get();
    active.focusedStep.set(63U);
    active.page.set(7U);
    test_support::drainNotifications();
    for (uint8_t round = 0U; round < 4U; ++round) {
        for (uint8_t i = 1U; i <= 16U; ++i) {
            const uint8_t target = i % 16U;
            {
                core::app::testing::ScopedExtmemAllocationFailure failure(1U);
                heapAllocations = 0U;
                countHeap = true;
                assert(seq::switchActiveTrack(*bank, active, target));
                countHeap = false;
                assert(heapAllocations == 0U);
                assert(core::app::testing::extmemAllocationAttempt == 0U);
            }
            assert(&active.pattern() == &bank->track(target));
            assert(&active.clip() == &bank->clip(target));
            assert(active.focusedStep.get() < active.pattern().length);
            for (uint8_t track = 0U; track < 16U; ++track) {
                assert(bank->track(track).graph.get() == graphs[track]);
                assert(bank->track(track).ccLanes.get() == ccLanes[track]);
                assert(seq::liveHistoryPatternSnapshotMatches(
                    bank->track(track), bank->clip(track), before[track]));
            }
            test_support::drainNotifications();
        }
    }
    assertTransientTrackStateReset(active, scratch);
    assert(!seq::switchActiveTrack(*bank, active, 0U));
    active.stepContentDraft.active.set(true);
    assert(!seq::switchActiveTrack(*bank, active, 1U));
    assert(bank->activeTrackIndex() == 0U);
    assert(&active.pattern() == &bank->track(0U));
    active.stepContentDraft.active.set(false);
    assert(seq::switchActiveTrack(*bank, active, 255U));
    assert(bank->activeTrackIndex() == 15U);
}

void test_observation_follows_selection_before_old_owner_destruction() {
    auto old = std::make_unique<seq::SequencerPatternState>();
    seq::SequencerPatternState next;
    seq::SequencerClipState oldClip, nextClip;
    seq::SequencerState editor{*old, oldClip};
    struct Observer {
        seq::SequencerState& editor;
        unsigned calls = 0U;
        uint8_t note = 0U;
        void render() { ++calls; note = editor.pattern().note[0U]; }
    } observer{editor};
    oc::state::StaticWatchGroup<1> watcher;
    watcher.bind<&Observer::render>(observer, 0U);
    assert(watcher.watch(editor.patternChanges.stepDataRevision));
    old->setStepNoteAt(0U, 61U);
    next.setStepNoteAt(0U, 72U);
    editor.selectPattern(next, nextClip);
    old.reset();
    assert(observer.calls == 0U);
    test_support::drainNotifications();
    assert(observer.calls == 1U && observer.note == 72U);
    next.setPatternSwingOffsetPercent(25);
    test_support::drainNotifications();
    assert(observer.calls == 1U); // Timing does not redraw a step-only consumer.
    next.setStepNoteAt(0U, 73U);
    test_support::drainNotifications();
    assert(observer.calls == 2U && observer.note == 73U);
    // Distinct owners with equal values and revisions still change selection.
    old = std::make_unique<seq::SequencerPatternState>();
    old->setStepNoteAt(0U, 73U);
    old->setStepDataRevision(next.stepDataRevision);
    editor.selectPattern(*old, oldClip);
    test_support::drainNotifications();
    assert(observer.calls == 3U && observer.note == 73U);
    next.setStepNoteAt(0U, 90U);
    test_support::drainNotifications();
    assert(observer.calls == 3U);
    // Ensure the borrowing editor is detached before old's local owner dies.
    editor.selectPattern(next, nextClip);
    test_support::drainNotifications();
}

void test_core_navigation_rebinds_autosave_without_heap_allocation() {
    test_support::CoreStorages storages;
    core::state::CoreState state(storages.settings);
    assert(state.setSharedTrackState(0xFFFFU, 0U));
    test_support::drainNotifications();
    state.flushProjectMutationCoalescing();
    heapAllocations = 0U;
    {
        core::app::testing::ScopedExtmemAllocationFailure failure(1U);
        countHeap = true;
        for (unsigned i = 1U; i <= 64U; ++i) {
            const uint8_t track = i % seq::SequencerTrackBankState::TRACK_COUNT;
            assert(state.setSharedTrackState(0xFFFFU, track));
            assert(&state.sequencer.pattern() == &state.sequencerTracks.track(track));
            auto& changes = state.sequencer.patternChanges;
            assert(changes.authoredRevision.subscriberCount() == 1U);
            const auto before = changes.patternScaleRevision.get();
            state.sequencerTracks.track((i - 1U) % 16U).bumpPatternScaleRevision();
            assert(changes.patternScaleRevision.get() == before);
            state.sequencer.pattern().bumpPatternScaleRevision();
            assert(changes.patternScaleRevision.get() == before + 1U);
            test_support::drainNotifications();
        }
        state.flushProjectMutationCoalescing();
        countHeap = false;
        assert(heapAllocations == 0U);
        assert(core::app::testing::extmemAllocationAttempt == 0U);
    }
}

void test_document_selection_and_destruction_preserve_pending_save() {
    unsigned saves = 0U;
    auto previous = std::make_unique<seq::SequencerPatternState>();
    seq::SequencerPatternState next;
    seq::SequencerClipState previousClip, nextClip;
    seq::SequencerState editor(*previous, previousClip);
    oc::state::ChangeCoalescer<1> coalescer([&] { ++saves; }, 300U);
    assert(coalescer.watch(editor.patternChanges.authoredRevision));
    test_support::drainNotifications();
    assert(!coalescer.hasPendingChanges());
    assert(previous->setStepNoteAt(0U, 73U));
    // The musical owner may disappear before the deferred save notification.
    previous.reset();
    editor.selectPattern(next, nextClip);
    test_support::drainNotifications();
    assert(coalescer.hasPendingChanges());
    coalescer.flush();
    assert(saves == 1U);

    seq::SequencerPatternState inactive;
    assert(inactive.setStepNoteAt(0U, 90U));
    test_support::drainNotifications();
    assert(!coalescer.hasPendingChanges());
    assert(next.setStepNoteAt(0U, 91U));
    test_support::drainNotifications();
    coalescer.flush();
    assert(saves == 2U);
}
} // namespace

int main() {
    test_navigation_preserves_every_musical_owner_and_revision();
    test_observation_follows_selection_before_old_owner_destruction();
    test_document_selection_and_destruction_preserve_pending_save();
    test_core_navigation_rebinds_autosave_without_heap_allocation();
    std::cout << "Bank ownership, navigation, filtered observation and save rebinding passed\n";
}
