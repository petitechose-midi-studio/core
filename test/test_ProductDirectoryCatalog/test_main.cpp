#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include <oc/impl/HostFileSystem.hpp>

#include "../../src/persistence/ProductDirectoryCatalog.hpp"
#include "../../src/persistence/ProductFileService.hpp"
#include "../support/ProductFileTestMutation.hpp"

namespace {

using core::persistence::ProductDirectoryAssetEntry;
using core::persistence::ProductDirectoryAssetQuery;
using core::persistence::ProductDirectoryCatalog;
using core::persistence::ProductFileService;
using core::persistence::ProductPersistenceJobOwner;
using oc::type::ErrorCode;

constexpr const char* CATALOG_DIRECTORY = "library/catalog-test";
constexpr const char* OVERFLOW_DIRECTORY = "library/catalog-overflow";

class CountingHostFileSystem final : public oc::impl::HostFileSystem {
public:
    explicit CountingHostFileSystem(const char* rootPath)
        : oc::impl::HostFileSystem(rootPath) {}

    oc::type::Result<oc::interface::FileInfo> stat(
        const char* path
    ) override {
        ++statCalls;
        return oc::impl::HostFileSystem::stat(path);
    }

    oc::type::Result<void> list(
        const char* path,
        oc::interface::DirectoryEntryVisitor visitor,
        void* context
    ) override {
        ++listCalls;
        return oc::impl::HostFileSystem::list(path, visitor, context);
    }

    oc::type::Result<size_t> read(
        const char* path,
        uint32_t offset,
        uint8_t* buffer,
        size_t size
    ) override {
        ++readCalls;
        return oc::impl::HostFileSystem::read(path, offset, buffer, size);
    }

    void resetCounters() {
        statCalls = 0U;
        listCalls = 0U;
        readCalls = 0U;
    }

    uint32_t statCalls = 0U;
    uint32_t listCalls = 0U;
    uint32_t readCalls = 0U;
};

std::filesystem::path testRoot(const char* suffix) {
    return std::filesystem::temp_directory_path() /
           (std::string("midi-studio-core-product-directory-catalog-") +
            suffix);
}

void resetRoot(const std::filesystem::path& root) {
    std::error_code error;
    std::filesystem::remove_all(root, error);
}

void writeHostEntry(
    const std::filesystem::path& root,
    const char* directory,
    const std::string& name,
    uint8_t marker
) {
    auto relative = std::filesystem::path(directory);
    if (!relative.empty() && relative.begin()->string() == "library") {
        relative = std::filesystem::path("midi-studio") / relative;
    }
    const auto path = root / relative / name;
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    assert(!error);
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    assert(stream.good());
    stream.put(static_cast<char>(marker));
    stream.close();
    assert(stream.good());
}

bool readMarkerMetadata(
    ProductFileService& files,
    const char* currentPath,
    const char*,
    uint32_t fileSize,
    char* outSemanticName,
    size_t outSemanticNameSize
) {
    if (fileSize != 1U || outSemanticName == nullptr ||
        outSemanticNameSize == 0U) {
        return false;
    }
    uint8_t marker = 0U;
    const auto read = files.read(currentPath, 0U, &marker, 1U);
    if (!read || read.value() != 1U) return false;
    const int written = std::snprintf(
        outSemanticName,
        outSemanticNameSize,
        "Marker %c",
        static_cast<char>(marker)
    );
    return written > 0 &&
           static_cast<size_t>(written) < outSemanticNameSize;
}

ProductDirectoryAssetQuery catalogQuery() {
    return {
        .directory = CATALOG_DIRECTORY,
        .extension = ".cat",
        .generatedIdPrefix = "asset-",
        .maxFileSize = 16U,
        .metadataReader = readMarkerMetadata,
    };
}

void assertPending(const oc::type::Result<void>& result) {
    assert(!result);
    assert(result.error().code == ErrorCode::HARDWARE_BUSY);
}

uint16_t settleCatalog(
    ProductFileService& files,
    ProductDirectoryCatalog& catalog,
    CountingHostFileSystem& filesystem,
    uint32_t& nowMs
) {
    uint16_t turns = 0U;
    while (catalog.pending() &&
           turns < ProductDirectoryCatalog::MAX_ENTRIES + 2U) {
        const uint32_t listsBefore = filesystem.listCalls;
        const uint32_t readsBefore = filesystem.readCalls;
        const uint32_t statsBefore = filesystem.statCalls;
        assert(files.persistenceJobs().beginTurn(nowMs));
        catalog.advance(nowMs, false);
        ++nowMs;
        ++turns;
        assert(filesystem.listCalls - listsBefore <= 1U);
        assert(filesystem.readCalls - readsBefore <= 1U);
        assert(filesystem.statCalls == statsBefore);
    }
    assert(!catalog.pending());
    return turns;
}

void test_catalog_is_bounded_cached_and_playback_gated() {
    const auto root = testRoot("bounded");
    resetRoot(root);

    CountingHostFileSystem filesystem(root.string().c_str());
    assert(filesystem.init());
    ProductFileService files(filesystem);
    assert(files.init());
    ProductDirectoryCatalog catalog(files);

    assert(ProductDirectoryCatalog::RAW_SNAPSHOT_BYTES == 19'456U);
    assert(sizeof(ProductDirectoryCatalog) <= 64U * 1024U);

    writeHostEntry(root, CATALOG_DIRECTORY, "asset-001.cat", 'C');
    writeHostEntry(root, CATALOG_DIRECTORY, "asset-002.cat", 'A');
    writeHostEntry(root, CATALOG_DIRECTORY, "asset-003.cat", 'B');
    writeHostEntry(root, CATALOG_DIRECTORY, "asset-004.CAT", 'D');
    writeHostEntry(root, CATALOG_DIRECTORY, "ignored.tmp", 'X');
    filesystem.resetCounters();

    const auto query = catalogQuery();
    assertPending(catalog.requestAssets(
        query,
        ProductPersistenceJobOwner::STEP_PRESET_CATALOG
    ));
    assert(catalog.pending());
    assert(files.persistenceJobs().depth() == 1U);

    uint32_t nowMs = 1U;
    assert(files.persistenceJobs().beginTurn(nowMs));
    catalog.advance(nowMs, true);
    ++nowMs;
    assert(catalog.pending());
    assert(filesystem.listCalls == 0U);
    assert(filesystem.readCalls == 0U);
    assert(filesystem.statCalls == 0U);

    const uint16_t turns = settleCatalog(
        files,
        catalog,
        filesystem,
        nowMs
    );
    assert(turns == 6U);
    assert(files.persistenceJobs().depth() == 0U);
    assert(files.persistenceJobs().highWater() == 1U);
    assert(filesystem.listCalls == 1U);
    assert(filesystem.readCalls == 3U);
    assert(filesystem.statCalls == 0U);

    uint16_t count = 0U;
    const ProductDirectoryAssetEntry* entries =
        catalog.assetEntries(query, count);
    assert(entries != nullptr);
    assert(count == 3U);
    assert(std::strcmp(entries[0].id, "asset-002") == 0);
    assert(std::strcmp(entries[1].id, "asset-003") == 0);
    assert(std::strcmp(entries[2].id, "asset-001") == 0);
    assert(entries[0].metadataReadable);

    char nextId[ProductDirectoryAssetEntry::ID_SIZE] = {};
    assert(catalog.nextGeneratedId(query, nextId, sizeof(nextId)));
    assert(std::strcmp(nextId, "asset-005") == 0);

    const uint32_t cachedLists = filesystem.listCalls;
    const uint32_t cachedReads = filesystem.readCalls;
    assert(catalog.requestAssets(
        query,
        ProductPersistenceJobOwner::STEP_PRESET_CATALOG
    ));
    assert(catalog.nextGeneratedId(query, nextId, sizeof(nextId)));
    assert(filesystem.listCalls == cachedLists);
    assert(filesystem.readCalls == cachedReads);
    assert(filesystem.statCalls == 0U);

    const uint8_t marker = 'D';
    assert(core::test::writeProductFileFixture(
        files,
        "library/catalog-test/asset-005.cat",
        0U,
        &marker,
        1U
    ));
    filesystem.resetCounters();

    assertPending(catalog.requestAssets(
        query,
        ProductPersistenceJobOwner::STEP_PRESET_CATALOG
    ));
    const uint16_t refreshTurns = settleCatalog(
        files,
        catalog,
        filesystem,
        nowMs
    );
    assert(refreshTurns == 7U);
    entries = catalog.assetEntries(query, count);
    assert(entries != nullptr);
    assert(count == 4U);
    assert(filesystem.listCalls == 1U);
    assert(filesystem.readCalls == 4U);
    assert(filesystem.statCalls == 0U);
    assert(catalog.nextGeneratedId(query, nextId, sizeof(nextId)));
    assert(std::strcmp(nextId, "asset-006") == 0);

    resetRoot(root);
    std::cout
        << "[PASS] bounded catalog cache, playback gate and invalidation\n";
}

void test_entry_257_fails_without_publishing_a_partial_snapshot() {
    const auto root = testRoot("overflow");
    resetRoot(root);

    CountingHostFileSystem filesystem(root.string().c_str());
    assert(filesystem.init());
    ProductFileService files(filesystem);
    assert(files.init());
    ProductDirectoryCatalog catalog(files);

    for (uint16_t index = 0U;
         index <= ProductDirectoryCatalog::MAX_ENTRIES;
         ++index) {
        char name[32] = {};
        const int written = std::snprintf(
            name,
            sizeof(name),
            "entry-%03u.bin",
            static_cast<unsigned>(index)
        );
        assert(written > 0 && static_cast<size_t>(written) < sizeof(name));
        writeHostEntry(root, OVERFLOW_DIRECTORY, name, 0x5AU);
    }
    filesystem.resetCounters();

    assertPending(catalog.requestRaw(
        OVERFLOW_DIRECTORY,
        ProductPersistenceJobOwner::PROJECT_CATALOG
    ));
    uint32_t nowMs = 1U;
    const uint16_t turns = settleCatalog(
        files,
        catalog,
        filesystem,
        nowMs
    );
    assert(turns == 1U);
    assert(filesystem.listCalls == 1U);
    assert(filesystem.readCalls == 0U);
    assert(filesystem.statCalls == 0U);
    assert(files.persistenceJobs().depth() == 0U);

    uint16_t count = 99U;
    assert(catalog.rawEntries(OVERFLOW_DIRECTORY, count) == nullptr);
    assert(count == 0U);
    const auto repeated = catalog.requestRaw(
        OVERFLOW_DIRECTORY,
        ProductPersistenceJobOwner::PROJECT_CATALOG
    );
    assert(!repeated);
    assert(repeated.error().code == ErrorCode::RESOURCE_EXHAUSTED);
    assert(filesystem.listCalls == 1U);

    resetRoot(root);
    std::cout << "[PASS] entry 257 rejects the complete catalog\n";
}

}  // namespace

int main() {
    test_catalog_is_bounded_cached_and_playback_gated();
    test_entry_257_fails_without_publishing_a_partial_snapshot();
    std::cout << "[PASS] ProductDirectoryCatalog tests\n";
    return 0;
}
