#include "sequencer/SequencerRuntimeSnapshotBank.hpp"

#include <config/PlatformCompat.hpp>
#include <oc/realtime/InterruptGuard.hpp>

#include "state/project/ProjectDomainRules.hpp"
#include "state/sequencer/SequencerCcLanePatternOps.hpp"
#include "state/sequencer/SequencerSnapshotOps.hpp"
#include "state/sequencer/SequencerStepContentDraftOps.hpp"

namespace core::sequencer {

FLASHMEM SequencerRuntimeSnapshotBank::SequencerRuntimeSnapshotBank(
    core::state::sequencer::SequencerState& sequencer,
    core::state::sequencer::SequencerTrackBankState& trackBank,
    core::state::project::ProjectNavigationState& projectNavigation
)
    : sequencer_(sequencer)
    , track_bank_(trackBank)
    , project_navigation_(projectNavigation) {}

// Snapshot construction belongs to the main-loop control plane. The timer ISR
// only consumes the committed bank through the small accessors below, so keep
// this comparatively large scan/copy path out of scarce ITCM.
FLASHMEM uint8_t SequencerRuntimeSnapshotBank::refresh(
    const core::state::sequencer::SequencerClipRuntimeSources& sources
) {
    last_refresh_succeeded_ = false;
    const uint8_t currentIndex = active_index_;
    const uint8_t writeIndex = static_cast<uint8_t>(currentIndex ^ 0x1U);
    const bool forceRefresh = force_refresh_[writeIndex];
    auto& runtimeSnapshot = snapshots_[writeIndex];
    auto& writeSignatures = track_signatures_[writeIndex];
    auto& clipSourceSignatures = clip_source_signatures_[writeIndex];
    auto& laneSourceSignatures = lane_source_signatures_[writeIndex];

    const uint8_t activeTrack =
        core::state::sequencer::SequencerTrackBankState::clampTrackIndex(
            track_bank_.activeTrackIndex()
        );
    const auto& activePattern =
        core::state::sequencer::authoringPattern(sequencer_);
    const auto& activeClip =
        core::state::sequencer::authoringClip(sequencer_);
    uint16_t lanePresentMask = 0;
    for (uint8_t i = 0;
         i < core::state::sequencer::SequencerTrackBankState::TRACK_COUNT;
         ++i) {
        const auto& clipSource = sources[i];
        const auto* lanes = clipSource.document != nullptr
            ? clipSource.document->ccLanes.get()
            : core::state::sequencer::sequencerCcLaneView(
                  i == activeTrack ? activePattern : track_bank_.track(i));
        if (lanes != nullptr) {
            lanePresentMask = static_cast<uint16_t>(lanePresentMask | (1U << i));
        }
    }
    if (lanePresentMask != 0 && !lane_snapshots_[writeIndex]) {
        lane_snapshots_[writeIndex] =
            core::app::makeExtmemUnique<SequencerCcLaneRuntimeProjectSnapshot>();
        if (!lane_snapshots_[writeIndex]) {
            // Keep the currently committed flat+lane generation intact and
            // retry from the non-realtime update path on the next refresh.
            return currentIndex;
        }
    }
    if (lane_snapshots_[writeIndex]) {
        auto& lanes = *lane_snapshots_[writeIndex];
        if (lanes.presentMask != lanePresentMask) {
            lanes.presentMask = lanePresentMask;
        }
        for (uint8_t i = 0; i < lanes.tracks.size(); ++i) {
            const auto& clipSource = sources[i];
            const auto& residentPattern = i == activeTrack
                ? activePattern
                : track_bank_.track(i);
            const auto* source = clipSource.document != nullptr
                ? clipSource.document->ccLanes.get()
                : core::state::sequencer::sequencerCcLaneView(residentPattern);
            const uint32_t sourceRevision = clipSource.document != nullptr
                ? clipSource.generation
                : residentPattern.ccLaneRevision.get();
            auto& signature = laneSourceSignatures[i];
            if (forceRefresh && source == nullptr) {
                signature.identity = nullptr;
                signature.revision = sourceRevision;
                continue;
            }
            if (!forceRefresh && signature.matches(source, sourceRevision)) {
                continue;
            }
            if (source == nullptr) {
                lanes.tracks[i] = {};
            } else {
                lanes.tracks[i] = *source;
            }
            signature.identity = source;
            signature.revision = sourceRevision;
            ++lane_payload_write_count_;
        }
    }

    runtimeSnapshot.activeTrack = activeTrack;
    runtimeSnapshot.enabledMask = track_bank_.currentEnabledMask();
    if (!refreshDrumTracks_(writeIndex, sources)) {
        // Do not publish a flat generation without its Track-kind payload.
        // Allocation is retried from this non-realtime path on the next pass.
        return currentIndex;
    }
    runtimeSnapshot.projectScaleRevision = track_bank_.projectScaleRevisionSignal().get();
    runtimeSnapshot.projectScaleSettings = track_bank_.projectScaleSettings();
    runtimeSnapshot.projectSwingPercent =
        core::state::project::sanitizeProjectSwingPercent(
            project_navigation_.transportSwingPercent
        );
    const ProjectTimingContext projectTiming{runtimeSnapshot.projectSwingPercent};

    for (uint8_t i = 0; i < runtimeSnapshot.tracks.size(); ++i) {
        const auto& clipSource = sources[i];
        const auto* document = clipSource.document;
        const auto& source = i == activeTrack ? activePattern : track_bank_.track(i);
        const auto& sourceClip = i == activeTrack ? activeClip : track_bank_.clip(i);
        auto signature = document != nullptr
            ? captureRuntimeStateSignature(document->pattern, document->clip)
            : captureRuntimeStateSignature(source, sourceClip,
                  runtimeSnapshot.projectScaleSettings, projectTiming);
        if (document != nullptr) {
            signature.effectiveScaleSettings =
                core::state::sequencer::resolveEffectiveScaleSettings(
                    runtimeSnapshot.projectScaleSettings,
                    document->pattern.scalePolicy,
                    document->pattern.scaleOverride);
            signature.effectiveSwingPercent =
                core::state::sequencer::SequencerPatternState::clampEffectiveSwingPercent(
                    static_cast<int16_t>(runtimeSnapshot.projectSwingPercent) +
                    document->pattern.swingOffsetPercent);
        }
        if (!forceRefresh && clipSourceSignatures[i].matches(clipSource) &&
            writeSignatures[i].matches(signature)) {
            continue;
        }

        // Both residences publish the same runtime representation. Inspect only
        // the signature on a cache hit; copy musical arrays only on a miss.
        if (document != nullptr) {
            runtimeSnapshot.tracks[i] = document->pattern;
            runtimeSnapshot.clips[i] = document->clip;
        } else {
            core::state::sequencer::captureSnapshot(source, runtimeSnapshot.tracks[i]);
            core::state::sequencer::captureSnapshot(sourceClip, runtimeSnapshot.clips[i]);
        }
        runtimeSnapshot.tracks[i].effectiveScaleSettings = signature.effectiveScaleSettings;
        runtimeSnapshot.tracks[i].effectiveSwingPercent = signature.effectiveSwingPercent;
        clipSourceSignatures[i] = {
            clipSource.address.slot,
            clipSource.generation,
        };
        writeSignatures[i] = signature;
    }

    force_refresh_[writeIndex] = false;
    last_refresh_succeeded_ = true;
    return writeIndex;
}

void SequencerRuntimeSnapshotBank::commit(uint8_t snapshotIndex) {
    oc::realtime::InterruptGuard lock;
    active_index_ = static_cast<uint8_t>(snapshotIndex & 0x1U);
}

const SequencerRuntimeSnapshotBank::Snapshot& SequencerRuntimeSnapshotBank::snapshot(
    uint8_t snapshotIndex
) const {
    return snapshots_[snapshotIndex & 0x1U];
}

const SequencerCcLaneRuntimeProjectSnapshot*
SequencerRuntimeSnapshotBank::laneSnapshot(uint8_t snapshotIndex) const {
    return lane_snapshots_[snapshotIndex & 0x1U].get();
}

FLASHMEM bool SequencerRuntimeSnapshotBank::refreshDrumTracks_(
    uint8_t writeIndex,
    const core::state::sequencer::SequencerClipRuntimeSources& sources
) {
    const uint8_t slotIndex = static_cast<uint8_t>(writeIndex & 0x1U);
    uint16_t presentMask = 0U;
    for (uint8_t track = 0U; track < sources.size(); ++track) {
        const uint16_t bit = static_cast<uint16_t>(1U << track);
        if ((track_bank_.currentEnabledMask() & bit) == 0U) continue;
        const bool drum = sources[track].document != nullptr
            ? sources[track].document->trackKind ==
                core::state::sequencer::SequencerTrackKind::DRUM
            : track_bank_.trackKind(track) ==
                core::state::sequencer::SequencerTrackKind::DRUM;
        if (drum) presentMask = static_cast<uint16_t>(presentMask | bit);
    }
    auto& slot = drum_snapshots_[slotIndex];
    if (presentMask == 0U && !slot) return true;

    if (!slot) {
        slot = core::app::makeExtmemUnique<
            SequencerDrumRuntimeProjectSnapshot>();
        if (!slot) return false;
    }

    slot->presentMask = presentMask;
    for (uint8_t track = 0U; track < slot->tracks.size(); ++track) {
        const uint16_t trackBit = static_cast<uint16_t>(1U << track);
        if ((presentMask & trackBit) == 0U) continue;

        const auto& clipSource = sources[track];
        const uint8_t sourceSlot = clipSource.address.slot;
        const uint32_t sourceGeneration = clipSource.generation;
        const uint32_t contentRevision = clipSource.document != nullptr
            ? sourceGeneration
            : track_bank_.drumTrackRevision(track);
        if (!force_refresh_[slotIndex] &&
            slot->sourceSlots[track] == sourceSlot &&
            slot->sourceGenerations[track] == sourceGeneration &&
            slot->sourceRevisions[track] == contentRevision) {
            continue;
        }
        if (clipSource.document != nullptr) {
            if (!clipSource.document->drum) return false;
            core::state::sequencer::captureDrumRuntimeSnapshot(
                *clipSource.document->drum,
                slot->tracks[track]
            );
        } else {
            core::state::sequencer::captureDrumRuntimeSnapshot(
                track_bank_.drumTrack(track),
                slot->tracks[track]
            );
        }
        // Runtime lifecycle generations do not collide across Project loads,
        // unlike persisted authored counters that commonly restart at one.
        slot->tracks[track].revision = contentRevision;
        slot->sourceRevisions[track] = contentRevision;
        slot->sourceSlots[track] = sourceSlot;
        slot->sourceGenerations[track] = sourceGeneration;
    }
    return true;
}

const SequencerDrumRuntimeProjectSnapshot*
SequencerRuntimeSnapshotBank::drumSnapshot(
    uint8_t snapshotIndex
) const {
    return drum_snapshots_[snapshotIndex & 0x1U].get();
}

const SequencerRuntimeSnapshotBank::Snapshot& SequencerRuntimeSnapshotBank::activeSnapshot() const {
    return snapshot(active_index_);
}

}  // namespace core::sequencer
