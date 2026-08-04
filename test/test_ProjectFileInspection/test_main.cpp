#ifdef NDEBUG
#undef NDEBUG
#endif

#include <array>
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>

#include "persistence/ProjectFileInspection.hpp"
#include "persistence/ProjectFileLimits.hpp"
#include "persistence/ProjectSnapshotPersistenceCodec.hpp"
#include "state/project/ProjectSnapshot.hpp"

namespace {

namespace inspection = core::persistence::project_file_inspection;
namespace project_file = core::persistence::project_file;
namespace snapshot_codec = core::persistence::project_snapshot_codec;

using ProjectBytes = std::array<uint8_t, core::persistence::PROJECT_FILE_MAX_SIZE>;

uint32_t encodeDefaultProject(ProjectBytes& bytes) {
    core::state::project::ProjectSnapshot snapshot{};
    assert(snapshot.projectControl);
    const auto encoded = snapshot_codec::encodeProjectSnapshot(
        snapshot,
        bytes.data(),
        static_cast<uint32_t>(bytes.size())
    );
    assert(encoded.status == project_file::Status::OK);
    return encoded.bytesWritten;
}

void testCurrentProjectInspectionAndRewrite() {
    auto input = std::make_unique<ProjectBytes>();
    auto output = std::make_unique<ProjectBytes>();
    assert(input && output);
    const uint32_t size = encodeDefaultProject(*input);

    project_file::LoadReport inspectReport{};
    const auto inspected = inspection::inspectProjectBytes(
        input->data(),
        size,
        &inspectReport
    );
    assert(inspected.status == inspection::Status::CURRENT);
    assert(inspected.loadStatus == project_file::LoadStatus::OK);
    assert(inspected.overwriteSafe);
    assert(inspectReport.ok());

    project_file::LoadReport rewriteReport{};
    const auto rewritten = inspection::rewriteProjectBytes(
        input->data(),
        size,
        output->data(),
        static_cast<uint32_t>(output->size()),
        &rewriteReport
    );
    assert(rewritten.status == inspection::Status::CURRENT);
    assert(rewritten.bytesWritten == size);
    assert(std::equal(
        input->begin(),
        input->begin() + size,
        output->begin()
    ));

    std::cout << "[PASS] current project inspection and canonical rewrite\n";
}

void testInvalidAndTruncatedProjectsAreNeverRewritten() {
    auto input = std::make_unique<ProjectBytes>();
    auto output = std::make_unique<ProjectBytes>();
    assert(input && output);
    const uint32_t size = encodeDefaultProject(*input);
    output->fill(0xA5U);
    const auto before = *output;

    project_file::LoadReport report{};
    const auto truncated = inspection::rewriteProjectBytes(
        input->data(),
        size - 1U,
        output->data(),
        static_cast<uint32_t>(output->size()),
        &report
    );
    assert(truncated.status != inspection::Status::CURRENT);
    assert(!truncated.overwriteSafe);
    assert(truncated.bytesWritten == 0U);
    assert(*output == before);

    const auto invalidOutput = inspection::rewriteProjectBytes(
        input->data(),
        size,
        nullptr,
        0U
    );
    assert(invalidOutput.status == inspection::Status::FAILED);
    assert(invalidOutput.containerStatus == project_file::Status::INVALID_ARGUMENT);

    std::cout << "[PASS] invalid projects are never rewritten\n";
}

void testUnsupportedFormatIsInspectableButNeverPublishedOrRewritten() {
    auto input = std::make_unique<ProjectBytes>();
    auto output = std::make_unique<ProjectBytes>();
    assert(input && output);
    const uint32_t size = encodeDefaultProject(*input);
    (*input)[4] = static_cast<uint8_t>(
        project_file::CONTAINER_VERSION_MAJOR + 1U
    );

    project_file::LoadReport inspectReport{};
    const auto inspected = inspection::inspectProjectBytes(
        input->data(),
        size,
        &inspectReport
    );
    assert(inspected.status == inspection::Status::UNSUPPORTED);
    assert(
        inspected.loadStatus ==
        project_file::LoadStatus::INSPECTION_ISSUES
    );
    assert(!inspected.overwriteSafe);
    assert(
        inspectReport.status ==
        project_file::LoadStatus::INSPECTION_ISSUES
    );

    core::state::project::ProjectSnapshot destination{};
    std::strncpy(
        destination.project.metadata.name.data(),
        "unchanged",
        destination.project.metadata.name.size() - 1U
    );
    project_file::LoadReport decodeReport{};
    const auto decoded = inspection::decodeProjectBytes(
        input->data(),
        size,
        destination,
        &decodeReport
    );
    assert(decoded.status == inspection::Status::FAILED);
    assert(
        std::strcmp(
            destination.project.metadata.name.data(),
            "unchanged"
        ) == 0
    );

    output->fill(0xA5U);
    const auto before = *output;
    project_file::LoadReport rewriteReport{};
    const auto rewritten = inspection::rewriteProjectBytes(
        input->data(),
        size,
        output->data(),
        static_cast<uint32_t>(output->size()),
        &rewriteReport
    );
    assert(rewritten.status == inspection::Status::FAILED);
    assert(rewritten.bytesWritten == 0U);
    assert(*output == before);

    std::cout
        << "[PASS] unsupported format is inspection-only\n";
}

}  // namespace

int main() {
    testCurrentProjectInspectionAndRewrite();
    testInvalidAndTruncatedProjectsAreNeverRewritten();
    testUnsupportedFormatIsInspectableButNeverPublishedOrRewritten();
    std::cout << "All ProjectFileInspection tests passed\n";
    return 0;
}
