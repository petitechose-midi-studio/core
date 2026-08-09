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

uint8_t SequencerRuntimeSnapshotBank::refresh() {
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
#if defined(MS_DRUM_TRACK_UX_PROTOTYPE)
    refreshDrumPrototype_(writeIndex, activeTrack);
#endif
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

#if defined(MS_DRUM_TRACK_UX_PROTOTYPE)
FLASHMEM void SequencerRuntimeSnapshotBank::refreshDrumPrototype_(
    uint8_t writeIndex,
    uint8_t activeTrack
) {
    const auto& prototype = sequencer_.drumTrackUxPrototype;
    auto& slot = drum_prototype_slots_[writeIndex & 0x1U];
    slot.active = prototype.gridVisible() &&
                  prototype.targetTrack == activeTrack;
    slot.track = activeTrack;
    if (!slot.active) return;

    const uint32_t revision =
        core::state::sequencer::drumRuntimeRevision(*prototype.drumTrack);
    if (slot.pattern.revision == revision) return;
    core::state::sequencer::captureDrumRuntimeSnapshot(
        *prototype.drumTrack,
        slot.pattern
    );
}

const core::state::sequencer::DrumPatternRuntimeSnapshot*
SequencerRuntimeSnapshotBank::drumPrototypeSnapshot(
    uint8_t snapshotIndex
) const {
    const auto& slot = drum_prototype_slots_[snapshotIndex & 0x1U];
    return slot.active ? &slot.pattern : nullptr;
}

uint8_t SequencerRuntimeSnapshotBank::drumPrototypeTrack(
    uint8_t snapshotIndex
) const {
    const auto& slot = drum_prototype_slots_[snapshotIndex & 0x1U];
    return slot.active ? slot.track
                       : core::state::sequencer::SequencerTrackBankState::TRACK_COUNT;
}
#endif

const SequencerRuntimeSnapshotBank::Snapshot& SequencerRuntimeSnapshotBank::activeSnapshot() const {
    return snapshot(active_index_);
}

}  // namespace core::sequencer
