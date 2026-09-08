#include "state/sequencer/SequencerProjectScaleOps.hpp"

#include <algorithm>
#include "state/sequencer/SequencerHistory.hpp"
#include "state/sequencer/SequencerTrackBankOps.hpp"

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

namespace {

using Graph = oc::note::sequencer::StepSequencerGraph;
using Chord = oc::note::sequencer::StepSequencerChordSpec;

struct ChordWriter {
    SequencerProjectScaleChordChange* chords;
    uint32_t count = 0U;
};
FLASHMEM void writeProjectedChord(void* context, uint16_t node,
                                 const Chord& before, const Chord& after) {
    auto& writer = *static_cast<ChordWriter*>(context);
    writer.chords[writer.count++] = {node, before, after};
}

}  // namespace

FLASHMEM SequencerHistoryProjectScaleChangePtr prepareHistoryProjectScaleChange(
    const SequencerTrackBankState& bank, const SequencerState& active,
    const SequencerClipGridState& clips, ScaleSettings target,
    SequencerChordContextProjectionStats& projection
) {
    projection = {};
    target.clamp();
    if (sameScaleSettings(bank.projectScaleSettings(), target)) return {};
    auto change = core::app::makeExtmemUniqueCold<SequencerHistoryProjectScaleChange>();
    if (!change) return {};
    change->before = bank.projectScaleSettings();
    change->after = target;
    change->projectScaleRevision = bank.projectScaleRevisionSignal().get();

    // Both passes are read-only. Count first, then allocate exactly the changed
    // formulas. There is no full Graph copy, maximum-size chord buffer or live rollback.
    for (uint8_t pass = 0U; pass < 2U; ++pass) {
        ChordWriter writer{change->chords.get()};
        uint16_t patternIndex = 0U;
        auto visit = [&](SequencerClipAddress address, const auto& notes, uint8_t length,
                         const Graph* graph, SequencerPitchEditMode mode,
                         SequencerPatternScalePolicy policy, uint32_t graphRevision,
                         uint32_t scaleRevision) {
            if (isPatternScaleOverride(policy)) return;
            const auto stats = visitProjectedPatternChords(
                notes, length, graph, mode, change->before, target,
                pass == 0U ? nullptr : writeProjectedChord, &writer);
            if (pass == 0U) {
                change->patterns[patternIndex] = {
                    address, static_cast<uint16_t>(stats.changed), change->chordCount, graphRevision, scaleRevision};
                change->chordCount += stats.changed;
                projection.merge(stats);
            }
            ++patternIndex;
        };
        for (uint8_t track = 0U; track < SequencerTrackBankState::TRACK_COUNT; ++track) {
            const auto& pattern = canonicalTrackPattern(bank, active, track);
            visit({track, clips.residentSlot(track)}, pattern.note, pattern.length.get(),
                  pattern.graph.get(), pattern.pitchEditMode, pattern.scalePolicy,
                  pattern.graphRevision.get(), pattern.patternScaleRevision.get());
            for (uint8_t slot = 0U; slot < SequencerClipGridState::SLOT_COUNT; ++slot) {
                const SequencerClipAddress address{track, slot};
                const auto* doc = clips.inactiveDocument(address);
                if (doc == nullptr) continue;
                visit(address, doc->pattern.note, doc->pattern.length, doc->graph.get(),
                      doc->pattern.pitchEditMode, doc->pattern.scalePolicy,
                      doc->pattern.graphRevision, doc->pattern.patternScaleRevision);
            }
        }
        change->patternCount = patternIndex;
        if (pass == 0U) {
            if (change->chordCount == 0U) break;
            change->chords = core::app::makeExtmemUniqueArrayForOverwrite<
                SequencerProjectScaleChordChange>(change->chordCount);
            if (!change->chords) return {};
        }
    }
    return change;
}


}  // namespace core::state::sequencer
