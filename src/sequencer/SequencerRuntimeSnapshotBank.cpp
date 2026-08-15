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
FLASHMEM uint8_t SequencerRuntimeSnapshotBank::refresh() {
    last_refresh_succeeded_ = false;
    const uint8_t currentIndex = active_index_;
    const uint8_t writeIndex = static_cast<uint8_t>(currentIndex ^ 0x1U);
    auto& runtimeSnapshot = snapshots_[writeIndex];
    auto& writeSignatures = track_signatures_[writeIndex];
    auto& laneSourceSignatures = lane_source_signatures_[writeIndex];

    const uint8_t activeTrack =
        core::state::sequencer::SequencerTrackBankState::clampTrackIndex(
            track_bank_.activeTrackIndex()
        );
    const auto& activePattern =
        core::state::sequencer::authoringPattern(sequencer_);

    uint16_t lanePresentMask = 0;
    for (uint8_t i = 0;
         i < core::state::sequencer::SequencerTrackBankState::TRACK_COUNT;
         ++i) {
        const auto& source =
            (i == activeTrack) ? activePattern : track_bank_.track(i);
        if (core::state::sequencer::sequencerCcLaneView(source) != nullptr) {
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
            const auto& sourcePattern =
                (i == activeTrack) ? activePattern : track_bank_.track(i);
            const auto* source =
                core::state::sequencer::sequencerCcLaneView(sourcePattern);
            const uint32_t sourceRevision = sourcePattern.ccLaneRevision.get();
            auto& signature = laneSourceSignatures[i];
            if (signature.matches(source, sourceRevision)) {
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
    if (!refreshDrumTracks_(writeIndex)) {
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
        const auto& source = (i == activeTrack)
            ? activePattern
            : track_bank_.track(i);
        const auto signature =
            captureRuntimeStateSignature(
                source,
                runtimeSnapshot.projectScaleSettings,
                projectTiming
            );
        if (writeSignatures[i].matches(signature)) {
            continue;
        }

        core::state::sequencer::captureSnapshot(source, runtimeSnapshot.tracks[i]);
        runtimeSnapshot.tracks[i].effectiveScaleSettings =
            core::state::sequencer::resolveEffectiveScaleSettings(
                runtimeSnapshot.projectScaleSettings,
                runtimeSnapshot.tracks[i].scalePolicy,
                runtimeSnapshot.tracks[i].scaleOverride
            );
        runtimeSnapshot.tracks[i].effectiveSwingPercent =
            source.effectiveSwingPercent(runtimeSnapshot.projectSwingPercent);
        writeSignatures[i] = signature;
    }

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
    uint8_t writeIndex
) {
    const uint8_t slotIndex = static_cast<uint8_t>(writeIndex & 0x1U);
    const uint16_t presentMask = static_cast<uint16_t>(
        track_bank_.drumTrackMask() & track_bank_.currentEnabledMask());
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

        const auto& source = track_bank_.drumTrack(track);
        const uint32_t sourceRevision =
            track_bank_.drumTrackRevision(track);
        if (slot->sourceRevisions[track] == sourceRevision) {
            continue;
        }
        core::state::sequencer::captureDrumRuntimeSnapshot(
            source,
            slot->tracks[track]
        );
        // Runtime lifecycle generations do not collide across Project loads,
        // unlike persisted authored counters that commonly restart at one.
        slot->tracks[track].revision = sourceRevision;
        slot->sourceRevisions[track] = sourceRevision;
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
