#pragma once

/**
 * @file StatusBarState.hpp
 * @brief Status bar reactive state
 */

#include <cstdint>

#include <oc/time/Time.hpp>
#include <oc/state/Signal.hpp>
#include <oc/state/SignalString.hpp>

#include "config/Timing.hpp"

namespace core::state {

using oc::state::Signal;
using oc::state::SignalLabel;

/**
 * @brief State for TopBar and TransportBar
 */
struct StatusBarState {
    // TopBar
    SignalLabel pageName;

    // TransportBar - MIDI Note indicators
    Signal<bool> noteInActive{false};
    Signal<bool> noteOutActive{false};

    // TransportBar - MIDI CC indicators
    Signal<bool> ccInActive{false};
    Signal<bool> ccOutActive{false};

    // TransportBar - Transport
    Signal<bool> playing{false};
    Signal<float> tempo{120.0f};
    Signal<float> tempoDisplay{120.0f};

    // TransportBar - Clock sync indicators
    Signal<bool> syncExternalSource{false};
    Signal<bool> syncInputPulse{false};
    Signal<bool> tempoLocked{false};
    Signal<bool> transportLocked{false};

    // TransportBar - Beat
    Signal<bool> beatPulse{false};

    StatusBarState() {
        pageName.set("Page 1");
    }

    void pulseNoteIn() {
        pulseTransient(noteInActive, note_in_until_ms_, Config::Timing::STATUS_MIDI_PULSE_MS);
    }

    void pulseNoteOut() {
        pulseTransient(noteOutActive, note_out_until_ms_, Config::Timing::STATUS_MIDI_PULSE_MS);
    }

    void pulseCcIn() {
        pulseTransient(ccInActive, cc_in_until_ms_, Config::Timing::STATUS_MIDI_PULSE_MS);
    }

    void pulseCcOut() {
        pulseTransient(ccOutActive, cc_out_until_ms_, Config::Timing::STATUS_MIDI_PULSE_MS);
    }

    void pulseSyncInput() {
        pulseTransient(syncInputPulse, sync_input_until_ms_, Config::Timing::STATUS_MIDI_PULSE_MS);
    }

    void pulseBeat() {
        pulseTransient(beatPulse, beat_until_ms_, Config::Timing::STATUS_BEAT_PULSE_MS);
    }

    void updateTransient(uint32_t nowMs) {
        expireTransient(noteInActive, note_in_until_ms_, nowMs);
        expireTransient(noteOutActive, note_out_until_ms_, nowMs);
        expireTransient(ccInActive, cc_in_until_ms_, nowMs);
        expireTransient(ccOutActive, cc_out_until_ms_, nowMs);
        expireTransient(syncInputPulse, sync_input_until_ms_, nowMs);
        expireTransient(beatPulse, beat_until_ms_, nowMs);
    }

private:
    static void pulseTransient(Signal<bool>& signal, uint32_t& untilMs, uint32_t durationMs) {
        untilMs = oc::time::millis() + durationMs;
        if (!signal.get()) {
            signal.set(true);
        }
    }

    static void expireTransient(Signal<bool>& signal, uint32_t& untilMs, uint32_t nowMs) {
        if (!signal.get()) return;
        if (static_cast<uint32_t>(nowMs - untilMs) < 0x80000000u) {
            signal.set(false);
        }
    }

    uint32_t note_in_until_ms_ = 0;
    uint32_t note_out_until_ms_ = 0;
    uint32_t cc_in_until_ms_ = 0;
    uint32_t cc_out_until_ms_ = 0;
    uint32_t sync_input_until_ms_ = 0;
    uint32_t beat_until_ms_ = 0;
};

}  // namespace core::state
