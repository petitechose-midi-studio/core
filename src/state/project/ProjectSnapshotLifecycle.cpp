#include "state/project/ProjectSnapshot.hpp"

#include <config/PlatformCompat.hpp>

namespace core::state::project {

FLASHMEM ProjectSnapshot::ProjectSnapshot()
    : projectControl(
          core::app::makeExtmemUnique<
              core::state::modulation::ProjectControlDomainState
          >()
      ) {
    for (uint8_t track = 0U;
         track < core::state::sequencer::SequencerClipGridState::TRACK_COUNT;
         ++track) {
        if ((sequencer.flat.enabledMask & static_cast<uint16_t>(1U << track)) != 0U) {
            clips.residentSlots[track] = 0U;
        }
    }
    for (uint8_t i = 0; i < macroTracks.size(); ++i) {
        macroTracks[i].initDefaults(i);
    }
}
FLASHMEM ProjectSnapshot::~ProjectSnapshot() = default;
FLASHMEM ProjectSnapshot::ProjectSnapshot(ProjectSnapshot&&) noexcept = default;
FLASHMEM ProjectSnapshot& ProjectSnapshot::operator=(ProjectSnapshot&&) noexcept = default;

FLASHMEM ProjectSnapshotPtr makeProjectSnapshot() {
    auto snapshot = core::app::makeExtmemUnique<ProjectSnapshot>();
    if (!snapshot || !snapshot->projectControl) return {};
    return snapshot;
}

}  // namespace core::state::project
