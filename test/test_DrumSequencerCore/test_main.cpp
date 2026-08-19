#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>

#include <oc/note/sequencer/SequencerEvent.hpp>

#include "persistence/DrumTrackPersistenceCodec.hpp"
#include "sequencer/DrumPlaybackEngine.hpp"
#include "state/sequencer/DrumPatternState.hpp"
#include "state/sequencer/SequencerGraphOps.hpp"
#include "state/sequencer/SequencerPatternState.hpp"
#include "state/sequencer/SequencerTrackBankState.hpp"

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

    size_t eventCount(SequencerEventType type, uint8_t note) const {
        size_t result = 0U;
        for (size_t i = 0U; i < count; ++i) {
            if (events[i].type == type && events[i].note == note) {
                ++result;
            }
        }
        return result;
    }

    size_t eventCount(SequencerEventType type) const {
        size_t result = 0U;
        for (size_t i = 0U; i < count; ++i) {
            if (events[i].type == type) ++result;
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

    bool hasNoteOnVelocity(
        uint8_t note,
        uint32_t tick,
        uint8_t channel,
        uint8_t velocity
    ) const {
        for (size_t i = 0U; i < count; ++i) {
            const auto& event = events[i];
            if (event.type == SequencerEventType::NoteOn &&
                event.note == note && event.tick == tick &&
                event.channel == channel && event.velocity == velocity) {
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

drum::DrumLaneDescriptor laneDescriptor(
    uint8_t note,
    drum::DrumLaneRole role,
    drum::DrumLaneIcon icon,
    uint8_t color,
    const char* name
) {
    drum::DrumLaneDescriptor descriptor{
        .midiNote = note,
        .role = role,
    };
    drum::setDrumLaneIcon(descriptor, icon);
    drum::setDrumLaneColorIndex(descriptor, color);
    drum::setDrumLaneName(descriptor, name);
    return descriptor;
}

void test_lane_roles_resolve_defaults_and_keep_only_real_overrides() {
    drum::DrumLaneDescriptor descriptor{
        .midiNote = 38U,
        .role = drum::DrumLaneRole::SNARE,
    };
    assert(descriptor.overrideMask == 0U);
    assert(std::strcmp(drum::drumLaneDisplayName(descriptor), "Snare") == 0);
    assert(drum::drumLaneDisplayIcon(descriptor) ==
           drum::DrumLaneIcon::SNARE);
    assert(drum::drumLaneDisplayColorIndex(descriptor) == 3U);

    assert(drum::setDrumLaneRole(descriptor, drum::DrumLaneRole::KICK));
    assert(descriptor.overrideMask == 0U);
    assert(std::strcmp(drum::drumLaneDisplayName(descriptor), "Kick") == 0);

    assert(drum::setDrumLaneName(descriptor, "Backbeat"));
    assert(drum::setDrumLaneIcon(descriptor, drum::DrumLaneIcon::GENERIC));
    assert(drum::setDrumLaneColorIndex(descriptor, 2U));
    assert(descriptor.overrideMask == drum::DRUM_LANE_OVERRIDE_ALL);
    assert(drum::setDrumLaneRole(descriptor, drum::DrumLaneRole::SNARE));
    assert(std::strcmp(
        drum::drumLaneDisplayName(descriptor),
        "Backbeat"
    ) == 0);
    assert(drum::drumLaneDisplayIcon(descriptor) ==
           drum::DrumLaneIcon::GENERIC);
    assert(drum::drumLaneDisplayColorIndex(descriptor) == 2U);
    assert(drum::drumLaneIdentityOverrideCount(descriptor) == 3U);

    assert(drum::resetDrumLaneIdentityOverrides(descriptor));
    assert(drum::drumLaneIdentityOverrideCount(descriptor) == 0U);
    assert(std::strcmp(drum::drumLaneDisplayName(descriptor), "Snare") == 0);
    assert(drum::drumLaneDisplayIcon(descriptor) ==
           drum::DrumLaneIcon::SNARE);
    assert(drum::drumLaneDisplayColorIndex(descriptor) == 3U);
    assert(descriptor.midiNote == 38U);
    assert(descriptor.role == drum::DrumLaneRole::SNARE);
    assert(!drum::resetDrumLaneIdentityOverrides(descriptor));

    assert(drum::setDrumLaneName(descriptor, "Backbeat"));
    assert(drum::setDrumLaneIcon(descriptor, drum::DrumLaneIcon::GENERIC));
    assert(drum::setDrumLaneColorIndex(descriptor, 2U));

    assert(drum::setDrumLaneName(descriptor, "Snare"));
    assert(drum::setDrumLaneIcon(descriptor, drum::DrumLaneIcon::SNARE));
    assert(drum::setDrumLaneColorIndex(descriptor, 3U));
    assert(descriptor.overrideMask == 0U);
    assert(descriptor.name[0U] == '\0');
    assert(descriptor.icon == drum::DrumLaneIcon::GENERIC);
    assert(descriptor.colorIndex == 0U);

    std::cout << "[PASS] Drum roles resolve defaults and compact overrides\n";
}

void test_empty_kit_lane_lifecycle_keeps_mapping_and_rhythm_aligned() {
    drum::DrumTrackState track;
    track.reset(drum::DrumKitPreset::EMPTY);
    assert(track.kit.laneCount == 0U);

    assert(track.appendLane(laneDescriptor(
        36U,
        drum::DrumLaneRole::KICK,
        drum::DrumLaneIcon::KICK,
        0U,
        "Kick"
    )));
    assert(track.appendLane(laneDescriptor(
        38U,
        drum::DrumLaneRole::SNARE,
        drum::DrumLaneIcon::SNARE,
        1U,
        "Snare"
    )));
    assert(track.pattern.setStepEnabled(0U, 1U, true));
    assert(track.pattern.setStepEnabled(1U, 3U, true));
    assert(track.bindAdvancedRootSlot(7U, 1U, 3U));
    assert(track.advancedRootSlot(1U, 3U) == 7);

    assert(track.insertLane(1U, laneDescriptor(
        42U,
        drum::DrumLaneRole::CLOSED_HAT,
        drum::DrumLaneIcon::CLOSED_HAT,
        4U,
        "C.Hat"
    )));
    assert(track.kit.laneCount == 3U);
    assert(std::strcmp(drum::drumLaneDisplayName(track.kit.lanes[1U]), "C.Hat") == 0);
    assert(track.pattern.stepEnabled(0U, 1U));
    assert(!track.pattern.stepEnabled(1U, 3U));
    assert(track.pattern.stepEnabled(2U, 3U));
    assert(track.advancedRootSlot(2U, 3U) == 7);

    assert(track.moveLane(2U, 0U));
    assert(track.kit.lanes[0U].midiNote == 38U);
    assert(track.pattern.stepEnabled(0U, 3U));
    assert(track.advancedRootSlot(0U, 3U) == 7);
    assert(track.pattern.stepEnabled(1U, 1U));
    assert(track.removeLane(1U));
    assert(track.kit.laneCount == 2U);
    assert(track.kit.lanes[1U].midiNote == 42U);
    assert(!track.pattern.stepEnabled(1U, 1U));
    assert(track.advancedRootSlot(0U, 3U) == 7);

    std::cout << "[PASS] Empty kit lane lifecycle keeps rhythm aligned\n";
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
    engine.setPattern(&snapshot, nullptr, 9U);

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
    engine.setPattern(&snapshot, nullptr, 2U);
    for (uint32_t tick = 0U; tick <= 4U; ++tick) {
        engine.update(tick, true);
    }
    assert(sink.hasNoteOn(36U, 3U, 2U));

    std::cout << "[PASS] Lane-local nudge keeps the common transport phase\n";
}

void test_duplicate_note_retrigger_replaces_the_stale_note_off() {
    drum::DrumTrackState track;
    track.reset();
    assert(track.kit.setLaneCount(2U));
    assert(track.kit.setLane(1U, {36U, drum::DrumLaneRole::SNARE}));
    assert(track.pattern.setStepEnabled(0U, 0U, true));
    assert(track.pattern.setStepEnabled(1U, 0U, true));

    drum::DrumPatternRuntimeSnapshot snapshot{};
    drum::captureDrumRuntimeSnapshot(track, snapshot);
    RecordingSink sink;
    DrumPlaybackEngine engine{sink};
    engine.setPattern(&snapshot, nullptr, 9U);
    for (uint32_t tick = 0U; tick <= 6U; ++tick) {
        engine.update(tick, true);
    }

    assert(sink.noteOnCount(36U) == 2U);
    // One immediate off performs the retrigger and one future off closes the
    // replacement note. The superseded future edge must not survive.
    assert(sink.eventCount(SequencerEventType::NoteOff, 36U) == 2U);
    assert(!engine.telemetry().diagnostics.schedulerCapacityExceeded);

    std::cout << "[PASS] Duplicate Drum notes retrigger without stale NoteOff\n";
}

void test_long_gates_remain_bounded_across_all_sixteen_lanes() {
    drum::DrumTrackState track;
    track.reset();
    assert(track.kit.setLaneCount(drum::DRUM_MAX_LANES));
    for (uint8_t lane = 0U; lane < drum::DRUM_MAX_LANES; ++lane) {
        assert(track.kit.setLane(
            lane,
            {static_cast<uint8_t>(36U + lane), drum::DrumLaneRole::CUSTOM}
        ));
        assert(track.pattern.setLaneTimingCustom(lane, 1U, 4U));
        assert(track.pattern.setStepEnabled(lane, 0U, true));
        assert(track.pattern.setStepGate(
            lane,
            0U,
            drum::DRUM_MAX_GATE_PERCENT
        ));
    }

    drum::DrumPatternRuntimeSnapshot snapshot{};
    drum::captureDrumRuntimeSnapshot(track, snapshot);
    RecordingSink sink;
    DrumPlaybackEngine engine{sink};
    engine.setPattern(&snapshot, nullptr, 9U);
    for (uint32_t tick = 0U; tick <= 48U; ++tick) {
        engine.update(tick, true);
    }

    assert(!engine.telemetry().diagnostics.schedulerCapacityExceeded);
    assert(!engine.telemetry().diagnostics.sinkRejectedEvent);
    for (uint8_t lane = 0U; lane < drum::DRUM_MAX_LANES; ++lane) {
        assert(sink.noteOnCount(static_cast<uint8_t>(36U + lane)) == 9U);
    }

    std::cout << "[PASS] Sixteen long-gate lanes keep bounded pending state\n";
}

void test_pattern_generation_change_panics_once_and_rephases() {
    drum::DrumTrackState track;
    track.reset();
    assert(track.kit.setLaneCount(1U));
    assert(track.pattern.setStepEnabled(0U, 0U, true));
    assert(track.pattern.setStepGate(0U, 0U, 400U));

    drum::DrumPatternRuntimeSnapshot first{};
    drum::captureDrumRuntimeSnapshot(track, first);
    RecordingSink sink;
    DrumPlaybackEngine engine{sink};
    engine.setPattern(&first, nullptr, 9U);
    engine.update(0U, true);
    assert(sink.hasNoteOn(36U, 0U, 9U));

    assert(track.kit.setLane(0U, {38U, drum::DrumLaneRole::SNARE}));
    drum::DrumPatternRuntimeSnapshot second{};
    drum::captureDrumRuntimeSnapshot(track, second);
    assert(first.revision != second.revision);
    engine.setPattern(&second, nullptr, 9U);
    engine.update(1U, true);

    assert(sink.eventCount(SequencerEventType::AllNotesOff) == 1U);
    assert(engine.isPlaying());
    assert(!engine.telemetry().diagnostics.schedulerCapacityExceeded);

    std::cout << "[PASS] Drum generation change panics once and rephases\n";
}

void test_expression_change_hot_swaps_without_retrigger() {
    drum::DrumTrackState track;
    track.reset();
    assert(track.kit.setLaneCount(1U));
    assert(track.pattern.setLaneTimingCustom(0U, 1U, 4U));
    assert(track.pattern.setStepEnabled(0U, 0U, true));
    assert(track.pattern.setStepVelocity(0U, 0U, 80U));
    assert(track.pattern.setStepGate(0U, 0U, 400U));

    drum::DrumPatternRuntimeSnapshot first{};
    drum::captureDrumRuntimeSnapshot(track, first);
    RecordingSink sink;
    DrumPlaybackEngine engine{sink};
    engine.setPattern(&first, nullptr, 9U);
    engine.update(0U, true);
    assert(sink.hasNoteOnVelocity(36U, 0U, 9U, 80U));

    assert(track.pattern.setStepVelocity(0U, 0U, 110U));
    drum::DrumPatternRuntimeSnapshot second{};
    drum::captureDrumRuntimeSnapshot(track, second);
    assert(first.revision != second.revision);
    engine.setPattern(&second, nullptr, 9U);
    engine.update(1U, true);

    assert(sink.eventCount(SequencerEventType::AllNotesOff) == 0U);
    assert(sink.noteOnCount(36U) == 1U);
    for (uint32_t tick = 2U; tick <= 6U; ++tick) {
        engine.update(tick, true);
    }
    assert(sink.noteOnCount(36U) == 2U);
    assert(sink.hasNoteOnVelocity(36U, 6U, 9U, 110U));

    std::cout << "[PASS] Drum expression edits hot-swap without retrigger\n";
}

void test_advanced_micro_sequence_keeps_fixed_drum_pitch() {
    drum::DrumTrackState track;
    track.reset();
    assert(track.kit.setLaneCount(1U));
    assert(track.pattern.setLaneTimingCustom(0U, 1U, 4U));
    assert(track.pattern.setStepEnabled(0U, 0U, true));
    assert(track.pattern.setStepVelocity(0U, 0U, 100U));

    core::state::sequencer::SequencerPatternState graphPattern{};
    constexpr uint8_t rootSlot = 7U;
    const auto sequence = core::state::sequencer::createMicroSequence(
        graphPattern,
        core::state::sequencer::rootStepNodeId(rootSlot),
        2U
    );
    assert(sequence.ok);
    const auto* graph = core::state::sequencer::graphView(graphPattern);
    assert(graph != nullptr);
    const auto* child = graph->sequence(sequence.id);
    assert(child != nullptr);
    assert(core::state::sequencer::setNodeNoteOffset(
        graphPattern,
        child->firstStepNode,
        12
    ));
    assert(core::state::sequencer::setNodeVelocityOffset(
        graphPattern,
        child->firstStepNode,
        -20
    ));
    assert(track.bindAdvancedRootSlot(rootSlot, 0U, 0U));

    drum::DrumPatternRuntimeSnapshot snapshot{};
    drum::captureDrumRuntimeSnapshot(track, snapshot);
    RecordingSink sink;
    DrumPlaybackEngine engine{sink};
    engine.setPattern(&snapshot, graph, 4U);
    for (uint32_t tick = 0U; tick <= 5U; ++tick) {
        engine.update(tick, true);
    }

    const auto signature = engine.captureResolvedPageSignature(0U, 0U);
    drum::DrumResolvedPageProjection resolved{};
    engine.buildResolvedPageProjection(signature, resolved);
    const uint64_t firstCell =
        drum::DrumResolvedPageProjection::cellBit(0U, 0U);
    assert((resolved.validMask & firstCell) != 0U);
    assert((resolved.playedMask & firstCell) != 0U);
    assert(resolved.velocity[0U] == 80U);
    assert(resolved.microLength[0U] == 2U);
    assert(resolved.microMask[0U] == 0x3U);
    assert(resolved.cyclePresentMask == 0U);
    assert(resolved.contextKey == 0U);

    assert(sink.noteOnCount(36U) == 2U);
    assert(sink.noteOnCount(48U) == 0U);
    assert(sink.hasNoteOnVelocity(36U, 0U, 4U, 80U));
    assert(sink.hasNoteOnVelocity(36U, 3U, 4U, 100U));
    assert(!engine.telemetry().diagnostics.schedulerCapacityExceeded);

    std::cout << "[PASS] Drum MicroSequence keeps one fixed lane pitch\n";
}

void test_advanced_cycle_states_follow_lane_loop_cycles() {
    drum::DrumTrackState track;
    track.reset();
    assert(track.kit.setLaneCount(1U));
    assert(track.pattern.setLaneTimingCustom(0U, 1U, 4U));
    assert(track.pattern.setStepEnabled(0U, 0U, true));

    core::state::sequencer::SequencerPatternState graphPattern{};
    constexpr uint8_t rootSlot = 11U;
    const auto cycle = core::state::sequencer::createCycleStateSet(
        graphPattern,
        core::state::sequencer::rootStepNodeId(rootSlot),
        2U
    );
    assert(cycle.ok);
    const auto* graph = core::state::sequencer::graphView(graphPattern);
    assert(graph != nullptr);
    const auto* states = graph->cycleSet(cycle.id);
    assert(states != nullptr);
    assert(core::state::sequencer::setNodeEnabledOverride(
        graphPattern,
        states->firstStateNode,
        false
    ));
    assert(track.bindAdvancedRootSlot(rootSlot, 0U, 0U));

    drum::DrumPatternRuntimeSnapshot snapshot{};
    drum::captureDrumRuntimeSnapshot(track, snapshot);
    RecordingSink sink;
    DrumPlaybackEngine engine{sink};
    engine.setPattern(&snapshot, graph, 6U);
    engine.update(0U, true);
    const auto firstCycleSignature =
        engine.captureResolvedPageSignature(0U, 0U);
    drum::DrumResolvedPageProjection firstCycle{};
    engine.buildResolvedPageProjection(firstCycleSignature, firstCycle);
    const uint64_t firstCell =
        drum::DrumResolvedPageProjection::cellBit(0U, 0U);
    assert((firstCycle.cyclePresentMask & firstCell) != 0U);
    assert((firstCycle.validMask & firstCell) != 0U);
    assert((firstCycle.playedMask & firstCell) == 0U);

    for (uint32_t tick = 1U; tick <= 6U; ++tick) {
        engine.update(tick, true);
    }
    const auto secondCycleSignature =
        engine.captureResolvedPageSignature(0U, 0U);
    assert(!firstCycleSignature.matches(secondCycleSignature));
    drum::DrumResolvedPageProjection secondCycle{};
    engine.buildResolvedPageProjection(secondCycleSignature, secondCycle);
    assert((secondCycle.validMask & firstCell) != 0U);
    assert((secondCycle.playedMask & firstCell) != 0U);

    for (uint32_t tick = 7U; tick <= 12U; ++tick) {
        engine.update(tick, true);
    }

    assert(!sink.hasNoteOn(36U, 0U, 6U));
    assert(sink.hasNoteOn(36U, 6U, 6U));
    assert(!sink.hasNoteOn(36U, 12U, 6U));
    assert(!engine.telemetry().diagnostics.schedulerCapacityExceeded);

    std::cout << "[PASS] Drum Cycle States follow each lane loop cycle\n";
}

void test_track_bank_owns_independent_drum_tracks() {
    drum::SequencerTrackBankState bank{};
    bank.reset();
    bank.syncSharedTrackState(0x000BU, 0U);
    assert(bank.setTrackKind(1U, drum::SequencerTrackKind::DRUM, true));
    assert(bank.setTrackKind(3U, drum::SequencerTrackKind::DRUM, true));
    assert(bank.drumTrackMask() == 0x000AU);

    assert(bank.drumTrack(1U).pattern.setStepEnabled(0U, 1U, true));
    bank.publishDrumMutation(1U);
    assert(bank.drumTrack(3U).pattern.setStepEnabled(1U, 3U, true));
    bank.publishDrumMutation(3U);
    assert(bank.drumTrack(1U).pattern.stepEnabled(0U, 1U));
    assert(!bank.drumTrack(1U).pattern.stepEnabled(1U, 3U));
    assert(bank.drumTrack(3U).pattern.stepEnabled(1U, 3U));
    assert(!bank.drumTrack(3U).pattern.stepEnabled(0U, 1U));

    auto snapshot = std::make_unique<drum::DrumTrackBankSnapshot>();
    assert(snapshot);
    bank.captureDrumTrackBank(*snapshot);
    drum::SequencerTrackBankState restored{};
    restored.reset();
    assert(restored.applyDrumTrackBank(*snapshot));
    assert(restored.drumTrackMask() == 0x000AU);
    assert(restored.drumTrack(1U).pattern.stepEnabled(0U, 1U));
    assert(restored.drumTrack(3U).pattern.stepEnabled(1U, 3U));

    restored.clearDrumTrackBank();
    assert(restored.drumTrackMask() == 0U);
    assert(restored.drumTrack(1U).kit.lanes[0U].midiNote == 36U);
    assert(restored.drumTrack(1U).pattern.lanes[0U].velocity[0U] ==
           drum::DRUM_DEFAULT_VELOCITY);

    std::cout << "[PASS] Track bank keeps independent persistent Drum owners\n";
}

void test_drum_track_codec_is_exact_and_atomic() {
    namespace codec = core::persistence::sequencer_codec;
    drum::DrumTrackState source{};
    source.reset();
    assert(source.kit.setLaneCount(3U));
    assert(source.kit.setLane(
        2U,
        laneDescriptor(
            51U,
            drum::DrumLaneRole::HIGH_TOM,
            drum::DrumLaneIcon::TOM,
            6U,
            "FloorTom"
        )
    ));
    assert(source.pattern.setDefaults(16U, 4U));
    assert(source.pattern.setLaneTimingCustom(1U, 7U, 2U));
    assert(source.pattern.setStepEnabled(1U, 6U, true));
    assert(source.pattern.setStepVelocity(1U, 6U, 117U));
    assert(source.pattern.setStepGate(1U, 6U, 375U));
    assert(source.pattern.setStepNudge(1U, 6U, -23));
    assert(source.pattern.setStepProbability(1U, 6U, 61U));
    assert(source.bindAdvancedRootSlot(11U, 1U, 6U));

    auto bytes = std::make_unique<
        std::array<uint8_t, codec::DRUM_TRACK_RECORD_SIZE>>();
    assert(bytes);
    assert(codec::encodeDrumTrackRecord(
        source,
        bytes->data(),
        static_cast<uint16_t>(bytes->size())
    ));

    drum::DrumTrackState loaded{};
    loaded.reset();
    assert(codec::decodeDrumTrackRecord(
        bytes->data(),
        static_cast<uint16_t>(bytes->size()),
        loaded
    ));
    assert(loaded.kit.laneCount == 3U);
    assert(loaded.kit.lanes[2U].midiNote == 51U);
    assert(loaded.kit.lanes[2U].role == drum::DrumLaneRole::HIGH_TOM);
    assert(loaded.kit.lanes[2U].overrideMask ==
           (drum::DRUM_LANE_OVERRIDE_NAME |
            drum::DRUM_LANE_OVERRIDE_COLOR));
    assert(loaded.kit.lanes[2U].icon == drum::DrumLaneIcon::GENERIC);
    assert(loaded.kit.lanes[2U].colorIndex == 6U);
    assert(drum::drumLaneDisplayIcon(loaded.kit.lanes[2U]) ==
           drum::DrumLaneIcon::TOM);
    assert(std::strcmp(
        drum::drumLaneDisplayName(loaded.kit.lanes[2U]),
        "FloorTom"
    ) == 0);
    assert(loaded.pattern.defaultLength == 16U);
    assert(loaded.pattern.effectiveLength(1U) == 7U);
    assert(loaded.pattern.effectiveStepsPerBeat(1U) == 2U);
    assert(loaded.pattern.stepEnabled(1U, 6U));
    assert(loaded.pattern.lanes[1U].velocity[6U] == 117U);
    assert(loaded.pattern.lanes[1U].gate[6U] == 375U);
    assert(loaded.pattern.lanes[1U].nudge[6U] == -23);
    assert(loaded.pattern.lanes[1U].probability[6U] == 61U);
    assert(loaded.advancedRootSlot(1U, 6U) == 11);

    drum::DrumTrackState sentinel{};
    sentinel.reset();
    assert(sentinel.pattern.setStepEnabled(0U, 2U, true));
    const uint32_t sentinelRevision = sentinel.pattern.revision;
    (*bytes)[3U] = 1U;  // reserved header byte
    assert(!codec::decodeDrumTrackRecord(
        bytes->data(),
        static_cast<uint16_t>(bytes->size()),
        sentinel
    ));
    assert(sentinel.pattern.revision == sentinelRevision);
    assert(sentinel.pattern.stepEnabled(0U, 2U));

    std::cout << "[PASS] Drum record round-trips and rejects atomically\n";
}

}  // namespace

static_assert(
    sizeof(drum::DrumPatternRuntimeSnapshot) < 12U * 1024U,
    "Drum runtime snapshot must remain a cold PSRAM-sized payload"
);
static_assert(
    sizeof(core::sequencer::DrumPlaybackEngine) < 2048U,
    "One Drum Track engine must remain a bounded hot allocation"
);

int main() {
    test_kit_mapping_is_separate_from_rhythm();
    test_lane_roles_resolve_defaults_and_keep_only_real_overrides();
    test_empty_kit_lane_lifecycle_keeps_mapping_and_rhythm_aligned();
    test_three_lane_polymeter_uses_one_channel();
    test_lane_nudge_is_scheduled_on_the_shared_timeline();
    test_duplicate_note_retrigger_replaces_the_stale_note_off();
    test_long_gates_remain_bounded_across_all_sixteen_lanes();
    test_pattern_generation_change_panics_once_and_rephases();
    test_expression_change_hot_swaps_without_retrigger();
    test_advanced_micro_sequence_keeps_fixed_drum_pitch();
    test_advanced_cycle_states_follow_lane_loop_cycles();
    test_track_bank_owns_independent_drum_tracks();
    test_drum_track_codec_is_exact_and_atomic();
    std::cout << "All Drum sequencer core tests passed\n";
    return 0;
}
