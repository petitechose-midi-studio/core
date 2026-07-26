#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <type_traits>

#include "state/sequencer/SequencerCcLaneDomain.hpp"
#include "state/sequencer/SequencerPatternRegionOps.hpp"
#include "state/sequencer/SequencerPatternState.hpp"
#include "state/sequencer/SequencerSnapshots.hpp"
#include "ui/sequencer/SequencerPatternTimelineModel.hpp"

namespace {

namespace seq = core::state::sequencer;
namespace timeline = core::ui::sequencer;

timeline::SequencerPatternTimelineViewport viewport(
    uint16_t width,
    uint8_t height,
    uint8_t windowStart = 0U,
    uint8_t windowCount = 8U
) {
    return {
        .width = width,
        .height = height,
        .windowStartStep = windowStart,
        .windowStepCount = windowCount,
    };
}

seq::SequencerCcLaneDraft laneDraft(uint8_t controller) {
    seq::SequencerCcLaneDraft draft{};
    draft.destination.controller = controller;
    return draft;
}

void createConstantLane(
    seq::SequencerCcLaneBank& bank,
    uint8_t laneIndex,
    uint8_t value
) {
    assert(seq::createSequencerCcLane(
        bank,
        laneIndex,
        laneDraft(static_cast<uint8_t>(70U + laneIndex))
    ).changed());
    assert(seq::setSequencerCcLaneEvent(
        bank,
        laneIndex,
        0U,
        value
    ).changed());
}

void testFootprintAndZeroOneFourLaneSampling() {
    static_assert(std::is_trivially_copyable_v<
        timeline::SequencerPatternTimelineGeometry
    >);
    static_assert(sizeof(timeline::SequencerPatternTimelineGeometry) < 4096U);
    static_assert(
        sizeof(timeline::SequencerPatternTimelineGeometry) >= 1280U
    );

    seq::SequencerPatternState pattern{};
    assert(pattern.setContentLength(8U) == false);
    timeline::SequencerPatternTimelineGeometry geometry{};
    assert(timeline::rebuildSequencerPatternTimelineGeometry(
        pattern,
        nullptr,
        viewport(32U, 128U),
        geometry
    ));
    assert(geometry.ccLaneCount == 0U);
    assert(geometry.sourceLaneIndex[0] ==
        timeline::SEQUENCER_PATTERN_TIMELINE_INVALID_LANE);

    seq::SequencerCcLaneBank bank{};
    createConstantLane(bank, 0U, 32U);
    assert(timeline::rebuildSequencerPatternTimelineGeometry(
        pattern,
        &bank,
        viewport(32U, 128U),
        geometry
    ));
    assert(geometry.ccLaneCount == 1U);
    assert(geometry.sourceLaneIndex[0] == 0U);
    for (uint16_t x = 0U; x < 32U; ++x) {
        assert(timeline::sequencerPatternTimelineCcSampleValid(
            geometry,
            0U,
            x
        ));
        assert(geometry.ccY[0][x] == 95U);
    }

    createConstantLane(bank, 1U, 0U);
    createConstantLane(bank, 2U, 64U);
    createConstantLane(bank, 3U, 127U);
    assert(timeline::rebuildSequencerPatternTimelineGeometry(
        pattern,
        &bank,
        viewport(32U, 128U),
        geometry
    ));
    assert(geometry.ccLaneCount == 4U);
    const uint8_t expectedY[] = {95U, 127U, 63U, 0U};
    for (uint8_t lane = 0U; lane < 4U; ++lane) {
        assert(geometry.sourceLaneIndex[lane] == lane);
        assert(timeline::sequencerPatternTimelineCcSampleValid(
            geometry,
            lane,
            31U
        ));
        assert(geometry.ccY[lane][31U] == expectedY[lane]);
    }

    // Reusing the retained owner with no lane clears every previous sample.
    assert(timeline::rebuildSequencerPatternTimelineGeometry(
        pattern,
        nullptr,
        viewport(32U, 128U),
        geometry
    ));
    assert(geometry.ccLaneCount == 0U);
    assert(!timeline::sequencerPatternTimelineCcSampleValid(
        geometry,
        0U,
        0U
    ));
}

void testNotesUseImplicitXAndPixelY() {
    seq::SequencerPatternState pattern{};
    pattern.setEnabled(2U, true);
    assert(pattern.setStepNoteAt(2U, 127U));
    assert(pattern.setStepVelocityAt(2U, 0U));
    assert(pattern.setStepGateAt(2U, 200U));
    assert(pattern.setStepNudgeAt(2U, 50));
    assert(pattern.setStepProbabilityAt(2U, 100U) == false);
    assert(pattern.setStepProbabilityAt(2U, 25U));

    timeline::SequencerPatternTimelineGeometry geometry{};
    assert(timeline::rebuildSequencerPatternTimelineGeometry(
        pattern,
        nullptr,
        viewport(80U, 128U),
        geometry
    ));
    assert(geometry.activeSteps.test(2U));
    assert(geometry.steps[2].noteY == 0U);
    assert(geometry.steps[2].velocityY == 127U);
    assert(geometry.steps[2].probabilityY >= 94U &&
           geometry.steps[2].probabilityY <= 96U);
    assert(geometry.steps[2].gatePercent == 200U);
    assert(geometry.steps[2].nudgePercent == 50);
    assert(timeline::sequencerPatternTimelineBoundaryX(geometry, 4U) == 40U);
    assert(timeline::sequencerPatternTimelineStepOnsetX(geometry, 2U) == 25U);
    assert(timeline::sequencerPatternTimelineStepGateEndX(geometry, 2U) == 45U);
}

void testRegionWindowAndProjectionValidity() {
    seq::SequencerPatternState pattern{};
    assert(seq::setPatternPlaybackRegion(pattern, {8U, 2U, 4U, 7U}));
    seq::SequencerCcLaneBank bank{};
    assert(seq::createSequencerCcLane(bank, 0U, laneDraft(74U)).changed());
    assert(seq::setSequencerCcLaneEvent(bank, 0U, 2U, 10U).changed());

    timeline::SequencerPatternTimelineGeometry geometry{};
    assert(timeline::rebuildSequencerPatternTimelineGeometry(
        pattern,
        &bank,
        viewport(8U, 128U, 4U, 8U),
        geometry
    ));
    assert(geometry.playStartX == 2U);
    assert(geometry.loopStartX == 4U);
    assert(geometry.loopEndX == 7U);
    assert(geometry.windowStartX == 4U);
    assert(geometry.windowEndX == 8U);
    assert(geometry.key.windowStepCount == 4U);
    assert(!timeline::sequencerPatternTimelineCcSampleValid(
        geometry,
        0U,
        0U
    ));
    assert(!timeline::sequencerPatternTimelineCcSampleValid(
        geometry,
        0U,
        1U
    ));
    for (uint16_t x = 2U; x < 7U; ++x) {
        assert(timeline::sequencerPatternTimelineCcSampleValid(
            geometry,
            0U,
            x
        ));
    }
    assert(!timeline::sequencerPatternTimelineCcSampleValid(
        geometry,
        0U,
        7U
    ));

    assert(seq::setPatternPlaybackRegion(pattern, {128U, 16U, 32U, 96U}));
    assert(timeline::rebuildSequencerPatternTimelineGeometry(
        pattern,
        nullptr,
        viewport(320U, 200U, 64U, 8U),
        geometry
    ));
    assert(geometry.playStartX == 40U);
    assert(geometry.loopStartX == 80U);
    assert(geometry.loopEndX == 240U);
    assert(geometry.windowStartX == 160U);
    assert(geometry.windowEndX == 180U);
}

void testRebuildKeyCoversRevisionsDimensionsWindowAndLayers() {
    seq::SequencerPatternState pattern{};
    seq::SequencerCcLaneBank bank{};
    createConstantLane(bank, 0U, 64U);
    auto view = viewport(80U, 120U);
    timeline::SequencerPatternTimelineRebuildKey initial{};
    assert(timeline::makeSequencerPatternTimelineRebuildKey(
        pattern,
        &bank,
        view,
        initial
    ));

    auto changedView = view;
    changedView.width = 81U;
    timeline::SequencerPatternTimelineRebuildKey changed{};
    assert(timeline::makeSequencerPatternTimelineRebuildKey(
        pattern,
        &bank,
        changedView,
        changed
    ));
    assert(!(changed == initial));

    changedView = view;
    changedView.windowStartStep = 4U;
    changedView.windowStepCount = 4U;
    assert(timeline::makeSequencerPatternTimelineRebuildKey(
        pattern,
        &bank,
        changedView,
        changed
    ));
    assert(!(changed == initial));

    changedView = view;
    changedView.layerMask ^= timeline::sequencerPatternTimelineLayerBit(
        timeline::SequencerPatternTimelineLayer::VELOCITY
    );
    assert(timeline::makeSequencerPatternTimelineRebuildKey(
        pattern,
        &bank,
        changedView,
        changed
    ));
    assert(!(changed == initial));

    pattern.setEnabled(7U, true);
    assert(timeline::makeSequencerPatternTimelineRebuildKey(
        pattern,
        &bank,
        view,
        changed
    ));
    assert(!(changed == initial));

    pattern.setEnabled(7U, false);
    assert(pattern.setStepVelocityAt(0U, 100U));
    assert(timeline::makeSequencerPatternTimelineRebuildKey(
        pattern,
        &bank,
        view,
        changed
    ));
    assert(!(changed == initial));

    const auto beforeBankRevision = bank.revision;
    assert(seq::setSequencerCcLaneEvent(bank, 0U, 1U, 40U).changed());
    assert(bank.revision != beforeBankRevision);
    assert(timeline::makeSequencerPatternTimelineRebuildKey(
        pattern,
        &bank,
        view,
        changed
    ));
    assert(changed.ccBankRevision == bank.revision);
    assert(!(changed == initial));
}

void testInvalidInputLeavesRetainedGeometryUntouched() {
    seq::SequencerPatternState pattern{};
    timeline::SequencerPatternTimelineGeometry geometry{};
    assert(timeline::rebuildSequencerPatternTimelineGeometry(
        pattern,
        nullptr,
        viewport(80U, 120U),
        geometry
    ));
    const auto before = geometry;
    auto invalid = viewport(321U, 120U);
    assert(!timeline::rebuildSequencerPatternTimelineGeometry(
        pattern,
        nullptr,
        invalid,
        geometry
    ));
    assert(std::memcmp(&before, &geometry, sizeof(geometry)) == 0);
}

void testSnapshotPreviewHasExactIndependentRebuildIdentity() {
    seq::SequencerPatternSnapshot first{};
    first.length = 8U;
    first.playStart = 0U;
    first.loopStart = 0U;
    first.loopEnd = 8U;
    first.stepDataRevision = 9U;
    first.note[1] = 12U;
    first.velocity[1] = 64U;
    first.gate[1] = 100U;

    auto second = first;
    second.note[1] = 100U;
    // Deterministic Randomize previews inherit the same persisted revision;
    // their exact cold-content fingerprint must still invalidate geometry.
    timeline::SequencerPatternTimelineRebuildKey firstKey{};
    timeline::SequencerPatternTimelineRebuildKey secondKey{};
    const auto view = viewport(80U, 128U);
    assert(timeline::makeSequencerPatternTimelineRebuildKey(
        first,
        nullptr,
        view,
        firstKey
    ));
    assert(timeline::makeSequencerPatternTimelineRebuildKey(
        second,
        nullptr,
        view,
        secondKey
    ));
    assert(firstKey.stepDataRevision == secondKey.stepDataRevision);
    assert(firstKey.sourceFingerprint != secondKey.sourceFingerprint);
    assert(!(firstKey == secondKey));

    timeline::SequencerPatternTimelineGeometry firstGeometry{};
    timeline::SequencerPatternTimelineGeometry secondGeometry{};
    assert(timeline::rebuildSequencerPatternTimelineGeometry(
        first,
        nullptr,
        view,
        firstGeometry
    ));
    assert(timeline::rebuildSequencerPatternTimelineGeometry(
        second,
        nullptr,
        view,
        secondGeometry
    ));
    assert(firstGeometry.steps[1].noteY != secondGeometry.steps[1].noteY);

    auto chance = first;
    chance.probability[1] = 100U;
    timeline::SequencerPatternTimelineRebuildKey chanceKey{};
    assert(timeline::makeSequencerPatternTimelineRebuildKey(
        chance, nullptr, view, chanceKey
    ));
    assert(chanceKey.sourceFingerprint != firstKey.sourceFingerprint);
    timeline::SequencerPatternTimelineGeometry chanceGeometry{};
    assert(timeline::rebuildSequencerPatternTimelineGeometry(
        chance, nullptr, view, chanceGeometry
    ));
    assert(chanceGeometry.steps[1].probabilityY == 0U);
}

void testPlayheadUsesRegionAndDamagesOnlyOldAndNewBands() {
    seq::SequencerPatternState pattern{};
    assert(seq::setPatternPlaybackRegion(pattern, {8U, 2U, 4U, 6U}));
    timeline::SequencerPatternTimelineGeometry geometry{};
    assert(timeline::rebuildSequencerPatternTimelineGeometry(
        pattern,
        nullptr,
        viewport(80U, 120U),
        geometry
    ));
    const auto geometryBefore = geometry;

    timeline::SequencerPatternTimelinePlayhead prelude{};
    assert(timeline::projectSequencerPatternTimelinePlayhead(
        geometry,
        0U,
        32768U,
        prelude
    ));
    assert(prelude.visible && prelude.column == 25U);

    timeline::SequencerPatternTimelinePlayhead loopStart{};
    assert(timeline::projectSequencerPatternTimelinePlayhead(
        geometry,
        2U,
        0U,
        loopStart
    ));
    assert(loopStart.column == 40U);
    timeline::SequencerPatternTimelinePlayhead wrapped{};
    assert(timeline::projectSequencerPatternTimelinePlayhead(
        geometry,
        4U,
        0U,
        wrapped
    ));
    assert(wrapped.column == loopStart.column);

    timeline::SequencerPatternTimelinePlayhead local{};
    assert(timeline::projectSequencerPatternTimelinePlayheadLocalStep(
        geometry,
        5,
        32768U,
        local
    ));
    assert(local.column == 55U);
    assert(!timeline::projectSequencerPatternTimelinePlayheadLocalStep(
        geometry,
        1,
        0U,
        local
    ));
    assert(!timeline::projectSequencerPatternTimelinePlayheadLocalStep(
        geometry,
        6,
        0U,
        local
    ));

    auto damage = timeline::sequencerPatternTimelinePlayheadDamage(
        geometry,
        prelude,
        loopStart
    );
    assert(damage.count == 2U);
    assert(damage.bands[0].x == 24U && damage.bands[0].width == 3U);
    assert(damage.bands[1].x == 39U && damage.bands[1].width == 3U);
    assert(damage.bands[0].height == 120U);
    assert(damage.bands[1].height == 120U);

    damage = timeline::sequencerPatternTimelinePlayheadDamage(
        geometry,
        loopStart,
        wrapped
    );
    assert(damage.count == 0U);
    damage = timeline::sequencerPatternTimelinePlayheadDamage(
        geometry,
        wrapped,
        {}
    );
    assert(damage.count == 1U);
    assert(damage.bands[0].x == 39U);

    // Hot projection and damage never mutate/rebuild retained geometry.
    assert(std::memcmp(&geometryBefore, &geometry, sizeof(geometry)) == 0);
}

void testFull128StepPatternAndSnapshotRebuild() {
    seq::SequencerPatternState pattern{};
    const bool resized = pattern.setContentLength(
        seq::SequencerPatternState::MAX_STEPS
    );
    assert(resized);
    auto mask = pattern.enabledMask.get();
    mask.setBit(127U, true);
    pattern.enabledMask.set(mask);
    pattern.note[127] = 91U;
    pattern.velocity[127] = 119U;
    pattern.probability[127] = 37U;
    pattern.bumpStepDataRevision();

    timeline::SequencerPatternTimelineGeometry geometry{};
    const auto fullViewport = viewport(304U, 132U, 120U, 8U);
    const bool patternRebuilt = timeline::rebuildSequencerPatternTimelineGeometry(
        pattern, nullptr, fullViewport, geometry
    );
    assert(patternRebuilt);
    assert(geometry.key.contentLength == 128U);
    assert(geometry.activeSteps.test(127U));
    assert(geometry.steps[127].velocityY < geometry.key.height);

    seq::SequencerPatternSnapshot snapshot{};
    snapshot.length = 128U;
    snapshot.playStart = 0U;
    snapshot.loopStart = 0U;
    snapshot.loopEnd = 128U;
    snapshot.enabledMask.setBit(127U, true);
    snapshot.note[127] = 84U;
    snapshot.velocity[127] = 127U;
    snapshot.probability[127] = 100U;
    const bool snapshotRebuilt = timeline::rebuildSequencerPatternTimelineGeometry(
        snapshot, nullptr, fullViewport, geometry
    );
    assert(snapshotRebuilt);
    assert(geometry.key.contentLength == 128U);
    assert(geometry.activeSteps.test(127U));
}

}  // namespace

int main() {
    testFootprintAndZeroOneFourLaneSampling();
    testNotesUseImplicitXAndPixelY();
    testRegionWindowAndProjectionValidity();
    testRebuildKeyCoversRevisionsDimensionsWindowAndLayers();
    testInvalidInputLeavesRetainedGeometryUntouched();
    testSnapshotPreviewHasExactIndependentRebuildIdentity();
    testPlayheadUsesRegionAndDamagesOnlyOldAndNewBands();
    testFull128StepPatternAndSnapshotRebuild();
    std::cout << "SequencerPatternTimelineModel tests passed (geometry "
              << sizeof(timeline::SequencerPatternTimelineGeometry)
              << " bytes)\n";
    return 0;
}
