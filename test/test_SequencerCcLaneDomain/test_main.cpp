#ifdef NDEBUG
#undef NDEBUG
#endif

#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>

#include "sequencer/SequencerCcLaneRuntime.hpp"
#include "state/sequencer/SequencerCcLaneDomain.hpp"
#include "state/sequencer/SequencerCcLaneRouting.hpp"
#include "state/shared/MidiCcDestinationResolver.hpp"

namespace {

namespace seq = core::state::sequencer;
namespace shared = core::state::shared;

seq::SequencerCcLaneDraft draft(
    uint8_t cc,
    seq::SequencerCcLaneRoutePolicy policy =
        seq::SequencerCcLaneRoutePolicy::INHERIT_TRACK,
    uint8_t channel = 0,
    uint8_t port = 0,
    uint8_t initial = 64
) {
    return {
        .destination = seq::SequencerCcLaneDestination{
            .controller = cc,
            .minimum = 0,
            .maximum = 127,
            .routePolicy = policy,
            .pinnedPort = port,
            .pinnedChannel = channel,
        },
        .initialValue = initial,
    };
}

seq::SequencerCcTrackRoute route(
    uint8_t channel,
    uint8_t port = 0,
    shared::MidiCcRouteValidity validity = shared::MidiCcRouteValidity::VALID
) {
    return {
        .port = port,
        .channel = channel,
        .validity = validity,
    };
}

void createWithEvent(
    seq::SequencerCcLaneBank& bank,
    uint8_t lane,
    uint8_t cc,
    uint8_t step,
    uint8_t value,
    seq::SequencerCcLaneRoutePolicy policy =
        seq::SequencerCcLaneRoutePolicy::INHERIT_TRACK,
    uint8_t pinnedChannel = 0
) {
    assert(seq::createSequencerCcLane(
        bank,
        lane,
        draft(cc, policy, pinnedChannel)
    ).changed());
    assert(seq::setSequencerCcLaneEvent(bank, lane, step, value).changed());
}

struct RuntimeFixture {
    std::array<seq::SequencerCcLaneBank, 16> banks{};
    core::sequencer::SequencerCcLaneRuntime::Inputs inputs{};
    core::sequencer::SequencerCcLaneRuntime runtime{};

    RuntimeFixture() {
        for (uint8_t track = 0; track < inputs.size(); ++track) {
            inputs[track].lanes = &banks[track];
            inputs[track].route = route(track);
            inputs[track].enabled = true;
            inputs[track].step = 0;
            inputs[track].stepTriggered = true;
        }
    }

    core::sequencer::SequencerCcLaneRuntimeFrame tick(bool playing = true) {
        core::sequencer::SequencerCcLaneRuntimeFrame frame{};
        const auto status = runtime.buildMusicalTickFrame(inputs, playing, frame);
        assert(status == frame.status);
        return frame;
    }
};

void testCreateIsSilentAndInitialIsEditProposal() {
    seq::SequencerCcLaneBank bank{};
    const auto before = bank;
    const auto created = seq::createSequencerCcLane(bank, 0, draft(74));
    assert(created.changed());
    assert(seq::sequencerCcLaneCount(bank) == 1);
    assert(!bank.lanes[0].activeMask.any());
    assert((bank.lanes[0].values == std::array<uint8_t, 128>{}));
    assert(bank.revision == before.revision + 1U);

    assert(seq::proposedSequencerCcLaneEventValue(bank.lanes[0], 12, 16) == 64);
    assert(seq::setSequencerCcLaneEvent(bank, 0, 3, 91).changed());
    // Every `--` starts at Initial; the held Live value is separate telemetry.
    assert(seq::proposedSequencerCcLaneEventValue(bank.lanes[0], 4, 16) == 64);
    assert(seq::proposedSequencerCcLaneEventValue(bank.lanes[0], 3, 16) == 91);
}

void testMutationsAreStrictAndAtomic() {
    seq::SequencerCcLaneBank bank{};
    assert(seq::createSequencerCcLane(bank, 0, draft(1)).changed());
    const auto stable = bank;

    auto invalidDraft = draft(1);
    invalidDraft.destination.minimum = 100;
    invalidDraft.destination.maximum = 20;
    assert(seq::updateSequencerCcLaneSettings(bank, 0, invalidDraft).status ==
           seq::SequencerCcLaneMutationStatus::INVALID_DESTINATION);
    assert(std::memcmp(&bank, &stable, sizeof(bank)) == 0);

    assert(seq::setSequencerCcLaneEvent(bank, 0, 128, 10).status ==
           seq::SequencerCcLaneMutationStatus::INVALID_STEP);
    assert(std::memcmp(&bank, &stable, sizeof(bank)) == 0);

    auto ranged = draft(1);
    ranged.destination.minimum = 20;
    ranged.destination.maximum = 100;
    assert(seq::updateSequencerCcLaneSettings(bank, 0, ranged).changed());
    const auto rangedStable = bank;
    assert(seq::setSequencerCcLaneEvent(bank, 0, 2, 19).status ==
           seq::SequencerCcLaneMutationStatus::INVALID_DESTINATION);
    assert(std::memcmp(&bank, &rangedStable, sizeof(bank)) == 0);
}

void testFourLaneCapacityAndCanonicalDecode() {
    seq::SequencerCcLaneBank bank{};
    for (uint8_t lane = 0; lane < seq::SequencerCcLaneBank::MAX_LANES; ++lane) {
        assert(seq::createSequencerCcLane(
            bank,
            lane,
            draft(static_cast<uint8_t>(20U + lane))
        ).changed());
    }
    assert(seq::sequencerCcLaneCount(bank) == 4);
    assert(seq::firstFreeSequencerCcLane(bank) == -1);

    seq::SequencerCcLaneBank persisted{};
    persisted.lanes[2].values[73] = 99;  // stale reserved data in empty lane
    assert(!seq::validSequencerCcLaneBank(persisted));
    seq::SequencerCcLaneBank decoded{};
    assert(seq::decodeCanonicalSequencerCcLaneBank(persisted, decoded));
    assert(seq::validSequencerCcLaneBank(decoded));
    assert(decoded.lanes[2].values[73] == 0);

    persisted.formatVersion = 99;
    const auto decodedBefore = decoded;
    assert(!seq::decodeCanonicalSequencerCcLaneBank(persisted, decoded));
    assert(std::memcmp(&decoded, &decodedBefore, sizeof(decoded)) == 0);
}

void testInheritedPinnedAndNoRouteResolution() {
    seq::SequencerCcLaneBank bank{};
    assert(seq::createSequencerCcLane(bank, 0, draft(74)).changed());
    auto resolved = seq::resolveSequencerCcLaneDestination(bank.lanes[0], route(4, 2));
    assert(resolved.ok());
    assert(resolved.destination.identity.port == 2);
    assert(resolved.destination.identity.channel == 4);

    const auto noRoute = route(
        shared::MidiCcDestinationIdentity::INVALID_CHANNEL,
        shared::MidiCcDestinationIdentity::INVALID_PORT,
        shared::MidiCcRouteValidity::NO_ROUTE
    );
    resolved = seq::resolveSequencerCcLaneDestination(bank.lanes[0], noRoute);
    assert(resolved.ok());
    assert(resolved.destination.routeValidity == shared::MidiCcRouteValidity::NO_ROUTE);

    assert(seq::createSequencerCcLane(
        bank,
        1,
        draft(71, seq::SequencerCcLaneRoutePolicy::PINNED, 11, 3)
    ).changed());
    resolved = seq::resolveSequencerCcLaneDestination(bank.lanes[1], route(1, 0));
    assert(resolved.ok());
    assert(resolved.destination.identity.port == 3);
    assert(resolved.destination.identity.channel == 11);

    const auto unassigned = seq::makeSequencerCcTrackRoute(
        0,
        shared::MidiCcDestinationIdentity::INVALID_CHANNEL
    );
    assert(unassigned.validity == shared::MidiCcRouteValidity::NO_ROUTE);
    assert(unassigned.port == shared::MidiCcDestinationIdentity::INVALID_PORT);
    assert(unassigned.channel ==
           shared::MidiCcDestinationIdentity::INVALID_CHANNEL);
    resolved = seq::resolveSequencerCcLaneDestination(bank.lanes[0], unassigned);
    assert(resolved.ok());
    assert(resolved.destination.routeValidity == shared::MidiCcRouteValidity::NO_ROUTE);
    // Pinning is lane-local and remains valid while the Track is unassigned.
    resolved = seq::resolveSequencerCcLaneDestination(bank.lanes[1], unassigned);
    assert(resolved.ok());
    assert(resolved.destination.routeValidity == shared::MidiCcRouteValidity::VALID);
    assert(resolved.destination.identity.port == 3);
    assert(resolved.destination.identity.channel == 11);
}

void testGlobalDuplicatePreflightAcrossAllPatterns() {
    std::array<seq::SequencerCcLaneBank, 16> banks{};
    seq::SequencerCcProjectRoutingView project{};
    for (uint8_t track = 0; track < project.size(); ++track) {
        project[track].lanes = &banks[track];
        project[track].trackRoute = route(track);
    }

    assert(seq::createSequencerCcLane(banks[0], 0, draft(74)).changed());
    project[15].trackRoute = route(0);  // cross-Pattern collision on Ch1/CC74
    auto result = seq::preflightSequencerCcLaneDestination(
        project,
        {15, 3},
        draft(74)
    );
    assert(result.duplicate);
    assert(result.existing.track == 0 && result.existing.lane == 0);

    project[15].trackRoute = route(15);
    result = seq::preflightSequencerCcLaneDestination(project, {15, 3}, draft(74));
    assert(!result.duplicate);

    // Same-Track inherited duplicates remain blocked while routing is dormant.
    project[2].trackRoute = route(
        shared::MidiCcDestinationIdentity::INVALID_CHANNEL,
        shared::MidiCcDestinationIdentity::INVALID_PORT,
        shared::MidiCcRouteValidity::NO_ROUTE
    );
    assert(seq::createSequencerCcLane(banks[2], 0, draft(18)).changed());
    result = seq::preflightSequencerCcLaneDestination(project, {2, 1}, draft(18));
    assert(result.duplicate);
    assert(result.existing.track == 2);

    // Identical explicit pins are global duplicates regardless of Track routes.
    assert(seq::createSequencerCcLane(
        banks[7],
        0,
        draft(21, seq::SequencerCcLaneRoutePolicy::PINNED, 5, 1)
    ).changed());
    result = seq::preflightSequencerCcLaneDestination(
        project,
        {14, 0},
        draft(21, seq::SequencerCcLaneRoutePolicy::PINNED, 5, 1)
    );
    assert(result.duplicate && result.existing.track == 7);
}

void testCollisionAppearsAfterInterTrackRouteChange() {
    std::array<seq::SequencerCcLaneBank, 16> banks{};
    seq::SequencerCcProjectRoutingView project{};
    for (uint8_t track = 0; track < project.size(); ++track) {
        project[track].lanes = &banks[track];
        project[track].trackRoute = route(track);
    }
    assert(seq::createSequencerCcLane(banks[3], 2, draft(74)).changed());
    assert(seq::createSequencerCcLane(banks[9], 1, draft(74)).changed());
    assert(seq::scanSequencerCcLaneConflicts(project).count == 0);

    project[9].trackRoute = route(3);
    const auto conflicts = seq::scanSequencerCcLaneConflicts(project);
    assert(conflicts.count == 1);
    assert(conflicts.conflicts[0].winner.track == 3);
    assert(conflicts.conflicts[0].winner.lane == 2);
    assert(conflicts.conflicts[0].loser.track == 9);
    assert(conflicts.conflicts[0].loser.lane == 1);
}

void testRuntimeEmptyHoldMigrationMuteStopAndPin() {
    RuntimeFixture h;
    assert(h.tick().candidateCount == 0);  // empty banks are silent

    createWithEvent(h.banks[3], 0, 74, 2, 96);
    h.inputs[3].step = 2;
    auto frame = h.tick();
    assert(frame.ok() && frame.candidateCount == 1);
    assert(frame.authoredEventCount == 1);
    assert(frame.candidates[0].localValue == 96);
    assert(frame.candidates[0].destination.identity.channel == 3);

    h.inputs[3].step = 3;
    frame = h.tick();
    assert(frame.candidateCount == 1);  // stepped hold
    assert(frame.authoredEventCount == 0);
    assert(frame.routeMigrationCount == 0);

    h.inputs[3].route = route(8);
    frame = h.tick();
    assert(frame.candidateCount == 1);
    assert(frame.routeMigrationCount == 1);
    assert(frame.candidates[0].destination.identity.channel == 8);
    // Exactly one new-route candidate: no reset is synthesized for Ch4.

    h.inputs[3].muted = true;
    h.inputs[3].route = route(10);
    frame = h.tick();
    assert(frame.candidateCount == 0);
    assert(h.runtime.hasHeldValue(3, 0));
    h.inputs[3].muted = false;
    frame = h.tick();
    assert(frame.candidateCount == 1 && frame.routeMigrationCount == 1);
    assert(frame.candidates[0].destination.identity.channel == 10);

    frame = h.tick(false);
    assert(frame.candidateCount == 0);
    assert(h.runtime.hasHeldValue(3, 0));
    frame = h.tick(true);
    assert(frame.candidateCount == 1);
    assert(frame.routeMigrationCount == 0);

    createWithEvent(
        h.banks[4],
        0,
        71,
        1,
        88,
        seq::SequencerCcLaneRoutePolicy::PINNED,
        6
    );
    h.inputs[4].step = 1;
    frame = h.tick();
    assert(frame.candidateCount == 2);
    h.inputs[4].route = route(12);
    frame = h.tick();
    bool sawPinned = false;
    for (uint8_t i = 0; i < frame.candidateCount; ++i) {
        if (frame.contributions[i].address.track != 4) continue;
        sawPinned = true;
        assert(frame.candidates[i].destination.identity.channel == 6);
        assert(!frame.contributions[i].routeMigratedThisTick);
    }
    assert(sawPinned);
}

void testRuntimeNoRouteAndMissingBankAreSilentSafeInputs() {
    RuntimeFixture h;
    createWithEvent(h.banks[0], 0, 74, 0, 45);
    h.inputs[0].route = route(
        shared::MidiCcDestinationIdentity::INVALID_CHANNEL,
        shared::MidiCcDestinationIdentity::INVALID_PORT,
        shared::MidiCcRouteValidity::NO_ROUTE
    );
    auto frame = h.tick();
    assert(frame.candidateCount == 1);
    assert(frame.noRouteCount == 1);

    shared::MidiCcResolutionTelemetry telemetry{};
    assert(shared::resolveMidiCcDestinations(
        frame.candidates.data(),
        frame.candidateCount,
        shared::MidiCcResolutionMode::LIVE,
        telemetry
    ) == shared::MidiCcResolveStatus::OK);
    assert(telemetry.emissionCount == 0);

    h.inputs[0].lanes = nullptr;  // canonical V3/no-lanes input
    frame = h.tick();
    assert(frame.ok() && frame.candidateCount == 0);
    assert(!h.runtime.hasHeldValue(0, 0));
}

void testRuntimeFrameIsTransactionalOnMalformedInput() {
    RuntimeFixture h;
    createWithEvent(h.banks[0], 0, 10, 0, 55);
    auto frame = h.tick();
    assert(frame.candidateCount == 1);
    assert(h.runtime.heldValue(0, 0) == 55);

    h.banks[15].formatVersion = 77;
    frame = h.tick();
    assert(frame.status ==
           core::sequencer::SequencerCcLaneRuntimeStatus::INVALID_INPUT);
    assert(frame.candidateCount == 0);
    assert(h.runtime.heldValue(0, 0) == 55);  // no partial state publish

    h.banks[15].formatVersion = seq::SequencerCcLaneBank::FORMAT_VERSION;
    h.inputs[0].stepTriggered = false;
    frame = h.tick();
    assert(frame.ok() && frame.candidateCount == 1);
    assert(frame.candidates[0].localValue == 55);
}

void testFrozenTrackKeepsPreviousAudibleGenerationUntilActivationBoundary() {
    RuntimeFixture h;
    createWithEvent(h.banks[6], 0, 74, 0, 45);
    h.inputs[6].route = route(6);
    auto frame = h.tick();
    assert(frame.candidateCount == 1);
    assert(frame.candidates[0].localValue == 45);
    assert(frame.candidates[0].destination.identity.channel == 6);

    assert(seq::setSequencerCcLaneEvent(h.banks[6], 0, 0, 99).changed());
    h.inputs[6].route = route(12);
    h.inputs[6].frozen = true;
    frame = h.tick();
    assert(frame.candidateCount == 1);
    assert(frame.suppressedTrackCount == 1);
    assert(frame.candidates[0].localValue == 45);
    assert(frame.candidates[0].destination.identity.channel == 6);
    assert(frame.authoredEventCount == 0);
    assert(frame.routeMigrationCount == 0);

    h.inputs[6].frozen = false;
    frame = h.tick();
    assert(frame.candidateCount == 1);
    assert(frame.candidates[0].localValue == 99);
    assert(frame.candidates[0].destination.identity.channel == 12);
    assert(frame.authoredEventCount == 1);
    assert(frame.routeMigrationCount == 1);
}

void testWorstCase64LanesAndResolverPriority() {
    RuntimeFixture h;
    for (uint8_t track = 0; track < 16; ++track) {
        for (uint8_t lane = 0; lane < 4; ++lane) {
            createWithEvent(
                h.banks[track],
                lane,
                static_cast<uint8_t>(20U + lane),
                0,
                static_cast<uint8_t>(track * 4U + lane)
            );
        }
    }
    auto frame = h.tick();
    assert(frame.ok());
    assert(frame.candidateCount == 64);
    assert(frame.authoredEventCount == 64);

    shared::MidiCcResolutionTelemetry resolved{};
    assert(shared::resolveMidiCcDestinations(
        frame.candidates.data(),
        frame.candidateCount,
        shared::MidiCcResolutionMode::LIVE,
        resolved
    ) == shared::MidiCcResolveStatus::OK);
    assert(resolved.destinationCount == 64);
    assert(resolved.emissionCount == 64);

    // One persistent Manual candidate and one computed Macro candidate share
    // T1/L1's destination. Fixed order must be Manual > Lane > Macro.
    std::array<shared::MidiCcCandidate, 3> conflict{
        shared::MidiCcCandidate{
            .destination = frame.candidates[0].destination,
            .author = shared::MidiCcAuthor{
                .candidateClass = shared::MidiCcCandidateClass::MACRO_COMPUTED,
                .stableAddress = 400,
            },
            .localValue = 12,
        },
        frame.candidates[0],
        shared::MidiCcCandidate{
            .destination = frame.candidates[0].destination,
            .author = shared::MidiCcAuthor{
                .candidateClass = shared::MidiCcCandidateClass::LIVE_MANUAL,
                .stableAddress = 400,
            },
            .localValue = 101,
        },
    };
    assert(shared::resolveMidiCcDestinations(
        conflict.data(),
        conflict.size(),
        shared::MidiCcResolutionMode::LIVE,
        resolved
    ) == shared::MidiCcResolveStatus::OK);
    assert(resolved.destinationCount == 1);
    assert(resolved.destinations[0].winner.author.candidateClass ==
           shared::MidiCcCandidateClass::LIVE_MANUAL);
    assert(resolved.destinations[0].finalValue == 101);
    assert(resolved.destinations[0].loserCount == 2);

    assert(sizeof(core::sequencer::SequencerCcLaneRuntime) <= 4096U);
    assert(sizeof(seq::SequencerCcLaneBank) <= 656U);
}

}  // namespace

int main() {
    testCreateIsSilentAndInitialIsEditProposal();
    testMutationsAreStrictAndAtomic();
    testFourLaneCapacityAndCanonicalDecode();
    testInheritedPinnedAndNoRouteResolution();
    testGlobalDuplicatePreflightAcrossAllPatterns();
    testCollisionAppearsAfterInterTrackRouteChange();
    testRuntimeEmptyHoldMigrationMuteStopAndPin();
    testRuntimeNoRouteAndMissingBankAreSilentSafeInputs();
    testRuntimeFrameIsTransactionalOnMalformedInput();
    testFrozenTrackKeepsPreviousAudibleGenerationUntilActivationBoundary();
    testWorstCase64LanesAndResolverPriority();
    std::cout << "All Sequencer CC lane domain/runtime tests passed.\n";
    return 0;
}
