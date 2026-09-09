#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <cstdio>
#include <cstdint>
#include <iostream>
#include <limits>
#include <cstring>

#include "state/project/ProjectTrackDomainOps.hpp"

namespace project = core::state::project;

namespace {

void testDefaultsUseExistingMidiConventionAndNoMixFlags() {
    project::ProjectTrackState state{};

    assert(state.revision.get() == 0U);
    assert(state.authored.mutedMask == 0U);
    assert(state.authored.soloMask == 0U);
    for (uint8_t track = 0U; track < project::PROJECT_TRACK_COUNT; ++track) {
        assert(project::projectTrackMidiChannel(state, track) == track);
        assert(project::projectTrackDelayMs(state, track) == 0);
        assert(!project::projectTrackMuted(state, track));
        assert(!project::projectTrackSoloed(state, track));
        assert(project::projectTrackCustomName(state, track)[0] == '\0');
        char name[project::PROJECT_TRACK_NAME_MAX_LENGTH + 1U]{};
        project::formatProjectTrackName(state, track, name, sizeof(name));
        char expected[project::PROJECT_TRACK_NAME_MAX_LENGTH + 1U]{};
        std::snprintf(
            expected,
            sizeof(expected),
            "Track %u",
            static_cast<unsigned>(track + 1U)
        );
        assert(std::strcmp(name, expected) == 0);
    }

    const auto defaults = project::defaultProjectTrackSnapshot();
    assert(project::validProjectTrackSnapshot(defaults));
    assert(project::sameProjectTrackSnapshot(state.authored, defaults));
    assert(sizeof(defaults) == 196U);
}

void testTypedMutationsAreStrictAndNoOpsDoNotPublish() {
    project::ProjectTrackState state{};

    assert(project::setProjectTrackMidiChannel(state, 2U, 10U).changed());
    assert(project::projectTrackMidiChannel(state, 2U) == 10U);
    assert(state.revision.get() == 1U);
    assert(project::setProjectTrackMidiChannel(state, 2U, 10U).status ==
           project::ProjectTrackMutationStatus::NO_CHANGE);
    assert(state.revision.get() == 1U);

    assert(project::setProjectTrackDelayMs(state, 2U, -100).changed());
    assert(project::projectTrackDelayMs(state, 2U) == -100);
    assert(state.revision.get() == 2U);
    assert(project::setProjectTrackDelayMs(state, 2U, -100).status ==
           project::ProjectTrackMutationStatus::NO_CHANGE);
    assert(state.revision.get() == 2U);

    const auto stable = state.authored;
    assert(project::setProjectTrackMidiChannel(state, 16U, 0U).status ==
           project::ProjectTrackMutationStatus::INVALID_TRACK);
    assert(project::setProjectTrackMidiChannel(state, 2U, 16U).status ==
           project::ProjectTrackMutationStatus::INVALID_MIDI_CHANNEL);
    assert(project::setProjectTrackDelayMs(state, 2U, -101).status ==
           project::ProjectTrackMutationStatus::INVALID_DELAY);
    assert(project::setProjectTrackDelayMs(state, 2U, 101).status ==
           project::ProjectTrackMutationStatus::INVALID_DELAY);
    assert(project::sameProjectTrackSnapshot(state.authored, stable));
    assert(state.revision.get() == 2U);

    assert(project::setProjectTrackDelayMs(state, 2U, 100).changed());
    assert(project::projectTrackDelayMs(state, 2U) == 100);

    assert(project::setProjectTrackName(state, 2U, "Bass").changed());
    assert(std::strcmp(project::projectTrackCustomName(state, 2U), "Bass") == 0);
    assert(project::setProjectTrackName(state, 2U, "Bass").status ==
           project::ProjectTrackMutationStatus::NO_CHANGE);
    assert(project::setProjectTrackName(state, 2U, "Track 3").changed());
    assert(project::projectTrackCustomName(state, 2U)[0] == '\0');
    assert(project::setProjectTrackName(state, 2U, "123456789").status ==
           project::ProjectTrackMutationStatus::INVALID_NAME);
}

void testMuteSoloAreIndependentAndAudibilityIsDeterministic() {
    project::ProjectTrackState state{};
    constexpr uint16_t enabled = 0x000FU;

    assert(project::audibleMask(state, enabled) == enabled);
    assert(project::setProjectTrackMuted(state, 1U, true).changed());
    assert(project::projectTrackMuted(state, 1U));
    assert(project::audibleMask(state, enabled) == 0x000DU);

    assert(project::setProjectTrackSoloed(state, 1U, true).changed());
    assert(project::setProjectTrackSoloed(state, 2U, true).changed());
    assert(project::projectTrackMuted(state, 1U));
    assert(project::projectTrackSoloed(state, 1U));
    assert(project::audibleMask(state, enabled) == 0x0004U);

    // Enabled remains a caller-owned input: flags on disabled Tracks persist.
    assert(project::setProjectTrackSoloMask(state, 0x8000U).changed());
    assert(project::audibleMask(state, enabled) == 0U);
    assert(state.authored.mutedMask == 0x0002U);

    assert(project::setProjectTrackMutedMask(state, 0x8002U).changed());
    assert(project::setProjectTrackMutedMask(state, 0x8002U).status ==
           project::ProjectTrackMutationStatus::NO_CHANGE);
    assert(state.authored.soloMask == 0x8000U);
}

void testSnapshotApplyIsAtomicCompactAndOneRevision() {
    project::ProjectTrackState state{};
    auto snapshot = project::defaultProjectTrackSnapshot();
    snapshot.midiChannels[0] = 15U;
    snapshot.delayMs[0] = -37;
    snapshot.mutedMask = 0x0050U;
    snapshot.soloMask = 0x1040U;
    std::strncpy(snapshot.names[0].data(), "Lead", snapshot.names[0].size());

    assert(project::applyProjectTrackSnapshot(state, snapshot).changed());
    assert(state.revision.get() == 1U);
    assert(project::sameProjectTrackSnapshot(state.authored, snapshot));
    assert(project::applyProjectTrackSnapshot(state, snapshot).status ==
           project::ProjectTrackMutationStatus::NO_CHANGE);
    assert(state.revision.get() == 1U);

    project::ProjectTrackSnapshot captured{};
    project::captureProjectTrackSnapshot(state, captured);
    assert(project::sameProjectTrackSnapshot(captured, snapshot));

    auto invalidChannel = snapshot;
    invalidChannel.midiChannels[15] = 16U;
    assert(project::applyProjectTrackSnapshot(state, invalidChannel).status ==
           project::ProjectTrackMutationStatus::INVALID_SNAPSHOT);
    auto invalidDelay = snapshot;
    invalidDelay.delayMs[15] = 101;
    assert(project::applyProjectTrackSnapshot(state, invalidDelay).status ==
           project::ProjectTrackMutationStatus::INVALID_SNAPSHOT);
    auto invalidName = snapshot;
    invalidName.names[15].fill('x');
    assert(project::applyProjectTrackSnapshot(state, invalidName).status ==
           project::ProjectTrackMutationStatus::INVALID_SNAPSHOT);
    assert(project::sameProjectTrackSnapshot(state.authored, snapshot));
    assert(state.revision.get() == 1U);

    assert(project::resetProjectTracks(state).changed());
    assert(state.revision.get() == 2U);
    assert(project::sameProjectTrackSnapshot(
        state.authored,
        project::defaultProjectTrackSnapshot()
    ));
    assert(project::resetProjectTracks(state).status ==
           project::ProjectTrackMutationStatus::NO_CHANGE);
    assert(state.revision.get() == 2U);
}

void testRevisionWrapsToNonZero() {
    project::ProjectTrackState state{};
    state.revision.set(std::numeric_limits<uint32_t>::max());
    assert(project::setProjectTrackMuted(state, 15U, true).changed());
    assert(state.revision.get() == 1U);
}

}  // namespace

int main() {
    testDefaultsUseExistingMidiConventionAndNoMixFlags();
    testTypedMutationsAreStrictAndNoOpsDoNotPublish();
    testMuteSoloAreIndependentAndAudibilityIsDeterministic();
    testSnapshotApplyIsAtomicCompactAndOneRevision();
    testRevisionWrapsToNonZero();
    std::cout << "Project Track domain tests passed\n";
    return 0;
}
