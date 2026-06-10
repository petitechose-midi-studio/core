#include <cassert>
#include <array>
#include <cstring>
#include <iostream>

#include "../../src/app/ExtmemAllocator.hpp"
#include "../../src/persistence/ProjectSnapshotPersistenceCodec.hpp"
#include "../../src/persistence/ProjectStatePersistenceCodec.hpp"
#include "../../src/state/CoreState.hpp"
#include "../../src/state/macro/MacroWorkflow.hpp"
#include "../../src/state/project/ProjectSnapshot.hpp"
#include "../../src/state/sequencer/SequencerHistory.hpp"
#include "../../src/state/sequencer/SequencerScaleState.hpp"
#include "../support/CoreStorages.hpp"

namespace {

namespace project = core::state::project;
namespace project_file = core::persistence::project_file;
namespace snapshot_codec = core::persistence::project_snapshot_codec;

using oc::note::sequencer::StepSequencerScaleConstraintMode;
using oc::note::sequencer::StepSequencerScaleSettings;
using oc::note::sequencer::StepSequencerScaleType;

core::state::CoreState makeCoreState(test_support::CoreStorages& storages) {
    return core::state::CoreState{
        storages.settings,
        storages.macroLibrary,
        storages.sequencerPatternLibrary,
        storages.sequencerSetLibrary,
    };
}

bool reportHas(const project_file::LoadReport& report, project_file::LoadCode code) {
    for (uint8_t i = 0; i < report.itemCount; ++i) {
        if (report.items[i].code == code) return true;
    }
    return false;
}

bool sameScale(const StepSequencerScaleSettings& lhs,
               const StepSequencerScaleSettings& rhs) {
    return lhs.root == rhs.root && lhs.type == rhs.type && lhs.mode == rhs.mode;
}

void configureProjectSession(core::state::CoreState& state) {
    std::strncpy(
        state.project.metadata.id.data(),
        "p777",
        state.project.metadata.id.size() - 1
    );
    std::strncpy(
        state.project.metadata.name.data(),
        "p777",
        state.project.metadata.name.size() - 1
    );
    state.project.metadata.modifiedCounter = 77;
    state.project.metadata.dirty = true;
    state.project.metadata.hasSavedIdentity = true;

    state.statusBar.tempo.set(128.5f);
    state.statusBar.tempoDisplay.set(128.5f);
    state.projectNavigation.transportSwingPercent = 18;
    state.projectNavigation.transportRunMode = 1;
    state.projectNavigation.patternsInheritScale = false;
    state.projectNavigation.clipsInheritScale = true;

    StepSequencerScaleSettings scale{
        .root = 2,
        .type = StepSequencerScaleType::WholeTone,
        .mode = StepSequencerScaleConstraintMode::ConstrainNearest,
    };
    assert(state.sequencerTracks.setProjectScaleSettings(scale));

    assert(state.setSharedTrackState(0x0007, 2));
    auto& page = state.pages.activePageData();
    std::strncpy(page.name, "FullSnap", sizeof(page.name) - 1);
    page.cc[2] = 91;
    page.values[2] = 0.625f;
    core::state::macro::MacroWorkflow::syncRuntimeFromActivePage(state.macros, state.pages);

    state.sequencer.pattern.length.set(12);
    state.sequencer.pattern.stepsPerBeat.set(5);
    state.sequencer.pattern.midiChannel.set(6);
    state.sequencer.setStepDataAt(0, 64, 100, 80);
    state.sequencer.setStepDataAt(4, 67, 88, 55);
    state.sequencer.pattern.toggle(0);
    state.sequencer.pattern.toggle(4);
    state.sequencer.focusedStep.set(4);
    state.sequencer.page.set(0);
}

void assertRuntimeMatchesConfigured(core::state::CoreState& state) {
    assert(std::strcmp(state.project.metadata.id.data(), "p777") == 0);
    assert(std::strcmp(state.project.metadata.name.data(), "p777") == 0);
    assert(state.project.metadata.modifiedCounter == 77);
    assert(state.project.metadata.dirty);
    assert(state.project.metadata.hasSavedIdentity);
    assert(state.statusBar.tempo.get() == 128.5f);
    assert(state.projectNavigation.transportSwingPercent == 18);
    assert(state.projectNavigation.transportRunMode == 1);
    assert(!state.projectNavigation.patternsInheritScale);
    assert(state.projectNavigation.clipsInheritScale);

    StepSequencerScaleSettings expectedScale{
        .root = 2,
        .type = StepSequencerScaleType::WholeTone,
        .mode = StepSequencerScaleConstraintMode::ConstrainNearest,
    };
    assert(sameScale(state.sequencerTracks.projectScaleSettings(), expectedScale));

    assert(state.sharedTrackEnabledMask.get() == 0x0007);
    assert(state.sharedTrackActive.get() == 2);
    assert(std::strcmp(state.pages.activePageData().name, "FullSnap") == 0);
    assert(state.pages.activePageData().cc[2] == 91);
    assert(state.pages.activePageData().values[2] == 0.625f);
    assert(state.macros.slots[2].value.get() == 0.625f);

    assert(state.sequencer.pattern.length.get() == 12);
    assert(state.sequencer.pattern.stepsPerBeat.get() == 5);
    assert(state.sequencer.pattern.midiChannel.get() == 6);
    assert(state.sequencer.pattern.isEnabled(0));
    assert(state.sequencer.pattern.isEnabled(4));
    assert(state.sequencer.pattern.note[0] == 64);
    assert(state.sequencer.pattern.note[4] == 67);
    assert(state.sequencer.focusedStep.get() == 4);
}

void test_project_snapshot_roundtrip_restores_runtime_state() {
    test_support::CoreStorages sourceStorages;
    auto sourceState = makeCoreState(sourceStorages);
    configureProjectSession(sourceState);

    project::ProjectSnapshot sourceSnapshot;
    assert(project::captureProjectSnapshot(sourceState, sourceSnapshot));

    auto buffer = core::app::makeExtmemUnique<std::array<uint8_t, 32768>>();
    assert(buffer);
    auto encodeResult = snapshot_codec::encodeProjectSnapshot(
        sourceSnapshot,
        buffer->data(),
        static_cast<uint32_t>(buffer->size())
    );
    assert(encodeResult.status == project_file::Status::OK);

    project::ProjectSnapshot loadedSnapshot;
    project_file::LoadReport report{};
    auto decodeResult = snapshot_codec::decodeProjectSnapshot(
        buffer->data(),
        encodeResult.bytesWritten,
        loadedSnapshot,
        &report
    );
    assert(decodeResult.ok);
    assert(decodeResult.loadStatus == project_file::LoadStatus::OK);
    assert(decodeResult.overwriteSafe);
    assert(report.ok());
    assert(core::state::sequencer::sameMusicalHistorySnapshot(
        sourceSnapshot.sequencer,
        loadedSnapshot.sequencer
    ));

    test_support::CoreStorages targetStorages;
    auto targetState = makeCoreState(targetStorages);
    assert(project::applyProjectSnapshot(targetState, loadedSnapshot));
    assertRuntimeMatchesConfigured(targetState);

    std::cout << "[PASS] test_project_snapshot_roundtrip_restores_runtime_state\n";
}

void test_project_snapshot_decode_defaults_missing_macro_and_sequencer_chunks() {
    project::ProjectState state;
    std::strncpy(state.metadata.id.data(), "p010", state.metadata.id.size() - 1);
    std::strncpy(state.metadata.name.data(), "p010", state.metadata.name.size() - 1);

    uint8_t bytes[512] = {};
    auto encodeResult = core::persistence::project_state_codec::encodeProjectState(
        state,
        bytes,
        sizeof(bytes)
    );
    assert(encodeResult.status == project_file::Status::OK);

    project::ProjectSnapshot snapshot;
    project_file::LoadReport report{};
    auto decodeResult = snapshot_codec::decodeProjectSnapshot(
        bytes,
        encodeResult.bytesWritten,
        snapshot,
        &report
    );
    assert(decodeResult.ok);
    assert(decodeResult.overwriteSafe);
    assert(report.ok());
    assert(reportHas(report, project_file::LoadCode::MISSING_OPTIONAL_CHUNK));
    assert(reportHas(report, project_file::LoadCode::DEFAULTED_CHUNK));
    assert(std::strcmp(snapshot.project.metadata.id.data(), "p010") == 0);
    assert(std::strcmp(snapshot.project.metadata.name.data(), "p010") == 0);
    assert(snapshot.sharedTrackEnabledMask == core::state::macro::MacroPagesState::DEFAULT_TRACK_ENABLED_MASK);

    std::cout << "[PASS] test_project_snapshot_decode_defaults_missing_macro_and_sequencer_chunks\n";
}

void test_project_snapshot_future_sequencer_chunk_blocks_overwrite() {
    const uint8_t payload[] = {1, 2, 3};
    const project_file::ChunkView chunks[] = {{
        .id = project_file::chunkIdValue(project_file::ChunkId::SEQUENCER_STATE),
        .versionMajor = static_cast<uint8_t>(
            snapshot_codec::PROJECT_SNAPSHOT_CHUNK_VERSION_MAJOR + 1
        ),
        .versionMinor = 0,
        .flags = 0,
        .data = payload,
        .size = sizeof(payload),
    }};

    uint8_t bytes[160] = {};
    auto encodeResult = project_file::encode(chunks, 1, 0, bytes, sizeof(bytes));
    assert(encodeResult.status == project_file::Status::OK);

    project::ProjectSnapshot snapshot;
    project_file::LoadReport report{};
    auto decodeResult = snapshot_codec::decodeProjectSnapshot(
        bytes,
        encodeResult.bytesWritten,
        snapshot,
        &report
    );
    assert(decodeResult.ok);
    assert(!decodeResult.overwriteSafe);
    assert(report.status == project_file::LoadStatus::PARTIAL);
    assert(report.hasUnknownUnsupportedData);
    assert(reportHas(report, project_file::LoadCode::UNSUPPORTED_CHUNK_VERSION));
    assert(reportHas(report, project_file::LoadCode::DEFAULTED_CHUNK));

    std::cout << "[PASS] test_project_snapshot_future_sequencer_chunk_blocks_overwrite\n";
}

}  // namespace

int main() {
    std::cout << "==============================================\n";
    std::cout << "ProjectSnapshotPersistenceCodec tests\n";
    std::cout << "==============================================\n\n";

    test_project_snapshot_roundtrip_restores_runtime_state();
    test_project_snapshot_decode_defaults_missing_macro_and_sequencer_chunks();
    test_project_snapshot_future_sequencer_chunk_blocks_overwrite();

    std::cout << "\n==============================================\n";
    std::cout << "All tests passed\n";
    std::cout << "==============================================\n";
    return 0;
}
