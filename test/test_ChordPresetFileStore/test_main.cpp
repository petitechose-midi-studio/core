#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <cstring>
#include <filesystem>
#include <iostream>

#include <oc/impl/HostFileSystem.hpp>
#include <oc/note/sequencer/StepSequencerChordPreset.hpp>

#include "../../src/persistence/ChordPresetFileStore.hpp"
#include "../../src/persistence/ProductFileService.hpp"

namespace {

using core::persistence::ChordPresetFileListEntry;
using core::persistence::ChordPresetFilePageDirection;
using core::persistence::ChordPresetFileStore;
using core::persistence::ProductFileService;
using namespace oc::note::sequencer;

std::filesystem::path testRoot() {
    return std::filesystem::temp_directory_path() /
           "midi-studio-core-chord-preset-file-store-test";
}

void resetTestRoot() {
    std::error_code error;
    std::filesystem::remove_all(testRoot(), error);
}

StepSequencerChordPreset preset(
    const char* id,
    const char* name,
    uint8_t secondInterval
) {
    StepSequencerChordPreset result{};
    result.reset();
    result.valid = true;
    assert(setChordPresetMetadata(result, id, name));
    result.formula = StepSequencerChordSpec::semantic(
        StepSequencerChordHarmony::Custom,
        4,
        StepSequencerChordVoicing::Open,
        1,
        StepSequencerChordIntervalBasis::ChromaticSemitones
    );
    result.formula.setCustomIntervals({
        0,
        secondInterval,
        static_cast<uint8_t>(secondInterval + 4U),
        static_cast<uint8_t>(secondInterval + 7U),
        0,
        0,
        0,
        0,
    });
    result.formula.strum = 12;
    result.formula.velocityCurve = -7;
    result.sourceShapeHint = StepSequencerChordHarmony::Minor7;
    assert(setChordPresetSourceContext(
        result,
        {
            .root = 5,
            .type = StepSequencerScaleType::HarmonicMinor,
            .mode = StepSequencerScaleConstraintMode::ConstrainNearest,
        },
        8
    ));
    return result;
}

void test_roundtrip_metadata_sort_pagination_and_next_id() {
    resetTestRoot();
    oc::impl::HostFileSystem filesystem(testRoot().string().c_str());
    assert(filesystem.init());
    ProductFileService productFiles(filesystem);
    assert(productFiles.init());
    ChordPresetFileStore store(productFiles);

    const auto zulu = preset("chord-preset-001", "Zulu", 3);
    const auto alpha = preset("chord-preset-002", "alpha", 4);
    assert(store.save(zulu));
    assert(store.save(alpha));
    assert(std::filesystem::exists(
        testRoot() / "midi-studio" / "library" / "chord-presets" /
        "chord-preset-001.mscp"
    ));

    ChordPresetFileListEntry entries[2]{};
    const auto listed = store.list(entries, 2);
    assert(listed);
    assert(listed.value().count == 2);
    assert(listed.value().totalCount == 2);
    assert(!listed.value().truncated);
    assert(std::strcmp(entries[0].id, "chord-preset-002") == 0);
    assert(std::strcmp(entries[0].semanticName, "alpha") == 0);
    assert(entries[0].metadataReadable);
    assert(std::strcmp(entries[1].id, "chord-preset-001") == 0);

    ChordPresetFileListEntry page[1]{};
    const auto firstPage = store.list(page, 1);
    assert(firstPage);
    assert(firstPage.value().count == 1);
    assert(!firstPage.value().hasPrevious);
    assert(firstPage.value().hasNext);
    assert(std::strcmp(page[0].id, "chord-preset-002") == 0);

    const auto secondPage = store.listPage(
        page,
        1,
        "chord-preset-002",
        ChordPresetFilePageDirection::FORWARD
    );
    assert(secondPage);
    assert(secondPage.value().hasPrevious);
    assert(!secondPage.value().hasNext);
    assert(std::strcmp(page[0].id, "chord-preset-001") == 0);

    const auto previousPage = store.listPage(
        page,
        1,
        "chord-preset-001",
        ChordPresetFilePageDirection::BACKWARD
    );
    assert(previousPage);
    assert(!previousPage.value().hasPrevious);
    assert(previousPage.value().hasNext);
    assert(std::strcmp(page[0].id, "chord-preset-002") == 0);

    char nextId[core::state::project::ProjectMetadata::ID_SIZE] = {};
    assert(store.nextPresetId(nextId, sizeof(nextId)));
    assert(std::strcmp(nextId, "chord-preset-003") == 0);

    StepSequencerChordPreset loaded{};
    const auto loadedResult = store.load("chord-preset-001", loaded);
    assert(loadedResult);
    assert(loaded.valid);
    assert(std::strcmp(loaded.semanticName, "Zulu") == 0);
    assert(chordSpecsEqual(zulu.formula, loaded.formula));
    assert(loaded.sourceRootPitchClass == 0);

    resetTestRoot();
    std::cout
        << "[PASS] test_roundtrip_metadata_sort_pagination_and_next_id\n";
}

void test_corrupt_payload_is_rejected_transactionally() {
    resetTestRoot();
    oc::impl::HostFileSystem filesystem(testRoot().string().c_str());
    assert(filesystem.init());
    ProductFileService productFiles(filesystem);
    assert(productFiles.init());
    ChordPresetFileStore store(productFiles);

    const auto source = preset("chord-preset-001", "Minor Open", 3);
    assert(store.save(source));
    const uint8_t corrupt = 0xFFU;
    const char* path =
        "library/chord-presets/chord-preset-001.mscp";
    assert(productFiles.write(
        path,
        ChordPresetFileStore::MAX_FILE_SIZE - 1U,
        &corrupt,
        1
    ));
    assert(productFiles.flush(path));

    StepSequencerChordPreset loaded{};
    loaded.valid = true;
    const auto result = store.load("chord-preset-001", loaded);
    assert(!result);
    assert(result.error().code == oc::type::ErrorCode::STORAGE_CORRUPT);
    assert(!loaded.valid);

    resetTestRoot();
    std::cout << "[PASS] test_corrupt_payload_is_rejected_transactionally\n";
}

}  // namespace

int main() {
    test_roundtrip_metadata_sort_pagination_and_next_id();
    test_corrupt_payload_is_rejected_transactionally();
    std::cout << "[PASS] ChordPresetFileStore tests\n";
    return 0;
}
