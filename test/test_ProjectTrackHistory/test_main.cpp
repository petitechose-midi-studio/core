#include <cassert>
#include <cstdint>
#include <iostream>

#include "state/project/ProjectHistoryCoordinator.hpp"
#include "state/project/ProjectTrackDomainOps.hpp"
#include "state/project/ProjectTrackDomainServices.hpp"
#include "state/project/ProjectTrackHistory.hpp"

namespace {

namespace project = core::state::project;

struct PublicationProbe {
    uint8_t publishCount = 0U;

    static void publish(void* context) {
        auto* self = static_cast<PublicationProbe*>(context);
        assert(self != nullptr);
        ++self->publishCount;
    }
};

project::ProjectTrackDomainServices makeServices(
    project::ProjectTrackState& state,
    project::ProjectTrackHistoryService& history,
    PublicationProbe& probe
) {
    return project::ProjectTrackDomainServices{
        {state, history},
        {
            .context = &probe,
            .publishCommittedMutation = &PublicationProbe::publish,
        },
    };
}

void test_one_gesture_is_one_atomic_history_command() {
    project::ProjectTrackState state;
    project::ProjectTrackHistoryService history;
    project::ProjectHistoryCoordinator timeline;
    history.setProjectHistoryEventSink(&timeline.eventSink());
    PublicationProbe probe;
    auto services = makeServices(state, history, probe);

    assert(services.beginGesture(
        project::ProjectTrackHistoryActionKind::MidiChannel,
        2U
    ));
    assert(services.setMidiChannel(2U, 5U));
    assert(services.setMidiChannel(2U, 7U));
    assert(services.setMidiChannel(2U, 9U));
    assert(history.undoCount() == 0U);
    assert(timeline.undoCount() == 0U);
    assert(services.endGesture());

    assert(state.authored.midiChannels[2] == 9U);
    assert(history.undoCount() == 1U);
    assert(timeline.undoCount() == 1U);
    assert(probe.publishCount == 1U);
    const auto* entry = history.peekUndo();
    assert(entry != nullptr);
    assert(entry->trackIndex == 2U);
    assert(entry->kind == project::ProjectTrackHistoryActionKind::MidiChannel);
    assert(entry->before.midiChannels[2] == 2U);
    assert(entry->after.midiChannels[2] == 9U);

    assert(services.undo());
    assert(state.authored.midiChannels[2] == 2U);
    assert(history.redoCount() == 1U);
    assert(timeline.redoCount() == 1U);
    assert(services.redo());
    assert(state.authored.midiChannels[2] == 9U);
    assert(probe.publishCount == 3U);

    std::cout << "[PASS] one encoder gesture publishes one atomic Track command\n";
}

void test_all_actions_are_bounded_and_no_op_safe() {
    project::ProjectTrackState state;
    project::ProjectTrackHistoryService history;
    PublicationProbe probe;
    auto services = makeServices(state, history, probe);

    assert(!services.setMidiChannel(0U, 0U));
    assert(!services.setMidiChannel(0U, 16U));
    assert(!services.setDelayMs(0U, 101));
    assert(!services.setDelayMs(0U, -101));
    assert(!services.setMuted(project::PROJECT_TRACK_COUNT, true));
    assert(history.undoCount() == 0U);
    assert(probe.publishCount == 0U);

    assert(services.setDelayMs(0U, -37));
    assert(services.setMuted(1U, true));
    assert(services.setSoloed(3U, true));
    assert(history.undoCount() == 3U);
    assert(state.authored.delayMs[0] == -37);
    assert(project::projectTrackMuted(state, 1U));
    assert(project::projectTrackSoloed(state, 3U));

    assert(services.undo());
    assert(!project::projectTrackSoloed(state, 3U));
    assert(services.undo());
    assert(!project::projectTrackMuted(state, 1U));
    assert(services.undo());
    assert(state.authored.delayMs[0] == 0);

    std::cout << "[PASS] Channel, Delay, Mute and Solo reject invalid/no-op edits\n";
}

void test_multi_track_mute_mask_is_one_atomic_history_command() {
    project::ProjectTrackState state;
    project::ProjectTrackHistoryService history;
    project::ProjectHistoryCoordinator timeline;
    history.setProjectHistoryEventSink(&timeline.eventSink());
    PublicationProbe probe;
    auto services = makeServices(state, history, probe);

    assert(!services.setMutedMask(
        0x0005U,
        project::PROJECT_TRACK_COUNT
    ));
    assert(state.authored.mutedMask == 0U);
    assert(history.undoCount() == 0U);

    assert(services.setMutedMask(0x0005U, 0U));
    assert(state.authored.mutedMask == 0x0005U);
    assert(history.undoCount() == 1U);
    assert(timeline.undoCount() == 1U);
    assert(probe.publishCount == 1U);
    const auto* entry = history.peekUndo();
    assert(entry != nullptr);
    assert(entry->kind == project::ProjectTrackHistoryActionKind::Mute);
    assert(entry->trackIndex == 0U);
    assert(entry->before.mutedMask == 0U);
    assert(entry->after.mutedMask == 0x0005U);

    assert(services.undo());
    assert(state.authored.mutedMask == 0U);
    assert(services.redo());
    assert(state.authored.mutedMask == 0x0005U);

    std::cout
        << "[PASS] multi-Track Mute mask is one global history command\n";
}

void test_cancel_restores_exact_state_without_history_or_publish() {
    project::ProjectTrackState state;
    project::ProjectTrackHistoryService history;
    PublicationProbe probe;
    auto services = makeServices(state, history, probe);
    const auto initial = state.authored;

    assert(services.beginGesture(
        project::ProjectTrackHistoryActionKind::Delay,
        4U
    ));
    assert(services.setDelayMs(4U, 42));
    assert(!services.setMuted(4U, true));
    assert(services.cancelGesture());
    assert(project::sameProjectTrackSnapshot(state.authored, initial));
    assert(history.undoCount() == 0U);
    assert(probe.publishCount == 0U);

    std::cout << "[PASS] cancelled gestures restore exact state without history\n";
}

void test_capacity_is_bounded_and_identities_remain_valid() {
    project::ProjectTrackState state;
    project::ProjectTrackHistoryService history;
    project::ProjectHistoryCoordinator timeline;
    history.setProjectHistoryEventSink(&timeline.eventSink());
    PublicationProbe probe;
    auto services = makeServices(state, history, probe);

    for (uint8_t value = 1U;
         value <= project::ProjectTrackHistoryService::ENTRY_LIMIT + 2U;
         ++value) {
        assert(services.setDelayMs(0U, value));
    }
    assert(history.undoCount() == project::ProjectTrackHistoryService::ENTRY_LIMIT);
    assert(timeline.undoCount() == project::ProjectTrackHistoryService::ENTRY_LIMIT);

    for (uint8_t i = 0U;
         i < project::ProjectTrackHistoryService::ENTRY_LIMIT;
         ++i) {
        assert(services.undo());
    }
    assert(!services.undo());
    // The first two commands were evicted; their applied result is the exact
    // barrier before the oldest retained payload.
    assert(state.authored.delayMs[0] == 2);

    std::cout << "[PASS] fixed eight-slot history establishes an exact barrier\n";
}

void test_history_is_fail_closed_after_external_divergence() {
    project::ProjectTrackState state;
    project::ProjectTrackHistoryService history;
    PublicationProbe probe;
    auto services = makeServices(state, history, probe);

    assert(services.setMidiChannel(5U, 11U));
    assert(project::setProjectTrackMidiChannel(state, 5U, 12U).changed());
    assert(!services.undo());
    assert(state.authored.midiChannels[5] == 12U);
    assert(history.undoCount() == 1U);

    std::cout << "[PASS] history refuses to overwrite externally diverged state\n";
}

}  // namespace

int main() {
    test_one_gesture_is_one_atomic_history_command();
    test_all_actions_are_bounded_and_no_op_safe();
    test_multi_track_mute_mask_is_one_atomic_history_command();
    test_cancel_restores_exact_state_without_history_or_publish();
    test_capacity_is_bounded_and_identities_remain_valid();
    test_history_is_fail_closed_after_external_divergence();

    std::cout << "\nAll ProjectTrackHistory tests passed.\n";
    return 0;
}
