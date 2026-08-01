#include "handler/settings/SequencerSettingsDomainServices.hpp"

#include <algorithm>

#include <config/PlatformCompat.hpp>

#include "state/sequencer/SequencerScaleCatalog.hpp"

namespace core::handler {

namespace {

using oc::note::sequencer::StepSequencerScaleSettings;
namespace catalog = core::state::sequencer::scale_catalog;

}  // namespace

FLASHMEM SequencerSettingsDomainServices::SequencerSettingsDomainServices(StateRefs state)
    : track_bank_(&state.trackBank) {}

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

}  // namespace core::handler
