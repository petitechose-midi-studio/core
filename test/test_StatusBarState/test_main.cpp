#include <cassert>
#include <cstdint>
#include <iostream>
#include <limits>

#include <oc/time/Time.hpp>

#include "../../src/config/Timing.hpp"
#include "../../src/state/StatusBarState.hpp"

namespace {

uint32_t g_mock_now_ms = 0;

uint32_t mockTimeMs() {
    return g_mock_now_ms;
}

void test_sync_input_explicit_time_expires_cleanly_across_wraparound() {
    core::state::StatusBarState state;
    const uint32_t start = std::numeric_limits<uint32_t>::max() - 20;

    state.pulseSyncInput(start);
    assert(state.syncInputPulse.get());

    state.updateTransient(start + Config::Timing::STATUS_MIDI_PULSE_MS - 1);
    assert(state.syncInputPulse.get());

    state.updateTransient(start + Config::Timing::STATUS_MIDI_PULSE_MS);
    assert(!state.syncInputPulse.get());

    std::cout << "[PASS] test_sync_input_explicit_time_expires_cleanly_across_wraparound\n";
}

void test_track_note_pulse_targets_only_requested_track() {
    core::state::StatusBarState state;

    g_mock_now_ms = 100;
    state.pulseTrackNote(2, 96);
    state.pulseTrackNote(99, 127);

    for (uint8_t i = 0; i < state.TRACK_COUNT; ++i) {
        const uint8_t expected = (i == 2) ? 96 : 0;
        assert(state.trackNoteActivity[i].get() == expected);
    }

    state.updateTransient(g_mock_now_ms + Config::Timing::STATUS_MIDI_PULSE_MS - 1);
    assert(state.trackNoteActivity[2].get() == 96);

    state.updateTransient(g_mock_now_ms + Config::Timing::STATUS_MIDI_PULSE_MS);
    assert(state.trackNoteActivity[2].get() == 0);

    std::cout << "[PASS] test_track_note_pulse_targets_only_requested_track\n";
}

void test_retriggered_transients_extend_their_own_window_only() {
    core::state::StatusBarState state;

    g_mock_now_ms = 1000;
    state.pulseNoteIn();
    state.pulseCcOut();

    assert(state.noteInActive.get());
    assert(state.ccOutActive.get());

    g_mock_now_ms += 40;
    state.pulseNoteIn();

    state.updateTransient(1079);
    assert(state.noteInActive.get());
    assert(state.ccOutActive.get());

    state.updateTransient(1080);
    assert(state.noteInActive.get());
    assert(!state.ccOutActive.get());

    state.updateTransient(1120);
    assert(!state.noteInActive.get());

    std::cout << "[PASS] test_retriggered_transients_extend_their_own_window_only\n";
}

}  // namespace

int main() {
    oc::time::setProvider(mockTimeMs);

    test_sync_input_explicit_time_expires_cleanly_across_wraparound();
    test_track_note_pulse_targets_only_requested_track();
    test_retriggered_transients_extend_their_own_window_only();

    std::cout << "\nAll StatusBarState tests passed.\n";
    return 0;
}
