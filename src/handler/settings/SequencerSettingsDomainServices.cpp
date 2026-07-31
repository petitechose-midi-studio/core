#include "handler/settings/SequencerSettingsDomainServices.hpp"

#include <algorithm>

#include <config/PlatformCompat.hpp>

#include "state/sequencer/SequencerChordContextProjection.hpp"
#include "state/sequencer/SequencerScaleCatalog.hpp"

namespace core::handler {

namespace {

using oc::note::sequencer::StepSequencerScaleSettings;
namespace catalog = core::state::sequencer::scale_catalog;

FLASHMEM bool sameScaleSettings(
    StepSequencerScaleSettings lhs,
    StepSequencerScaleSettings rhs
) {
    lhs.clamp();
    rhs.clamp();
    return lhs.root == rhs.root &&
           lhs.type == rhs.type &&
           lhs.mode == rhs.mode;
}

}  // namespace

FLASHMEM SequencerSettingsDomainServices::SequencerSettingsDomainServices(StateRefs state)
    : active_sequencer_(&state.activeSequencer)
    , track_bank_(&state.trackBank) {}

FLASHMEM int SequencerSettingsDomainServices::currentChoiceIndex(uint8_t row) const {
    const StepSequencerScaleSettings projectSettings = track_bank_->projectScaleSettings();
    switch (row) {
        case 0:
            return std::clamp<int>(projectSettings.root, 0, catalog::ROOT_COUNT - 1);
        case 1:
            return catalog::scaleTypeIndex(projectSettings.type);
        case 2:
            return catalog::constraintModeIndex(projectSettings.mode);
        default:
            return 0;
    }
}

FLASHMEM int SequencerSettingsDomainServices::choiceCount(uint8_t row) const {
    switch (row) {
        case 0: return catalog::ROOT_COUNT;
        case 1: return catalog::SCALE_TYPE_COUNT;
        case 2: return catalog::CONSTRAINT_MODE_COUNT;
        default: return 0;
    }
}

FLASHMEM core::state::sequencer::SequencerChordContextProjectionStats
SequencerSettingsDomainServices::applyChoice(
    uint8_t row,
    int choiceIndex
) const {
    const StepSequencerScaleSettings sourceSettings =
        track_bank_->projectScaleSettings();
    StepSequencerScaleSettings settings = sourceSettings;

    switch (row) {
        case 0:
            settings.root = static_cast<uint8_t>(
                std::clamp(choiceIndex, 0, catalog::ROOT_COUNT - 1)
            );
            break;
        case 1:
            settings.type = catalog::SCALE_TYPE_VALUES[
                std::clamp(choiceIndex, 0, catalog::SCALE_TYPE_COUNT - 1)
            ];
            break;
        case 2:
            settings.mode = catalog::CONSTRAINT_MODE_VALUES[
                std::clamp(choiceIndex, 0, catalog::CONSTRAINT_MODE_COUNT - 1)
            ];
            break;
        default:
            return {};
    }

    const auto projection = sameScaleSettings(sourceSettings, settings)
        ? core::state::sequencer::SequencerChordContextProjectionStats{}
        : core::state::sequencer::projectInheritedChordContexts(
              *track_bank_,
              *active_sequencer_,
              sourceSettings,
              settings
          );
    if (track_bank_->setProjectScaleSettings(settings) &&
        !core::state::sequencer::isPatternScaleOverride(active_sequencer_->pattern.scalePolicy)) {
        active_sequencer_->invalidateVariationTelemetry();
    }
    return projection;
}

}  // namespace core::handler
