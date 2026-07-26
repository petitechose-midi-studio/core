#include "ui/sequencer/SequencerTrackPasteProjection.hpp"

#include <array>

#include <config/PlatformCompat.hpp>
#include <config/Timing.hpp>

#include "state/sequencer/SequencerTrackTransferAction.hpp"
#include "ui/sequencer/SequencerViewModelBuilder.hpp"

namespace core::ui::sequencer {

FLASHMEM SequencerTrackPasteProjection projectSequencerTrackPaste(
    const SequencerViewModelSource& source
) {
    SequencerTrackPasteProjection projection{};
    projection.targetTrack = source.trackNavigation.previewAddSlot.get()
        ? core::state::sequencer::SequencerTrackBankState::clampTrackIndex(
              source.trackNavigation.previewTrackIndex.get()
          )
        : core::state::sequencer::SequencerTrackBankState::clampTrackIndex(
              source.sharedTrackActive.get()
          );

    projection.plan = core::state::buildSequencerTrackClipboardTransferPlan(
        source.structureClipboard,
        source.tracks,
        source.projectTracks,
        projection.targetTrack,
        source.trackActivations.pendingTrackMask()
    );
    projection.copyAvailable = !source.trackNavigation.previewAddSlot.get();
    const auto& paste = source.sequencer.structureUi.trackPaste;
    if (paste.feedback.active && paste.plan.hasEntries()) {
        projection.plan = paste.plan;
        projection.targetTrack = paste.plan.entry.targetTrack;
    }
    projection.guard = paste.guard;
    projection.feedback = paste.feedback;
    projection.operationGeneration = paste.operationGeneration;
    projection.activationGeneration = paste.activationGeneration;
    projection.detailVisible = paste.detailVisible;
    projection.action =
        core::state::sequencer::buildSequencerTrackTransferActionSpec(
            projection.plan,
            projection.targetTrack,
            projection.copyAvailable,
            static_cast<uint16_t>(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS)
        );
    return projection;
}

FLASHMEM SequencerTrackPastePreflightViewModel projectSequencerTrackPastePreflight(
    const SequencerViewModelSource& source
) {
    const auto projection = projectSequencerTrackPaste(source);
    const bool trackContext = source.navigationFocus.get() ==
        core::state::StructureNavigationFocus::TRACK;
    if (!trackContext && !projection.feedback.active) return {};
    std::array<
        core::state::sequencer::SequencerTrackActivationTelemetry,
        core::state::sequencer::SequencerTrackBankState::TRACK_COUNT> telemetry{};
    for (uint8_t track = 0; track < telemetry.size(); ++track) {
        telemetry[track] = source.trackActivations.telemetry(track);
    }
    return buildSequencerTrackPastePreflightViewModel(
        projection,
        projection.feedback.status ==
                core::state::contextual::OperationFeedbackStatus::PRESSED ||
            projection.feedback.status ==
                core::state::contextual::OperationFeedbackStatus::ARMED,
        telemetry
    );
}

}  // namespace core::ui::sequencer
