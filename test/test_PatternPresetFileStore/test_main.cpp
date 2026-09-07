#ifdef NDEBUG
#undef NDEBUG
#endif

#include <array>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iostream>

#include <oc/impl/HostFileSystem.hpp>

#include "persistence/PatternPresetFileStore.hpp"
#include "persistence/ProductFileService.hpp"
#include "state/sequencer/SequencerHistory.hpp"
#include "state/sequencer/SequencerState.hpp"

namespace {

namespace codec =
    core::persistence::sequencer_pattern_preset_codec;
namespace seq = core::state::sequencer;

using core::persistence::PatternPresetFileListEntry;
using core::persistence::PatternPresetFilePageDirection;
using core::persistence::PatternPresetFileStore;
using core::persistence::ProductDirectoryCatalog;
using core::persistence::ProductFileService;

std::filesystem::path testRoot() {
    return std::filesystem::temp_directory_path() /
        "midi-studio-core-pattern-preset-file-store-test";
}

void resetTestRoot() {
    std::error_code error;
    std::filesystem::remove_all(testRoot(), error);
}

oc::type::Result<core::persistence::PatternPresetFileListResult>
listSettled(
    ProductFileService& files,
    ProductDirectoryCatalog& catalog,
    PatternPresetFileStore& store,
    PatternPresetFileListEntry* entries,
    uint8_t capacity
) {
    auto listed = store.listPage(
        entries,
        capacity,
        nullptr,
        PatternPresetFilePageDirection::FORWARD
    );
    for (uint32_t nowMs = 1U;
         !listed &&
         listed.error().code == oc::type::ErrorCode::HARDWARE_BUSY &&
         nowMs <= ProductDirectoryCatalog::MAX_ENTRIES + 2U;
         ++nowMs) {
        assert(files.persistenceJobs().beginTurn(nowMs));
        catalog.advance(nowMs, false);
        listed = store.listPage(
            entries,
            capacity,
            nullptr,
            PatternPresetFilePageDirection::FORWARD
        );
    }
    return listed;
}

oc::type::Result<core::persistence::PatternPresetFileListResult>
listFoldersSettled(
    ProductFileService& files,
    ProductDirectoryCatalog& catalog,
    PatternPresetFileStore& store,
    PatternPresetFileListEntry* entries,
    uint8_t capacity
) {
    auto listed = store.listFoldersPage(
        entries,
        capacity,
        nullptr,
        PatternPresetFilePageDirection::FORWARD
    );
    for (uint32_t nowMs = 1U;
         !listed &&
         listed.error().code == oc::type::ErrorCode::HARDWARE_BUSY &&
         nowMs <= ProductDirectoryCatalog::MAX_ENTRIES + 2U;
         ++nowMs) {
        assert(files.persistenceJobs().beginTurn(nowMs));
        catalog.advance(nowMs, false);
        listed = store.listFoldersPage(
            entries,
            capacity,
            nullptr,
            PatternPresetFilePageDirection::FORWARD
        );
    }
    return listed;
}

void testPatternPresetFileStoreRoundTrip() {
    resetTestRoot();
    oc::impl::HostFileSystem filesystem(testRoot().string().c_str());
    assert(filesystem.init());
    ProductFileService files(filesystem);
    assert(files.init());
    ProductDirectoryCatalog catalog(files);
    PatternPresetFileStore store(files, catalog);

    PatternPresetFileListEntry emptyEntries[1]{};
    const auto empty = listSettled(
        files,
        catalog,
        store,
        emptyEntries,
        1U
    );
    assert(empty);
    assert(empty.value().count == 0U);
    char firstId[seq::SEQUENCER_PRESET_TECHNICAL_ID_SIZE]{};
    assert(store.nextPresetId(firstId, sizeof(firstId)));
    assert(std::strcmp(firstId, "pattern-preset-001") == 0);

    seq::SequencerState source{};
    source.reset();
    assert(source.pattern.setContentLength(24U));
    source.pattern.setEnabled(4U, true);
    assert(source.pattern.setStepDataAt(4U, 67U, 109U, 175U, -5, 81U));

    seq::SequencerPatternPresetMetadata metadata{};
    assert(seq::setSequencerPatternPresetMetadata(
        metadata,
        seq::SequencerTrackKind::INSTRUMENT,
        "pattern-preset-001",
        "Broken pulse"
    ));

    std::array<uint8_t, PatternPresetFileStore::MAX_FILE_SIZE> payload{};
    const auto encoded = codec::encode(
        metadata,
        source.pattern,
        nullptr,
        payload.data(),
        static_cast<uint16_t>(payload.size())
    );
    assert(encoded.ok());

    const auto saved = store.save(
        metadata.technicalId,
        payload.data(),
        encoded.bytesWritten
    );
    assert(saved);
    assert(std::strcmp(saved.value().id, "pattern-preset-001") == 0);
    assert(std::strcmp(
        saved.value().path,
        "library/pattern-presets/pattern-preset-001.mspp"
    ) == 0);

    PatternPresetFileListEntry entries[4]{};
    const auto listed = listSettled(files, catalog, store, entries, 4U);
    assert(listed);
    assert(listed.value().count == 1U);
    assert(entries[0].metadataReadable);
    assert(std::strcmp(entries[0].id, "pattern-preset-001") == 0);
    assert(std::strcmp(entries[0].semanticName, "Broken pulse") == 0);

    assert(store.createFolder("Beats"));
    const auto mixed = listSettled(files, catalog, store, entries, 4U);
    assert(mixed && mixed.value().count == 2U);
    assert(std::strcmp(entries[0].id, "@Beats") == 0);
    assert(std::strcmp(entries[1].id, "pattern-preset-001") == 0);
    const auto afterFolder = store.listPage(entries, 4U, "@Beats",
        PatternPresetFilePageDirection::FORWARD);
    assert(afterFolder && afterFolder.value().count == 1U);
    assert(std::strcmp(entries[0].id, "pattern-preset-001") == 0);
    const auto beforeAsset = store.listPage(entries, 4U, "pattern-preset-001",
        PatternPresetFilePageDirection::BACKWARD);
    assert(beforeAsset && beforeAsset.value().count == 1U);
    assert(std::strcmp(entries[0].id, "@Beats") == 0);

    char nextId[seq::SEQUENCER_PRESET_TECHNICAL_ID_SIZE]{};
    assert(store.nextPresetId(nextId, sizeof(nextId)));
    assert(std::strcmp(nextId, "pattern-preset-002") == 0);

    std::array<uint8_t, PatternPresetFileStore::MAX_FILE_SIZE> loadedBytes{};
    uint16_t loadedSize = 0U;
    assert(store.load(
        "pattern-preset-001",
        loadedBytes.data(),
        static_cast<uint16_t>(loadedBytes.size()),
        loadedSize
    ));
    assert(loadedSize == encoded.bytesWritten);

    seq::SequencerState decoded{};
    decoded.reset();
    seq::SequencerPatternPresetMetadata decodedMetadata{};
    assert(codec::decode(
        loadedBytes.data(),
        loadedSize,
        decodedMetadata,
        decoded.pattern,
        nullptr
    ));
    assert(std::strcmp(decodedMetadata.semanticName, "Broken pulse") == 0);
    assert(seq::sameMusicalPatternState(source.pattern, decoded.pattern));

    resetTestRoot();
    std::cout << "[PASS] PatternPresetFileStore round-trip and catalog\n";
}

void testPatternPresetFolderLifecycle() {
    resetTestRoot();
    oc::impl::HostFileSystem filesystem(testRoot().string().c_str());
    assert(filesystem.init());
    ProductFileService files(filesystem);
    assert(files.init());
    ProductDirectoryCatalog catalog(files);
    PatternPresetFileStore root(files, catalog);

    assert(root.createFolder("Beats"));
    assert(root.createFolder("Archive"));
    PatternPresetFileListEntry folders[4]{};
    auto listed = listFoldersSettled(files, catalog, root, folders, 4U);
    assert(listed && listed.value().count == 2U);
    assert(std::strcmp(folders[0].id, "@Archive") == 0);
    assert(std::strcmp(folders[1].id, "@Beats") == 0);

    assert(root.renameFolder("Archive", "Collections"));
    seq::SequencerPatternPresetLocation beatsLocation{};
    assert(beatsLocation.enter("Beats"));
    PatternPresetFileStore beats(files, catalog, beatsLocation);
    const uint8_t payload[] = {1U, 2U, 3U};
    assert(beats.save("pattern-preset-001", payload, sizeof(payload)));

    seq::SequencerPatternPresetLocation collectionsLocation{};
    assert(collectionsLocation.enter("Collections"));
    assert(beats.movePreset("pattern-preset-001", collectionsLocation));
    PatternPresetFileStore collections(files, catalog, collectionsLocation);
    uint8_t loaded[8]{};
    uint16_t loadedSize = 0U;
    assert(collections.load(
        "pattern-preset-001",
        loaded,
        sizeof(loaded),
        loadedSize
    ));
    assert(loadedSize == sizeof(payload));

    assert(root.moveFolder("Beats", collectionsLocation));
    PatternPresetFileListEntry nested[2]{};
    listed = listFoldersSettled(
        files,
        catalog,
        collections,
        nested,
        2U
    );
    assert(listed && listed.value().count == 1U);
    assert(std::strcmp(nested[0].id, "@Beats") == 0);

    assert(collections.removeEmptyFolder("Beats"));
    assert(collections.remove("pattern-preset-001"));
    assert(root.removeEmptyFolder("Collections"));

    resetTestRoot();
    std::cout << "[PASS] PatternPresetFileStore folder lifecycle\n";
}

void testDenseFolderPagination() {
    using core::persistence::compareProductCatalogNames;
    assert(compareProductCatalogNames("Beats", "beats") == 0);
    assert(compareProductCatalogNames("", "Beat") < 0);
    assert(compareProductCatalogNames("Beat", "beats") < 0);
    assert(compareProductCatalogNames("b", "A") > 0);
    assert(compareProductCatalogNames("\x80", "z") > 0);
    resetTestRoot();
    oc::impl::HostFileSystem filesystem(testRoot().string().c_str());
    assert(filesystem.init());
    ProductFileService files(filesystem);
    assert(files.init());
    ProductDirectoryCatalog catalog(files);
    PatternPresetFileStore store(files, catalog);
    for (unsigned i = ProductDirectoryCatalog::MAX_ENTRIES; i > 0; --i) {
        char name[16]{};
        std::snprintf(name, sizeof(name), "Folder%03u", i - 1U);
        assert(store.createFolder(name));
    }
    std::array<PatternPresetFileListEntry, 8> entries{};
    assert(listFoldersSettled(files, catalog, store, entries.data(), entries.size()));
    for (const bool foldersOnly : {false, true}) {
        const auto list = [&](const char* anchor, PatternPresetFilePageDirection direction) {
            return foldersOnly
                ? store.listFoldersPage(entries.data(), entries.size(), anchor, direction)
                : store.listPage(entries.data(), entries.size(), anchor, direction);
        };
        for (const auto direction : {PatternPresetFilePageDirection::FORWARD,
                                     PatternPresetFilePageDirection::BACKWARD}) {
            const auto start = std::chrono::steady_clock::now();
            const auto page = list("@Folder247", direction);
            const auto elapsed = std::chrono::steady_clock::now() - start;
            assert(page && page.value().count == entries.size());
            assert(page.value().totalCount == ProductDirectoryCatalog::MAX_ENTRIES);
            assert(page.value().hasPrevious);
            assert(page.value().hasNext == (direction == PatternPresetFilePageDirection::BACKWARD));
            const unsigned begin = direction == PatternPresetFilePageDirection::FORWARD ? 248U : 239U;
            for (unsigned i = 0; i < entries.size(); ++i) {
                char expected[16]{};
                std::snprintf(expected, sizeof(expected), "@Folder%03u", begin + i);
                assert(std::strcmp(entries[i].id, expected) == 0);
            }
            std::cout << "[MEASURE] folder-page only=" << foldersOnly
                      << " direction=" << unsigned(direction) << " us="
                      << std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count() << '\n';
        }
        assert(!list("@Missing", PatternPresetFilePageDirection::FORWARD));
        const auto empty = list("@Folder000", PatternPresetFilePageDirection::BACKWARD);
        assert(empty && empty.value().count == 0 && !empty.value().hasPrevious);
    }
    resetTestRoot();
}

}  // namespace

int main() {
    testPatternPresetFileStoreRoundTrip();
    testPatternPresetFolderLifecycle();
    testDenseFolderPagination();
    return 0;
}
