#pragma once

#include <array>
#include <cstdint>

#include <oc/state/Signal.hpp>

#include "state/sequencer/DrumPatternState.hpp"
#include "state/sequencer/SequencerPatternState.hpp"

namespace core::state::sequencer {

using oc::state::Signal;

enum class SequencerTrackKind : uint8_t {
    INSTRUMENT = 0,
    DRUM,
};

struct DrumTrackBankSnapshot {
    uint16_t drumTrackMask = 0U;
    std::array<DrumTrackState, 16> tracks{};
};

/**
 * Owns persistent sequencer state for all shared tracks.
 *
 * The active editor is kept outside this bank for low-friction UI editing.
 * Flat authored values are projected between the fixed editor signals and the
 * bank on a switch, while the PSRAM Graph/CC payload ownership is exchanged.
 * This keeps bindings stable without cloning either cold payload on the hot
 * Track-switch gesture.
 */
struct SequencerTrackBankState {
    static constexpr uint8_t TRACK_COUNT = 16;

    SequencerTrackBankState();

    static constexpr uint8_t clampTrackIndex(uint8_t track) {
        return (track >= TRACK_COUNT) ? static_cast<uint8_t>(TRACK_COUNT - 1) : track;
    }

    static uint16_t sanitizeEnabledMask(uint16_t enabledMask);
    static uint8_t sanitizeActiveTrack(uint16_t enabledMask, uint8_t activeTrack);

    SequencerPatternState& track(uint8_t index) {
        return tracks_[clampTrackIndex(index)];
    }

    const SequencerPatternState& track(uint8_t index) const {
        return tracks_[clampTrackIndex(index)];
    }

    [[nodiscard]] SequencerTrackKind trackKind(uint8_t index) const {
        const uint16_t bit = static_cast<uint16_t>(1U << clampTrackIndex(index));
        return (drum_track_mask_ & bit) != 0U
            ? SequencerTrackKind::DRUM
            : SequencerTrackKind::INSTRUMENT;
    }

    [[nodiscard]] bool isDrumTrack(uint8_t index) const {
        return trackKind(index) == SequencerTrackKind::DRUM;
    }

    [[nodiscard]] uint16_t drumTrackMask() const { return drum_track_mask_; }

    DrumTrackState& drumTrack(uint8_t index) {
        return drum_tracks_[clampTrackIndex(index)];
    }

    const DrumTrackState& drumTrack(uint8_t index) const {
        return drum_tracks_[clampTrackIndex(index)];
    }

    bool setTrackKind(
        uint8_t index,
        SequencerTrackKind kind,
        bool resetPayload = false,
        DrumKitPreset drumPreset = DrumKitPreset::GENERAL_MIDI
    );
    void restoreDrumTrack(
        uint8_t index,
        SequencerTrackKind kind,
        const DrumTrackState& state
    );
    void captureDrumTrackBank(DrumTrackBankSnapshot& out) const;
    bool applyDrumTrackBank(const DrumTrackBankSnapshot& snapshot);
    void clearDrumTrackBank();

    void captureSharedTrackState(uint16_t& enabledMaskOut, uint8_t& activeTrackOut) const {
        enabledMaskOut = enabled_mask_.get();
        activeTrackOut = active_track_.get();
    }

    void syncSharedTrackState(uint16_t enabledMaskIn, uint8_t activeTrackIn);
    uint8_t activeTrackIndex() const { return active_track_.get(); }
    uint16_t currentEnabledMask() const { return enabled_mask_.get(); }
    Signal<uint8_t, 8>& activeTrackSignal() { return active_track_; }
    const Signal<uint8_t, 8>& activeTrackSignal() const { return active_track_; }
    Signal<uint16_t, 16>& enabledMaskSignal() { return enabled_mask_; }
    const Signal<uint16_t, 16>& enabledMaskSignal() const { return enabled_mask_; }
    Signal<uint32_t, 8>& projectScaleRevisionSignal() { return project_scale_revision_; }
    const Signal<uint32_t, 8>& projectScaleRevisionSignal() const { return project_scale_revision_; }
    Signal<uint32_t, 8>& drumRevisionSignal() { return drum_revision_; }
    const Signal<uint32_t, 8>& drumRevisionSignal() const { return drum_revision_; }
    [[nodiscard]] uint32_t drumTrackRevision(uint8_t index) const {
        return drum_track_revisions_[clampTrackIndex(index)];
    }
    void publishDrumMutation(uint8_t index);

    oc::note::sequencer::StepSequencerScaleSettings projectScaleSettings() const {
        auto settings = project_scale_settings_;
        settings.clamp();
        return settings;
    }

    bool setProjectScaleSettings(oc::note::sequencer::StepSequencerScaleSettings settings);

    bool isTrackEnabled(uint8_t index) const {
        const uint8_t clamped = clampTrackIndex(index);
        return (enabled_mask_.get() & static_cast<uint16_t>(1U << clamped)) != 0;
    }

    void reset();

private:
    Signal<uint8_t, 8> active_track_{0};
    Signal<uint16_t, 16> enabled_mask_{0x0001};
    Signal<uint32_t, 8> project_scale_revision_{0};
    Signal<uint32_t, 8> drum_revision_{0};
    oc::note::sequencer::StepSequencerScaleSettings project_scale_settings_{};
    uint16_t drum_track_mask_ = 0U;
    std::array<uint32_t, TRACK_COUNT> drum_track_revisions_{};
    std::array<SequencerPatternState, TRACK_COUNT> tracks_{};
    std::array<DrumTrackState, TRACK_COUNT> drum_tracks_{};
};

}  // namespace core::state::sequencer
