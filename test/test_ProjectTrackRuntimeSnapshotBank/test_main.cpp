#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <cstdint>
#include <iostream>

#include "sequencer/ProjectTrackRuntimeSnapshotBank.hpp"
#include "state/project/ProjectTrackDomainOps.hpp"

namespace project = core::state::project;
namespace sequencer = core::sequencer;

namespace {

const sequencer::ProjectTrackRuntimeSnapshot& requireSnapshot(
    const sequencer::ProjectTrackRuntimeSnapshotBank& bank,
    uint8_t slot
) {
    const auto* snapshot = bank.snapshot(slot);
    assert(snapshot != nullptr);
    return *snapshot;
}

void testExactStaticFootprint() {
    static_assert(sizeof(sequencer::ProjectTrackRuntimeSnapshot) == 60U);
    static_assert(sizeof(sequencer::ProjectTrackRuntimeSnapshotBank) == 120U);
    assert(sizeof(sequencer::ProjectTrackRuntimeSnapshot) == 60U);
    assert(sizeof(sequencer::ProjectTrackRuntimeSnapshotBank) == 120U);
}

void testDefaultProjectionUsesCanonicalTrackDefaults() {
    project::ProjectTrackState state{};
    sequencer::ProjectTrackRuntimeSnapshotBank bank{};

    assert(bank.publish(0U, state, 0x000FU));
    const auto& snapshot = requireSnapshot(bank, 0U);
    assert(snapshot.revision == 0U);
    assert(snapshot.enabledMask == 0x000FU);
    assert(snapshot.mutedMask == 0U);
    assert(snapshot.soloMask == 0U);
    assert(snapshot.audibleMask == 0x000FU);
    for (uint8_t track = 0U; track < project::PROJECT_TRACK_COUNT; ++track) {
        assert(snapshot.midiChannels[track] == track);
        assert(snapshot.delayMs[track] == 0);
    }
}

void testProjectionCapturesChannelsDelaysMasksAndRevision() {
    project::ProjectTrackState state{};
    assert(project::setProjectTrackMidiChannel(state, 2U, 10U).changed());
    assert(project::setProjectTrackDelayMs(state, 2U, -37).changed());
    assert(project::setProjectTrackDelayMs(state, 15U, 100).changed());
    assert(project::setProjectTrackMutedMask(state, 0x0002U).changed());
    assert(project::setProjectTrackSoloMask(state, 0x0006U).changed());

    sequencer::ProjectTrackRuntimeSnapshotBank bank{};
    assert(bank.publish(1U, state, 0x000FU));
    const auto& snapshot = requireSnapshot(bank, 1U);
    assert(snapshot.revision == state.revision.get());
    assert(snapshot.revision == 5U);
    assert(snapshot.midiChannels[2] == 10U);
    assert(snapshot.delayMs[2] == -37);
    assert(snapshot.delayMs[15] == 100);
    assert(snapshot.enabledMask == 0x000FU);
    assert(snapshot.mutedMask == 0x0002U);
    assert(snapshot.soloMask == 0x0006U);
    assert(snapshot.audibleMask == 0x0004U);
}

void testEnabledMaskIsCallerOwnedAndFlagsRemainAuthored() {
    project::ProjectTrackState state{};
    assert(project::setProjectTrackMutedMask(state, 0x8002U).changed());
    assert(project::setProjectTrackSoloMask(state, 0x8004U).changed());

    sequencer::ProjectTrackRuntimeSnapshot snapshot{};
    sequencer::captureProjectTrackRuntimeSnapshot(state, 0x000FU, snapshot);
    assert(snapshot.enabledMask == 0x000FU);
    assert(snapshot.mutedMask == 0x8002U);
    assert(snapshot.soloMask == 0x8004U);
    assert(snapshot.audibleMask == 0x0004U);

    sequencer::captureProjectTrackRuntimeSnapshot(state, 0x0003U, snapshot);
    assert(snapshot.enabledMask == 0x0003U);
    assert(snapshot.mutedMask == 0x8002U);
    assert(snapshot.soloMask == 0x8004U);
    assert(snapshot.audibleMask == 0U);
}

void testSlotsRemainIndependentUntilExplicitlyRepublished() {
    project::ProjectTrackState state{};
    sequencer::ProjectTrackRuntimeSnapshotBank bank{};

    assert(bank.publish(0U, state, 0x0001U));
    const auto slot0Revision = requireSnapshot(bank, 0U).revision;
    assert(project::setProjectTrackMidiChannel(state, 0U, 15U).changed());
    assert(project::setProjectTrackDelayMs(state, 0U, -100).changed());
    assert(project::setProjectTrackSoloed(state, 0U, true).changed());

    assert(bank.publish(1U, state, 0x0001U));
    const auto& slot0 = requireSnapshot(bank, 0U);
    const auto& slot1 = requireSnapshot(bank, 1U);
    assert(slot0.revision == slot0Revision);
    assert(slot0.midiChannels[0] == 0U);
    assert(slot0.delayMs[0] == 0);
    assert(slot0.soloMask == 0U);
    assert(slot1.revision == 3U);
    assert(slot1.midiChannels[0] == 15U);
    assert(slot1.delayMs[0] == -100);
    assert(slot1.soloMask == 0x0001U);
    assert(slot1.audibleMask == 0x0001U);

    assert(bank.publish(0U, state, 0x0003U));
    const auto& refreshedSlot0 = requireSnapshot(bank, 0U);
    assert(refreshedSlot0.revision == slot1.revision);
    assert(refreshedSlot0.enabledMask == 0x0003U);
    assert(refreshedSlot0.midiChannels[0] == slot1.midiChannels[0]);
}

void testInvalidSlotIsRejectedWithoutMutatingEitherSlot() {
    project::ProjectTrackState state{};
    sequencer::ProjectTrackRuntimeSnapshotBank bank{};
    assert(bank.publish(0U, state, 0x0001U));
    assert(bank.publish(1U, state, 0x0002U));

    assert(project::setProjectTrackMuted(state, 0U, true).changed());
    assert(!bank.publish(2U, state, 0xFFFFU));
    assert(bank.snapshot(2U) == nullptr);
    assert(requireSnapshot(bank, 0U).enabledMask == 0x0001U);
    assert(requireSnapshot(bank, 0U).mutedMask == 0U);
    assert(requireSnapshot(bank, 1U).enabledMask == 0x0002U);
    assert(requireSnapshot(bank, 1U).mutedMask == 0U);
}

}  // namespace

int main() {
    testExactStaticFootprint();
    testDefaultProjectionUsesCanonicalTrackDefaults();
    testProjectionCapturesChannelsDelaysMasksAndRevision();
    testEnabledMaskIsCallerOwnedAndFlagsRemainAuthored();
    testSlotsRemainIndependentUntilExplicitlyRepublished();
    testInvalidSlotIsRejectedWithoutMutatingEitherSlot();
    std::cout << "Project Track runtime snapshot bank tests passed\n";
    return 0;
}
