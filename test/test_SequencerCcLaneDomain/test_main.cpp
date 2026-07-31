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

void testBulkPatternTransformsPreserveCcValuesAndTransitions() {
    seq::SequencerCcLaneBank bank{};
    createWithEvent(bank, 0, 74, 0, 20);
    assert(seq::setSequencerCcLaneTransition(
        bank,
        0,
        0,
        seq::SequencerCcLaneTransition::EASE_IN_OUT
    ).changed());
    const uint32_t beforeDuplicate = bank.revision;
    assert(seq::duplicateSequencerCcLaneBankRange(bank, 0, 8, 8));
    assert(bank.revision == beforeDuplicate + 1U);
    assert(bank.lanes[0].activeMask.test(8));
    assert(bank.lanes[0].values[8] == 20);
    assert(seq::sequencerCcLaneTransition(bank.lanes[0], 8) ==
           seq::SequencerCcLaneTransition::EASE_IN_OUT);

    const uint32_t beforeRotate = bank.revision;
    assert(seq::rotateSequencerCcLaneBank(bank, 16, 1));
    assert(bank.revision == beforeRotate + 1U);
    assert(bank.lanes[0].activeMask.test(1));
    assert(bank.lanes[0].activeMask.test(9));
    assert(!bank.lanes[0].activeMask.test(0));

    seq::SequencerCcLaneBank structural{};
    createWithEvent(structural, 0, 71, 2, 30);
    assert(seq::setSequencerCcLaneEvent(structural, 0, 10, 100).changed());
    assert(seq::setSequencerCcLaneTransition(
        structural,
        0,
        10,
        seq::SequencerCcLaneTransition::LINEAR
    ).changed());

    const uint32_t beforeInsert = structural.revision;
    assert(seq::insertSequencerCcLaneBankSpan(structural, 16, 8, 8));
    assert(structural.revision == beforeInsert + 1U);
    assert(structural.lanes[0].activeMask.test(2));
    assert(structural.lanes[0].activeMask.test(18));
    assert(structural.lanes[0].values[18] == 100);
    assert(seq::sequencerCcLaneTransition(structural.lanes[0], 18) ==
           seq::SequencerCcLaneTransition::LINEAR);
    assert(!structural.lanes[0].activeMask.test(10));

    const uint32_t beforeRemove = structural.revision;
    assert(seq::removeSequencerCcLaneBankSpan(structural, 24, 0, 8));
    assert(structural.revision == beforeRemove + 1U);
    assert(!structural.lanes[0].activeMask.test(2));
    assert(structural.lanes[0].activeMask.test(10));
    assert(structural.lanes[0].values[10] == 100);
    assert(seq::sequencerCcLaneTransition(structural.lanes[0], 10) ==
           seq::SequencerCcLaneTransition::LINEAR);

    const uint32_t beforeTrim = structural.revision;
    assert(seq::trimSequencerCcLaneBank(structural, 8));
    assert(structural.revision == beforeTrim + 1U);
    assert(!structural.lanes[0].activeMask.test(10));
}

void testRuntimeProjectionUsesRegionAndResetsTransactionally() {
    seq::SequencerCcLaneBank bank{};
    createWithEvent(bank, 0, 74, 1, 10);
    assert(seq::setSequencerCcLaneTransition(
        bank,
        0,
        1,
        seq::SequencerCcLaneTransition::LINEAR
    ).changed());
    assert(seq::setSequencerCcLaneEvent(bank, 0, 4, 40).changed());

    core::sequencer::SequencerCcLaneRuntime runtime;
    core::sequencer::SequencerCcLaneRuntime::Inputs inputs{};
    const oc::note::sequencer::StepSequencerPlaybackRegion region{8, 1, 3, 6};
    inputs[0] = {
        .lanes = &bank,
        .route = route(0),
        .step = 1,
        .patternLength = 8,
        .tickInStep = 0,
        .ticksPerStep = 1,
        .playbackOrdinal = 0,
        .playbackRegion = region,
        .enabled = true,
        .stepTriggered = true,
    };

    core::sequencer::SequencerCcLaneRuntimeFrame frame{};
    for (uint32_t ordinal = 0; ordinal <= 3; ++ordinal) {
        oc::note::sequencer::StepSequencerPlaybackPosition position{};
        assert(oc::note::sequencer::tryResolvePlaybackOrdinal(
            region,
            ordinal,
            position
        ));
        inputs[0].playbackOrdinal = ordinal;
        inputs[0].step = position.stepIndex;
        inputs[0].stepTriggered = true;
        assert(runtime.buildMusicalTickFrame(inputs, true, frame) ==
               core::sequencer::SequencerCcLaneRuntimeStatus::OK);
        assert(frame.candidateCount == 1);
        assert(frame.candidates[0].localValue ==
               static_cast<uint8_t>(10U + ordinal * 10U));
    }
    assert(runtime.hasHeldValue(0, 0));
    assert(runtime.heldValue(0, 0) == 40);

    // A malformed later Track must not publish the pending region reset of T1.
    seq::SequencerCcLaneBank malformed{};
    malformed.formatVersion = 99;
    inputs[0].playbackRegion = {8, 6, 6, 8};
    inputs[0].playbackOrdinal = 0;
    inputs[0].step = 6;
    inputs[1] = {
        .lanes = &malformed,
        .route = route(1),
        .step = 0,
        .patternLength = 8,
        .ticksPerStep = 1,
        .playbackOrdinal = 0,
        .playbackRegion = {8, 0, 0, 8},
        .enabled = true,
        .stepTriggered = true,
    };
    assert(runtime.buildMusicalTickFrame(inputs, true, frame) ==
           core::sequencer::SequencerCcLaneRuntimeStatus::INVALID_INPUT);
    assert(runtime.hasHeldValue(0, 0));
    assert(runtime.heldValue(0, 0) == 40);

    inputs[1].lanes = nullptr;
    assert(runtime.buildMusicalTickFrame(inputs, true, frame) ==
           core::sequencer::SequencerCcLaneRuntimeStatus::OK);
    assert(frame.candidateCount == 0);
    assert(!runtime.hasHeldValue(0, 0));
}

void setRuntimePosition(
    core::sequencer::SequencerCcLaneTrackRuntimeInput& input,
    const seq::SequencerCcLaneBank& bank,
    const oc::note::sequencer::StepSequencerPlaybackRegion& region,
    uint32_t ordinal,
    bool stepTriggered = false,
    uint8_t tickInStep = 0,
    uint8_t ticksPerStep = 1
) {
    oc::note::sequencer::StepSequencerPlaybackPosition position{};
    assert(oc::note::sequencer::tryResolvePlaybackOrdinal(
        region,
        ordinal,
        position
    ));
    input.lanes = &bank;
    input.route = route(0);
    input.step = position.stepIndex;
    input.patternLength = region.contentLength;
    input.tickInStep = tickInStep;
    input.ticksPerStep = ticksPerStep;
    input.playbackOrdinal = ordinal;
    input.playbackRegion = region;
    input.enabled = true;
    input.stepTriggered = stepTriggered;
}

void testPredictiveScratchCapturesFirstEventWithoutAdvancingAudibleState() {
    seq::SequencerCcLaneBank bank{};
    createWithEvent(bank, 0, 74, 2, 42);
    const auto region =
        oc::note::sequencer::StepSequencerPlaybackRegion::fullLength(8);

    core::sequencer::SequencerCcLaneRuntime audible{};
    core::sequencer::SequencerCcLaneRuntime::Inputs currentInputs{};
    setRuntimePosition(currentInputs[0], bank, region, 0, true);
    core::sequencer::SequencerCcLaneRuntimeFrame frame{};
    assert(audible.buildMusicalTickFrame(currentInputs, true, frame) ==
           core::sequencer::SequencerCcLaneRuntimeStatus::OK);
    assert(frame.candidateCount == 0);
    assert(!audible.hasHeldValue(0, 0));

    const auto audibleBefore = audible;
    core::sequencer::SequencerCcLaneRuntime scratch{};
    assert(scratch.seedFrom(audible));
    auto lookaheadInputs = currentInputs;
    setRuntimePosition(lookaheadInputs[0], bank, region, 3, false);
    lookaheadInputs[0].emissionMode =
        core::sequencer::SequencerCcLaneEmissionMode::PREDICTIVE_LOOKAHEAD;
    lookaheadInputs[0].lookaheadStartOrdinal = 0;
    assert(scratch.buildMusicalTickFrame(lookaheadInputs, true, frame) ==
           core::sequencer::SequencerCcLaneRuntimeStatus::OK);
    assert(frame.candidateCount == 1);
    assert(frame.authoredEventCount == 1);
    assert(frame.candidates[0].localValue == 42);
    assert(scratch.hasHeldValue(0, 0));
    assert(!audible.hasHeldValue(0, 0));
    assert(std::memcmp(&audible, &audibleBefore, sizeof(audible)) == 0);
}

void testScratchSeedCopiesCompleteAudibleState() {
    std::array<seq::SequencerCcLaneBank, 2> banks{};
    for (uint8_t lane = 0; lane < 4; ++lane) {
        createWithEvent(
            banks[0],
            lane,
            static_cast<uint8_t>(70U + lane),
            0,
            static_cast<uint8_t>(20U + lane)
        );
    }
    createWithEvent(banks[1], 0, 90, 0, 99);
    const auto region =
        oc::note::sequencer::StepSequencerPlaybackRegion::fullLength(8);
    core::sequencer::SequencerCcLaneRuntime source{};
    core::sequencer::SequencerCcLaneRuntime::Inputs inputs{};
    setRuntimePosition(inputs[0], banks[0], region, 0, true);
    setRuntimePosition(inputs[1], banks[1], region, 0, true);
    inputs[1].route = route(1);
    core::sequencer::SequencerCcLaneRuntimeFrame frame{};
    assert(source.buildMusicalTickFrame(inputs, true, frame) ==
           core::sequencer::SequencerCcLaneRuntimeStatus::OK);
    assert(frame.candidateCount == 5);

    core::sequencer::SequencerCcLaneRuntime scratch{};
    assert(scratch.seedFrom(source));
    for (uint8_t lane = 0; lane < 4; ++lane) {
        assert(scratch.hasHeldValue(0, lane));
        assert(scratch.heldValue(0, lane) == static_cast<uint8_t>(20U + lane));
    }
    assert(scratch.hasHeldValue(1, 0));
    assert(scratch.heldValue(1, 0) == 99U);
}

void testPredictiveScratchDoesNotResurrectPreWindowLifecycleEvent() {
    seq::SequencerCcLaneBank bank{};
    createWithEvent(bank, 0, 74, 1, 55);
    const auto region =
        oc::note::sequencer::StepSequencerPlaybackRegion::fullLength(8);

    core::sequencer::SequencerCcLaneRuntime audible{};
    core::sequencer::SequencerCcLaneRuntime::Inputs inputs{};
    setRuntimePosition(inputs[0], bank, region, 1, true);
    core::sequencer::SequencerCcLaneRuntimeFrame frame{};
    assert(audible.buildMusicalTickFrame(inputs, true, frame) ==
           core::sequencer::SequencerCcLaneRuntimeStatus::OK);
    assert(audible.heldValue(0, 0) == 55);

    // Replacing the lane creates a new lifecycle. Its only event is before the
    // predictive observation boundary and therefore must remain silent.
    assert(seq::deleteSequencerCcLane(bank, 0).changed());
    createWithEvent(bank, 0, 74, 0, 99);
    core::sequencer::SequencerCcLaneRuntime scratch{};
    assert(scratch.seedFrom(audible));
    setRuntimePosition(inputs[0], bank, region, 3, false);
    inputs[0].emissionMode =
        core::sequencer::SequencerCcLaneEmissionMode::PREDICTIVE_LOOKAHEAD;
    inputs[0].lookaheadStartOrdinal = 2;
    assert(scratch.buildMusicalTickFrame(inputs, true, frame) ==
           core::sequencer::SequencerCcLaneRuntimeStatus::OK);
    assert(frame.candidateCount == 0);
    assert(!scratch.hasHeldValue(0, 0));
    assert(audible.heldValue(0, 0) == 55);
}

void testPredictiveScratchFindsEventAcrossLoopWrap() {
    seq::SequencerCcLaneBank bank{};
    createWithEvent(bank, 0, 74, 2, 91);
    const oc::note::sequencer::StepSequencerPlaybackRegion region{8, 0, 2, 6};

    core::sequencer::SequencerCcLaneRuntime audible{};
    core::sequencer::SequencerCcLaneRuntime::Inputs inputs{};
    setRuntimePosition(inputs[0], bank, region, 5, false);
    core::sequencer::SequencerCcLaneRuntimeFrame frame{};
    assert(audible.buildMusicalTickFrame(inputs, true, frame) ==
           core::sequencer::SequencerCcLaneRuntimeStatus::OK);
    assert(frame.candidateCount == 0);

    core::sequencer::SequencerCcLaneRuntime scratch{};
    assert(scratch.seedFrom(audible));
    setRuntimePosition(inputs[0], bank, region, 6, false);
    inputs[0].emissionMode =
        core::sequencer::SequencerCcLaneEmissionMode::PREDICTIVE_LOOKAHEAD;
    inputs[0].lookaheadStartOrdinal = 5;
    assert(scratch.buildMusicalTickFrame(inputs, true, frame) ==
           core::sequencer::SequencerCcLaneRuntimeStatus::OK);
    assert(frame.candidateCount == 1);
    assert(frame.authoredEventCount == 1);
    assert(frame.candidates[0].localValue == 91);
}

void testPredictiveScratchAcceptsBoundedOrdinalAcrossUint32Wrap() {
    seq::SequencerCcLaneBank bank{};
    createWithEvent(bank, 0, 74, 0, 87);
    const auto region =
        oc::note::sequencer::StepSequencerPlaybackRegion::fullLength(8);

    constexpr uint32_t startOrdinal = UINT32_MAX - 2U;
    constexpr uint32_t futureOrdinal = 1U;
    core::sequencer::SequencerCcLaneRuntime audible{};
    core::sequencer::SequencerCcLaneRuntime::Inputs inputs{};
    setRuntimePosition(inputs[0], bank, region, startOrdinal, false);
    core::sequencer::SequencerCcLaneRuntimeFrame frame{};
    assert(audible.buildMusicalTickFrame(inputs, true, frame) ==
           core::sequencer::SequencerCcLaneRuntimeStatus::OK);
    assert(frame.candidateCount == 0U);

    core::sequencer::SequencerCcLaneRuntime scratch{};
    assert(scratch.seedFrom(audible));
    setRuntimePosition(inputs[0], bank, region, futureOrdinal, false);
    inputs[0].emissionMode =
        core::sequencer::SequencerCcLaneEmissionMode::PREDICTIVE_LOOKAHEAD;
    inputs[0].lookaheadStartOrdinal = startOrdinal;
    assert(static_cast<uint32_t>(futureOrdinal - startOrdinal) == 4U);
    assert(scratch.buildMusicalTickFrame(inputs, true, frame) ==
           core::sequencer::SequencerCcLaneRuntimeStatus::OK);
    assert(frame.candidateCount == 1U);
    assert(frame.authoredEventCount == 1U);
    assert(frame.candidates[0].localValue == 87U);
    assert(!audible.hasHeldValue(0U, 0U));
}

void testPredictiveScratchRejectsInvalidDeltaTransactionally() {
    seq::SequencerCcLaneBank bank{};
    createWithEvent(bank, 0, 74, 0, 64);
    const auto region =
        oc::note::sequencer::StepSequencerPlaybackRegion::fullLength(8);
    core::sequencer::SequencerCcLaneRuntime audible{};
    core::sequencer::SequencerCcLaneRuntime::Inputs inputs{};
    setRuntimePosition(inputs[0], bank, region, 0, true);
    core::sequencer::SequencerCcLaneRuntimeFrame frame{};
    assert(audible.buildMusicalTickFrame(inputs, true, frame) ==
           core::sequencer::SequencerCcLaneRuntimeStatus::OK);

    core::sequencer::SequencerCcLaneRuntime scratch{};
    assert(scratch.seedFrom(audible));
    setRuntimePosition(
        inputs[0],
        bank,
        region,
        core::sequencer::SequencerCcLaneRuntime::MAX_LOOKAHEAD_ORDINAL_DELTA + 1U,
        false
    );
    inputs[0].emissionMode =
        core::sequencer::SequencerCcLaneEmissionMode::PREDICTIVE_LOOKAHEAD;
    inputs[0].lookaheadStartOrdinal = 0;
    assert(scratch.buildMusicalTickFrame(inputs, true, frame) ==
           core::sequencer::SequencerCcLaneRuntimeStatus::INVALID_INPUT);
    assert(frame.candidateCount == 0);
    assert(scratch.hasHeldValue(0, 0));
    assert(scratch.heldValue(0, 0) == 64);

    setRuntimePosition(inputs[0], bank, region, 4, false);
    inputs[0].emissionMode =
        core::sequencer::SequencerCcLaneEmissionMode::PREDICTIVE_LOOKAHEAD;
    inputs[0].lookaheadStartOrdinal = 5;
    assert(scratch.buildMusicalTickFrame(inputs, true, frame) ==
           core::sequencer::SequencerCcLaneRuntimeStatus::INVALID_INPUT);
    assert(frame.candidateCount == 0);
    assert(scratch.hasHeldValue(0, 0));
    assert(scratch.heldValue(0, 0) == 64);
}

void testPredictiveHeldValueInterpolatesAtFutureFractionAndRetargetsRoute() {
    seq::SequencerCcLaneBank bank{};
    createWithEvent(bank, 0, 74, 0, 0);
    assert(seq::setSequencerCcLaneEvent(bank, 0, 4, 100).changed());
    assert(seq::setSequencerCcLaneTransition(
        bank,
        0,
        0,
        seq::SequencerCcLaneTransition::LINEAR
    ).changed());
    const auto region =
        oc::note::sequencer::StepSequencerPlaybackRegion::fullLength(8);

    core::sequencer::SequencerCcLaneRuntime audible{};
    core::sequencer::SequencerCcLaneRuntime::Inputs inputs{};
    setRuntimePosition(inputs[0], bank, region, 0, true, 0, 4);
    core::sequencer::SequencerCcLaneRuntimeFrame frame{};
    assert(audible.buildMusicalTickFrame(inputs, true, frame) ==
           core::sequencer::SequencerCcLaneRuntimeStatus::OK);
    assert(frame.candidates[0].localValue == 0);

    const auto audibleBefore = audible;
    core::sequencer::SequencerCcLaneRuntime scratch{};
    assert(scratch.seedFrom(audible));
    setRuntimePosition(inputs[0], bank, region, 2, false, 2, 4);
    inputs[0].route = route(7);
    inputs[0].emissionMode =
        core::sequencer::SequencerCcLaneEmissionMode::PREDICTIVE_LOOKAHEAD;
    inputs[0].lookaheadStartOrdinal = 0;
    assert(scratch.buildMusicalTickFrame(inputs, true, frame) ==
           core::sequencer::SequencerCcLaneRuntimeStatus::OK);
    assert(frame.candidateCount == 1);
    assert(frame.candidates[0].localValue == 63);
    assert(frame.candidates[0].destination.identity.channel == 7);
    assert(frame.contributions[0].routeRetargetedThisTick);
    assert(std::memcmp(&audible, &audibleBefore, sizeof(audible)) == 0);
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

void testTransitionsAreOwnedByEventsAndInterpolateAtMusicalTicks() {
    assert(seq::interpolateSequencerCcLaneValue(
        0, 100, seq::SequencerCcLaneTransition::HOLD, 0.5f
    ) == 0);
    assert(seq::interpolateSequencerCcLaneValue(
        0, 100, seq::SequencerCcLaneTransition::LINEAR, 0.5f
    ) == 50);
    assert(seq::interpolateSequencerCcLaneValue(
        0, 100, seq::SequencerCcLaneTransition::EASE_IN, 0.5f
    ) == 25);
    assert(seq::interpolateSequencerCcLaneValue(
        0, 100, seq::SequencerCcLaneTransition::EASE_OUT, 0.5f
    ) == 75);
    assert(seq::interpolateSequencerCcLaneValue(
        0, 100, seq::SequencerCcLaneTransition::EASE_IN_OUT, 0.25f
    ) == 16);

    RuntimeFixture h;
    createWithEvent(h.banks[0], 0, 74, 0, 0);
    assert(seq::setSequencerCcLaneEvent(h.banks[0], 0, 2, 100).changed());
    assert(seq::sequencerCcLaneTransition(h.banks[0].lanes[0], 0) ==
           seq::SequencerCcLaneTransition::HOLD);
    assert(seq::setSequencerCcLaneTransition(
        h.banks[0], 0, 0, seq::SequencerCcLaneTransition::LINEAR
    ).changed());

    h.inputs[0].patternLength = 4;
    h.inputs[0].ticksPerStep = 4;
    h.inputs[0].tickInStep = 0;
    h.inputs[0].step = 0;
    h.inputs[0].stepTriggered = true;
    auto frame = h.tick();
    assert(frame.candidates[0].localValue == 0);

    h.inputs[0].step = 1;
    h.inputs[0].tickInStep = 0;
    h.inputs[0].stepTriggered = false;
    frame = h.tick();
    assert(frame.candidates[0].localValue == 50);
    assert(frame.contributions[0].valueChangedThisTick);

    assert(seq::setSequencerCcLaneTransition(
        h.banks[0], 0, 0, seq::SequencerCcLaneTransition::EASE_IN
    ).changed());
    h.runtime.resetProject();
    h.inputs[0].step = 0;
    h.inputs[0].stepTriggered = true;
    (void)h.tick();
    h.inputs[0].step = 1;
    h.inputs[0].stepTriggered = false;
    frame = h.tick();
    assert(frame.candidates[0].localValue == 25);

    // Three-bit packing is exercised on steps whose field crosses a byte.
    assert(seq::setSequencerCcLaneTransition(
        h.banks[0], 0, 2, seq::SequencerCcLaneTransition::EASE_IN_OUT
    ).changed());
    assert(seq::sequencerCcLaneTransition(h.banks[0].lanes[0], 2) ==
           seq::SequencerCcLaneTransition::EASE_IN_OUT);
    assert(seq::sequencerCcLaneTransition(h.banks[0].lanes[0], 0) ==
           seq::SequencerCcLaneTransition::EASE_IN);

    assert(seq::clearSequencerCcLaneEvent(h.banks[0], 0, 0).changed());
    assert(seq::sequencerCcLaneTransition(h.banks[0].lanes[0], 0) ==
           seq::SequencerCcLaneTransition::HOLD);
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
    persisted.lanes[2].values[73] = 99;  // non-canonical data in empty lane
    assert(!seq::validSequencerCcLaneBank(persisted));
    seq::SequencerCcLaneBank decoded{};
    decoded.revision = 999U;
    assert(!seq::decodeCanonicalSequencerCcLaneBank(persisted, decoded));
    assert(decoded.revision == 999U);

    persisted.lanes[2] = {};
    assert(seq::decodeCanonicalSequencerCcLaneBank(persisted, decoded));
    assert(seq::validSequencerCcLaneBank(decoded));
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

void testRuntimeEmptyHoldRetargetMuteStopAndPin() {
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
    assert(frame.routeRetargetCount == 0);

    h.inputs[3].route = route(8);
    frame = h.tick();
    assert(frame.candidateCount == 1);
    assert(frame.routeRetargetCount == 1);
    assert(frame.candidates[0].destination.identity.channel == 8);
    // Exactly one new-route candidate: no reset is synthesized for Ch4.

    h.inputs[3].muted = true;
    h.inputs[3].route = route(10);
    frame = h.tick();
    assert(frame.candidateCount == 0);
    assert(h.runtime.hasHeldValue(3, 0));
    h.inputs[3].muted = false;
    frame = h.tick();
    assert(frame.candidateCount == 1 && frame.routeRetargetCount == 1);
    assert(frame.candidates[0].destination.identity.channel == 10);

    frame = h.tick(false);
    assert(frame.candidateCount == 0);
    assert(h.runtime.hasHeldValue(3, 0));
    frame = h.tick(true);
    assert(frame.candidateCount == 1);
    assert(frame.routeRetargetCount == 0);

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
        assert(!frame.contributions[i].routeRetargetedThisTick);
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
    constexpr uint8_t lifecycleIndex =
        6U * seq::SequencerCcLaneBank::MAX_LANES;
    const uint16_t audibleGeneration =
        frame.lifecycleGenerations[lifecycleIndex];
    assert(audibleGeneration != 0U);
    assert(frame.candidateCount == 1);
    assert(frame.candidates[0].localValue == 45);
    assert(frame.candidates[0].destination.identity.channel == 6);

    assert(seq::setSequencerCcLaneEvent(h.banks[6], 0, 0, 99).changed());
    h.banks[6].lanes[0].lifecycleGeneration =
        seq::nextSequencerCcLaneLifecycleGeneration(audibleGeneration);
    const uint16_t stagedGeneration =
        h.banks[6].lanes[0].lifecycleGeneration;
    assert(stagedGeneration != audibleGeneration);
    h.inputs[6].route = route(12);
    h.inputs[6].frozen = true;
    frame = h.tick();
    assert(frame.candidateCount == 1);
    assert(frame.suppressedTrackCount == 1);
    assert(frame.candidates[0].localValue == 45);
    assert(frame.candidates[0].destination.identity.channel == 6);
    assert(frame.authoredEventCount == 0);
    assert(frame.routeRetargetCount == 0);
    assert(frame.lifecycleGenerations[lifecycleIndex] == audibleGeneration);

    // More than one frozen scheduler pass must retain the same audible
    // generation; the staged payload is still not observable.
    frame = h.tick();
    assert(frame.lifecycleGenerations[lifecycleIndex] == audibleGeneration);

    h.inputs[6].frozen = false;
    frame = h.tick();
    assert(frame.candidateCount == 1);
    assert(frame.candidates[0].localValue == 99);
    assert(frame.candidates[0].destination.identity.channel == 12);
    assert(frame.authoredEventCount == 1);
    // A new lifecycle is a content replacement, not a retarget of the old
    // Lane's route. It becomes audible atomically at this boundary.
    assert(frame.routeRetargetCount == 0);
    assert(frame.lifecycleGenerations[lifecycleIndex] == stagedGeneration);
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
    assert(sizeof(seq::SequencerCcLaneBank) <= 848U);
}

void testFull128StepLaneValidationAndRotation() {
    seq::SequencerCcLaneBank bank{};
    createWithEvent(bank, 0U, 74U, 127U, 99U);

    const bool valid = seq::validSequencerCcLaneBank(bank);
    assert(valid);
    const auto settingsResult = seq::updateSequencerCcLaneSettings(
        bank, 0U, draft(74U)
    );
    assert(settingsResult.status == seq::SequencerCcLaneMutationStatus::NO_CHANGE);

    const bool rotated = seq::rotateSequencerCcLaneBank(bank, 128U, 1);
    assert(rotated);
    assert(bank.lanes[0].activeMask.test(0U));
    assert(bank.lanes[0].values[0] == 99U);
    assert(!bank.lanes[0].activeMask.test(127U));
}

}  // namespace

int main() {
    testBulkPatternTransformsPreserveCcValuesAndTransitions();
    testRuntimeProjectionUsesRegionAndResetsTransactionally();
    testPredictiveScratchCapturesFirstEventWithoutAdvancingAudibleState();
    testScratchSeedCopiesCompleteAudibleState();
    testPredictiveScratchDoesNotResurrectPreWindowLifecycleEvent();
    testPredictiveScratchFindsEventAcrossLoopWrap();
    testPredictiveScratchAcceptsBoundedOrdinalAcrossUint32Wrap();
    testPredictiveScratchRejectsInvalidDeltaTransactionally();
    testPredictiveHeldValueInterpolatesAtFutureFractionAndRetargetsRoute();
    testCreateIsSilentAndInitialIsEditProposal();
    testMutationsAreStrictAndAtomic();
    testTransitionsAreOwnedByEventsAndInterpolateAtMusicalTicks();
    testFourLaneCapacityAndCanonicalDecode();
    testInheritedPinnedAndNoRouteResolution();
    testGlobalDuplicatePreflightAcrossAllPatterns();
    testCollisionAppearsAfterInterTrackRouteChange();
    testRuntimeEmptyHoldRetargetMuteStopAndPin();
    testRuntimeNoRouteAndMissingBankAreSilentSafeInputs();
    testRuntimeFrameIsTransactionalOnMalformedInput();
    testFrozenTrackKeepsPreviousAudibleGenerationUntilActivationBoundary();
    testWorstCase64LanesAndResolverPriority();
    testFull128StepLaneValidationAndRotation();
    std::cout << "All Sequencer CC lane domain/runtime tests passed.\n";
    return 0;
}
