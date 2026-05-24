#include "handler/sequencer/PatternPitchSettingsDomainServices.hpp"

#include <algorithm>

#include <config/PlatformCompat.hpp>

#include "state/sequencer/SequencerScaleCatalog.hpp"

namespace core::handler {

namespace {

using oc::note::sequencer::StepSequencerScaleSettings;
namespace catalog = core::state::sequencer::scale_catalog;

StepSequencerScaleSettings editableScaleSettings(
    const core::state::sequencer::SequencerState& sequencer,
    const core::state::sequencer::SequencerTrackBankState& trackBank
) {
    auto settings = core::state::sequencer::isPatternScaleOverride(sequencer.scalePolicy)
        ? sequencer.scaleOverride
        : trackBank.projectScaleSettings();
    settings.clamp();
    return settings;
}

}  // namespace

FLASHMEM PatternPitchSettingsDomainServices::PatternPitchSettingsDomainServices(StateRefs state)
    : sequencer_(&state.sequencer)
    , track_bank_(&state.trackBank) {}

FLASHMEM int PatternPitchSettingsDomainServices::currentChoiceIndex(uint8_t row) const {
    const auto settings = editableScaleSettings(*sequencer_, *track_bank_);

    switch (row) {
        case 0:
            return core::state::sequencer::isPatternScaleOverride(sequencer_->scalePolicy) ? 1 : 0;
        case 1:
            return std::clamp<int>(settings.root, 0, catalog::ROOT_COUNT - 1);
        case 2:
            return catalog::scaleTypeIndex(settings.type);
        case 3:
            return catalog::pitchEditModeIndex(sequencer_->pitchEditMode);
        default:
            return 0;
    }
}

FLASHMEM int PatternPitchSettingsDomainServices::choiceCount(uint8_t row) const {
    switch (row) {
        case 0:
            return catalog::PATTERN_SCALE_POLICY_COUNT;
        case 1:
            return core::state::sequencer::isPatternScaleOverride(sequencer_->scalePolicy)
                ? catalog::ROOT_COUNT
                : 0;
        case 2:
            return core::state::sequencer::isPatternScaleOverride(sequencer_->scalePolicy)
                ? catalog::SCALE_TYPE_COUNT
                : 0;
        case 3:
            return catalog::PITCH_EDIT_MODE_COUNT;
        default:
            return 0;
    }
}

FLASHMEM void PatternPitchSettingsDomainServices::applyChoice(uint8_t row, int choiceIndex) const {
    switch (row) {
        case 0:
            if (choiceIndex <= 0) {
                sequencer_->setPatternScalePolicy(
                    core::state::sequencer::SequencerPatternScalePolicy::INHERIT_PROJECT
                );
                return;
            }

            if (!core::state::sequencer::isPatternScaleOverride(sequencer_->scalePolicy)) {
                sequencer_->setPatternScaleOverride(track_bank_->projectScaleSettings());
            }
            sequencer_->setPatternScalePolicy(
                core::state::sequencer::SequencerPatternScalePolicy::OVERRIDE
            );
            return;

        case 1: {
            auto settings = sequencer_->scaleOverride;
            settings.root = static_cast<uint8_t>(
                std::clamp(choiceIndex, 0, catalog::ROOT_COUNT - 1)
            );
            sequencer_->setPatternScaleOverride(settings);
            return;
        }

        case 2: {
            auto settings = sequencer_->scaleOverride;
            settings.type = catalog::SCALE_TYPE_VALUES[
                std::clamp(choiceIndex, 0, catalog::SCALE_TYPE_COUNT - 1)
            ];
            sequencer_->setPatternScaleOverride(settings);
            return;
        }

        case 3:
            sequencer_->setPitchEditMode(
                choiceIndex <= 0
                    ? core::state::sequencer::SequencerPitchEditMode::CHROMATIC
                    : core::state::sequencer::SequencerPitchEditMode::SCALE_DEGREES
            );
            return;

        default:
            return;
    }
}

}  // namespace core::handler
