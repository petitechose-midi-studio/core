#include "state/sequencer/SequencerProjectScaleOps.hpp"

#include <algorithm>

#include <config/PlatformCompat.hpp>

#include "state/sequencer/SequencerScaleCatalog.hpp"

namespace core::state::sequencer {

namespace {

using ScaleSettings = oc::note::sequencer::StepSequencerScaleSettings;
namespace catalog = scale_catalog;

FLASHMEM bool sameScaleSettings(ScaleSettings lhs, ScaleSettings rhs) {
    lhs.clamp();
    rhs.clamp();
    return lhs.root == rhs.root &&
           lhs.type == rhs.type &&
           lhs.mode == rhs.mode;
}

}  // namespace

FLASHMEM SequencerProjectScaleChoice resolveProjectScaleChoice(
    ScaleSettings current,
    uint8_t row,
    int choiceIndex
) {
    current.clamp();
    SequencerProjectScaleChoice choice{.target = current};

    switch (row) {
        case 0:
            choice.target.root = static_cast<uint8_t>(
                std::clamp(choiceIndex, 0, catalog::ROOT_COUNT - 1)
            );
            break;
        case 1:
            choice.target.type = catalog::SCALE_TYPE_VALUES[
                std::clamp(choiceIndex, 0, catalog::SCALE_TYPE_COUNT - 1)
            ];
            break;
        case 2:
            choice.target.mode = catalog::CONSTRAINT_MODE_VALUES[
                std::clamp(choiceIndex, 0, catalog::CONSTRAINT_MODE_COUNT - 1)
            ];
            break;
        default:
            return choice;
    }

    choice.target.clamp();
    choice.valid = true;
    choice.changes = !sameScaleSettings(current, choice.target);
    return choice;
}

FLASHMEM SequencerProjectScaleMutationResult applyProjectScaleTransition(
    SequencerTrackBankState& bank,
    SequencerState& active,
    ScaleSettings target
) {
    ScaleSettings source = bank.projectScaleSettings();
    source.clamp();
    target.clamp();
    if (sameScaleSettings(source, target)) return {};

    SequencerProjectScaleMutationResult result{};
    result.projection = projectInheritedChordContexts(
        bank,
        active,
        source,
        target
    );

    if (!bank.setProjectScaleSettings(target)) return result;

    if (!isPatternScaleOverride(active.pattern.scalePolicy)) {
        active.pattern.bumpPatternScaleRevision();
        active.invalidateVariationTelemetry();
    }

    // The selected bank slot is never canonical. Legacy synchronization may
    // have materialized cold owners there; discard them only after a committed
    // state transition and never attribute a musical revision to the scratch.
    auto& activeScratch = bank.track(bank.activeTrackIndex());
    activeScratch.graph.reset();
    activeScratch.ccLanes.reset();

    result.changed = true;
    return result;
}

}  // namespace core::state::sequencer
