#include <cassert>
#include <iostream>

#include "../../src/sequencer/SequencerPlaybackProfiler.hpp"

namespace {

void test_snapshot_waits_for_full_window() {
    core::sequencer::SequencerPlaybackProfiler profiler;
    core::sequencer::SequencerPlaybackProfiler::Snapshot snapshot{};

    profiler.record(10, true, 100, {}, 100);
    assert(!profiler.takeSnapshot(999, snapshot));

    std::cout << "[PASS] test_snapshot_waits_for_full_window\n";
}

void test_snapshot_reports_activity_and_tick_jump() {
    core::sequencer::SequencerPlaybackProfiler profiler;
    core::sequencer::SequencerPlaybackActivitySnapshot activity{};
    core::sequencer::SequencerPlaybackProfiler::Snapshot snapshot{};

    activity.noteOnCount = 2;
    activity.noteOffCount = 1;
    activity.queuedEventCount = 3;
    profiler.record(10, true, 100, activity, 100);

    activity.noteOnCount = 1;
    activity.noteOffCount = 0;
    activity.panicNoteOffCount = 2;
    activity.queuedEventCount = 3;
    profiler.record(13, true, 300, activity, 120);

    assert(profiler.takeSnapshot(1200, snapshot));
    assert(snapshot.updateCount == 2);
    assert(snapshot.avgUpdateUs == 200);
    assert(snapshot.maxUpdateUs == 300);
    assert(snapshot.noteOnCount == 3);
    assert(snapshot.noteOffCount == 1);
    assert(snapshot.panicNoteOffCount == 2);
    assert(snapshot.queuedEventCount == 6);
    assert(snapshot.maxTickJump == 3);
    assert(snapshot.lateNoteOnCount == 1);
    assert(snapshot.maxNoteBurst == 2);

    std::cout << "[PASS] test_snapshot_reports_activity_and_tick_jump\n";
}

void test_maybe_log_resets_without_perf_log() {
    core::sequencer::SequencerPlaybackProfiler profiler;
    core::sequencer::SequencerPlaybackActivitySnapshot activity{};
    core::sequencer::SequencerPlaybackProfiler::Snapshot snapshot{};

    activity.noteOnCount = 1;
    activity.queuedEventCount = 1;
    profiler.record(1, true, 100, activity, 100);
    profiler.maybeLog(200);

    assert(!profiler.takeSnapshot(1300, snapshot));

    std::cout << "[PASS] test_maybe_log_resets_without_perf_log\n";
}

}  // namespace

int main() {
    test_snapshot_waits_for_full_window();
    test_snapshot_reports_activity_and_tick_jump();
    test_maybe_log_resets_without_perf_log();
    std::cout << "All SequencerPlaybackProfiler tests passed\n";
    return 0;
}
