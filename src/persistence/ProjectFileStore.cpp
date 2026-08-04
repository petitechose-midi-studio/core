#include "persistence/ProjectFileStore.hpp"

#include <cstring>

#include <config/PlatformCompat.hpp>

#include "persistence/ProjectFileTransactions.hpp"
#include "persistence/ProductFilePath.hpp"
#include "state/project/ProjectSlug.hpp"

namespace core::persistence {

namespace {

using oc::type::ErrorCode;

}  // namespace

FLASHMEM ProjectFileStore::ProjectFileStore(
    ProductFileService& files,
    ProductDirectoryCatalog& catalog
) : files_(files), catalog_(catalog) {}

FLASHMEM bool ProjectFileStore::validProjectId_(const char* projectId) {
    return core::state::project::validProjectSlug(projectId);
}

FLASHMEM bool ProjectFileStore::buildPaths_(const char* projectId, ProjectPaths& out) {
    if (!validProjectId_(projectId)) return false;
    return copyProductRelativePath(out.directory, sizeof(out.directory), "projects") &&
           formatProductRelativePath(
               out.current,
               sizeof(out.current),
               "projects/%s.mspj",
               projectId
           ) &&
           formatProductRelativePath(
               out.backup,
               sizeof(out.backup),
               "projects/%s.mspj.bak",
               projectId
           ) &&
           formatProductRelativePath(
               out.tmp,
               sizeof(out.tmp),
               "tmp/%s.mspj.tmp",
               projectId
           );
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#elif defined(_MSC_VER)
__declspec(noinline)
#endif
FLASHMEM oc::type::Result<ProjectSaveResult> ProjectFileStore::saveWithPaths_(
    ProductFileService& files,
    ProjectFileWriteWorkspace& workspace,
    const core::state::project::ProjectSnapshot& snapshot,
    const ProjectPaths& paths
) {
    // ProjectPaths owns the large bounded path buffers in the caller. Keep the
    // lease-bearing transaction in this sequential cold frame so increasing
    // its exact ABI does not increase the caller's peak DTCM stack usage.
    ProjectSaveTransaction transaction(files, workspace);
    return project_file_transactions::saveToCompletion(
        transaction,
        snapshot,
        {
            .directory = paths.directory,
            .current = paths.current,
            .backup = paths.backup,
            .tmp = paths.tmp,
        }
    );
}

FLASHMEM oc::type::Result<ProjectSaveResult> ProjectFileStore::save(
    const core::state::project::ProjectSnapshot& snapshot
) {
    if (std::strcmp(
            snapshot.project.metadata.id.data(),
            snapshot.project.metadata.name.data()
        ) != 0) {
        return oc::type::Result<ProjectSaveResult>::err(
            {ErrorCode::INVALID_ARGUMENT, "project slug mismatch"}
        );
    }

    ProjectPaths paths{};
    if (!buildPaths_(snapshot.project.metadata.id.data(), paths)) {
        return oc::type::Result<ProjectSaveResult>::err(
            {ErrorCode::INVALID_ARGUMENT, "invalid project id"}
        );
    }

    return saveWithPaths_(files_, workspace_, snapshot, paths);
}

FLASHMEM oc::type::Result<ProjectLoadResult> ProjectFileStore::load(
    const char* projectId,
    core::state::project::ProjectSnapshot& out,
    core::persistence::project_file::LoadReport* report
) {
    ProjectPaths paths{};
    if (!buildPaths_(projectId, paths)) {
        return oc::type::Result<ProjectLoadResult>::err(
            {ErrorCode::INVALID_ARGUMENT, "invalid project id"}
        );
    }

    return project_file_transactions::loadWithBackup(
        files_, workspace_, paths.current, paths.backup, out, report
    );
}

FLASHMEM oc::type::Result<ProjectListResult> ProjectFileStore::listProjects(
    ProjectListEntry* entries,
    uint8_t capacity
) {
    if (entries == nullptr && capacity > 0) {
        return oc::type::Result<ProjectListResult>::err(
            {ErrorCode::INVALID_ARGUMENT, "invalid project list buffer"}
        );
    }
    if (capacity == 0) {
        return oc::type::Result<ProjectListResult>::ok(ProjectListResult{});
    }

    for (uint8_t i = 0; i < capacity; ++i) {
        entries[i] = ProjectListEntry{};
    }

    const auto ready = catalog_.requestRaw(
        "projects",
        ProductPersistenceJobOwner::PROJECT_CATALOG
    );
    if (!ready) return oc::type::Result<ProjectListResult>::err(ready.error());

    uint16_t rawCount = 0U;
    const auto* rawEntries = catalog_.rawEntries("projects", rawCount);
    if (rawEntries == nullptr) {
        return oc::type::Result<ProjectListResult>::err(
            {ErrorCode::HARDWARE_BUSY, "project catalog not ready"}
        );
    }

    ProjectListResult result{};
    constexpr const char* extension = core::state::project::PROJECT_FILE_EXTENSION;
    constexpr size_t extensionLength =
        core::state::project::PROJECT_FILE_EXTENSION_LENGTH;
    for (uint16_t index = 0U; index < rawCount; ++index) {
        const auto& entry = rawEntries[index];
        if (entry.type != oc::interface::FileType::FILE || entry.nameTruncated ||
            entry.sizeBytes == 0U || entry.sizeBytes > MAX_PROJECT_FILE_SIZE) {
            continue;
        }
        const size_t nameLength = std::strlen(entry.name);
        if (nameLength <= extensionLength ||
            std::strcmp(entry.name + nameLength - extensionLength, extension) != 0) {
            continue;
        }
        char projectId[core::state::project::ProjectMetadata::ID_SIZE] = {};
        const size_t slugLength = nameLength - extensionLength;
        if (slugLength >= sizeof(projectId)) continue;
        std::memcpy(projectId, entry.name, slugLength);
        projectId[slugLength] = '\0';
        if (!validProjectId_(projectId)) continue;

        ProjectListEntry candidate{};
        std::strncpy(candidate.id, projectId, sizeof(candidate.id) - 1U);
        candidate.sizeBytes = entry.sizeBytes;
        uint8_t insert = result.count;
        while (insert > 0U &&
               std::strcmp(entries[insert - 1U].id, candidate.id) > 0) {
            if (insert < capacity) {
                entries[insert] = entries[insert - 1U];
            }
            --insert;
        }
        if (result.count < capacity) {
            entries[insert] = candidate;
            ++result.count;
        } else {
            result.truncated = true;
            if (insert < capacity) entries[insert] = candidate;
        }
    }
    return oc::type::Result<ProjectListResult>::ok(result);
}

FLASHMEM oc::type::Result<void> ProjectFileStore::nextProjectId(
    char* out,
    size_t outSize
) {
    if (out == nullptr || outSize == 0U) {
        return oc::type::Result<void>::err(
            {ErrorCode::INVALID_ARGUMENT, "invalid next project id buffer"}
        );
    }
    const auto ready = catalog_.requestRaw(
        "projects",
        ProductPersistenceJobOwner::PROJECT_CATALOG
    );
    if (!ready) return ready;
    return catalog_.nextGeneratedId(
        "projects",
        "p",
        core::state::project::PROJECT_FILE_EXTENSION,
        out,
        outSize
    );
}

}  // namespace core::persistence
