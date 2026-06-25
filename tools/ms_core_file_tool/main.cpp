#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "persistence/ProjectFileLimits.hpp"
#include "persistence/ProjectMigration.hpp"
#include "StepGraphPresetTool.hpp"

namespace {

namespace migration = core::persistence::project_file_migration;
namespace project_file = core::persistence::project_file;
namespace step_graph_preset_tool = core::tools::ms_core_file_tool;

struct Args {
    std::string command;
    std::string inputPath;
    std::string outputPath;
    bool json = false;
    bool allowPartial = false;
};

const char* loadStatusName(project_file::LoadStatus status) {
    switch (status) {
        case project_file::LoadStatus::OK:
            return "ok";
        case project_file::LoadStatus::MIGRATED:
            return "migrated";
        case project_file::LoadStatus::PARTIAL:
            return "partial";
        case project_file::LoadStatus::FAILED:
            return "failed";
        default:
            return "unknown";
    }
}

const char* containerStatusName(project_file::Status status) {
    switch (status) {
        case project_file::Status::OK:
            return "ok";
        case project_file::Status::INVALID_ARGUMENT:
            return "invalid_argument";
        case project_file::Status::BUFFER_TOO_SMALL:
            return "buffer_too_small";
        case project_file::Status::TOO_MANY_CHUNKS:
            return "too_many_chunks";
        case project_file::Status::INVALID_CONTAINER:
            return "invalid_container";
        case project_file::Status::OUTPUT_CAPACITY_EXCEEDED:
            return "output_capacity_exceeded";
        case project_file::Status::SCRATCH_ALLOCATION_FAILED:
            return "scratch_allocation_failed";
        default:
            return "unknown";
    }
}

const char* severityName(project_file::LoadSeverity severity) {
    switch (severity) {
        case project_file::LoadSeverity::INFO:
            return "info";
        case project_file::LoadSeverity::WARNING:
            return "warning";
        case project_file::LoadSeverity::ERROR:
            return "error";
        case project_file::LoadSeverity::FATAL:
            return "fatal";
        default:
            return "unknown";
    }
}

const char* codeName(project_file::LoadCode code) {
    switch (code) {
        case project_file::LoadCode::OK:
            return "ok";
        case project_file::LoadCode::BUFFER_TOO_SMALL:
            return "buffer_too_small";
        case project_file::LoadCode::INVALID_MAGIC:
            return "invalid_magic";
        case project_file::LoadCode::INVALID_HEADER:
            return "invalid_header";
        case project_file::LoadCode::UNSUPPORTED_CONTAINER_VERSION:
            return "unsupported_container_version";
        case project_file::LoadCode::TOO_MANY_CHUNKS:
            return "too_many_chunks";
        case project_file::LoadCode::CHUNK_DIRECTORY_INVALID:
            return "chunk_directory_invalid";
        case project_file::LoadCode::CHUNK_OUT_OF_BOUNDS:
            return "chunk_out_of_bounds";
        case project_file::LoadCode::CHUNK_CRC_MISMATCH:
            return "chunk_crc_mismatch";
        case project_file::LoadCode::CHUNK_PAYLOAD_INVALID:
            return "chunk_payload_invalid";
        case project_file::LoadCode::UNKNOWN_CHUNK:
            return "unknown_chunk";
        case project_file::LoadCode::DUPLICATE_CHUNK:
            return "duplicate_chunk";
        case project_file::LoadCode::OUTPUT_CAPACITY_EXCEEDED:
            return "output_capacity_exceeded";
        case project_file::LoadCode::MISSING_OPTIONAL_CHUNK:
            return "missing_optional_chunk";
        case project_file::LoadCode::DEFAULTED_CHUNK:
            return "defaulted_chunk";
        case project_file::LoadCode::UNSUPPORTED_CHUNK_VERSION:
            return "unsupported_chunk_version";
        case project_file::LoadCode::MIGRATED_CHUNK:
            return "migrated_chunk";
        default:
            return "unknown";
    }
}

bool readFile(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return false;
    const auto size = file.tellg();
    if (size < 0) return false;
    file.seekg(0, std::ios::beg);
    out.resize(static_cast<size_t>(size));
    if (out.empty()) return true;
    return static_cast<bool>(file.read(reinterpret_cast<char*>(out.data()), size));
}

bool writeFile(const std::string& path, const uint8_t* data, uint32_t size) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) return false;
    if (size == 0) return true;
    file.write(reinterpret_cast<const char*>(data), size);
    return static_cast<bool>(file);
}

void printJsonReport(const migration::Result& result,
                     const project_file::LoadReport& report,
                     const char* operation) {
    std::cout << "{";
    std::cout << "\"operation\":\"" << operation << "\",";
    std::cout << "\"fileKind\":\"project\",";
    std::cout << "\"status\":\"" << migration::statusName(result.status) << "\",";
    std::cout << "\"loadStatus\":\"" << loadStatusName(result.loadStatus) << "\",";
    std::cout << "\"containerStatus\":\"" << containerStatusName(result.containerStatus) << "\",";
    std::cout << "\"overwriteSafe\":" << (result.overwriteSafe ? "true" : "false") << ",";
    std::cout << "\"hasUnknownUnsupportedData\":"
              << (report.hasUnknownUnsupportedData ? "true" : "false") << ",";
    std::cout << "\"bytesWritten\":" << result.bytesWritten << ",";
    std::cout << "\"items\":[";
    for (uint8_t i = 0; i < report.itemCount; ++i) {
        const auto& item = report.items[i];
        if (i > 0) std::cout << ",";
        std::cout << "{";
        std::cout << "\"severity\":\"" << severityName(item.severity) << "\",";
        std::cout << "\"code\":\"" << codeName(item.code) << "\",";
        std::cout << "\"chunkId\":" << item.chunkId << ",";
        std::cout << "\"sourceMajor\":" << static_cast<uint32_t>(item.sourceMajor) << ",";
        std::cout << "\"sourceMinor\":" << static_cast<uint32_t>(item.sourceMinor) << ",";
        std::cout << "\"targetMajor\":" << static_cast<uint32_t>(item.targetMajor) << ",";
        std::cout << "\"targetMinor\":" << static_cast<uint32_t>(item.targetMinor);
        std::cout << "}";
    }
    std::cout << "]}\n";
}

void printTextReport(const migration::Result& result,
                     const project_file::LoadReport& report,
                     const char* operation) {
    std::cout << operation << ": " << migration::statusName(result.status)
              << " load=" << loadStatusName(result.loadStatus)
              << " container=" << containerStatusName(result.containerStatus)
              << " overwriteSafe=" << (result.overwriteSafe ? "true" : "false")
              << " bytesWritten=" << result.bytesWritten << "\n";
    for (uint8_t i = 0; i < report.itemCount; ++i) {
        const auto& item = report.items[i];
        std::cout << "  - " << severityName(item.severity)
                  << " " << codeName(item.code)
                  << " chunk=" << item.chunkId
                  << " source=" << static_cast<uint32_t>(item.sourceMajor)
                  << "." << static_cast<uint32_t>(item.sourceMinor)
                  << " target=" << static_cast<uint32_t>(item.targetMajor)
                  << "." << static_cast<uint32_t>(item.targetMinor)
                  << "\n";
    }
}

void printReport(const migration::Result& result,
                 const project_file::LoadReport& report,
                 const Args& args) {
    if (args.json) {
        printJsonReport(result, report, args.command.c_str());
    } else {
        printTextReport(result, report, args.command.c_str());
    }
}

int statusExitCode(migration::Status status) {
    switch (status) {
        case migration::Status::CURRENT:
        case migration::Status::MIGRATED:
            return 0;
        case migration::Status::PARTIAL:
            return 2;
        case migration::Status::FAILED:
        default:
            return 1;
    }
}

void printUsage() {
    std::cerr << "Usage:\n"
              << "  ms-core-file-tool inspect <file.mspj> [--json]\n"
              << "  ms-core-file-tool validate <file.mspj> [--json]\n"
              << "  ms-core-file-tool migrate <file.mspj> --out <file.mspj> "
                 "[--json] [--allow-partial]\n"
              << "  ms-core-file-tool inspect-step-graph-preset <file.msgp> [--json]\n"
              << "  ms-core-file-tool validate-step-graph-preset <file.msgp> [--json]\n";
}

bool parseArgs(int argc, char** argv, Args& out) {
    if (argc < 3) return false;
    out.command = argv[1];
    out.inputPath = argv[2];
    for (int i = 3; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--json") {
            out.json = true;
        } else if (arg == "--allow-partial") {
            out.allowPartial = true;
        } else if (arg == "--out" && i + 1 < argc) {
            out.outputPath = argv[++i];
        } else {
            return false;
        }
    }
    if (out.command != "inspect" &&
        out.command != "validate" &&
        out.command != "migrate" &&
        !step_graph_preset_tool::isStepGraphPresetCommand(out.command)) {
        return false;
    }
    if (out.command == "migrate" && out.outputPath.empty()) return false;
    if (out.command != "migrate" && !out.outputPath.empty()) return false;
    if (step_graph_preset_tool::isStepGraphPresetCommand(out.command) && out.allowPartial) {
        return false;
    }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    Args args;
    if (!parseArgs(argc, argv, args)) {
        printUsage();
        return 64;
    }

    std::vector<uint8_t> input;
    if (!readFile(args.inputPath, input)) {
        std::cerr << "Failed to read input file: " << args.inputPath << "\n";
        return 66;
    }

    if (step_graph_preset_tool::isStepGraphPresetCommand(args.command)) {
        return step_graph_preset_tool::runStepGraphPresetCommand(
            args.command,
            input,
            args.json
        );
    }

    project_file::LoadReport report{};
    migration::Result result{};
    if (args.command == "migrate") {
        std::vector<uint8_t> output(core::persistence::PROJECT_FILE_MAX_SIZE);
        result = migration::migrateProjectBytesToCurrent(
            input.data(),
            static_cast<uint32_t>(input.size()),
            output.data(),
            static_cast<uint32_t>(output.size()),
            &report,
            {.allowPartialOutput = args.allowPartial}
        );
        if (result.bytesWritten > 0) {
            if (!writeFile(args.outputPath, output.data(), result.bytesWritten)) {
                std::cerr << "Failed to write output file: " << args.outputPath << "\n";
                return 73;
            }
        }
    } else {
        result = migration::inspectProjectBytes(
            input.data(),
            static_cast<uint32_t>(input.size()),
            &report
        );
    }

    printReport(result, report, args);
    if (args.command == "validate" && result.status != migration::Status::CURRENT) {
        return statusExitCode(result.status);
    }
    return statusExitCode(result.status);
}
