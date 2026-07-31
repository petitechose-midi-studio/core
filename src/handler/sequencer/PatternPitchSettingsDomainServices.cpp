#include "handler/sequencer/PatternPitchSettingsDomainServices.hpp"

#include <algorithm>

#include <config/PlatformCompat.hpp>

#include "state/sequencer/SequencerChordContextProjection.hpp"
#include "state/sequencer/SequencerScaleCatalog.hpp"

namespace core::handler {

namespace {

using oc::note::sequencer::StepSequencerScaleSettings;
namespace catalog = core::state::sequencer::scale_catalog;

FLASHMEM StepSequencerScaleSettings editableScaleSettings(
    const core::state::sequencer::SequencerState& sequencer,
    const core::state::sequencer::SequencerTrackBankState& trackBank
) {
    const auto& pattern = sequencer.pattern;
    auto settings = core::state::sequencer::isPatternScaleOverride(pattern.scalePolicy)
        ? pattern.scaleOverride
        : trackBank.projectScaleSettings();
    settings.clamp();
    return settings;
}

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

FLASHMEM core::state::sequencer::SequencerChordContextProjectionStats
projectContextChange(
    core::state::sequencer::SequencerState& sequencer,
    StepSequencerScaleSettings source,
    StepSequencerScaleSettings target
) {
    if (sameScaleSettings(source, target)) return {};
    return core::state::sequencer::projectPatternChordContext(
        sequencer,
        source,
        target,
        sequencer.pattern.pitchEditMode,
        sequencer.pattern.pitchEditMode
    );
}

FLASHMEM void invalidatePublishedPitchContext(
    core::state::sequencer::SequencerState& sequencer
) {
    sequencer.invalidateVariationTelemetry();
}

FLASHMEM bool synchronizeActiveGraphDraftPitchContext(
    core::state::sequencer::SequencerState& sequencer
) {
    auto* draft = sequencer.stepContentDraft.pattern();
    if (draft == nullptr) return false;

    bool changed =
        draft->setPatternScalePolicy(sequencer.pattern.scalePolicy);
    changed =
        draft->setPatternScaleOverride(sequencer.pattern.scaleOverride) ||
        changed;
    changed =
        draft->setPitchEditMode(sequencer.pattern.pitchEditMode) ||
        changed;
    if (changed) sequencer.stepContentDraft.touch();
    return changed;
}

}  // namespace

FLASHMEM PatternPitchSettingsDomainServices::PatternPitchSettingsDomainServices(StateRefs state)
    : sequencer_(&state.sequencer)
    , track_bank_(&state.trackBank) {}

FLASHMEM int PatternPitchSettingsDomainServices::currentChoiceIndex(uint8_t row) const {
    const auto settings = editableScaleSettings(*sequencer_, *track_bank_);
    const auto& pattern = sequencer_->pattern;

    switch (row) {
        case 0:
            return core::state::sequencer::isPatternScaleOverride(pattern.scalePolicy) ? 1 : 0;
        case 1:
            return std::clamp<int>(settings.root, 0, catalog::ROOT_COUNT - 1);
        case 2:
            return catalog::scaleTypeIndex(settings.type);
        case 3:
            return catalog::pitchEditModeIndex(pattern.pitchEditMode);
        default:
            return 0;
    }
}

FLASHMEM int PatternPitchSettingsDomainServices::choiceCount(uint8_t row) const {
    const auto& pattern = sequencer_->pattern;
    switch (row) {
        case 0:
            return catalog::PATTERN_SCALE_POLICY_COUNT;
        case 1:
            return core::state::sequencer::isPatternScaleOverride(pattern.scalePolicy)
                ? catalog::ROOT_COUNT
                : 0;
        case 2:
            return core::state::sequencer::isPatternScaleOverride(pattern.scalePolicy)
                ? catalog::SCALE_TYPE_COUNT
                : 0;
        case 3:
            return catalog::PITCH_EDIT_MODE_COUNT;
        default:
            return 0;
    }
}

FLASHMEM core::state::sequencer::SequencerChordContextProjectionStats
PatternPitchSettingsDomainServices::applyChoice(
    uint8_t row,
    int choiceIndex
) const {
    using ProjectionStats =
        core::state::sequencer::SequencerChordContextProjectionStats;
    auto& pattern = sequencer_->pattern;
    const auto sourceSettings =
        editableScaleSettings(*sequencer_, *track_bank_);

    switch (row) {
        case 0: {
            if (choiceIndex <= 0) {
                const auto targetSettings =
                    track_bank_->projectScaleSettings();
                const ProjectionStats projection = projectContextChange(
                    *sequencer_,
                    sourceSettings,
                    targetSettings
                );
                const bool changed = pattern.setPatternScalePolicy(
                    core::state::sequencer::SequencerPatternScalePolicy::INHERIT_PROJECT
                );
                (void)synchronizeActiveGraphDraftPitchContext(*sequencer_);
                if (changed || projection.hasChanges()) {
                    invalidatePublishedPitchContext(*sequencer_);
                }
                return projection;
            }

            bool changed = false;
            if (!core::state::sequencer::isPatternScaleOverride(pattern.scalePolicy)) {
                changed =
                    pattern.setPatternScaleOverride(
                        track_bank_->projectScaleSettings()
                    ) ||
                    changed;
            }
            changed = pattern.setPatternScalePolicy(
                core::state::sequencer::SequencerPatternScalePolicy::OVERRIDE
            ) || changed;
            (void)synchronizeActiveGraphDraftPitchContext(*sequencer_);
            if (changed) invalidatePublishedPitchContext(*sequencer_);
            return {};
        }

        case 1: {
            if (!core::state::sequencer::isPatternScaleOverride(
                    pattern.scalePolicy
                )) {
                return {};
            }
            auto settings = pattern.scaleOverride;
            settings.root = static_cast<uint8_t>(
                std::clamp(choiceIndex, 0, catalog::ROOT_COUNT - 1)
            );
            const ProjectionStats projection = projectContextChange(
                *sequencer_,
                sourceSettings,
                settings
            );
            const bool changed =
                pattern.setPatternScaleOverride(settings);
            (void)synchronizeActiveGraphDraftPitchContext(*sequencer_);
            if (changed || projection.hasChanges()) {
                invalidatePublishedPitchContext(*sequencer_);
            }
            return projection;
        }

        case 2: {
            if (!core::state::sequencer::isPatternScaleOverride(
                    pattern.scalePolicy
                )) {
                return {};
            }
            auto settings = pattern.scaleOverride;
            settings.type = catalog::SCALE_TYPE_VALUES[
                std::clamp(choiceIndex, 0, catalog::SCALE_TYPE_COUNT - 1)
            ];
            const ProjectionStats projection = projectContextChange(
                *sequencer_,
                sourceSettings,
                settings
            );
            const bool changed =
                pattern.setPatternScaleOverride(settings);
            (void)synchronizeActiveGraphDraftPitchContext(*sequencer_);
            if (changed || projection.hasChanges()) {
                invalidatePublishedPitchContext(*sequencer_);
            }
            return projection;
        }

        case 3: {
            const auto targetMode = catalog::PITCH_EDIT_MODE_VALUES[
                std::clamp(choiceIndex, 0, catalog::PITCH_EDIT_MODE_COUNT - 1)
            ];
            const auto sourceMode = pattern.pitchEditMode;
            if (sourceMode == targetMode) return {};
            const ProjectionStats projection =
                core::state::sequencer::projectPatternChordContext(
                    *sequencer_,
                    sourceSettings,
                    sourceSettings,
                    sourceMode,
                    targetMode
                );
            const bool changed = pattern.setPitchEditMode(targetMode);
            (void)synchronizeActiveGraphDraftPitchContext(*sequencer_);
            if (changed || projection.hasChanges()) {
                invalidatePublishedPitchContext(*sequencer_);
            }
            return projection;
        }

        default:
            return {};
    }
}

}  // namespace core::handler
