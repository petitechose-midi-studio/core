#pragma once

/**
 * @file StatusBarState.hpp
 * @brief Status bar reactive state
 */

#include <array>
#include <cstdint>

#include <oc/time/Time.hpp>
#include <oc/state/Signal.hpp>

#include "config/Timing.hpp"

namespace core::state {

using oc::state::Signal;

/**
 * @brief Reactive transport and runtime activity feedback.
 */
struct StatusBarState {
    static constexpr uint8_t TRACK_COUNT = 16;
    static constexpr uint8_t TRANSIENT_NOTE_IN = 1U << 0;
    static constexpr uint8_t TRANSIENT_NOTE_OUT = 1U << 1;
    static constexpr uint8_t TRANSIENT_CC_IN = 1U << 2;
    static constexpr uint8_t TRANSIENT_CC_OUT = 1U << 3;
    static constexpr uint8_t TRANSIENT_SYNC_INPUT = 1U << 4;
    static constexpr uint8_t TRANSIENT_BEAT = 1U << 5;

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
    std::array<Signal<uint8_t, 4>, TRACK_COUNT> trackNoteActivity{};

    StatusBarState();
    ~StatusBarState();

    void pulseNoteIn() {
        pulseNoteIn(oc::time::millis());
    }

    void pulseNoteIn(uint32_t nowMs) {
        pulseTransientAt(
            noteInActive,
            note_in_until_ms_,
            Config::Timing::STATUS_MIDI_PULSE_MS,
            nowMs,
            active_transient_mask_,
            TRANSIENT_NOTE_IN
        );
    }

    void pulseNoteOut() {
        pulseNoteOut(oc::time::millis());
    }

    void pulseNoteOut(uint32_t nowMs) {
        pulseTransientAt(
            noteOutActive,
            note_out_until_ms_,
            Config::Timing::STATUS_MIDI_PULSE_MS,
            nowMs,
            active_transient_mask_,
            TRANSIENT_NOTE_OUT
        );
    }

    void pulseCcIn() {
        pulseCcIn(oc::time::millis());
    }

    void pulseCcIn(uint32_t nowMs) {
        pulseTransientAt(
            ccInActive,
            cc_in_until_ms_,
            Config::Timing::STATUS_MIDI_PULSE_MS,
            nowMs,
            active_transient_mask_,
            TRANSIENT_CC_IN
        );
    }

    void pulseCcOut() {
        pulseCcOut(oc::time::millis());
    }

    void pulseCcOut(uint32_t nowMs) {
        pulseTransientAt(
            ccOutActive,
            cc_out_until_ms_,
            Config::Timing::STATUS_MIDI_PULSE_MS,
            nowMs,
            active_transient_mask_,
            TRANSIENT_CC_OUT
        );
    }

    void pulseSyncInput() {
        pulseSyncInput(oc::time::millis());
    }

    void pulseSyncInput(uint32_t nowMs) {
        pulseTransientAt(
            syncInputPulse,
            sync_input_until_ms_,
            Config::Timing::STATUS_MIDI_PULSE_MS,
            nowMs,
            active_transient_mask_,
            TRANSIENT_SYNC_INPUT
        );
    }

    void pulseBeat() {
        pulseBeat(oc::time::millis());
    }

    void pulseBeat(uint32_t nowMs) {
        pulseTransientAt(
            beatPulse,
            beat_until_ms_,
            Config::Timing::STATUS_BEAT_PULSE_MS,
            nowMs,
            active_transient_mask_,
            TRANSIENT_BEAT
        );
    }

    void pulseTrackNote(uint8_t track, uint8_t velocity) {
        pulseTrackNote(track, velocity, oc::time::millis());
    }

    void pulseTrackNote(uint8_t track, uint8_t velocity, uint32_t nowMs) {
        if (track >= TRACK_COUNT) return;
        track_note_until_ms_[track] = nowMs + Config::Timing::STATUS_MIDI_PULSE_MS;
        active_track_note_mask_ |= static_cast<uint16_t>(1U << track);
        trackNoteActivity[track].set(velocity);
    }

    void updateTransient(uint32_t nowMs) {
        if (active_transient_mask_ & TRANSIENT_NOTE_IN) {
            expireTransient(
                noteInActive,
                note_in_until_ms_,
                nowMs,
                active_transient_mask_,
                TRANSIENT_NOTE_IN
            );
        }
        if (active_transient_mask_ & TRANSIENT_NOTE_OUT) {
            expireTransient(
                noteOutActive,
                note_out_until_ms_,
                nowMs,
                active_transient_mask_,
                TRANSIENT_NOTE_OUT
            );
        }
        if (active_transient_mask_ & TRANSIENT_CC_IN) {
            expireTransient(ccInActive, cc_in_until_ms_, nowMs, active_transient_mask_, TRANSIENT_CC_IN);
        }
        if (active_transient_mask_ & TRANSIENT_CC_OUT) {
            expireTransient(ccOutActive,
                            cc_out_until_ms_,
                            nowMs,
                            active_transient_mask_,
                            TRANSIENT_CC_OUT);
        }
        if (active_transient_mask_ & TRANSIENT_SYNC_INPUT) {
            expireTransient(syncInputPulse,
                            sync_input_until_ms_,
                            nowMs,
                            active_transient_mask_,
                            TRANSIENT_SYNC_INPUT);
        }
        if (active_transient_mask_ & TRANSIENT_BEAT) {
            expireTransient(beatPulse, beat_until_ms_, nowMs, active_transient_mask_, TRANSIENT_BEAT);
        }

        if (active_track_note_mask_ == 0) {
            return;
        }

        const uint16_t activeTrackMask = active_track_note_mask_;
        for (uint8_t i = 0; i < TRACK_COUNT; ++i) {
            if ((activeTrackMask & static_cast<uint16_t>(1U << i)) == 0) {
                continue;
            }
            expireTrackTransient(trackNoteActivity[i], track_note_until_ms_[i], nowMs, active_track_note_mask_, i);
        }
    }

private:
    static void pulseTransientAt(Signal<bool>& signal,
                                 uint32_t& untilMs,
                                 uint32_t durationMs,
                                 uint32_t nowMs,
                                 uint8_t& activeMask,
                                 uint8_t activeBit) {
        untilMs = nowMs + durationMs;
        activeMask |= activeBit;
        if (!signal.get()) {
            signal.set(true);
        }
    }

    static void expireTransient(Signal<bool>& signal,
                                uint32_t& untilMs,
                                uint32_t nowMs,
                                uint8_t& activeMask,
                                uint8_t activeBit) {
        if (!signal.get()) {
            activeMask &= static_cast<uint8_t>(~activeBit);
            return;
        }
        if (oc::time::deadlineReachedMs(nowMs, untilMs)) {
            signal.set(false);
            activeMask &= static_cast<uint8_t>(~activeBit);
        }
    }

    static void expireTrackTransient(Signal<uint8_t, 4>& signal,
                                     uint32_t& untilMs,
                                     uint32_t nowMs,
                                     uint16_t& activeMask,
                                     uint8_t track) {
        const uint16_t trackBit = static_cast<uint16_t>(1U << track);
        if (signal.get() == 0) {
            activeMask &= static_cast<uint16_t>(~trackBit);
            return;
        }
        if (oc::time::deadlineReachedMs(nowMs, untilMs)) {
            signal.set(0);
            activeMask &= static_cast<uint16_t>(~trackBit);
        }
    }

    uint32_t note_in_until_ms_ = 0;
    uint32_t note_out_until_ms_ = 0;
    uint32_t cc_in_until_ms_ = 0;
    uint32_t cc_out_until_ms_ = 0;
    uint32_t sync_input_until_ms_ = 0;
    uint32_t beat_until_ms_ = 0;
    std::array<uint32_t, TRACK_COUNT> track_note_until_ms_{};
    uint8_t active_transient_mask_ = 0;
    uint16_t active_track_note_mask_ = 0;
};

}  // namespace core::state
