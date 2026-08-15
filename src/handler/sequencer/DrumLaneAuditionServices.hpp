#pragma once

#include <cstdint>

namespace oc::api {
class MidiAPI;
}

namespace core::state {
struct StatusBarState;
}

namespace core::state::project {
struct ProjectTrackState;
}

namespace core::handler {

/**
 * Small output boundary for Drum Lane note preview.
 *
 * The Lane Editor owns timing and note lifetime. This service only resolves
 * the authored Track channel and forwards allocation-free MIDI edges, which
 * keeps the interaction testable without giving UI code ownership of the
 * realtime sequencer queue.
 */
class DrumLaneAuditionServices {
public:
    using SendNoteFn = bool (*)(
        void* context,
        uint8_t channel,
        uint8_t note,
        uint8_t velocity
    );
    using PanicFn = void (*)(void* context);

    struct Operations {
        SendNoteFn noteOn = nullptr;
        SendNoteFn noteOff = nullptr;
        PanicFn allNotesOff = nullptr;
    };

    DrumLaneAuditionServices() = default;

    static DrumLaneAuditionServices fromMidi(
        oc::api::MidiAPI& midi,
        const core::state::project::ProjectTrackState& projectTracks,
        const core::state::StatusBarState& statusBar
    );

    template <const Operations& operations>
    static DrumLaneAuditionServices fromStaticOperations(
        void* context,
        const core::state::project::ProjectTrackState& projectTracks,
        const core::state::StatusBarState& statusBar
    ) {
        return DrumLaneAuditionServices(
            context,
            &operations,
            projectTracks,
            statusBar
        );
    }

    [[nodiscard]] bool allowed(uint8_t track) const;
    [[nodiscard]] bool channelForTrack(
        uint8_t track,
        uint8_t& channel
    ) const;
    bool noteOn(uint8_t channel, uint8_t note, uint8_t velocity) const;
    bool noteOff(uint8_t channel, uint8_t note, uint8_t velocity) const;
    void allNotesOff() const;

private:
    DrumLaneAuditionServices(
        void* context,
        const Operations* operations,
        const core::state::project::ProjectTrackState& projectTracks,
        const core::state::StatusBarState& statusBar
    );

    void* context_ = nullptr;
    const Operations* operations_ = nullptr;
    const core::state::project::ProjectTrackState* project_tracks_ = nullptr;
    const core::state::StatusBarState* status_bar_ = nullptr;
};

static_assert(
    sizeof(void*) != 4U || sizeof(DrumLaneAuditionServices) == 16U,
    "Drum Lane audition boundary must remain a four-pointer facade"
);

}  // namespace core::handler
