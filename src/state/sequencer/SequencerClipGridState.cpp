#include "state/sequencer/SequencerClipGridState.hpp"

#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <type_traits>
#include <utility>

#include <config/PlatformCompat.hpp>

#include "state/sequencer/SequencerCcLanePatternOps.hpp"
#include "state/sequencer/SequencerClipRegionOps.hpp"
#include "state/sequencer/SequencerGraphOps.hpp"
#include "state/sequencer/SequencerSnapshotOps.hpp"
#include "state/sequencer/SequencerTrackBankOps.hpp"

namespace core::state::sequencer {

#if defined(ARDUINO_TEENSY41) && !defined(OC_DESKTOP)
static_assert(sizeof(SequencerClipDocument) == 864U, "Clip document RAM drift");
#if OC_ENABLE_STATS
static_assert(sizeof(SequencerClipGridState) == 1608U,
              "Diagnostic Clip grid RAM drift");
#else
static_assert(sizeof(SequencerClipGridState) == 1604U, "Clip grid RAM drift");
#endif
static_assert(sizeof(SequencerClipStructureChange) == 28U, "Clip history RAM drift");
static_assert(sizeof(oc::note::sequencer::StepSequencerGraph) == 14792U,
              "Clip graph RAM drift");
static_assert(sizeof(SequencerCcLaneBank) == 840U, "Clip CC RAM drift");
static_assert(sizeof(DrumTrackState) == 11128U, "Clip Drum RAM drift");
#endif

namespace {

using Graph = oc::note::sequencer::StepSequencerGraph;
constexpr uint32_t kExtmemAllocationOverheadEstimate = 16U;

constexpr std::array<SequencerLauncherFollowChoice, 14U> kFollowChoices{{
    SequencerLauncherFollowChoice::NONE,
    SequencerLauncherFollowChoice::NEXT,
    SequencerLauncherFollowChoice::PREVIOUS,
    SequencerLauncherFollowChoice::FIRST,
    SequencerLauncherFollowChoice::RANDOM_OTHER,
    SequencerLauncherFollowChoice::RANDOM_ANY,
    SequencerLauncherFollowChoice::TARGET_1,
    SequencerLauncherFollowChoice::TARGET_2,
    SequencerLauncherFollowChoice::TARGET_3,
    SequencerLauncherFollowChoice::TARGET_4,
    SequencerLauncherFollowChoice::TARGET_5,
    SequencerLauncherFollowChoice::TARGET_6,
    SequencerLauncherFollowChoice::TARGET_7,
    SequencerLauncherFollowChoice::TARGET_8,
}};

constexpr bool validLauncherFollowChoice(
    SequencerLauncherFollowChoice choice
) noexcept {
    for (const auto candidate : kFollowChoices) {
        if (candidate == choice) return true;
    }
    return false;
}

constexpr bool validLauncherBehavior(
    const SequencerLauncherBehavior& behavior
) noexcept {
    return behavior.length <= SequencerLauncherBehavior::MAX_LENGTH &&
        validLauncherFollowChoice(behavior.follow) &&
        behavior.quantization <=
            SequencerLauncherFollowQuantization::BAR;
}

FLASHMEM bool cloneGraph(
    const Graph* source,
    core::app::ExtmemUniquePtr<Graph>& out
) {
    out.reset();
    if (source == nullptr) return true;
    out = core::app::makeExtmemUniqueCold<Graph>(*source);
    return static_cast<bool>(out);
}

FLASHMEM bool samePatternSnapshot(
    const SequencerPatternSnapshot& lhs,
    const SequencerPatternSnapshot& rhs
) noexcept {
    return lhs.length == rhs.length &&
        lhs.stepsPerBeat == rhs.stepsPerBeat &&
        lhs.enabledMask == rhs.enabledMask &&
        lhs.stepDataRevision == rhs.stepDataRevision &&
        lhs.patternVariationRevision == rhs.patternVariationRevision &&
        lhs.patternScaleRevision == rhs.patternScaleRevision &&
        lhs.patternTimingRevision == rhs.patternTimingRevision &&
        lhs.graphRevision == rhs.graphRevision &&
        lhs.swingOffsetPercent == rhs.swingOffsetPercent &&
        lhs.patternNudgePercent == rhs.patternNudgePercent &&
        lhs.variationRanges.pitchSemitones == rhs.variationRanges.pitchSemitones &&
        lhs.variationRanges.velocity == rhs.variationRanges.velocity &&
        lhs.variationRanges.gatePercent == rhs.variationRanges.gatePercent &&
        lhs.variationRanges.nudge == rhs.variationRanges.nudge &&
        lhs.scalePolicy == rhs.scalePolicy &&
        lhs.scaleOverride.root == rhs.scaleOverride.root &&
        lhs.scaleOverride.type == rhs.scaleOverride.type &&
        lhs.scaleOverride.mode == rhs.scaleOverride.mode &&
        lhs.pitchEditMode == rhs.pitchEditMode &&
        lhs.note == rhs.note && lhs.velocity == rhs.velocity &&
        lhs.gate == rhs.gate && lhs.nudge == rhs.nudge &&
        lhs.probability == rhs.probability;
}

FLASHMEM bool sameGraph(const Graph* lhs, const Graph* rhs) noexcept {
    if (lhs == nullptr || rhs == nullptr) return lhs == rhs;
    static_assert(std::is_trivially_copyable_v<Graph>);
    return std::memcmp(lhs, rhs, sizeof(Graph)) == 0;
}

// Drum ownership is settled separately by the promoting or exchanging caller.
FLASHMEM void installDocumentPattern(
    SequencerTrackBankState& bank,
    SequencerState& active,
    uint8_t track,
    SequencerClipDocument& document
) noexcept {
    const bool editor = bank.activeTrackIndex() == track;
    if (editor) {
        installTrackContentSnapshotToEditorWithOwnedPayload(
            active,
            document.pattern,
            document.clip,
            std::move(document.graph),
            std::move(document.ccLanes)
        );
        active.pattern().ccLaneRevision.set(document.ccLaneRevision);
        resetTransientTrackState(active);
    } else {
        installTrackContentSnapshotWithOwnedPayload(
            bank.track(track),
            bank.clip(track),
            document.pattern,
            document.clip,
            std::move(document.graph),
            std::move(document.ccLanes)
        );
        bank.track(track).ccLaneRevision.set(document.ccLaneRevision);
    }
}

FLASHMEM void swapDrumState(
    DrumTrackState& left,
    DrumTrackState& right
) noexcept {
    static_assert(std::is_trivially_copyable_v<DrumTrackState>);
    auto* leftBytes = reinterpret_cast<uint8_t*>(&left);
    auto* rightBytes = reinterpret_cast<uint8_t*>(&right);
    for (size_t index = 0U; index < sizeof(DrumTrackState); ++index) {
        const uint8_t value = leftBytes[index];
        leftBytes[index] = rightBytes[index];
        rightBytes[index] = value;
    }
}

FLASHMEM bool exchangeCanonicalTrackDocument(
    SequencerTrackBankState& bank,
    SequencerState& active,
    uint8_t track,
    SequencerClipDocument& document
) noexcept {
    if (track >= SequencerTrackBankState::TRACK_COUNT ||
        document.trackKind != bank.trackKind(track) ||
        !validSequencerClipDocument(document, document.trackKind)) {
        return false;
    }

    auto& pattern = bank.track(track);
    auto& clip = bank.clip(track);
    if (!validClipRegion(pattern, clip)) return false;

    SequencerPatternSnapshot outgoingPattern;
    SequencerClipState outgoingClip;
    captureSnapshot(pattern, outgoingPattern);
    outgoingClip = clip;
    const uint32_t outgoingCcLaneRevision = pattern.ccLaneRevision.get();
    auto outgoingGraph = std::move(pattern.graph);
    auto outgoingCcLanes = std::move(pattern.ccLanes);
    installDocumentPattern(bank, active, track, document);

    document.pattern = outgoingPattern;
    document.clip = outgoingClip;
    document.ccLaneRevision = outgoingCcLaneRevision;
    document.graph = std::move(outgoingGraph);
    document.ccLanes = std::move(outgoingCcLanes);
    if (document.trackKind == SequencerTrackKind::DRUM) {
        swapDrumState(bank.drumTrack(track), *document.drum);
        bank.publishDrumMutation(track);
    }
    return true;
}

FLASHMEM uint32_t canonicalTrackRetainedBytes(
    const SequencerTrackBankState& bank,
    const SequencerState& active,
    uint8_t track
) noexcept {
    const auto& pattern = bank.track(track);
    uint32_t bytes = sizeof(SequencerClipDocument) +
        kExtmemAllocationOverheadEstimate;
    if (graphView(pattern) != nullptr) {
        bytes += sizeof(Graph) + kExtmemAllocationOverheadEstimate;
    }
    if (bank.trackKind(track) == SequencerTrackKind::DRUM) {
        bytes += sizeof(DrumTrackState) + kExtmemAllocationOverheadEstimate;
    } else if (pattern.ccLanes != nullptr) {
        bytes += sizeof(SequencerCcLaneBank) +
            kExtmemAllocationOverheadEstimate;
    }
    return bytes;
}

FLASHMEM uint16_t canonicalTrackRetainedSpans(
    const SequencerTrackBankState& bank,
    const SequencerState& active,
    uint8_t track
) noexcept {
    const auto& pattern = bank.track(track);
    return static_cast<uint16_t>(
        1U + (graphView(pattern) != nullptr ? 1U : 0U) +
        (bank.trackKind(track) == SequencerTrackKind::DRUM ||
                 pattern.ccLanes != nullptr
             ? 1U
             : 0U)
    );
}

}  // namespace

FLASHMEM SequencerLauncherFollowChoice stepSequencerLauncherFollowChoice(
    SequencerLauncherFollowChoice choice,
    int direction
) noexcept {
    if (direction == 0) return choice;
    size_t index = 0U;
    while (index < kFollowChoices.size() && kFollowChoices[index] != choice) {
        ++index;
    }
    if (index == kFollowChoices.size()) index = 0U;
    if (direction < 0) {
        if (index > 0U) --index;
    } else if (index + 1U < kFollowChoices.size()) {
        ++index;
    }
    return kFollowChoices[index];
}

FLASHMEM uint8_t sequencerLauncherFollowChoiceCount() noexcept {
    return static_cast<uint8_t>(kFollowChoices.size());
}

FLASHMEM uint8_t sequencerLauncherFollowChoiceIndex(
    SequencerLauncherFollowChoice choice
) noexcept {
    for (uint8_t index = 0U; index < kFollowChoices.size(); ++index) {
        if (kFollowChoices[index] == choice) return index;
    }
    return 0U;
}

FLASHMEM SequencerLauncherFollowChoice sequencerLauncherFollowChoiceAt(
    uint8_t index
) noexcept {
    return kFollowChoices[std::min<std::size_t>(
        index,
        kFollowChoices.size() - 1U
    )];
}

FLASHMEM SequencerClipDocument::SequencerClipDocument() = default;
FLASHMEM SequencerClipDocument::~SequencerClipDocument() = default;
FLASHMEM SequencerClipDocument::SequencerClipDocument(
    SequencerClipDocument&&
) noexcept = default;
FLASHMEM SequencerClipDocument& SequencerClipDocument::operator=(
    SequencerClipDocument&&
) noexcept = default;

FLASHMEM bool captureSequencerClipDocument(
    const SequencerPatternState& pattern,
    const SequencerClipState& clip,
    SequencerTrackKind trackKind,
    const DrumTrackState* drum,
    SequencerClipDocumentPtr& out
) {
    if (!validClipRegion(pattern, clip) ||
        (trackKind == SequencerTrackKind::DRUM) != (drum != nullptr)) {
        return false;
    }

    auto next = core::app::makeExtmemUniqueCold<SequencerClipDocument>();
    if (!next) return false;
    captureSnapshot(pattern, next->pattern);
    next->clip = clip;
    next->ccLaneRevision = pattern.ccLaneRevision.get();
    next->trackKind = trackKind;
    if (!cloneGraph(graphView(pattern), next->graph)) return false;

    if (trackKind == SequencerTrackKind::DRUM) {
        next->drum = core::app::makeExtmemUniqueCopy(*drum);
        if (!next->drum) return false;
    } else if (!cloneSequencerCcLaneBank(next->ccLanes, pattern.ccLanes.get())) {
        return false;
    }

    out = std::move(next);
    return true;
}

FLASHMEM bool createEmptySequencerClipDocument(
    SequencerTrackKind trackKind,
    const DrumTrackState* drumTemplate,
    SequencerClipDocumentPtr& out
) {
    if ((trackKind == SequencerTrackKind::DRUM) !=
        (drumTemplate != nullptr)) {
        return false;
    }

    auto next = core::app::makeExtmemUniqueCold<SequencerClipDocument>();
    if (!next) return false;
    next->pattern.resetStepData();
    // Match a newly reset live Pattern without constructing observers or
    // allocating a temporary reactive owner for this detached document.
    next->pattern.stepDataRevision = next->pattern.patternVariationRevision =
        next->pattern.patternScaleRevision = next->pattern.patternTimingRevision =
        next->pattern.graphRevision = next->ccLaneRevision = 1U;
    next->trackKind = trackKind;
    if (drumTemplate != nullptr) {
        next->drum = core::app::makeExtmemUniqueCopy(*drumTemplate);
        if (!next->drum) return false;
        next->drum->pattern.reset();
        next->drum->advancedStepKeys.fill(DRUM_ADVANCED_STEP_KEY_INVALID);
    }
    out = std::move(next);
    return true;
}

FLASHMEM bool cloneSequencerClipDocument(
    const SequencerClipDocument& source,
    SequencerClipDocumentPtr& out
) {
    auto next = core::app::makeExtmemUniqueCold<SequencerClipDocument>();
    if (!next) return false;
    next->pattern = source.pattern;
    next->clip = source.clip;
    next->ccLaneRevision = source.ccLaneRevision;
    next->trackKind = source.trackKind;
    if (!cloneGraph(source.graph.get(), next->graph) ||
        !cloneSequencerCcLaneBank(next->ccLanes, source.ccLanes.get())) {
        return false;
    }
    if (source.drum != nullptr) {
        next->drum = core::app::makeExtmemUniqueCopy(*source.drum);
        if (!next->drum) return false;
    }
    out = std::move(next);
    return true;
}

FLASHMEM bool sameSequencerClipDocument(
    const SequencerClipDocument& lhs,
    const SequencerClipDocument& rhs
) noexcept {
    static_assert(std::is_trivially_copyable_v<DrumTrackState>);
    const bool sameDrum = lhs.drum == nullptr || rhs.drum == nullptr
        ? lhs.drum == nullptr && rhs.drum == nullptr
        : std::memcmp(lhs.drum.get(), rhs.drum.get(), sizeof(DrumTrackState)) == 0;
    return samePatternSnapshot(lhs.pattern, rhs.pattern) &&
        lhs.clip.playStartTick == rhs.clip.playStartTick &&
        lhs.clip.loopStartTick == rhs.clip.loopStartTick &&
        lhs.clip.loopEndTick == rhs.clip.loopEndTick &&
        lhs.ccLaneRevision == rhs.ccLaneRevision &&
        lhs.trackKind == rhs.trackKind && sameGraph(lhs.graph.get(), rhs.graph.get()) &&
        sameOptionalSequencerCcLaneBank(lhs.ccLanes.get(), rhs.ccLanes.get()) &&
        sameDrum;
}

FLASHMEM bool validSequencerClipDocument(
    const SequencerClipDocument& document,
    SequencerTrackKind expectedKind
) noexcept {
    const bool drum = expectedKind == SequencerTrackKind::DRUM;
    if (document.trackKind != expectedKind ||
        drum != (document.drum != nullptr) ||
        (drum && document.ccLanes != nullptr)) {
        return false;
    }
    const uint16_t ticksPerStep = sequencerTicksPerStep(
        document.pattern.stepsPerBeat
    );
    const uint16_t contentEnd = static_cast<uint16_t>(
        document.pattern.length * ticksPerStep
    );
    return document.pattern.length > 0U && ticksPerStep > 0U &&
        document.clip.playStartTick % ticksPerStep == 0U &&
        document.clip.loopStartTick % ticksPerStep == 0U &&
        document.clip.loopEndTick % ticksPerStep == 0U &&
        document.clip.playStartTick <= document.clip.loopStartTick &&
        document.clip.loopStartTick < document.clip.loopEndTick &&
        document.clip.loopEndTick <= contentEnd;
}

FLASHMEM SequencerClipGridSnapshot::SequencerClipGridSnapshot() {
    reset();
}
FLASHMEM SequencerClipGridSnapshot::~SequencerClipGridSnapshot() = default;
FLASHMEM SequencerClipGridSnapshot::SequencerClipGridSnapshot(
    SequencerClipGridSnapshot&&
) noexcept = default;
FLASHMEM SequencerClipGridSnapshot& SequencerClipGridSnapshot::operator=(
    SequencerClipGridSnapshot&&
) noexcept = default;

FLASHMEM void SequencerClipGridSnapshot::reset() {
    residentSlots.fill(INVALID_SLOT);
    generations.fill(1U);
    stopMasks.fill(0U);
    clipBehaviors.fill({});
    sceneBehaviors.fill({});
    for (auto& document : documents) document.reset();
}

FLASHMEM SequencerClipGridState::SequencerClipGridState() {
    reset();
}
FLASHMEM SequencerClipGridState::~SequencerClipGridState() = default;

FLASHMEM uint32_t SequencerClipGridState::nextGeneration(
    uint32_t current
) noexcept {
    const uint32_t next = current + 1U;
    return next == 0U ? 1U : next;
}

FLASHMEM void SequencerClipGridState::publishMutation() noexcept {
    revision_.set(nextGeneration(revision_.get()));
}

FLASHMEM void SequencerClipGridState::reset(uint16_t enabledTrackMask) {
    enabled_track_mask_ = static_cast<uint16_t>(
        enabledTrackMask & static_cast<uint16_t>((1U << TRACK_COUNT) - 1U)
    );
    resident_slots_.fill(INVALID_SLOT);
    stop_masks_.fill(0U);
    clip_behaviors_.fill({});
    scene_behaviors_.fill({});
    inactive_retained_bytes_ = 0U;
    inactive_document_count_ = 0U;
    for (auto& cell : cells_) {
        cell.document.reset();
        cell.generation = 1U;
    }
    for (uint8_t track = 0U; track < TRACK_COUNT; ++track) {
        if ((enabled_track_mask_ & static_cast<uint16_t>(1U << track)) != 0U) {
            resident_slots_[track] = 0U;
        }
    }
    publishMutation();
}

FLASHMEM void SequencerClipGridState::clearTrack(uint8_t track) noexcept {
    if (track >= TRACK_COUNT) return;
    for (uint8_t slot = 0U; slot < SLOT_COUNT; ++slot) {
        auto& cell = cells_[cellIndex({track, slot})];
        if (cell.document != nullptr) {
            inactive_retained_bytes_ -= sequencerClipDocumentRetainedBytes(
                *cell.document
            );
            cell.document.reset();
            --inactive_document_count_;
        }
        stop_masks_[slot] = static_cast<uint16_t>(
            stop_masks_[slot] &
            static_cast<uint16_t>(~static_cast<uint16_t>(1U << track))
        );
        clip_behaviors_[cellIndex({track, slot})] = {};
        cell.generation = nextGeneration(cell.generation);
    }
    resident_slots_[track] = INVALID_SLOT;
}

FLASHMEM void SequencerClipGridState::synchronizeEnabledTracks(
    uint16_t enabledTrackMask
) {
    enabledTrackMask = static_cast<uint16_t>(
        enabledTrackMask & static_cast<uint16_t>((1U << TRACK_COUNT) - 1U)
    );
    bool changed = false;
    for (uint8_t track = 0U; track < TRACK_COUNT; ++track) {
        const bool enabled = (enabledTrackMask & static_cast<uint16_t>(1U << track)) != 0U;
        const bool wasEnabled =
            (enabled_track_mask_ & static_cast<uint16_t>(1U << track)) != 0U;
        if (enabled == wasEnabled) continue;
        changed = true;
        if (enabled) {
            resident_slots_[track] = 0U;
            cells_[cellIndex({track, 0U})].generation = nextGeneration(
                cells_[cellIndex({track, 0U})].generation
            );
        } else {
            clearTrack(track);
        }
    }
    enabled_track_mask_ = enabledTrackMask;
    if (changed) publishMutation();
}

FLASHMEM uint8_t SequencerClipGridState::residentSlot(
    uint8_t track
) const noexcept {
    return track < TRACK_COUNT ? resident_slots_[track] : INVALID_SLOT;
}

FLASHMEM bool SequencerClipGridState::isResident(
    SequencerClipAddress address
) const noexcept {
    return validAddress(address) && resident_slots_[address.track] == address.slot;
}

FLASHMEM bool SequencerClipGridState::isOccupied(
    SequencerClipAddress address
) const noexcept {
    return isResident(address) ||
        (validAddress(address) && cells_[cellIndex(address)].document != nullptr);
}

FLASHMEM SequencerLauncherSlotKind SequencerClipGridState::slotKind(
    SequencerClipAddress address
) const noexcept {
    if (!validAddress(address)) return SequencerLauncherSlotKind::EMPTY;
    if (isOccupied(address)) return SequencerLauncherSlotKind::CLIP;
    return isStop(address)
        ? SequencerLauncherSlotKind::STOP
        : SequencerLauncherSlotKind::EMPTY;
}

FLASHMEM bool SequencerClipGridState::isStop(
    SequencerClipAddress address
) const noexcept {
    return validAddress(address) &&
        (stop_masks_[address.slot] &
         static_cast<uint16_t>(1U << address.track)) != 0U;
}

FLASHMEM bool SequencerClipGridState::setStop(
    SequencerClipAddress address
) noexcept {
    if (!validAddress(address) || isOccupied(address) ||
        (enabled_track_mask_ & static_cast<uint16_t>(1U << address.track)) == 0U ||
        isStop(address)) {
        return false;
    }
    stop_masks_[address.slot] = static_cast<uint16_t>(
        stop_masks_[address.slot] |
        static_cast<uint16_t>(1U << address.track)
    );
    clip_behaviors_[cellIndex(address)] = {};
    auto& cell = cells_[cellIndex(address)];
    cell.generation = nextGeneration(cell.generation);
    publishMutation();
    return true;
}

FLASHMEM bool SequencerClipGridState::clearStop(
    SequencerClipAddress address
) noexcept {
    if (!isStop(address)) return false;
    stop_masks_[address.slot] = static_cast<uint16_t>(
        stop_masks_[address.slot] &
        static_cast<uint16_t>(~static_cast<uint16_t>(1U << address.track))
    );
    auto& cell = cells_[cellIndex(address)];
    cell.generation = nextGeneration(cell.generation);
    publishMutation();
    return true;
}

FLASHMEM SequencerLauncherBehavior SequencerClipGridState::clipBehavior(
    SequencerClipAddress address
) const noexcept {
    return validAddress(address)
        ? clip_behaviors_[cellIndex(address)]
        : SequencerLauncherBehavior{};
}

FLASHMEM SequencerLauncherBehavior SequencerClipGridState::sceneBehavior(
    uint8_t slot
) const noexcept {
    return slot < SLOT_COUNT
        ? scene_behaviors_[slot]
        : SequencerLauncherBehavior{};
}

FLASHMEM bool SequencerClipGridState::setClipBehavior(
    SequencerClipAddress address,
    SequencerLauncherBehavior behavior
) noexcept {
    if (!validAddress(address) || !isOccupied(address) ||
        !validLauncherBehavior(behavior) ||
        clip_behaviors_[cellIndex(address)] == behavior) {
        return false;
    }
    clip_behaviors_[cellIndex(address)] = behavior;
    publishMutation();
    return true;
}

FLASHMEM bool SequencerClipGridState::setSceneBehavior(
    uint8_t slot,
    SequencerLauncherBehavior behavior
) noexcept {
    if (slot >= SLOT_COUNT || !validLauncherBehavior(behavior) ||
        scene_behaviors_[slot] == behavior) {
        return false;
    }
    scene_behaviors_[slot] = behavior;
    publishMutation();
    return true;
}

FLASHMEM bool SequencerClipGridState::sceneUsed(uint8_t slot) const noexcept {
    if (slot >= SLOT_COUNT) return false;
    bool used = stop_masks_[slot] != 0U ||
        !(scene_behaviors_[slot] == SequencerLauncherBehavior{});
    for (uint8_t track = 0U; !used && track < TRACK_COUNT; ++track) {
        used = isOccupied({track, slot});
    }
    return used;
}

FLASHMEM uint8_t SequencerClipGridState::lastNavigableScene() const noexcept {
    uint8_t highest = 0U;
    for (uint8_t slot = 0U; slot < SLOT_COUNT; ++slot) {
        if (sceneUsed(slot)) highest = slot;
    }
    return highest < SLOT_COUNT - 1U
        ? static_cast<uint8_t>(highest + 1U)
        : highest;
}

FLASHMEM uint32_t SequencerClipGridState::generation(
    SequencerClipAddress address
) const noexcept {
    return validAddress(address) ? cells_[cellIndex(address)].generation : 0U;
}

FLASHMEM uint8_t SequencerClipGridState::occupiedCount() const noexcept {
    uint8_t residents = 0U;
    for (const uint8_t slot : resident_slots_) {
        if (slot != INVALID_SLOT) ++residents;
    }
    return static_cast<uint8_t>(residents + inactive_document_count_);
}

FLASHMEM const SequencerClipDocument* SequencerClipGridState::inactiveDocument(
    SequencerClipAddress address
) const noexcept {
    if (!validAddress(address) || isResident(address)) return nullptr;
    return cells_[cellIndex(address)].document.get();
}

FLASHMEM SequencerClipDocument* SequencerClipGridState::inactiveDocument(
    SequencerClipAddress address
) noexcept {
    if (!validAddress(address) || isResident(address)) return nullptr;
    return cells_[cellIndex(address)].document.get();
}

FLASHMEM bool SequencerClipGridState::markInactiveDocumentMutated(
    SequencerClipAddress address
) noexcept {
    if (!validAddress(address) || isResident(address)) return false;
    auto& cell = cells_[cellIndex(address)];
    if (!cell.document) return false;
    cell.generation = nextGeneration(cell.generation);
    publishMutation();
    return true;
}

FLASHMEM bool SequencerClipGridState::installInactiveDocument(
    SequencerClipAddress address,
    SequencerClipDocumentPtr&& document
) {
    const uint32_t retainedBytes = document != nullptr
        ? sequencerClipDocumentRetainedBytes(*document)
        : 0U;
    if (!document || !validSequencerClipDocument(*document, document->trackKind) ||
        !validAddress(address) || slotKind(address) !=
            SequencerLauncherSlotKind::EMPTY ||
        (enabled_track_mask_ & static_cast<uint16_t>(1U << address.track)) == 0U ||
        inactive_document_count_ >= MAX_INACTIVE_DOCUMENTS ||
        retainedBytes > MAX_INACTIVE_RETAINED_BYTES - inactive_retained_bytes_) {
        return false;
    }
    auto& cell = cells_[cellIndex(address)];
    cell.document = std::move(document);
    clip_behaviors_[cellIndex(address)] = {};
    cell.generation = nextGeneration(cell.generation);
    inactive_retained_bytes_ += retainedBytes;
    ++inactive_document_count_;
    publishMutation();
    return true;
}

FLASHMEM SequencerClipDocumentPtr SequencerClipGridState::removeInactiveDocument(
    SequencerClipAddress address
) {
    if (!validAddress(address) || isResident(address)) return {};
    auto& cell = cells_[cellIndex(address)];
    if (!cell.document) return {};
    auto removed = std::move(cell.document);
    inactive_retained_bytes_ -= sequencerClipDocumentRetainedBytes(*removed);
    cell.generation = nextGeneration(cell.generation);
    clip_behaviors_[cellIndex(address)] = {};
    --inactive_document_count_;
    publishMutation();
    return removed;
}

FLASHMEM bool SequencerClipGridState::moveClip(
    SequencerClipAddress source,
    SequencerClipAddress destination
) noexcept {
    if (!validAddress(source) || !validAddress(destination) ||
        source == destination ||
        !isOccupied(source) || slotKind(destination) !=
            SequencerLauncherSlotKind::EMPTY ||
        (enabled_track_mask_ &
         static_cast<uint16_t>(1U << destination.track)) == 0U ||
        (source.track != destination.track && isResident(source))) {
        return false;
    }

    auto& sourceCell = cells_[cellIndex(source)];
    auto& destinationCell = cells_[cellIndex(destination)];
    if (isResident(source)) {
        if (sourceCell.document || destinationCell.document) return false;
        resident_slots_[source.track] = destination.slot;
    } else {
        if (!sourceCell.document || destinationCell.document) return false;
        destinationCell.document = std::move(sourceCell.document);
    }
    clip_behaviors_[cellIndex(destination)] =
        clip_behaviors_[cellIndex(source)];
    clip_behaviors_[cellIndex(source)] = {};
    sourceCell.generation = nextGeneration(sourceCell.generation);
    destinationCell.generation = nextGeneration(destinationCell.generation);
    publishMutation();
    return true;
}

FLASHMEM bool SequencerClipGridState::clearResident(
    SequencerClipAddress address
) noexcept {
    if (!isResident(address)) return false;
    auto& cell = cells_[cellIndex(address)];
    if (cell.document != nullptr) return false;
    resident_slots_[address.track] = INVALID_SLOT;
    clip_behaviors_[cellIndex(address)] = {};
    cell.generation = nextGeneration(cell.generation);
    publishMutation();
    return true;
}

FLASHMEM bool SequencerClipGridState::restoreResident(
    SequencerClipAddress address,
    SequencerLauncherBehavior behavior
) noexcept {
    if (!validAddress(address) ||
        (enabled_track_mask_ & static_cast<uint16_t>(1U << address.track)) == 0U ||
        resident_slots_[address.track] != INVALID_SLOT ||
        slotKind(address) != SequencerLauncherSlotKind::EMPTY ||
        !validLauncherBehavior(behavior)) {
        return false;
    }
    resident_slots_[address.track] = address.slot;
    clip_behaviors_[cellIndex(address)] = behavior;
    auto& cell = cells_[cellIndex(address)];
    cell.generation = nextGeneration(cell.generation);
    publishMutation();
    return true;
}

FLASHMEM SequencerClipDocumentPtr
SequencerClipGridState::promoteInactiveDocument(
    SequencerClipAddress address
) noexcept {
    if (!validAddress(address) ||
        (enabled_track_mask_ & static_cast<uint16_t>(1U << address.track)) == 0U ||
        resident_slots_[address.track] != INVALID_SLOT || isStop(address)) {
        return {};
    }
    auto& cell = cells_[cellIndex(address)];
    if (cell.document == nullptr) return {};
    auto incoming = std::move(cell.document);
    inactive_retained_bytes_ -= sequencerClipDocumentRetainedBytes(*incoming);
    --inactive_document_count_;
    resident_slots_[address.track] = address.slot;
    cell.generation = nextGeneration(cell.generation);
    publishMutation();
    return incoming;
}

FLASHMEM bool canTransferSequencerClip(
    const SequencerClipGridState& grid,
    const SequencerTrackBankState& bank,
    SequencerClipAddress source,
    SequencerClipAddress destination,
    SequencerClipStructureAction action
) noexcept {
    if ((action != SequencerClipStructureAction::MOVE &&
         action != SequencerClipStructureAction::DUPLICATE_CLIP) ||
        !SequencerClipGridState::validAddress(source) ||
        !SequencerClipGridState::validAddress(destination) ||
        source == destination || !grid.isOccupied(source) ||
        grid.slotKind(destination) != SequencerLauncherSlotKind::EMPTY ||
        !bank.isTrackEnabled(source.track) ||
        !bank.isTrackEnabled(destination.track) ||
        bank.trackKind(source.track) != bank.trackKind(destination.track) ||
        (action == SequencerClipStructureAction::MOVE &&
         source.track != destination.track && grid.isResident(source))) {
        return false;
    }

    const auto* document = grid.inactiveDocument(source);
    return document == nullptr ||
        document->trackKind == bank.trackKind(destination.track);
}

FLASHMEM uint16_t compatibleSequencerClipTrackMask(
    const SequencerClipGridState& grid,
    const SequencerTrackBankState& bank,
    SequencerClipAddress source
) noexcept {
    if (!SequencerClipGridState::validAddress(source) ||
        !grid.isOccupied(source) || !bank.isTrackEnabled(source.track)) {
        return 0U;
    }

    const auto kind = bank.trackKind(source.track);
    uint16_t mask = 0U;
    for (uint8_t track = 0U;
         track < SequencerClipGridState::TRACK_COUNT;
         ++track) {
        if (bank.isTrackEnabled(track) && bank.trackKind(track) == kind) {
            mask = static_cast<uint16_t>(
                mask | static_cast<uint16_t>(1U << track));
        }
    }
    return mask;
}

FLASHMEM bool firstSequencerClipTransferDestination(
    const SequencerClipGridState& grid,
    const SequencerTrackBankState& bank,
    SequencerClipAddress source,
    SequencerClipStructureAction action,
    SequencerClipAddress& out
) noexcept {
    for (uint8_t offset = 1U;
         offset < SequencerClipGridState::SLOT_COUNT;
         ++offset) {
        const SequencerClipAddress candidate{
            source.track,
            static_cast<uint8_t>(
                (source.slot + offset) % SequencerClipGridState::SLOT_COUNT),
        };
        if (canTransferSequencerClip(grid, bank, source, candidate, action)) {
            out = candidate;
            return true;
        }
    }

    for (uint8_t trackOffset = 1U;
         trackOffset < SequencerClipGridState::TRACK_COUNT;
         ++trackOffset) {
        const uint8_t track = static_cast<uint8_t>(
            (source.track + trackOffset) %
            SequencerClipGridState::TRACK_COUNT);
        for (uint8_t slot = 0U;
             slot < SequencerClipGridState::SLOT_COUNT;
             ++slot) {
            const SequencerClipAddress candidate{track, slot};
            if (canTransferSequencerClip(
                    grid, bank, source, candidate, action)) {
                out = candidate;
                return true;
            }
        }
    }
    return false;
}

FLASHMEM bool canMoveSequencerClipSelection(
    const SequencerClipGridState& grid,
    const SequencerTrackBankState& bank,
    const SequencerState& active,
    const SequencerClipSelectionMask& selection,
    int8_t trackOffset,
    int8_t slotOffset
) noexcept {
    if ((trackOffset == 0 && slotOffset == 0) ||
        active.stepContentDraft.active.get()) {
        return false;
    }

    uint8_t count = 0U;
    uint8_t addedInactive = 0U;
    uint32_t addedRetainedBytes = 0U;
    for (uint8_t track = 0U;
         track < SequencerClipGridState::TRACK_COUNT;
         ++track) {
        if (selection[track] == 0U) continue;
        for (uint8_t slot = 0U;
             slot < SequencerClipGridState::SLOT_COUNT;
            ++slot) {
            const SequencerClipAddress source{track, slot};
            if ((selection[track] & static_cast<uint8_t>(1U << slot)) == 0U) {
                continue;
            }
            ++count;
            const int destinationTrack =
                static_cast<int>(track) + trackOffset;
            const int destinationSlot =
                static_cast<int>(slot) + slotOffset;
            if (destinationTrack < 0 ||
                destinationTrack >= SequencerClipGridState::TRACK_COUNT ||
                destinationSlot < 0 ||
                destinationSlot >= SequencerClipGridState::SLOT_COUNT ||
                !grid.isOccupied(source) || !bank.isTrackEnabled(track) ||
                !bank.isTrackEnabled(static_cast<uint8_t>(destinationTrack)) ||
                bank.trackKind(track) != bank.trackKind(
                    static_cast<uint8_t>(destinationTrack))) {
                return false;
            }
            const SequencerClipAddress destination{
                static_cast<uint8_t>(destinationTrack),
                static_cast<uint8_t>(destinationSlot),
            };
            if (grid.slotKind(destination) != SequencerLauncherSlotKind::EMPTY &&
                (selection[destination.track] & static_cast<uint8_t>(
                    1U << destination.slot)) == 0U) {
                return false;
            }
            const auto* document = grid.inactiveDocument(source);
            if (document != nullptr && document->trackKind !=
                    bank.trackKind(destination.track)) {
                return false;
            }
            if (trackOffset != 0 && grid.isResident(source)) {
                ++addedInactive;
                addedRetainedBytes += canonicalTrackRetainedBytes(
                    bank, active, track);
            }
        }
    }
    return count != 0U &&
        count <= SequencerClipMoveBatch::MAX_COUNT &&
        addedInactive <= SequencerClipGridState::MAX_INACTIVE_DOCUMENTS -
            grid.inactiveDocumentCount() &&
        addedRetainedBytes <=
            SequencerClipGridState::MAX_INACTIVE_RETAINED_BYTES -
                grid.inactiveRetainedBytes();
}

FLASHMEM bool firstSequencerClipSelectionMoveOffset(
    const SequencerClipGridState& grid,
    const SequencerTrackBankState& bank,
    const SequencerState& active,
    const SequencerClipSelectionMask& selection,
    int8_t& trackOffset,
    int8_t& slotOffset
) noexcept {
    constexpr int maxTrackOffset = SequencerClipGridState::TRACK_COUNT - 1;
    constexpr int maxSlotOffset = SequencerClipGridState::SLOT_COUNT - 1;
    for (int distance = 1;
         distance <= maxTrackOffset + maxSlotOffset;
         ++distance) {
        for (int track = -maxTrackOffset;
             track <= maxTrackOffset;
             ++track) {
            for (int slot = -maxSlotOffset;
                 slot <= maxSlotOffset;
                 ++slot) {
                if (std::abs(track) + std::abs(slot) != distance ||
                    !canMoveSequencerClipSelection(
                        grid,
                        bank,
                        active,
                        selection,
                        static_cast<int8_t>(track),
                        static_cast<int8_t>(slot))) {
                    continue;
                }
                trackOffset = static_cast<int8_t>(track);
                slotOffset = static_cast<int8_t>(slot);
                return true;
            }
        }
    }
    return false;
}

FLASHMEM uint16_t compatibleSequencerClipSelectionTrackMask(
    const SequencerTrackBankState& bank,
    const SequencerClipSelectionMask& selection,
    uint8_t anchorTrack
) noexcept {
    if (anchorTrack >= SequencerClipGridState::TRACK_COUNT) return 0U;
    uint16_t mask = 0U;
    for (uint8_t candidate = 0U;
         candidate < SequencerClipGridState::TRACK_COUNT;
         ++candidate) {
        const int offset = static_cast<int>(candidate) - anchorTrack;
        bool compatible = true;
        bool selected = false;
        for (uint8_t source = 0U;
             source < SequencerClipGridState::TRACK_COUNT;
             ++source) {
            if (selection[source] == 0U) continue;
            selected = true;
            const int destination = static_cast<int>(source) + offset;
            if (destination < 0 ||
                destination >= SequencerClipGridState::TRACK_COUNT ||
                !bank.isTrackEnabled(source) ||
                !bank.isTrackEnabled(static_cast<uint8_t>(destination)) ||
                bank.trackKind(source) !=
                    bank.trackKind(static_cast<uint8_t>(destination))) {
                compatible = false;
                break;
            }
        }
        if (selected && compatible) {
            mask = static_cast<uint16_t>(
                mask | static_cast<uint16_t>(1U << candidate));
        }
    }
    return mask;
}

FLASHMEM uint32_t sequencerClipDocumentRetainedBytes(
    const SequencerClipDocument& document
) noexcept {
    uint32_t bytes = sizeof(SequencerClipDocument) +
        kExtmemAllocationOverheadEstimate;
    if (document.graph != nullptr) {
        bytes += sizeof(Graph) + kExtmemAllocationOverheadEstimate;
    }
    if (document.ccLanes != nullptr) {
        bytes += sizeof(SequencerCcLaneBank) + kExtmemAllocationOverheadEstimate;
    }
    if (document.drum != nullptr) {
        bytes += sizeof(DrumTrackState) + kExtmemAllocationOverheadEstimate;
    }
    return bytes;
}

FLASHMEM uint16_t sequencerClipDocumentRetainedSpans(
    const SequencerClipDocument& document
) noexcept {
    return static_cast<uint16_t>(
        1U + (document.graph != nullptr ? 1U : 0U) +
        (document.ccLanes != nullptr ? 1U : 0U) +
        (document.drum != nullptr ? 1U : 0U));
}

FLASHMEM SequencerClipStructureChangePtr prepareSequencerClipInstallChange(
    SequencerClipStructureAction action,
    SequencerClipAddress destination,
    SequencerClipDocumentPtr document,
    SequencerLauncherBehavior behavior
) {
    if ((action != SequencerClipStructureAction::CREATE &&
         action != SequencerClipStructureAction::DUPLICATE_CLIP) ||
         !SequencerClipGridState::validAddress(destination) || !document ||
         !validLauncherBehavior(behavior)) {
        return {};
    }
    auto change = core::app::makeExtmemUniqueCold<SequencerClipStructureChange>();
    if (!change) return {};
    change->action = action;
    change->source = destination;
    change->destination = destination;
    change->retainedBytes = static_cast<uint32_t>(
        sizeof(SequencerClipStructureChange) + kExtmemAllocationOverheadEstimate +
        sequencerClipDocumentRetainedBytes(*document));
    change->retainedSpans = static_cast<uint16_t>(
        1U + sequencerClipDocumentRetainedSpans(*document));
    change->behavior = behavior;
    change->document = std::move(document);
    return change;
}

FLASHMEM SequencerClipStructureChangePtr prepareSequencerClipDeleteChange(
    const SequencerClipGridState& grid,
    SequencerClipAddress source
) {
    const auto* document = grid.inactiveDocument(source);
    if (document == nullptr) return {};
    auto change = core::app::makeExtmemUniqueCold<SequencerClipStructureChange>();
    if (!change) return {};
    change->action = SequencerClipStructureAction::DELETE;
    change->source = source;
    change->destination = source;
    change->retainedBytes = static_cast<uint32_t>(
        sizeof(SequencerClipStructureChange) + kExtmemAllocationOverheadEstimate +
        sequencerClipDocumentRetainedBytes(*document));
    change->retainedSpans = static_cast<uint16_t>(
        1U + sequencerClipDocumentRetainedSpans(*document));
    change->behavior = grid.clipBehavior(source);
    return change;
}

FLASHMEM SequencerClipStructureChangePtr
prepareSequencerResidentClipDeleteChange(
    const SequencerClipGridState& grid,
    const SequencerTrackBankState& bank,
    const SequencerState& active,
    SequencerClipAddress source
) {
    if (!grid.isResident(source) || !bank.isTrackEnabled(source.track) ||
        active.stepContentDraft.active.get()) {
        return {};
    }
    const auto kind = bank.trackKind(source.track);
    SequencerClipDocumentPtr blank;
    if (!createEmptySequencerClipDocument(
            kind,
            kind == SequencerTrackKind::DRUM
                ? &bank.drumTrack(source.track)
                : nullptr,
            blank)) {
        return {};
    }
    auto change = core::app::makeExtmemUniqueCold<SequencerClipStructureChange>();
    if (!change) return {};
    change->action = SequencerClipStructureAction::DELETE;
    change->source = source;
    change->destination = source;
    change->resident = true;
    change->retainedBytes = static_cast<uint32_t>(
        sizeof(SequencerClipStructureChange) + kExtmemAllocationOverheadEstimate +
        std::max(
            sequencerClipDocumentRetainedBytes(*blank),
            canonicalTrackRetainedBytes(bank, active, source.track)
        )
    );
    change->retainedSpans = static_cast<uint16_t>(
        1U + std::max(
            sequencerClipDocumentRetainedSpans(*blank),
            canonicalTrackRetainedSpans(bank, active, source.track)
        )
    );
    change->behavior = grid.clipBehavior(source);
    change->document = std::move(blank);
    return change;
}

FLASHMEM SequencerClipStructureChangePtr prepareSequencerClipMoveChange(
    const SequencerClipGridState& grid,
    SequencerClipAddress source,
    SequencerClipAddress destination
) {
    if (!SequencerClipGridState::validAddress(source) ||
        !SequencerClipGridState::validAddress(destination) ||
        source == destination ||
        !grid.isOccupied(source) || grid.slotKind(destination) !=
            SequencerLauncherSlotKind::EMPTY ||
        (source.track != destination.track && grid.isResident(source))) {
        return {};
    }
    auto change = core::app::makeExtmemUniqueCold<SequencerClipStructureChange>();
    if (!change) return {};
    change->action = SequencerClipStructureAction::MOVE;
    change->source = source;
    change->destination = destination;
    change->retainedBytes = static_cast<uint32_t>(
        sizeof(SequencerClipStructureChange) + kExtmemAllocationOverheadEstimate);
    change->retainedSpans = 1U;
    return change;
}

FLASHMEM SequencerClipStructureChangePtr
prepareSequencerClipSelectionMoveChange(
    const SequencerClipGridState& grid,
    const SequencerTrackBankState& bank,
    const SequencerState& active,
    const SequencerClipSelectionMask& selection,
    int8_t trackOffset,
    int8_t slotOffset
) {
    if (!canMoveSequencerClipSelection(
            grid, bank, active, selection, trackOffset, slotOffset)) {
        return {};
    }
    auto batch = core::app::makeExtmemUniqueCold<SequencerClipMoveBatch>();
    if (!batch) return {};

    const int firstTrack = trackOffset > 0
        ? SequencerClipGridState::TRACK_COUNT - 1 : 0;
    const int lastTrack = trackOffset > 0
        ? -1 : SequencerClipGridState::TRACK_COUNT;
    const int trackStep = trackOffset > 0 ? -1 : 1;
    const int firstSlot = slotOffset > 0
        ? SequencerClipGridState::SLOT_COUNT - 1 : 0;
    const int lastSlot = slotOffset > 0
        ? -1 : SequencerClipGridState::SLOT_COUNT;
    const int slotStep = slotOffset > 0 ? -1 : 1;
    uint32_t retainedBytes = static_cast<uint32_t>(
        sizeof(SequencerClipStructureChange) +
        sizeof(SequencerClipMoveBatch) +
        2U * kExtmemAllocationOverheadEstimate);
    uint16_t retainedSpans = 2U;
    for (int track = firstTrack; track != lastTrack; track += trackStep) {
        for (int slot = firstSlot; slot != lastSlot; slot += slotStep) {
            const SequencerClipAddress source{
                static_cast<uint8_t>(track),
                static_cast<uint8_t>(slot),
            };
            if ((selection[source.track] & static_cast<uint8_t>(
                    1U << source.slot)) == 0U) {
                continue;
            }
            auto& entry = batch->entries[batch->count++];
            entry.source = source;
            entry.destination = {
                static_cast<uint8_t>(track + trackOffset),
                static_cast<uint8_t>(slot + slotOffset),
            };
            entry.behavior = grid.clipBehavior(source);
            if (trackOffset == 0 || !grid.isResident(source)) continue;

            entry.residentTransfer = true;
            const auto kind = bank.trackKind(source.track);
            if (!createEmptySequencerClipDocument(
                    kind,
                    kind == SequencerTrackKind::DRUM
                        ? &bank.drumTrack(source.track)
                        : nullptr,
                    entry.residentExchange)) {
                return {};
            }
            retainedBytes += std::max(
                sequencerClipDocumentRetainedBytes(*entry.residentExchange),
                canonicalTrackRetainedBytes(bank, active, source.track)
            );
            retainedSpans = static_cast<uint16_t>(
                retainedSpans + std::max(
                    sequencerClipDocumentRetainedSpans(
                        *entry.residentExchange),
                    canonicalTrackRetainedSpans(
                        bank, active, source.track)
                )
            );
        }
    }

    if (batch->count == 1U &&
        !batch->entries[0U].residentTransfer) {
        return prepareSequencerClipMoveChange(
            grid,
            batch->entries[0U].source,
            batch->entries[0U].destination
        );
    }

    auto change = core::app::makeExtmemUniqueCold<SequencerClipStructureChange>();
    if (!change) return {};
    change->action = SequencerClipStructureAction::MOVE;
    change->source = batch->entries[0U].source;
    change->destination = batch->entries[0U].destination;
    change->retainedBytes = retainedBytes;
    change->retainedSpans = retainedSpans;
    change->moveBatch = std::move(batch);
    return change;
}

FLASHMEM bool applySequencerClipMoveBatch(
    SequencerClipGridState& grid,
    SequencerTrackBankState& bank,
    SequencerState& active,
    SequencerClipMoveBatch& batch,
    bool after
) noexcept {
    if (batch.count == 0U || batch.count > batch.entries.size()) return false;
    for (uint8_t offset = 0U; offset < batch.count; ++offset) {
        const uint8_t index = after
            ? offset
            : static_cast<uint8_t>(batch.count - 1U - offset);
        auto& entry = batch.entries[index];
        const auto from = after ? entry.source : entry.destination;
        const auto to = after ? entry.destination : entry.source;
        if (!entry.residentTransfer) {
            if (!grid.moveClip(from, to)) return false;
            continue;
        }
        if (after) {
            if (entry.residentExchange == nullptr ||
                !grid.isResident(entry.source) ||
                !exchangeCanonicalTrackDocument(
                    bank,
                    active,
                    entry.source.track,
                    *entry.residentExchange) ||
                !grid.clearResident(entry.source) ||
                !grid.installInactiveDocument(
                    entry.destination,
                    std::move(entry.residentExchange))) {
                return false;
            }
            if (!(entry.behavior == SequencerLauncherBehavior{}) &&
                !grid.setClipBehavior(entry.destination, entry.behavior)) {
                return false;
            }
            continue;
        }
        entry.residentExchange = grid.removeInactiveDocument(entry.destination);
        if (!entry.residentExchange ||
            !exchangeCanonicalTrackDocument(
                bank,
                active,
                entry.source.track,
                *entry.residentExchange) ||
            !grid.restoreResident(entry.source, entry.behavior)) {
            return false;
        }
    }
    return true;
}

FLASHMEM bool applySequencerClipStructureChange(
    SequencerClipGridState& grid,
    SequencerClipStructureChange& change,
    bool after
) noexcept {
    if (change.resident || change.moveBatch != nullptr) return false;
    if (change.afterApplied == after) return false;

    switch (change.action) {
        case SequencerClipStructureAction::CREATE:
        case SequencerClipStructureAction::DUPLICATE_CLIP:
            if (after) {
                if (!change.document ||
                    !grid.installInactiveDocument(
                        change.destination, std::move(change.document))) {
                    return false;
                }
                if (!(change.behavior == SequencerLauncherBehavior{}) &&
                    !grid.setClipBehavior(change.destination, change.behavior)) {
                    return false;
                }
            } else {
                change.document = grid.removeInactiveDocument(change.destination);
                if (!change.document) return false;
            }
            break;
        case SequencerClipStructureAction::DELETE:
            if (after) {
                change.document = grid.removeInactiveDocument(change.source);
                if (!change.document) return false;
            } else if (!change.document ||
                       !grid.installInactiveDocument(
                           change.source, std::move(change.document))) {
                return false;
            } else if (!(change.behavior == SequencerLauncherBehavior{}) &&
                       !grid.setClipBehavior(change.source, change.behavior)) {
                return false;
            }
            break;
        case SequencerClipStructureAction::MOVE:
            if (!grid.moveClip(
                    after ? change.source : change.destination,
                    after ? change.destination : change.source)) {
                return false;
            }
            break;
        default: return false;
    }
    change.afterApplied = after;
    return true;
}

FLASHMEM bool applySequencerClipStructureChange(
    SequencerClipGridState& grid,
    SequencerTrackBankState& bank,
    SequencerState& active,
    SequencerClipStructureChange& change,
    bool after
) noexcept {
    if (change.moveBatch != nullptr) {
        if (change.action != SequencerClipStructureAction::MOVE ||
            change.afterApplied == after || !applySequencerClipMoveBatch(
                grid, bank, active, *change.moveBatch, after)) {
            return false;
        }
        change.afterApplied = after;
        return true;
    }
    if (!change.resident) {
        return applySequencerClipStructureChange(grid, change, after);
    }
    if (change.action != SequencerClipStructureAction::DELETE ||
        change.afterApplied == after || !change.document ||
        !SequencerClipGridState::validAddress(change.source) ||
        change.document->trackKind != bank.trackKind(change.source.track) ||
        active.stepContentDraft.active.get()) {
        return false;
    }

    const bool gridReady = after
        ? grid.isResident(change.source)
        : grid.residentSlot(change.source.track) ==
              SequencerClipGridState::INVALID_SLOT &&
              grid.slotKind(change.source) == SequencerLauncherSlotKind::EMPTY;
    if (!gridReady || !exchangeCanonicalTrackDocument(
            bank, active, change.source.track, *change.document)) {
        return false;
    }
    const bool gridChanged = after
        ? grid.clearResident(change.source)
        : grid.restoreResident(change.source, change.behavior);
    if (!gridChanged) {
#if defined(__GNUC__) || defined(__clang__)
        __builtin_trap();
#endif
        return false;
    }
    change.afterApplied = after;
    return true;
}

FLASHMEM bool captureSequencerClipGridSnapshot(
    const SequencerClipGridState& source,
    SequencerClipGridSnapshot& out
) {
    SequencerClipGridSnapshot next;
    next.residentSlots = source.resident_slots_;
    next.stopMasks = source.stop_masks_;
    next.clipBehaviors = source.clip_behaviors_;
    next.sceneBehaviors = source.scene_behaviors_;
    for (uint16_t index = 0U; index < SequencerClipGridState::CELL_COUNT; ++index) {
        next.generations[index] = source.cells_[index].generation;
        const auto* document = source.cells_[index].document.get();
        if (document != nullptr &&
            !cloneSequencerClipDocument(*document, next.documents[index])) {
            return false;
        }
    }
    out = std::move(next);
    return true;
}

FLASHMEM void extractSequencerClipGridSnapshot(
    SequencerClipGridState& source,
    SequencerClipGridSnapshot& out
) noexcept {
    out.reset();
    out.residentSlots = source.resident_slots_;
    out.stopMasks = source.stop_masks_;
    out.clipBehaviors = source.clip_behaviors_;
    out.sceneBehaviors = source.scene_behaviors_;
    for (uint16_t index = 0U; index < SequencerClipGridState::CELL_COUNT; ++index) {
        out.generations[index] = source.cells_[index].generation;
        out.documents[index] = std::move(source.cells_[index].document);
    }
    source.inactive_document_count_ = 0U;
    source.inactive_retained_bytes_ = 0U;
}

FLASHMEM bool cloneSequencerClipGridSnapshot(
    const SequencerClipGridSnapshot& source,
    SequencerClipGridSnapshot& out
) {
    SequencerClipGridSnapshot next;
    next.residentSlots = source.residentSlots;
    next.generations = source.generations;
    next.stopMasks = source.stopMasks;
    next.clipBehaviors = source.clipBehaviors;
    next.sceneBehaviors = source.sceneBehaviors;
    for (uint16_t index = 0U; index < SequencerClipGridState::CELL_COUNT; ++index) {
        const auto* document = source.documents[index].get();
        if (document != nullptr &&
            !cloneSequencerClipDocument(*document, next.documents[index])) {
            return false;
        }
    }
    out = std::move(next);
    return true;
}

FLASHMEM bool validSequencerClipGridSnapshot(
    const SequencerClipGridSnapshot& snapshot,
    uint16_t enabledTrackMask,
    uint16_t drumTrackMask
) noexcept {
    uint8_t inactiveCount = 0U;
    uint32_t inactiveRetainedBytes = 0U;
    for (uint8_t track = 0U; track < SequencerClipGridState::TRACK_COUNT; ++track) {
        const uint16_t bit = static_cast<uint16_t>(1U << track);
        const bool enabled = (enabledTrackMask & bit) != 0U;
        const uint8_t resident = snapshot.residentSlots[track];
        if ((!enabled && resident != SequencerClipGridState::INVALID_SLOT) ||
            (resident != SequencerClipGridState::INVALID_SLOT &&
             resident >= SequencerClipGridState::SLOT_COUNT)) {
            return false;
        }
        const SequencerTrackKind expectedKind = (drumTrackMask & bit) != 0U
            ? SequencerTrackKind::DRUM
            : SequencerTrackKind::INSTRUMENT;
        for (uint8_t slot = 0U; slot < SequencerClipGridState::SLOT_COUNT; ++slot) {
            const uint16_t cell = SequencerClipGridState::cellIndex({track, slot});
            const auto* document = snapshot.documents[
                cell
            ].get();
            const bool residentHere = slot == resident;
            const bool clipHere = residentHere || document != nullptr;
            const bool stopHere = (snapshot.stopMasks[slot] & bit) != 0U;
            if ((stopHere && (!enabled || clipHere)) ||
                (!clipHere && !(snapshot.clipBehaviors[cell] ==
                    SequencerLauncherBehavior{})) ||
                !validLauncherBehavior(snapshot.clipBehaviors[cell])) {
                return false;
            }
            if (document == nullptr) continue;
            if (!enabled || slot == resident ||
                !validSequencerClipDocument(*document, expectedKind)) {
                return false;
            }
            const uint32_t retainedBytes = sequencerClipDocumentRetainedBytes(
                *document
            );
            if (++inactiveCount > SequencerClipGridState::MAX_INACTIVE_DOCUMENTS ||
                retainedBytes > SequencerClipGridState::MAX_INACTIVE_RETAINED_BYTES -
                    inactiveRetainedBytes) {
                return false;
            }
            inactiveRetainedBytes += retainedBytes;
        }
    }
    for (const auto& behavior : snapshot.sceneBehaviors) {
        if (!validLauncherBehavior(behavior)) return false;
    }
    return true;
}

FLASHMEM bool restoreSequencerClipGridSnapshot(
    SequencerClipGridState& target,
    SequencerClipGridSnapshot&& snapshot,
    uint16_t enabledTrackMask
) noexcept {
    enabledTrackMask = static_cast<uint16_t>(
        enabledTrackMask &
        static_cast<uint16_t>((1U << SequencerClipGridState::TRACK_COUNT) - 1U)
    );
    uint8_t inactiveCount = 0U;
    uint32_t inactiveRetainedBytes = 0U;
    for (uint8_t track = 0U; track < SequencerClipGridState::TRACK_COUNT; ++track) {
        const uint8_t resident = snapshot.residentSlots[track];
        if (resident != SequencerClipGridState::INVALID_SLOT &&
            resident >= SequencerClipGridState::SLOT_COUNT) {
            return false;
        }
    }
    for (uint16_t index = 0U; index < SequencerClipGridState::CELL_COUNT; ++index) {
        const uint8_t track = static_cast<uint8_t>(
            index / SequencerClipGridState::SLOT_COUNT
        );
        const uint8_t slot = static_cast<uint8_t>(
            index % SequencerClipGridState::SLOT_COUNT
        );
        if (snapshot.documents[index] != nullptr) {
            const uint32_t retainedBytes = sequencerClipDocumentRetainedBytes(
                *snapshot.documents[index]
            );
            if (snapshot.residentSlots[track] == slot ||
                ++inactiveCount > SequencerClipGridState::MAX_INACTIVE_DOCUMENTS ||
                retainedBytes > SequencerClipGridState::MAX_INACTIVE_RETAINED_BYTES -
                    inactiveRetainedBytes) {
                return false;
            }
            inactiveRetainedBytes += retainedBytes;
        }
    }

    target.resident_slots_ = snapshot.residentSlots;
    target.enabled_track_mask_ = enabledTrackMask;
    target.stop_masks_ = snapshot.stopMasks;
    target.clip_behaviors_ = snapshot.clipBehaviors;
    target.scene_behaviors_ = snapshot.sceneBehaviors;
    target.inactive_document_count_ = inactiveCount;
    target.inactive_retained_bytes_ = inactiveRetainedBytes;
    for (uint16_t index = 0U; index < SequencerClipGridState::CELL_COUNT; ++index) {
        target.cells_[index].generation = snapshot.generations[index] == 0U
            ? 1U
            : snapshot.generations[index];
        target.cells_[index].document = std::move(snapshot.documents[index]);
    }
    target.publishMutation();
    return true;
}

FLASHMEM bool applySequencerClipGridSnapshot(
    SequencerClipGridState& target,
    const SequencerClipGridSnapshot& snapshot,
    uint16_t enabledTrackMask
) {
    SequencerClipGridSnapshot cloned;
    return cloneSequencerClipGridSnapshot(snapshot, cloned) &&
        restoreSequencerClipGridSnapshot(
            target, std::move(cloned), enabledTrackMask);
}

FLASHMEM bool switchResidentSequencerClip(
    SequencerClipGridState& grid,
    SequencerTrackBankState& bank,
    SequencerState& active,
    SequencerClipAddress target
) {
    if (!SequencerClipGridState::validAddress(target) ||
        !grid.isOccupied(target) || grid.isResident(target) ||
        !bank.isTrackEnabled(target.track) ||
        active.stepContentDraft.active.get()) {
        return false;
    }
    const auto* incoming = grid.inactiveDocument(target);
    if (incoming == nullptr ||
        !validSequencerClipDocument(*incoming, bank.trackKind(target.track))) {
        return false;
    }

    if (grid.residentSlot(target.track) == SequencerClipGridState::INVALID_SLOT) {
        auto promoted = grid.promoteInactiveDocument(target);
        if (!promoted) return false;
        installDocumentPattern(bank, active, target.track, *promoted);
        if (promoted->trackKind == SequencerTrackKind::DRUM) {
            bank.restoreDrumTrack(target.track, promoted->trackKind, *promoted->drum);
        } else {
            bank.setTrackKind(target.track, SequencerTrackKind::INSTRUMENT);
        }
        return true;
    }

    const uint8_t oldSlot = grid.resident_slots_[target.track];
    auto& incomingCell = grid.cells_[SequencerClipGridState::cellIndex(target)];
    auto& outgoingCell = grid.cells_[SequencerClipGridState::cellIndex({
        target.track,
        oldSlot,
    })];
    if (!incomingCell.document || outgoingCell.document) return false;

    const uint32_t incomingBytes = sequencerClipDocumentRetainedBytes(
        *incomingCell.document
    );
    if (incomingBytes > grid.inactive_retained_bytes_) return false;
    const uint32_t retainedWithoutIncoming =
        grid.inactive_retained_bytes_ - incomingBytes;
    const uint32_t outgoingBytes = canonicalTrackRetainedBytes(
        bank,
        active,
        target.track
    );
    if (outgoingBytes > SequencerClipGridState::MAX_INACTIVE_RETAINED_BYTES -
            retainedWithoutIncoming ||
        !exchangeCanonicalTrackDocument(
            bank,
            active,
            target.track,
            *incomingCell.document
        )) {
        return false;
    }

    outgoingCell.document = std::move(incomingCell.document);
    outgoingCell.generation = SequencerClipGridState::nextGeneration(
        outgoingCell.generation
    );
    grid.inactive_retained_bytes_ = retainedWithoutIncoming + outgoingBytes;
    grid.resident_slots_[target.track] = target.slot;
    grid.publishMutation();
    return true;
}

}  // namespace core::state::sequencer
