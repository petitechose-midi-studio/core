#include "sequencer/SequencerRuntimeSnapshotBank.hpp"

#include <oc/realtime/InterruptGuard.hpp>

#include "state/project/ProjectDomainRules.hpp"
#include "state/sequencer/SequencerSnapshotOps.hpp"

namespace core::sequencer {

SequencerRuntimeSnapshotBank::SequencerRuntimeSnapshotBank(
    core::state::sequencer::SequencerState& sequencer,
    core::state::sequencer::SequencerTrackBankState& trackBank,
    core::state::project::ProjectNavigationState& projectNavigation
)
    : sequencer_(sequencer)
    , track_bank_(trackBank)
    , project_navigation_(projectNavigation) {}

uint8_t SequencerRuntimeSnapshotBank::refresh() {
    const uint8_t currentIndex = active_index_;
    const uint8_t writeIndex = static_cast<uint8_t>(currentIndex ^ 0x1U);
    auto& runtimeSnapshot = snapshots_[writeIndex];
    auto& writeSignatures = track_signatures_[writeIndex];

    const uint8_t activeTrack =
        core::state::sequencer::SequencerTrackBankState::clampTrackIndex(
            track_bank_.activeTrackIndex()
        );

    runtimeSnapshot.activeTrack = activeTrack;
    runtimeSnapshot.enabledMask = track_bank_.currentEnabledMask();
    runtimeSnapshot.mutedMask = track_bank_.currentMutedMask();
    runtimeSnapshot.projectScaleRevision = track_bank_.projectScaleRevisionSignal().get();
    runtimeSnapshot.projectScaleSettings = track_bank_.projectScaleSettings();
    runtimeSnapshot.projectSwingPercent =
        core::state::project::sanitizeProjectSwingPercent(
            project_navigation_.transportSwingPercent
        );
    const ProjectTimingContext projectTiming{runtimeSnapshot.projectSwingPercent};

    for (uint8_t i = 0; i < runtimeSnapshot.tracks.size(); ++i) {
        const auto& source = (i == activeTrack) ? sequencer_.pattern : track_bank_.track(i);
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

const SequencerRuntimeSnapshotBank::Snapshot& SequencerRuntimeSnapshotBank::activeSnapshot() const {
    return snapshot(active_index_);
}

}  // namespace core::sequencer
