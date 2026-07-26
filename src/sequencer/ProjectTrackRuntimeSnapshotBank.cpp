#include "sequencer/ProjectTrackRuntimeSnapshotBank.hpp"

#include "state/project/ProjectTrackDomainOps.hpp"

namespace core::sequencer {

void captureProjectTrackRuntimeSnapshot(
    const core::state::project::ProjectTrackState& source,
    uint16_t enabledMask,
    ProjectTrackRuntimeSnapshot& out
) {
    out.revision = source.revision.get();
    out.delayMs = source.authored.delayMs;
    out.midiChannels = source.authored.midiChannels;
    out.enabledMask = enabledMask;
    out.mutedMask = source.authored.mutedMask;
    out.soloMask = source.authored.soloMask;
    out.audibleMask = core::state::project::audibleMask(source, enabledMask);
}

bool ProjectTrackRuntimeSnapshotBank::publish(
    uint8_t slot,
    const core::state::project::ProjectTrackState& source,
    uint16_t enabledMask
) {
    if (slot >= slots_.size()) return false;
    captureProjectTrackRuntimeSnapshot(source, enabledMask, slots_[slot]);
    return true;
}

const ProjectTrackRuntimeSnapshot* ProjectTrackRuntimeSnapshotBank::snapshot(
    uint8_t slot
) const {
    return slot < slots_.size() ? &slots_[slot] : nullptr;
}

}  // namespace core::sequencer
