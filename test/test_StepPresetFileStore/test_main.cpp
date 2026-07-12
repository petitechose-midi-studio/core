#ifdef NDEBUG
#undef NDEBUG
#endif

#include <array>
#include <cassert>
#include <cstring>
#include <filesystem>
#include <iostream>

#include <oc/impl/HostFileSystem.hpp>

#include "../../src/persistence/ProductFileService.hpp"
#include "../../src/persistence/StepPresetFileStore.hpp"
#include "../../src/state/sequencer/SequencerGraphOps.hpp"
#include "../../src/state/sequencer/SequencerGraphPresetWorkflow.hpp"
#include "../../src/state/sequencer/SequencerState.hpp"

namespace {

using core::persistence::ProductFileService;
using core::persistence::StepPresetFileListEntry;
using core::persistence::StepPresetFileStore;
using core::state::sequencer::SequencerState;
using core::state::sequencer::StepProperty;
using core::state::sequencer::createMicroSequence;
using core::state::sequencer::graphView;
using core::state::sequencer::loadFocusedStepGraphPreset;
using core::state::sequencer::nodeLocalVariationRange;
using core::state::sequencer::rootStepNodeId;
using core::state::sequencer::saveFocusedStepGraphPreset;
using core::state::sequencer::setNodeLocalVariationRange;
using core::state::sequencer::setNodeNoteOffset;
using core::state::sequencer::setNodeVelocityOffset;
using oc::note::sequencer::STEP_NODE_CHILD_SEQUENCE;

std::filesystem::path testRoot() {
    return std::filesystem::temp_directory_path() / "midi-studio-core-step-preset-file-store-test";
}

void resetTestRoot() {
    std::error_code ec;
    std::filesystem::remove_all(testRoot(), ec);
}

void prepareSource(SequencerState& source) {
    source.pattern.length.set(8);
    source.focusedStep.set(3);
    source.pattern.setEnabled(3, true);
    assert(source.setStepDataAt(3, 66, 93, 144, -4, 82));

    const auto micro = createMicroSequence(source.pattern, rootStepNodeId(3), 3);
    assert(micro.ok);
    const auto* graph = graphView(source.pattern);
    assert(graph != nullptr);
    const auto* sequence = graph->sequence(micro.id);
    assert(sequence != nullptr);
    const auto child = static_cast<uint16_t>(sequence->firstStepNode + 2U);
    assert(setNodeNoteOffset(source.pattern, child, 8));
    assert(setNodeVelocityOffset(source.pattern, child, -13));
    assert(setNodeLocalVariationRange(source.pattern, child, StepProperty::NOTE, 4));
}

void assertLoadedIntoTarget(const SequencerState& target) {
    assert(target.pattern.isEnabled(6));
    assert(target.pattern.note[6] == 66);
    assert(target.pattern.velocity[6] == 93);
    assert(target.pattern.gate[6] == 144);
    assert(target.pattern.nudge[6] == -4);
    assert(target.pattern.probability[6] == 82);

    const auto* graph = graphView(target.pattern);
    assert(graph != nullptr);
    const auto* root = graph->stepNode(rootStepNodeId(6));
    assert(root != nullptr);
    assert(root->has(STEP_NODE_CHILD_SEQUENCE));
    const auto* sequence = graph->sequence(root->childSequenceId);
    assert(sequence != nullptr);
    assert(sequence->length == 3);
    const auto* child = graph->stepNode(static_cast<uint16_t>(sequence->firstStepNode + 2U));
    assert(child != nullptr);
    assert(child->noteOffset == 8);
    assert(child->velocityOffset == -13);
    assert(nodeLocalVariationRange(*child, StepProperty::NOTE) == 4);
}

void test_step_preset_file_store_roundtrip_and_lists_files() {
    resetTestRoot();
    oc::impl::HostFileSystem filesystem(testRoot().string().c_str());
    assert(filesystem.init());
    ProductFileService productFiles(filesystem);
    assert(productFiles.init());
    StepPresetFileStore store(productFiles);

    SequencerState source;
    prepareSource(source);

    std::array<uint8_t, StepPresetFileStore::MAX_FILE_SIZE> payload{};
    const auto encoded = saveFocusedStepGraphPreset(
        source,
        payload.data(),
        static_cast<uint16_t>(payload.size())
    );
    assert(encoded.ok());
    assert(encoded.bytesWritten > 0);

    const auto saved = store.save("step-preset-001", payload.data(), encoded.bytesWritten);
    assert(saved);
    assert(std::strcmp(saved.value().presetId, "step-preset-001") == 0);
    assert(std::strcmp(saved.value().presetPath, "library/step-presets/step-preset-001.mssp") == 0);
    assert(std::filesystem::exists(
        testRoot() / "midi-studio" / "library" / "step-presets" / "step-preset-001.mssp"
    ));

    StepPresetFileListEntry entries[4]{};
    const auto listed = store.list(entries, 4);
    assert(listed);
    assert(listed.value().count == 1);
    assert(!listed.value().truncated);
    assert(std::strcmp(entries[0].id, "step-preset-001") == 0);

    char nextId[core::state::project::ProjectMetadata::ID_SIZE] = {};
    const auto next = store.nextPresetId(nextId, sizeof(nextId));
    assert(next);
    assert(std::strcmp(nextId, "step-preset-002") == 0);

    std::array<uint8_t, StepPresetFileStore::MAX_FILE_SIZE> loadedPayload{};
    uint16_t loadedSize = 0;
    const auto loaded = store.load(
        "step-preset-001",
        loadedPayload.data(),
        static_cast<uint16_t>(loadedPayload.size()),
        loadedSize
    );
    assert(loaded);
    assert(loadedSize == encoded.bytesWritten);

    SequencerState target;
    target.pattern.length.set(8);
    target.focusedStep.set(6);
    target.pattern.setEnabled(6, false);
    assert(target.setStepDataAt(6, 41, 11, 32, 5, 100));
    const auto applied = loadFocusedStepGraphPreset(target, loadedPayload.data(), loadedSize);
    assert(applied.ok());
    assertLoadedIntoTarget(target);

    assert(productFiles.rename(
        "library/step-presets/step-preset-001.mssp",
        "library/step-presets/step-preset-001.mssp.bak"
    ));
    loadedSize = 0;
    assert(store.load(
        "step-preset-001",
        loadedPayload.data(),
        static_cast<uint16_t>(loadedPayload.size()),
        loadedSize
    ));
    assert(loadedSize == encoded.bytesWritten);
    assert(std::filesystem::exists(
        testRoot() / "midi-studio" / "library" / "step-presets" /
        "step-preset-001.mssp"
    ));
    assert(!std::filesystem::exists(
        testRoot() / "midi-studio" / "library" / "step-presets" /
        "step-preset-001.mssp.bak"
    ));

    resetTestRoot();
    std::cout << "[PASS] test_step_preset_file_store_roundtrip_and_lists_files\n";
}

}  // namespace

int main() {
    test_step_preset_file_store_roundtrip_and_lists_files();
    std::cout << "[PASS] StepPresetFileStore tests\n";
    return 0;
}
