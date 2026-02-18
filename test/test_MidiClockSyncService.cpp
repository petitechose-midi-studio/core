#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <utility>

#include <oc/api/MidiAPI.hpp>
#include <oc/interface/IMidi.hpp>

#include "../src/sequencer/MidiClockSyncService.hpp"

namespace {

class MockMidiTransport : public oc::interface::IMidi {
public:
    oc::type::Result<void> init() override { return oc::type::Result<void>::ok(); }
    void update() override {}

    void sendCC(uint8_t, uint8_t, uint8_t) override {}
    void sendNoteOn(uint8_t, uint8_t, uint8_t) override {}
    void sendNoteOff(uint8_t, uint8_t, uint8_t) override {}
    void sendSysEx(const uint8_t*, size_t) override {}
    void sendProgramChange(uint8_t, uint8_t) override {}
    void sendPitchBend(uint8_t, int16_t) override {}
    void sendChannelPressure(uint8_t, uint8_t) override {}
    void allNotesOff() override {}

    void sendClock() override { clock_sent++; }
    void sendStart() override { start_sent++; }
    void sendStop() override { stop_sent++; }
    void sendContinue() override { continue_sent++; }

    void setOnCC(CCCallback cb) override { on_cc = std::move(cb); }
    void setOnNoteOn(NoteCallback cb) override { on_note_on = std::move(cb); }
    void setOnNoteOff(NoteCallback cb) override { on_note_off = std::move(cb); }
    void setOnSysEx(SysExCallback cb) override { on_sysex = std::move(cb); }
    void setOnClock(ClockCallback cb) override { on_clock = std::move(cb); }
    void setOnStart(RealtimeCallback cb) override { on_start = std::move(cb); }
    void setOnStop(RealtimeCallback cb) override { on_stop = std::move(cb); }
    void setOnContinue(RealtimeCallback cb) override { on_continue = std::move(cb); }

    int clock_sent = 0;
    int start_sent = 0;
    int stop_sent = 0;
    int continue_sent = 0;

    CCCallback on_cc;
    NoteCallback on_note_on;
    NoteCallback on_note_off;
    SysExCallback on_sysex;
    ClockCallback on_clock;
    RealtimeCallback on_start;
    RealtimeCallback on_stop;
    RealtimeCallback on_continue;
};

void assertNear(float actual, float expected, float epsilon) {
    if (std::fabs(actual - expected) > epsilon) {
        std::cerr << "assertNear failed: actual=" << actual
                  << " expected=" << expected
                  << " eps=" << epsilon << "\n";
        assert(false);
    }
}

void test_master_emits_realtime() {
    core::state::MidiSyncState sync;
    core::state::StatusBarState status;
    MockMidiTransport transport;
    oc::api::MidiAPI midi{transport};
    core::sequencer::MidiClockSyncService service{sync, status, midi};

    sync.mode.set(core::state::MidiSyncMode::MASTER);
    status.tempo.set(120.0f);

    service.update(0);
    status.playing.set(true);
    service.update(100);
    service.update(200);

    assert(transport.start_sent == 1);
    assert(transport.clock_sent > 0);
    assert(!status.syncExternalSource.get());
    assertNear(status.tempoDisplay.get(), 120.0f, 0.01f);

    status.playing.set(false);
    service.update(300);
    assert(transport.stop_sent == 1);

    std::cout << "[PASS] test_master_emits_realtime\n";
}

void test_slave_follows_external_clock_and_transport() {
    core::state::MidiSyncState sync;
    core::state::StatusBarState status;
    MockMidiTransport transport;
    oc::api::MidiAPI midi{transport};
    core::sequencer::MidiClockSyncService service{sync, status, midi};

    sync.mode.set(core::state::MidiSyncMode::SLAVE);
    sync.followTransport.set(true);
    status.playing.set(false);

    service.update(0);
    service.onStart();
    service.onClock(10'000, 10);
    service.onClock(20'000, 20);
    service.onClock(30'000, 30);
    service.onClock(40'000, 40);
    service.update(40);

    assert(service.playing());
    assert(service.tick() == 4);
    assert(transport.clock_sent == 0);
    assert(transport.start_sent == 0);

    service.onStop();
    service.update(60);
    assert(!service.playing());

    std::cout << "[PASS] test_slave_follows_external_clock_and_transport\n";
}

void test_auto_lock_and_fallback() {
    core::state::MidiSyncState sync;
    core::state::StatusBarState status;
    MockMidiTransport transport;
    oc::api::MidiAPI midi{transport};
    core::sequencer::MidiClockSyncService service{sync, status, midi};

    sync.mode.set(core::state::MidiSyncMode::AUTO);
    sync.autoLockClockCount.set(3);
    sync.autoFallbackMs.set(100);

    service.update(0);
    assert(sync.activeSource.get() == core::state::ClockSourceActive::INTERNAL);

    service.onClock(10'000, 10);
    service.onClock(20'000, 20);
    service.onClock(30'000, 30);
    service.update(30);
    assert(sync.activeSource.get() == core::state::ClockSourceActive::EXTERNAL);
    assert(service.consumeResyncRequest());

    service.update(200);
    assert(sync.activeSource.get() == core::state::ClockSourceActive::INTERNAL);
    assert(service.consumeResyncRequest());

    std::cout << "[PASS] test_auto_lock_and_fallback\n";
}

void test_external_source_updates_displayed_tempo_and_activity() {
    core::state::MidiSyncState sync;
    core::state::StatusBarState status;
    MockMidiTransport transport;
    oc::api::MidiAPI midi{transport};
    core::sequencer::MidiClockSyncService service{sync, status, midi};

    sync.mode.set(core::state::MidiSyncMode::SLAVE);
    status.tempo.set(99.0f);

    service.update(0);
    assert(status.syncExternalSource.get());
    assertNear(status.tempoDisplay.get(), 99.0f, 0.01f);

    uint32_t now = 100;
    for (int i = 0; i < 60; ++i) {
        service.onClock(static_cast<uint64_t>(now) * 1000ULL, now);
        service.update(now);
        now += 20;
    }
    service.update(now);

    assert(status.syncExternalSource.get());
    assert(status.syncInputPulse.get());
    assert(status.tempoDisplay.get() > 120.0f && status.tempoDisplay.get() < 130.0f);

    service.update(now + 1000);
    assert(!status.syncInputPulse.get());

    std::cout << "[PASS] test_external_source_updates_displayed_tempo_and_activity\n";
}

void test_external_tempo_precision_low_mid() {
    auto runScenario = [](uint32_t intervalMs, float minBpm, float maxBpm) {
        core::state::MidiSyncState sync;
        core::state::StatusBarState status;
        MockMidiTransport transport;
        oc::api::MidiAPI midi{transport};
        core::sequencer::MidiClockSyncService service{sync, status, midi};

        sync.mode.set(core::state::MidiSyncMode::SLAVE);

        uint32_t now = 100;
        for (int i = 0; i < 120; ++i) {
            service.onClock(static_cast<uint64_t>(now) * 1000ULL, now);
            service.update(now);
            now += intervalMs;
        }
        service.update(now);

        const float bpm = status.tempoDisplay.get();
        assert(bpm >= minBpm && bpm <= maxBpm);
    };

    runScenario(50, 49.0f, 51.0f);  // 50 BPM
    runScenario(25, 98.0f, 102.0f); // 100 BPM

    std::cout << "[PASS] test_external_tempo_precision_low_mid\n";
}

void test_external_tempo_tracks_fast_change() {
    core::state::MidiSyncState sync;
    core::state::StatusBarState status;
    MockMidiTransport transport;
    oc::api::MidiAPI midi{transport};
    core::sequencer::MidiClockSyncService service{sync, status, midi};

    sync.mode.set(core::state::MidiSyncMode::SLAVE);

    uint32_t now = 100;

    // Warm-up near 120 BPM (about 20.8 ms per MIDI clock)
    for (int i = 0; i < 64; ++i) {
        service.onClock(static_cast<uint64_t>(now) * 1000ULL, now);
        service.update(now);
        now += 21;
    }
    service.update(now);
    const float slowTempo = status.tempoDisplay.get();
    assert(slowTempo > 110.0f && slowTempo < 130.0f);

    // Jump to fast source near 180 BPM (about 13.9 ms per MIDI clock)
    for (int i = 0; i < 64; ++i) {
        service.onClock(static_cast<uint64_t>(now) * 1000ULL, now);
        service.update(now);
        now += 14;
    }
    service.update(now);
    const float fastTempo = status.tempoDisplay.get();

    assert(fastTempo > 170.0f && fastTempo < 190.0f);

    std::cout << "[PASS] test_external_tempo_tracks_fast_change\n";
}

}  // namespace

int main() {
    std::cout << "==============================================\n";
    std::cout << "MidiClockSyncService tests\n";
    std::cout << "==============================================\n\n";

    test_master_emits_realtime();
    test_slave_follows_external_clock_and_transport();
    test_auto_lock_and_fallback();
    test_external_source_updates_displayed_tempo_and_activity();
    test_external_tempo_precision_low_mid();
    test_external_tempo_tracks_fast_change();

    std::cout << "\n==============================================\n";
    std::cout << "All tests passed\n";
    std::cout << "==============================================\n";
    return 0;
}
