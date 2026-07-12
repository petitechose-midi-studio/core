#include "persistence/ProjectFileStore.hpp"

#include <cstring>

#include <config/PlatformCompat.hpp>

#include "persistence/ProjectFileTransactions.hpp"
#include "persistence/ProductFilePath.hpp"
#include "state/project/ProjectSlug.hpp"

namespace core::persistence {

namespace {

using oc::type::ErrorCode;

struct ProjectListContext {
    ProjectFileStore* store = nullptr;
    ProjectListEntry* entries = nullptr;
    uint8_t capacity = 0;
    ProjectListResult result{};
};

}  // namespace

FLASHMEM ProjectFileStore::ProjectFileStore(ProductFileService& files)
    : files_(files) {}

FLASHMEM bool ProjectFileStore::listProjectsVisitor_(
    const oc::interface::DirectoryEntry& entry,
    void* context
) {
    auto* list = static_cast<ProjectListContext*>(context);
    if (!list || !list->store || !list->entries) return false;
    if (entry.type != oc::interface::FileType::FILE || entry.nameTruncated) return true;

    constexpr const char* extension = core::state::project::PROJECT_FILE_EXTENSION;
    constexpr size_t extensionLength = core::state::project::PROJECT_FILE_EXTENSION_LENGTH;
    const size_t nameLength = std::strlen(entry.name);
    if (nameLength <= extensionLength) return true;
    if (std::strcmp(entry.name + nameLength - extensionLength, extension) != 0) return true;

    char projectId[core::state::project::ProjectMetadata::ID_SIZE] = {};
    const size_t slugLength = nameLength - extensionLength;
    if (slugLength >= sizeof(projectId)) return true;
    std::memcpy(projectId, entry.name, slugLength);
    projectId[slugLength] = '\0';
    if (!validProjectId_(projectId)) return true;

    ProjectPaths paths{};
    if (!buildPaths_(projectId, paths)) return true;

    auto info = list->store->files_.stat(paths.current);
    if (!info || info.value().type != oc::interface::FileType::FILE) return true;
    if (info.value().sizeBytes == 0 || info.value().sizeBytes > MAX_PROJECT_FILE_SIZE) {
        return true;
    }

    if (list->result.count >= list->capacity) {
        list->result.truncated = true;
        return true;
    }

    auto& target = list->entries[list->result.count++];
    std::strncpy(target.id, projectId, sizeof(target.id) - 1U);
    target.id[sizeof(target.id) - 1U] = '\0';
    target.sizeBytes = info.value().sizeBytes;
    return true;
}

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

    ProjectSaveTransaction transaction(files_, workspace_);
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

    ProjectListContext context{this, entries, capacity, ProjectListResult{}};
    auto listed = files_.list("projects", listProjectsVisitor_, &context);
    if (!listed) {
        return oc::type::Result<ProjectListResult>::err(listed.error());
    }
    for (uint8_t i = 1; i < context.result.count; ++i) {
        ProjectListEntry current = entries[i];
        uint8_t insert = i;
        while (insert > 0 && std::strcmp(entries[insert - 1U].id, current.id) > 0) {
            entries[insert] = entries[insert - 1U];
            --insert;
        }
        entries[insert] = current;
    }
    return oc::type::Result<ProjectListResult>::ok(context.result);
}

}  // namespace core::persistence
