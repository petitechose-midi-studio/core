#pragma once

#include <array>
#include <cstdint>

#include <oc/state/Signal.hpp>

#include "state/sequencer/SequencerPatternState.hpp"

namespace core::state::sequencer {

using oc::state::Signal;

/**
 * Owns persistent sequencer state for all shared tracks.
 *
 * The active editor is kept outside this bank for low-friction UI editing; ops
 * functions copy sanitized snapshots between the editor and bank on switches.
 */
struct SequencerTrackBankState {
    static constexpr uint8_t TRACK_COUNT = 16;

    SequencerTrackBankState();

    static constexpr uint8_t clampTrackIndex(uint8_t track) {
        return (track >= TRACK_COUNT) ? static_cast<uint8_t>(TRACK_COUNT - 1) : track;
    }

    SequencerPatternState& track(uint8_t index) {
        return tracks_[clampTrackIndex(index)];
    }

    const SequencerPatternState& track(uint8_t index) const {
        return tracks_[clampTrackIndex(index)];
    }

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

    oc::note::sequencer::StepSequencerScaleSettings projectScaleSettings() const {
        auto settings = project_scale_settings_;
        settings.clamp();
        return settings;
    }

    bool setProjectScaleSettings(oc::note::sequencer::StepSequencerScaleSettings settings) {
        settings.clamp();
        auto current = project_scale_settings_;
        current.clamp();
        if (current.root == settings.root &&
            current.type == settings.type &&
            current.mode == settings.mode) {
            return false;
        }

        project_scale_settings_ = settings;
        project_scale_revision_.set(project_scale_revision_.get() + 1U);
        for (auto& track : tracks_) {
            if (!isPatternScaleOverride(track.scalePolicy)) {
                track.bumpPatternScaleRevision();
            }
        }
        return true;
    }

    bool isTrackEnabled(uint8_t index) const {
        const uint8_t clamped = clampTrackIndex(index);
        return (enabled_mask_.get() & static_cast<uint16_t>(1U << clamped)) != 0;
    }

    void reset();

private:
    Signal<uint8_t, 8> active_track_{0};
    Signal<uint16_t, 16> enabled_mask_{0x0001};
    Signal<uint32_t, 8> project_scale_revision_{0};
    oc::note::sequencer::StepSequencerScaleSettings project_scale_settings_{};
    std::array<SequencerPatternState, TRACK_COUNT> tracks_{};
};

}  // namespace core::state::sequencer
