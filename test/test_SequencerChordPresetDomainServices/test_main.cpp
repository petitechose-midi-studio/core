#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>

#include <oc/impl/HostFileSystem.hpp>

#include "../../src/handler/sequencer/PatternPitchSettingsDomainServices.hpp"
#include "../../src/handler/sequencer/SequencerChordPresetDomainServices.hpp"
#include "../../src/persistence/ProductFileService.hpp"
#include "../../src/state/CoreState.hpp"
#include "../../src/state/sequencer/SequencerChordUiOps.hpp"
#include "../../src/state/sequencer/SequencerContentViewOps.hpp"
#include "../../src/state/sequencer/SequencerGraphOps.hpp"
#include "../../src/state/sequencer/SequencerStepContentDraftOps.hpp"
#include "../../src/state/sequencer/SequencerStepEditRows.hpp"
#include "../support/CoreStorages.hpp"

namespace {

namespace seq = core::state::sequencer;
using Basis =
    oc::note::sequencer::StepSequencerChordIntervalBasis;
using Compatibility = seq::SequencerChordPresetCompatibility;
using Harmony = oc::note::sequencer::StepSequencerChordHarmony;
using Spec = oc::note::sequencer::StepSequencerChordSpec;

std::filesystem::path testRoot() {
    return std::filesystem::temp_directory_path() /
           "midi-studio-core-chord-preset-domain-test";
}

void resetTestRoot() {
    std::error_code error;
    std::filesystem::remove_all(testRoot(), error);
}

struct Harness {
    test_support::CoreStorages storages;
    core::state::CoreState state;
    oc::impl::HostFileSystem filesystem;
    core::persistence::ProductFileService productFiles;
    core::persistence::ProductDirectoryCatalog productCatalog;
    core::handler::SequencerChordPresetDomainServices presets;

    Harness()
        : state(storages.settings)
        , filesystem(testRoot().string().c_str())
        , productFiles(filesystem)
        , productCatalog(productFiles)
        , presets(
              core::handler::SequencerChordPresetDomainServices::
                  fromCoreState(state, productFiles, productCatalog)
          ) {
        resetTestRoot();
        assert(filesystem.init());
        assert(productFiles.init());
        state.sequencer.pattern.setContentLength(8U);
        state.sequencer.pattern.note[0] = 60U;
    }

    void beginChordDraft(uint8_t step, uint16_t nodeId) {
        state.sequencer.stepEdit.visible.set(true);
        state.sequencer.stepEdit.stepIndex.set(step);
        state.sequencer.stepEdit.focusedRow.set(
            seq::step_edit_rows::CHORD
        );
        state.sequencer.stepEdit.chordEditor.active.set(true);
        assert(seq::beginStepContentDraft(
            state.sequencer,
            seq::SequencerStepContentDraftKind::CHORD,
            step,
            nodeId
        ));
    }
};

Spec explicitFormula(
    Basis basis,
    std::initializer_list<uint8_t> intervals
) {
    Spec result = Spec::semantic(
        Harmony::Custom,
        static_cast<uint8_t>(intervals.size()),
        oc::note::sequencer::StepSequencerChordVoicing::Open,
        1U,
        basis
    );
    std::array<uint8_t, Spec::MAX_CUSTOM_VOICES> values{};
    uint8_t index = 0U;
    for (uint8_t interval : intervals) {
        values[index++] = interval;
    }
    result.setCustomIntervals(values);
    result.strum = 17;
    result.velocityCurve = -9;
    result.clamp();
    return result;
}

void test_local_save_exact_load_and_cross_basis_projection() {
    Harness h;
    assert(h.state.sequencer.setPitchEditMode(
        seq::SequencerPitchEditMode::CHROMATIC
    ));
    const uint16_t nodeId = seq::rootStepNodeId(0U);
    h.beginChordDraft(0U, nodeId);
    const Spec chromatic = explicitFormula(
        Basis::ChromaticSemitones,
        {0U, 3U, 7U, 10U, 14U}
    );
    assert(seq::setAuthoringNodeChordSpec(
        h.state.sequencer,
        nodeId,
        chromatic
    ));
    seq::notifyStepContentDraftMutation(h.state.sequencer);

    const auto chromaticTarget = h.presets.captureTarget();
    assert(chromaticTarget.valid);
    assert(chromaticTarget.canSave);
    assert(!chromaticTarget.targetUsesScaleDegrees);
    assert(h.presets.savePreset(
        "minor-open",
        chromaticTarget,
        false
    ).ok());

    const auto exact = h.presets.inspectPreset(
        "minor-open",
        chromaticTarget,
        11U
    );
    assert(exact.status ==
           core::handler::SequencerChordPresetStatus::OK);
    assert(exact.descriptor.valid);
    assert(exact.descriptor.generation == 11U);
    assert(exact.descriptor.compatibility == Compatibility::READY);
    assert(
        exact.descriptor.targetBasis ==
        Basis::ChromaticSemitones
    );
    assert(exact.descriptor.resolution.count == 5U);

    const oc::note::sequencer::StepSequencerScaleSettings fHarmonicMinor{
        .root = 5U,
        .type =
            oc::note::sequencer::StepSequencerScaleType::HarmonicMinor,
        .mode = oc::note::sequencer::
            StepSequencerScaleConstraintMode::ConstrainNearest,
    };
    (void)h.state.sequencerTracks.setProjectScaleSettings(
        fHarmonicMinor
    );
    core::handler::PatternPitchSettingsDomainServices pitchSettings({
        h.state.sequencer,
        h.state.sequencerTracks,
    });
    const auto pitchProjection = pitchSettings.applyChoice(3U, 0);
    assert(pitchProjection.hasChanges());
    assert(
        h.state.sequencer.pattern.pitchEditMode ==
        seq::SequencerPitchEditMode::FOLLOW_SCALE
    );

    const auto scaleTarget = h.presets.captureTarget();
    assert(scaleTarget.valid);
    assert(scaleTarget.targetUsesScaleDegrees);
    const auto adapted = h.presets.inspectPreset(
        "minor-open",
        scaleTarget,
        12U
    );
    assert(adapted.status ==
           core::handler::SequencerChordPresetStatus::OK);
    assert(adapted.descriptor.compatibility ==
           Compatibility::WARNING_ADAPTED);
    assert(adapted.descriptor.targetBasis == Basis::ScaleDegrees);
    assert(adapted.descriptor.projectedFormula.voices() == 5U);
    assert(adapted.descriptor.resolution.count == 5U);

    const auto applied = h.presets.applyPreset(
        "minor-open",
        scaleTarget,
        adapted.descriptor.previewKey
    );
    assert(applied.ok());
    assert(applied.changed);
    const auto projected = seq::resolveStepChordUiState(
        h.state.sequencer,
        0U
    );
    assert(projected.mode ==
           oc::note::sequencer::StepSequencerChordMode::Local);
    assert(projected.spec.intervalBasis() == Basis::ScaleDegrees);
    assert(projected.spec.voices() == 5U);

    resetTestRoot();
    std::cout
        << "[PASS] test_local_save_exact_load_and_cross_basis_projection\n";
}

void test_single_cannot_save_and_parent_capture_is_flattened() {
    {
        Harness h;
        const uint16_t nodeId = seq::rootStepNodeId(0U);
        h.beginChordDraft(0U, nodeId);
        (void)seq::clearAuthoringNodeChordState(
            h.state.sequencer,
            nodeId
        );
        seq::notifyStepContentDraftMutation(h.state.sequencer);
        const auto single = h.presets.captureTarget();
        assert(single.valid);
        assert(!single.canSave);
        assert(
            h.presets.savePreset("single", single, false).status ==
            core::handler::SequencerChordPresetStatus::EMPTY
        );
    }

    resetTestRoot();
    Harness h;
    const uint16_t rootNode = seq::rootStepNodeId(0U);
    const Spec parent = explicitFormula(
        Basis::ChromaticSemitones,
        {0U, 4U, 7U, 11U, 16U, 19U}
    );
    assert(seq::setNodeChordSpec(
        h.state.sequencer.pattern,
        rootNode,
        parent
    ));
    const auto sequence = seq::createMicroSequence(
        h.state.sequencer.pattern,
        rootNode,
        2U
    );
    assert(sequence.ok);
    assert(seq::enterMicroSequenceContentView(
        h.state.sequencer,
        rootNode,
        sequence.id
    ));
    const auto* graph = seq::graphView(h.state.sequencer.pattern);
    assert(graph != nullptr);
    const auto* micro = graph->sequence(sequence.id);
    assert(micro != nullptr);
    const uint16_t childNode = micro->firstStepNode;
    h.beginChordDraft(0U, childNode);

    const auto inherited = h.presets.captureTarget();
    assert(inherited.valid);
    assert(inherited.canSave);
    assert(inherited.captureFormula.isCustom());
    assert(inherited.captureFormula.voices() == parent.voices());
    assert(h.presets.savePreset(
        "inherited-parent",
        inherited,
        false
    ).ok());
    const auto inspected = h.presets.inspectPreset(
        "inherited-parent",
        inherited,
        7U
    );
    assert(inspected.status ==
           core::handler::SequencerChordPresetStatus::OK);
    assert(inspected.descriptor.sourceFormula.isCustom());
    assert(
        inspected.descriptor.sourceFormula.voices() ==
        parent.voices()
    );

    resetTestRoot();
    std::cout
        << "[PASS] test_single_cannot_save_and_parent_capture_is_flattened\n";
}

void test_corrupt_asset_still_returns_an_inspection_descriptor() {
    Harness h;
    const uint16_t nodeId = seq::rootStepNodeId(0U);
    h.beginChordDraft(0U, nodeId);
    const auto target = h.presets.captureTarget();
    assert(target.valid);

    const auto library =
        testRoot() / "midi-studio" / "library" / "chord-presets";
    std::filesystem::create_directories(library);
    {
        std::ofstream out(library / "broken-chord.mscp", std::ios::binary);
        out << "not-a-chord-preset";
    }

    const auto inspected = h.presets.inspectPreset(
        "broken-chord",
        target,
        11U
    );
    assert(inspected.status ==
           core::handler::SequencerChordPresetStatus::CORRUPT);
    assert(inspected.descriptor.valid);
    assert(std::strcmp(
        inspected.descriptor.technicalId,
        "broken-chord"
    ) == 0);
    assert(inspected.descriptor.compatibility == Compatibility::CORRUPT);

    resetTestRoot();
    std::cout
        << "[PASS] test_corrupt_asset_still_returns_an_inspection_descriptor\n";
}

}  // namespace

int main() {
    test_local_save_exact_load_and_cross_basis_projection();
    test_single_cannot_save_and_parent_capture_is_flattened();
    test_corrupt_asset_still_returns_an_inspection_descriptor();
    std::cout
        << "[PASS] SequencerChordPresetDomainServices tests\n";
    return 0;
}
