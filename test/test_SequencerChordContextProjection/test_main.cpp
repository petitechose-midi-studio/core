#include <array>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>

#include <oc/note/sequencer/StepSequencerChord.hpp>

#include "../../src/handler/sequencer/SequencerChordProjectionFeedback.hpp"
#include "../../src/state/sequencer/SequencerChordContextProjection.hpp"
#include "../../src/state/sequencer/SequencerGraphOps.hpp"
#include "../../src/state/sequencer/SequencerStepContentDraftOps.hpp"

namespace {

using Basis =
    oc::note::sequencer::StepSequencerChordIntervalBasis;
using Harmony = oc::note::sequencer::StepSequencerChordHarmony;
using ScaleMode =
    oc::note::sequencer::StepSequencerScaleConstraintMode;
using ScaleSettings =
    oc::note::sequencer::StepSequencerScaleSettings;
using ScaleType = oc::note::sequencer::StepSequencerScaleType;
using Spec = oc::note::sequencer::StepSequencerChordSpec;
using core::state::sequencer::SequencerPatternState;
using core::state::sequencer::SequencerPitchEditMode;

ScaleSettings chromaticScale() {
    return ScaleSettings{
        .root = 0,
        .type = ScaleType::Chromatic,
        .mode = ScaleMode::Free,
    };
}

ScaleSettings fHarmonicMinor() {
    return ScaleSettings{
        .root = 5,
        .type = ScaleType::HarmonicMinor,
        .mode = ScaleMode::ConstrainNearest,
    };
}

Spec customChord(
    Basis basis,
    uint8_t second,
    uint8_t third
) {
    auto spec = Spec::semantic(
        Harmony::Custom,
        3,
        oc::note::sequencer::StepSequencerChordVoicing::Close,
        0,
        basis
    );
    spec.setCustomInterval(1, second);
    spec.setCustomInterval(2, third);
    return spec;
}

bool sameSpec(const Spec& lhs, const Spec& rhs) {
    return oc::note::sequencer::chordSpecsEqual(lhs, rhs);
}

const Spec& chordAt(
    const SequencerPatternState& pattern,
    uint16_t nodeId
) {
    const auto* graph =
        core::state::sequencer::graphView(pattern);
    assert(graph != nullptr);
    const auto* node = graph->stepNode(nodeId);
    assert(node != nullptr);
    assert(node->has(
        oc::note::sequencer::STEP_NODE_CHORD_LOCAL
    ));
    return node->chordSpec;
}

void authorScalePolicyChord(
    SequencerPatternState& pattern,
    uint8_t step,
    Spec spec
) {
    pattern.setPitchEditMode(
        SequencerPitchEditMode::FOLLOW_SCALE
    );
    pattern.note[step] = 65;
    assert(core::state::sequencer::setNodeChordSpec(
        pattern,
        core::state::sequencer::rootStepNodeId(step),
        spec
    ));
}

void test_chromatic_formula_projects_exactly_to_scale_degrees() {
    SequencerPatternState pattern;
    authorScalePolicyChord(
        pattern,
        0,
        customChord(Basis::ChromaticSemitones, 3, 5)
    );
    const uint32_t revision = pattern.graphRevision.get();

    const auto stats =
        core::state::sequencer::projectPatternChordContext(
            pattern,
            chromaticScale(),
            fHarmonicMinor()
        );

    const auto& projected = chordAt(pattern, 0);
    assert(projected.intervalBasis() == Basis::ScaleDegrees);
    assert(projected.isCustom());
    assert(projected.customInterval(1) == 2);
    assert(projected.customInterval(2) == 3);
    assert(stats.patternsVisited == 1);
    assert(stats.localChordsVisited == 1);
    assert(stats.projected == 1);
    assert(stats.changed == 1);
    assert(stats.exact == 1);
    assert(stats.adapted == 0);
    assert(pattern.graphRevision.get() == revision + 1U);

    std::cout
        << "[PASS] chromatic formula projects exactly to degrees\n";
}

void test_eight_voice_formula_projects_without_cardinality_loss() {
    SequencerPatternState pattern;
    auto source = Spec::semantic(
        Harmony::Custom,
        8U,
        oc::note::sequencer::StepSequencerChordVoicing::Close,
        0U,
        Basis::ChromaticSemitones
    );
    constexpr std::array<uint8_t, 8> intervals{
        0U, 2U, 3U, 5U, 7U, 8U, 11U, 12U,
    };
    source.setCustomIntervals(intervals);
    authorScalePolicyChord(pattern, 0U, source);

    const auto stats =
        core::state::sequencer::projectPatternChordContext(
            pattern,
            chromaticScale(),
            fHarmonicMinor()
        );

    const auto& projected = chordAt(pattern, 0U);
    assert(projected.voices() == 8U);
    for (uint8_t voice = 0U; voice < 8U; ++voice) {
        assert(projected.customInterval(voice) == voice);
    }
    assert(stats.changed == 1U);
    assert(stats.exact == 1U);
    assert(stats.droppedVoices == 0U);

    std::cout
        << "[PASS] eight-voice formula projects without loss\n";
}

void test_scale_formula_materializes_exact_semitones() {
    SequencerPatternState pattern;
    authorScalePolicyChord(
        pattern,
        0,
        customChord(Basis::ScaleDegrees, 2, 3)
    );

    const auto stats =
        core::state::sequencer::projectPatternChordContext(
            pattern,
            fHarmonicMinor(),
            chromaticScale()
        );

    const auto& projected = chordAt(pattern, 0);
    assert(
        projected.intervalBasis() ==
        Basis::ChromaticSemitones
    );
    assert(projected.isCustom());
    assert(projected.customInterval(1) == 3);
    assert(projected.customInterval(2) == 5);
    assert(stats.changed == 1);
    assert(stats.exact == 1);
    assert(!stats.hasAdaptations());

    std::cout
        << "[PASS] degree formula materializes exact semitones\n";
}

void test_scale_to_scale_preserves_raw_degree_formula() {
    SequencerPatternState pattern;
    const auto source =
        customChord(Basis::ScaleDegrees, 2, 3);
    authorScalePolicyChord(pattern, 0, source);
    auto target = fHarmonicMinor();
    target.root = 7;
    target.type = ScaleType::NaturalMinor;
    const uint32_t revision = pattern.graphRevision.get();

    const auto stats =
        core::state::sequencer::projectPatternChordContext(
            pattern,
            fHarmonicMinor(),
            target
        );

    assert(sameSpec(chordAt(pattern, 0), source));
    assert(stats.projected == 1);
    assert(stats.exact == 1);
    assert(stats.changed == 0);
    assert(pattern.graphRevision.get() == revision);

    std::cout
        << "[PASS] scale-to-scale preserves degree formula\n";
}

void test_nested_projection_uses_resolved_child_root() {
    SequencerPatternState pattern;
    pattern.setPitchEditMode(
        SequencerPitchEditMode::FOLLOW_SCALE
    );
    pattern.note[0] = 65;
    const auto micro =
        core::state::sequencer::createMicroSequence(
            pattern,
            core::state::sequencer::rootStepNodeId(0),
            1
        );
    assert(micro.ok);

    const auto* graph =
        core::state::sequencer::graphView(pattern);
    assert(graph != nullptr);
    const auto* sequence = graph->sequence(micro.id);
    assert(sequence != nullptr);
    const uint16_t childNodeId = sequence->firstStepNode;
    assert(core::state::sequencer::setNodeNoteOffset(
        pattern,
        childNodeId,
        2
    ));
    const auto source =
        customChord(Basis::ChromaticSemitones, 3, 5);
    assert(core::state::sequencer::setNodeChordSpec(
        pattern,
        childNodeId,
        source
    ));

    const uint8_t sourceChildRoot = 67;
    const uint8_t targetChildRoot =
        oc::note::sequencer::moveByScaleDegrees(
            65,
            2,
            fHarmonicMinor()
        );
    oc::note::sequencer::StepSequencerChordProjectionWorkspace workspace{};
    const auto expected =
        oc::note::sequencer::projectChordSpec(
            source,
            chromaticScale(),
            fHarmonicMinor(),
            sourceChildRoot,
            targetChildRoot,
            false,
            true,
            workspace
        );
    assert(expected.valid);

    const auto stats =
        core::state::sequencer::projectPatternChordContext(
            pattern,
            chromaticScale(),
            fHarmonicMinor()
        );

    assert(sameSpec(
        chordAt(pattern, childNodeId),
        expected.spec
    ));
    assert(stats.localChordsVisited == 1);
    assert(stats.projected == 1);

    std::cout
        << "[PASS] nested projection uses resolved child root\n";
}

void test_project_projection_skips_pattern_overrides() {
    core::state::sequencer::SequencerState active;
    core::state::sequencer::SequencerTrackBankState bank;
    authorScalePolicyChord(
        active.pattern,
        0,
        customChord(Basis::ChromaticSemitones, 3, 5)
    );
    authorScalePolicyChord(
        bank.track(1),
        0,
        customChord(Basis::ChromaticSemitones, 3, 5)
    );
    authorScalePolicyChord(
        bank.track(2),
        0,
        customChord(Basis::ChromaticSemitones, 3, 5)
    );
    bank.track(2).setPatternScalePolicy(
        core::state::sequencer::
            SequencerPatternScalePolicy::OVERRIDE
    );
    const auto overrideBefore = chordAt(bank.track(2), 0);

    const auto stats =
        core::state::sequencer::projectInheritedChordContexts(
            bank,
            active,
            chromaticScale(),
            fHarmonicMinor()
        );

    assert(stats.patternsVisited == 2);
    assert(stats.projected == 2);
    assert(stats.changed == 2);
    assert(
        chordAt(active.pattern, 0).intervalBasis() ==
        Basis::ScaleDegrees
    );
    assert(
        chordAt(bank.track(1), 0).intervalBasis() ==
        Basis::ScaleDegrees
    );
    assert(sameSpec(chordAt(bank.track(2), 0), overrideBefore));

    std::cout
        << "[PASS] Project projection skips Pattern overrides\n";
}

void test_project_projection_keeps_active_chord_draft_coherent() {
    using core::state::sequencer::SequencerStepContentDraftKind;

    core::state::sequencer::SequencerState active;
    core::state::sequencer::SequencerTrackBankState bank;
    const auto nodeId =
        core::state::sequencer::rootStepNodeId(0);
    authorScalePolicyChord(
        active.pattern,
        0,
        customChord(Basis::ChromaticSemitones, 3, 5)
    );
    assert(core::state::sequencer::beginStepContentDraft(
        active,
        SequencerStepContentDraftKind::CHORD,
        0,
        nodeId
    ));
    assert(core::state::sequencer::setAuthoringNodeChordSpec(
        active,
        nodeId,
        customChord(Basis::ChromaticSemitones, 4, 7)
    ));

    const auto stats =
        core::state::sequencer::projectInheritedChordContexts(
            bank,
            active,
            chromaticScale(),
            fHarmonicMinor()
        );

    assert(
        chordAt(active.pattern, nodeId).intervalBasis() ==
        Basis::ScaleDegrees
    );
    assert(active.stepContentDraft.chord.localPresent);
    assert(
        active.stepContentDraft.chord.spec.intervalBasis() ==
        Basis::ScaleDegrees
    );
    assert(stats.patternsVisited == 1U);
    assert(stats.localChordsVisited == 1U);
    assert(stats.projected == 1U);
    assert(stats.changed == 1U);

    std::cout
        << "[PASS] Project projection keeps active Chord draft coherent\n";
}

void test_graphless_chord_draft_uses_the_canonical_projector() {
    using core::state::sequencer::SequencerStepContentDraftKind;

    core::state::sequencer::SequencerState sequencer;
    sequencer.pattern.note[0] = 65;
    assert(sequencer.setPitchEditMode(
        SequencerPitchEditMode::CHROMATIC
    ));
    const auto nodeId =
        core::state::sequencer::rootStepNodeId(0);
    assert(core::state::sequencer::graphView(
        sequencer.pattern
    ) == nullptr);
    assert(core::state::sequencer::beginStepContentDraft(
        sequencer,
        SequencerStepContentDraftKind::CHORD,
        0,
        nodeId
    ));
    assert(core::state::sequencer::setAuthoringNodeChordSpec(
        sequencer,
        nodeId,
        customChord(Basis::ChromaticSemitones, 3, 5)
    ));

    const auto stats =
        core::state::sequencer::projectPatternChordContext(
            sequencer,
            chromaticScale(),
            fHarmonicMinor(),
            SequencerPitchEditMode::CHROMATIC,
            SequencerPitchEditMode::FOLLOW_SCALE
        );

    assert(core::state::sequencer::graphView(
        sequencer.pattern
    ) == nullptr);
    assert(
        sequencer.stepContentDraft.chord.spec.intervalBasis() ==
        Basis::ScaleDegrees
    );
    assert(stats.patternsVisited == 1U);
    assert(stats.localChordsVisited == 1U);
    assert(stats.projected == 1U);
    assert(stats.changed == 1U);

    std::cout
        << "[PASS] graphless Chord draft uses canonical projector\n";
}

void test_projection_feedback_is_lossy_only() {
    core::state::sequencer::SequencerHistoryFeedbackState feedback;
    core::state::sequencer::SequencerChordContextProjectionStats exact{};
    exact.changed = 1;
    exact.exact = 1;
    assert(!core::handler::showChordProjectionFeedback(
        feedback,
        exact,
        10
    ));
    assert(!feedback.visible.get());

    auto adapted = exact;
    adapted.exact = 0;
    adapted.adapted = 1;
    assert(core::handler::showChordProjectionFeedback(
        feedback,
        adapted,
        20
    ));
    assert(feedback.visible.get());
    assert(
        std::string(feedback.line1.data()) ==
        "Chord formulas adapted"
    );

    auto directionLimited = adapted;
    directionLimited.directionLimited = 1;
    assert(core::handler::showChordProjectionFeedback(
        feedback,
        directionLimited,
        30
    ));
    assert(
        std::string(feedback.line3.data()) ==
        "Direction relaxed - Undo"
    );

    core::state::sequencer::SequencerChordContextProjectionStats failed{};
    failed.failures = 1;
    assert(core::handler::showChordProjectionFeedback(
        feedback,
        failed,
        40
    ));
    assert(
        std::string(feedback.line1.data()) ==
        "Chord projection warning"
    );
    assert(std::string(feedback.line2.data()) == "1 formula unchanged");
    assert(std::string(feedback.line3.data()) == "Projection failed - Undo");

    std::cout
        << "[PASS] projection feedback reports lossy and failed changes\n";
}

}  // namespace

int main() {
    test_chromatic_formula_projects_exactly_to_scale_degrees();
    test_eight_voice_formula_projects_without_cardinality_loss();
    test_scale_formula_materializes_exact_semitones();
    test_scale_to_scale_preserves_raw_degree_formula();
    test_nested_projection_uses_resolved_child_root();
    test_project_projection_skips_pattern_overrides();
    test_project_projection_keeps_active_chord_draft_coherent();
    test_graphless_chord_draft_uses_the_canonical_projector();
    test_projection_feedback_is_lossy_only();
    std::cout
        << "All SequencerChordContextProjection tests passed\n";
    return 0;
}
