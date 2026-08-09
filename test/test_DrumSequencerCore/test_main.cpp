#include <array>
#include <cassert>
#include <cstdint>
#include <iostream>

#include <oc/note/sequencer/SequencerEvent.hpp>

#include "sequencer/DrumPlaybackEngine.hpp"
#include "state/sequencer/DrumPatternState.hpp"

namespace {

namespace drum = core::state::sequencer;
using core::sequencer::DrumPlaybackEngine;
using oc::note::sequencer::SequencerEvent;
using oc::note::sequencer::SequencerEventType;

class RecordingSink final : public oc::note::sequencer::ISequencerEventSink {
public:
    bool emitSequencerEvent(const SequencerEvent& event) override {
        if (count >= events.size()) return false;
        events[count++] = event;
        return true;
    }

    size_t noteOnCount(uint8_t note) const {
        size_t result = 0U;
        for (size_t i = 0U; i < count; ++i) {
            if (events[i].type == SequencerEventType::NoteOn &&
                events[i].note == note) {
                ++result;
            }
        }
        return result;
    }

    bool hasNoteOn(uint8_t note, uint32_t tick, uint8_t channel) const {
        for (size_t i = 0U; i < count; ++i) {
            const auto& event = events[i];
            if (event.type == SequencerEventType::NoteOn &&
                event.note == note && event.tick == tick &&
                event.channel == channel) {
                return true;
            }
        }
        return false;
    }

    std::array<SequencerEvent, 512U> events{};
    size_t count = 0U;
};

void test_kit_mapping_is_separate_from_rhythm() {
    drum::DrumTrackState track;
    track.reset();

    assert(track.kit.laneCount == 8U);
    assert(track.kit.lanes[0].midiNote == 36U);
    assert(track.kit.lanes[1].midiNote == 38U);
    assert(track.pattern.effectiveLength(0U) == 8U);
    assert(track.pattern.effectiveStepsPerBeat(0U) == 4U);

    assert(track.pattern.toggleStep(0U, 0U));
    const uint32_t rhythmRevision = track.pattern.revision;
    drum::DrumPatternRuntimeSnapshot beforeMapping{};
    drum::captureDrumRuntimeSnapshot(track, beforeMapping);
    assert(track.kit.setLane(
        0U,
        {35U, drum::DrumLaneRole::KICK}
    ));
    assert(track.pattern.revision == rhythmRevision);
    assert(track.pattern.stepEnabled(0U, 0U));
    drum::DrumPatternRuntimeSnapshot afterMapping{};
    drum::captureDrumRuntimeSnapshot(track, afterMapping);
    assert(beforeMapping.revision != afterMapping.revision);
    assert(beforeMapping.lanes[0U].midiNote == 36U);
    assert(afterMapping.lanes[0U].midiNote == 35U);
    assert(afterMapping.lanes[0U].enabledMask.test(0U));

    assert(track.pattern.setLaneTimingCustom(1U, 7U, 4U));
    assert(track.pattern.setLaneTimingCustom(2U, 4U, 2U));
    assert(track.pattern.effectiveLength(1U) == 7U);
    assert(track.pattern.effectiveStepsPerBeat(2U) == 2U);
    assert(track.pattern.setLaneTimingInherited(1U));
    assert(track.pattern.effectiveLength(1U) == 8U);

    std::cout << "[PASS] Drum kit mapping is independent from rhythm\n";
}

void test_three_lane_polymeter_uses_one_channel() {
    drum::DrumTrackState track;
    track.reset();
    assert(track.kit.setLaneCount(3U));
    assert(track.pattern.setLaneTimingCustom(1U, 7U, 4U));
    assert(track.pattern.setLaneTimingCustom(2U, 4U, 2U));

    // Kick: 8 x 1/16, Snare: 7 x 1/16, Hat: 4 x 1/8.
    assert(track.pattern.setStepEnabled(0U, 0U, true));
    assert(track.pattern.setStepEnabled(0U, 4U, true));
    assert(track.pattern.setStepEnabled(1U, 0U, true));
    assert(track.pattern.setStepEnabled(2U, 0U, true));
    assert(track.pattern.setStepVelocity(0U, 0U, 110U));
    assert(track.pattern.setStepVelocity(1U, 0U, 96U));
    assert(track.pattern.setStepVelocity(2U, 0U, 72U));

    drum::DrumPatternRuntimeSnapshot snapshot{};
    drum::captureDrumRuntimeSnapshot(track, snapshot);
    RecordingSink sink;
    DrumPlaybackEngine engine{sink};
    engine.setPattern(&snapshot, 9U);

    for (uint32_t tick = 0U; tick <= 96U; ++tick) {
        engine.update(tick, true);
    }

    assert(sink.noteOnCount(36U) == 5U);
    assert(sink.noteOnCount(38U) == 3U);
    assert(sink.noteOnCount(42U) == 3U);
    for (const uint32_t tick : {0U, 24U, 48U, 72U, 96U}) {
        assert(sink.hasNoteOn(36U, tick, 9U));
    }
    for (const uint32_t tick : {0U, 42U, 84U}) {
        assert(sink.hasNoteOn(38U, tick, 9U));
    }
    for (const uint32_t tick : {0U, 48U, 96U}) {
        assert(sink.hasNoteOn(42U, tick, 9U));
    }

    const auto& telemetry = engine.telemetry();
    assert(!telemetry.diagnostics.schedulerCapacityExceeded);

    engine.update(97U, false);
    assert(!engine.isPlaying());
    assert(sink.events[sink.count - 1U].type ==
           SequencerEventType::AllNotesOff);

    std::cout << "[PASS] Three independent Drum cycles share one MIDI channel\n";
}

void test_lane_nudge_is_scheduled_on_the_shared_timeline() {
    drum::DrumTrackState track;
    track.reset();
    assert(track.kit.setLaneCount(1U));
    assert(track.pattern.setLaneTimingCustom(0U, 4U, 4U));
    assert(track.pattern.setStepEnabled(0U, 1U, true));
    assert(track.pattern.setStepNudge(0U, 1U, -50));

    drum::DrumPatternRuntimeSnapshot snapshot{};
    drum::captureDrumRuntimeSnapshot(track, snapshot);
    RecordingSink sink;
    DrumPlaybackEngine engine{sink};
    engine.setPattern(&snapshot, 2U);
    for (uint32_t tick = 0U; tick <= 4U; ++tick) {
        engine.update(tick, true);
    }
    assert(sink.hasNoteOn(36U, 3U, 2U));

    std::cout << "[PASS] Lane-local nudge keeps the common transport phase\n";
}

}  // namespace

static_assert(
    sizeof(drum::DrumPatternRuntimeSnapshot) < 12U * 1024U,
    "Drum runtime snapshot must remain a cold PSRAM-sized payload"
);
static_assert(
    sizeof(core::sequencer::DrumPlaybackEngine) < 8U * 1024U,
    "One Drum Track engine must remain a bounded hot allocation"
);

int main() {
    test_kit_mapping_is_separate_from_rhythm();
    test_three_lane_polymeter_uses_one_channel();
    test_lane_nudge_is_scheduled_on_the_shared_timeline();
    std::cout << "All Drum sequencer core tests passed\n";
    return 0;
}
