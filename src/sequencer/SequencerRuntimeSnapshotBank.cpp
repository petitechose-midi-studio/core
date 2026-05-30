#include "sequencer/SequencerRuntimeSnapshotBank.hpp"

#include <oc/realtime/InterruptGuard.hpp>

#include "state/sequencer/SequencerSnapshotOps.hpp"

namespace core::sequencer {

SequencerRuntimeSnapshotBank::SequencerRuntimeSnapshotBank(
    core::state::sequencer::SequencerState& sequencer,
    core::state::sequencer::SequencerTrackBankState& trackBank
)
    : sequencer_(sequencer)
    , track_bank_(trackBank) {}

uint8_t SequencerRuntimeSnapshotBank::refresh() {
    const uint8_t currentIndex = active_index_;
    const uint8_t writeIndex = static_cast<uint8_t>(currentIndex ^ 0x1U);
    auto& runtimeSnapshot = snapshots_[writeIndex];
    runtimeSnapshot = snapshots_[currentIndex];

    const uint8_t activeTrack =
        core::state::sequencer::SequencerTrackBankState::clampTrackIndex(
            track_bank_.activeTrackIndex()
        );

    runtimeSnapshot.activeTrack = activeTrack;
    runtimeSnapshot.enabledMask = track_bank_.currentEnabledMask();
    runtimeSnapshot.projectScaleRevision = track_bank_.projectScaleRevisionSignal().get();
    runtimeSnapshot.projectScaleSettings = track_bank_.projectScaleSettings();

    const bool projectScaleChanged =
        project_scale_revision_ != runtimeSnapshot.projectScaleRevision;
    if (projectScaleChanged) {
        project_scale_revision_ = runtimeSnapshot.projectScaleRevision;
        track_signatures_.fill({});
    }

    for (uint8_t i = 0; i < runtimeSnapshot.tracks.size(); ++i) {
        const auto& source = (i == activeTrack) ? sequencer_.pattern : track_bank_.track(i);
        const auto signature =
            captureRuntimeStateSignature(source, runtimeSnapshot.projectScaleSettings);
        if (track_signatures_[i].matches(signature)) {
            continue;
        }

        core::state::sequencer::captureSnapshot(source, runtimeSnapshot.tracks[i]);
        runtimeSnapshot.tracks[i].effectiveScaleSettings =
            core::state::sequencer::resolveEffectiveScaleSettings(
                runtimeSnapshot.projectScaleSettings,
                runtimeSnapshot.tracks[i].scalePolicy,
                runtimeSnapshot.tracks[i].scaleOverride
            );
        track_signatures_[i] = signature;
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
