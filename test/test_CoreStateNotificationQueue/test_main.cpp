#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iostream>

#include <oc/state/NotificationQueue.hpp>
#include <oc/state/StaticSignalWatcher.hpp>
#if OC_ENABLE_STATS
#include <oc/log/Log.hpp>
#endif

#include "../../src/state/CoreState.hpp"
#include "../../src/state/project/ProjectSnapshot.hpp"
#include "../support/CoreStorages.hpp"
#include "../support/NotificationTestUtils.hpp"

namespace {

using core::state::CoreState;
using core::state::sequencer::SequencerContentViewKind;
using core::state::sequencer::SequencerPatternState;
using core::state::sequencer::StepProperty;
using test_support::CoreStorages;
using test_support::drainNotifications;

#if OC_ENABLE_STATS
void diagnosticChar(char value) { std::cerr.put(value); }
void diagnosticString(const char* value) { if (value) std::cerr << value; }
void diagnosticInt32(int32_t value) { std::cerr << value; }
void diagnosticUint32(uint32_t value) { std::cerr << value; }
void diagnosticFloat(float value) { std::cerr << value; }
void diagnosticBool(bool value) { std::cerr << (value ? "true" : "false"); }
uint32_t diagnosticTimeMs() { return 0; }

const oc::log::Output diagnosticOutput{
    diagnosticChar,
    diagnosticString,
    diagnosticInt32,
    diagnosticUint32,
    diagnosticFloat,
    diagnosticBool,
    diagnosticTimeMs,
};
#endif

struct RepresentativeSequencerObservers {
    size_t callbackCount = 0;

    oc::state::StaticWatchGroup<5> header;
    oc::state::StaticWatchGroup<7> headerStrip;
    oc::state::StaticWatchGroup<14> grid;
    oc::state::StaticWatchGroup<11> selector;
    oc::state::StaticWatchGroup<2> leftStrip;
    oc::state::StaticWatchGroup<4> bottomStrip;
    oc::state::StaticWatchGroup<15> encoderSync;
    oc::state::StaticWatchGroup<6> overlayPresenter;
    oc::state::StaticWatchGroup<1> trackSwitchReady;
    oc::state::StaticWatchGroup<2> retainedMacroView;
    oc::state::StaticWatchGroup<2> retainedProjectView;

    void onChanged() {
        ++callbackCount;
    }

    bool bind(CoreState& state) {
        header.bind<&RepresentativeSequencerObservers::onChanged>(
            *this, 0, "test.sequencer.header"
        );
        headerStrip.bind<&RepresentativeSequencerObservers::onChanged>(
            *this, 1, "test.sequencer.headerStrip"
        );
        grid.bind<&RepresentativeSequencerObservers::onChanged>(
            *this, 2, "test.sequencer.grid"
        );
        selector.bind<&RepresentativeSequencerObservers::onChanged>(
            *this, 3, "test.sequencer.selector"
        );
        leftStrip.bind<&RepresentativeSequencerObservers::onChanged>(
            *this, 4, "test.sequencer.leftStrip"
        );
        bottomStrip.bind<&RepresentativeSequencerObservers::onChanged>(
            *this, 5, "test.sequencer.bottomStrip"
        );
        encoderSync.bind<&RepresentativeSequencerObservers::onChanged>(
            *this, 6, "test.sequencer.encoderSync"
        );
        overlayPresenter.bind<&RepresentativeSequencerObservers::onChanged>(
            *this, 7, "test.sequencer.overlayPresenter"
        );
        trackSwitchReady.bind<&RepresentativeSequencerObservers::onChanged>(
            *this, 8, "test.sequencer.trackSwitchReady"
        );
        retainedMacroView.bind<&RepresentativeSequencerObservers::onChanged>(
            *this, 9, "test.retained.macroView"
        );
        retainedProjectView.bind<&RepresentativeSequencerObservers::onChanged>(
            *this, 10, "test.retained.projectView"
        );

        const bool headerBound = header.watchAll(
            state.sharedTrackActive,
            state.sharedTrackEnabledMask,
            state.sequencer.contentView.kind,
            state.sequencer.contentView.length,
            state.sequencer.contentView.revision
        );
        const bool headerStripBound = headerStrip.watchAll(
            state.sharedTrackActive,
            state.sharedTrackEnabledMask,
            state.sequencer.pattern.length,
            state.sequencer.page,
            state.sequencer.contentView.kind,
            state.sequencer.contentView.length,
            state.sequencer.contentView.revision
        );
        const bool gridBound = grid.watchAll(
            state.sequencer.pattern.length,
            state.sequencer.page,
            state.sequencer.focusedStep,
            state.sequencer.pattern.enabledMask,
            state.sequencer.playheadStep,
            state.sequencer.pattern.stepDataRevision,
            state.sequencer.variationTelemetryRevision,
            state.sequencer.pattern.patternVariationRevision,
            state.sequencer.pattern.patternScaleRevision,
            state.sequencerTracks.projectScaleRevisionSignal(),
            state.sequencer.activeStepProperty,
            state.sequencer.contentView.kind,
            state.sequencer.contentView.length,
            state.sequencer.contentView.revision
        );
        const bool selectorBound = selector.watchAll(
            state.sequencer.activeStepProperty,
            state.sequencer.pattern.graphRevision,
            state.sequencer.pattern.stepsPerBeat,
            state.sequencer.pattern.swingOffsetPercent,
            state.sequencer.pattern.patternNudgePercent,
            state.sequencer.pattern.patternTimingRevision,
            state.sequencer.pattern.length,
            state.sequencer.contentView.kind,
            state.sequencer.contentView.length,
            state.sequencer.contentView.revision,
            state.projectNavigation.contentRevision
        );
        const bool leftStripBound = leftStrip.watchAll(
            state.sequencer.activeStepProperty,
            state.sequencer.contentView.kind
        );
        const bool bottomStripBound = bottomStrip.watchAll(
            state.sequencer.activeStepProperty,
            state.sequencer.pattern.patternVariationRevision,
            state.sequencer.contentView.kind,
            state.sequencer.contentView.revision
        );
        const bool encoderSyncBound = encoderSync.watchAll(
            state.sequencer.page,
            state.sequencer.pattern.length,
            state.sequencer.pattern.graphRevision,
            state.sequencer.focusedStep,
            state.sequencer.activeStepProperty,
            state.sequencer.contentView.kind,
            state.sequencer.contentView.length,
            state.sequencer.contentView.revision,
            state.sequencer.pattern.patternScaleRevision,
            state.sequencerTracks.projectScaleRevisionSignal(),
            state.sequencer.pattern.stepsPerBeat,
            state.sequencer.pattern.swingOffsetPercent,
            state.sequencer.pattern.patternNudgePercent,
            state.sequencer.pattern.patternTimingRevision,
            state.sequencer.patternQuickControls.focusedItem
        );
        const bool overlayBound = overlayPresenter.watchAll(
            state.sequencer.pattern.enabledMask,
            state.sequencer.pattern.stepDataRevision,
            state.sequencer.pattern.patternScaleRevision,
            state.sequencer.pattern.graphRevision,
            state.sequencer.contentView.revision,
            state.sequencerTracks.projectScaleRevisionSignal()
        );
        const bool trackSwitchBound = trackSwitchReady.watchAll(
            state.sharedTrackActive
        );
        const bool macroViewBound = retainedMacroView.watchAll(
            state.sharedTrackActive,
            state.sharedTrackEnabledMask
        );
        const bool projectViewBound = retainedProjectView.watchAll(
            state.projectNavigation.contentRevision,
            state.sequencerTracks.projectScaleRevisionSignal()
        );

        return headerBound &&
               headerStripBound &&
               gridBound &&
               selectorBound &&
               leftStripBound &&
               bottomStripBound &&
               encoderSyncBound &&
               overlayBound &&
               trackSwitchBound &&
               macroViewBound &&
               projectViewBound;
    }
};

void configurePattern(SequencerPatternState& pattern, uint8_t track) {
    const uint8_t length = static_cast<uint8_t>(8U + (track % 8U));
    const uint8_t step = static_cast<uint8_t>(track % length);

    pattern.setContentLength(length);
    pattern.stepsPerBeat.set(static_cast<uint8_t>(2U + (track % 4U)));
    pattern.enabledMask.set({});
    pattern.setStepDataAt(
        step,
        static_cast<uint8_t>(48U + track),
        static_cast<uint8_t>(80U + track),
        static_cast<uint16_t>(90U + track),
        static_cast<int8_t>(track % 5U),
        static_cast<uint8_t>(85U + (track % 16U))
    );
    pattern.setEnabled(step, true);
    pattern.setPatternSwingOffsetPercent(static_cast<int>(track) - 8);
    pattern.setPatternNudgePercent(8 - static_cast<int>(track));
}

void prepareStoredFullBank(CoreState& state) {
    state.sequencerTracks.syncSharedTrackState(0xFFFFU, 0);

    configurePattern(state.sequencer.pattern, 0);
    for (uint8_t track = 1; track < core::state::sequencer::SequencerTrackBankState::TRACK_COUNT;
         ++track) {
        configurePattern(state.sequencerTracks.track(track), track);
    }

    // Full-set snapshots restore their own active track. A non-zero track is
    // the production-relevant stressor because all retained views observe the
    // shared active-track transition in the same atomic apply wave.
    assert(state.setSharedTrackState(0xFFFFU, 5));
}

void prepareDifferentLiveBank(CoreState& state) {
    state.sequencerTracks.reset();
    state.sequencer.reset();
    (void)state.setSharedTrackState(0x0001U, 0);
    assert(state.currentSharedTrackEnabledMask() == 0x0001U);
    assert(state.currentSharedActiveTrack() == 0U);

    state.sequencer.pattern.setContentLength(64);
    state.sequencer.pattern.stepsPerBeat.set(8);
    state.sequencer.pattern.enabledMask.set({});
    state.sequencer.setStepDataAt(63, 12, 34, 150, -12, 42);
    state.sequencer.pattern.setEnabled(63, true);
    state.sequencer.setPatternSwingOffsetPercent(25);
    state.sequencer.setPatternNudgePercent(-25);
    state.sequencer.page.set(7);
    state.sequencer.focusedStep.set(63);
    state.sequencer.activeStepProperty.set(StepProperty::PROBABILITY);

    state.sequencer.contentView.kind.set(SequencerContentViewKind::MICRO_SEQUENCE);
    state.sequencer.contentView.length.set(4);
    state.sequencer.contentView.depth.set(1);
    state.sequencer.contentView.stackDepth = 1;
    state.sequencer.contentView.bump();
}

void sampleQueue(oc::state::NotificationQueue& queue, size_t& peakPending) {
    peakPending = std::max(peakPending, queue.pendingCount());
    assert(queue.pendingCount() <= oc::state::NotificationQueue::maxPending());
}

void test_full_bank_project_apply_stays_within_notification_capacity() {
    static_assert(
        oc::state::NotificationQueue::maxPending() == 96,
        "MIDI Studio requires headroom above its measured 64-entry atomic wave"
    );

    CoreStorages storage;
    CoreStorages stagedStorage;
    storage.initAll();
    stagedStorage.initAll();

#if OC_ENABLE_STATS
    oc::log::setOutput(diagnosticOutput);
#endif

    CoreState state(
        storage.settings
    );
    CoreState staged(
        stagedStorage.settings
    );

    prepareStoredFullBank(staged);
    prepareDifferentLiveBank(state);

    core::state::project::ProjectSnapshot snapshot;
    assert(core::state::project::captureProjectSnapshot(staged, snapshot));
    drainNotifications();

    RepresentativeSequencerObservers observers;
    assert(observers.bind(state));

    auto& queue = oc::state::NotificationQueue::instance();
    queue.setDeferredMode(true);
    queue.resetOverflowCount();
    size_t peakPending = queue.pendingCount();
    sampleQueue(queue, peakPending);

    assert(core::state::project::applyProjectSnapshot(state, snapshot));
    sampleQueue(queue, peakPending);

    // Fourteen generic mutation-coalescer subscriptions are consumed by the
    // Project replacement owner. All independent UI/runtime observers remain.
    constexpr size_t EXPECTED_PROJECT_APPLY_PEAK = 62;
    if (peakPending != EXPECTED_PROJECT_APPLY_PEAK) {
        std::cerr << "Unexpected synchronous Project apply peak: " << peakPending
                  << "/" << oc::state::NotificationQueue::maxPending() << "\n";
    }
    assert(peakPending == EXPECTED_PROJECT_APPLY_PEAK);

    const size_t droppedBeforeFlush = queue.overflowCount();

    assert(state.sequencerTracks.currentEnabledMask() == 0xFFFFU);
    assert(state.currentSharedTrackEnabledMask() == 0xFFFFU);
    assert(state.currentSharedActiveTrack() == 5);
    assert(state.sequencer.pattern.length.get() == 13);
    assert(state.sequencer.pattern.note[5] == 53);
    assert(state.sequencer.pattern.velocity[5] == 85);
    assert(state.sequencer.pattern.gate[5] == 95);

    // flush() also exercises any callbacks that enqueue a later reactive wave.
    queue.flush();
    sampleQueue(queue, peakPending);
    assert(!queue.hasPending());
    assert(observers.callbackCount > 0);

    if (queue.overflowCount() != 0) {
        std::cerr << "NotificationQueue overflow: pending=" << queue.pendingCount()
                  << "/" << oc::state::NotificationQueue::maxPending()
                  << " peak=" << peakPending
                  << " droppedBeforeFlush=" << droppedBeforeFlush
                  << " droppedTotal=" << queue.overflowCount()
                  << " callbacks=" << observers.callbackCount << "\n";
    }
    assert(queue.overflowCount() == 0);

    std::cout << "[PASS] full-bank Project apply notification peak="
              << peakPending << "/" << oc::state::NotificationQueue::maxPending()
              << " callbacks=" << observers.callbackCount << "\n";
}

}  // namespace

int main() {
    std::cout.setf(std::ios::unitbuf);
    test_full_bank_project_apply_stays_within_notification_capacity();
    std::cout << "All CoreState notification queue tests passed.\n";
    return 0;
}
