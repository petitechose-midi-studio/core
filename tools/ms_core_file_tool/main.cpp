#include <algorithm>
#include <cstdint>
#include <cwctype>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <new>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#ifdef ERROR
#undef ERROR
#endif
#endif

#include "persistence/ProjectFileLimits.hpp"
#include "persistence/ProjectFileInspection.hpp"
#include "persistence/SequencerGraphAssetCodec.hpp"
#include "StepGraphPresetTool.hpp"

namespace {

namespace inspection = core::persistence::project_file_inspection;
namespace project_file = core::persistence::project_file;
namespace step_graph_asset_codec =
    core::persistence::sequencer_graph_asset_codec;
namespace step_graph_preset_tool = core::tools::ms_core_file_tool;

enum class ReadFileStatus {
    OK,
    IO_ERROR,
    TOO_LARGE,
};

struct Args {
    std::string command;
    std::string inputPath;
    std::string outputPath;
    std::string semanticName;
    bool json = false;
};

const char* loadStatusName(project_file::LoadStatus status) {
    switch (status) {
        case project_file::LoadStatus::OK:
            return "ok";
        case project_file::LoadStatus::INSPECTION_ISSUES:
            return "inspection_issues";
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
        case project_file::LoadCode::MISSING_REQUIRED_CHUNK:
            return "missing_required_chunk";
        case project_file::LoadCode::UNEXPECTED_CHUNK:
            return "unexpected_chunk";
        case project_file::LoadCode::UNSUPPORTED_CHUNK_FLAGS:
            return "unsupported_chunk_flags";
        case project_file::LoadCode::OUTPUT_CAPACITY_EXCEEDED:
            return "output_capacity_exceeded";
        case project_file::LoadCode::UNSUPPORTED_CHUNK_VERSION:
            return "unsupported_chunk_version";
        default:
            return "unknown";
    }
}

std::filesystem::path pathFromUtf8(const std::string& text) {
    return std::filesystem::path(reinterpret_cast<const char8_t*>(text.c_str()));
}

uint32_t inputSizeLimitForCommand(const std::string& command) {
    if (step_graph_preset_tool::isStepGraphPresetCommand(command)) {
        return step_graph_asset_codec::MAX_ENCODED_SIZE;
    }
    return core::persistence::PROJECT_FILE_MAX_SIZE;
}

ReadFileStatus readFile(
    const std::string& path,
    uint32_t maxSize,
    std::vector<uint8_t>& out
) {
    std::ifstream file(pathFromUtf8(path), std::ios::binary | std::ios::ate);
    if (!file) return ReadFileStatus::IO_ERROR;
    const auto end = file.tellg();
    if (end == std::ifstream::pos_type(-1)) return ReadFileStatus::IO_ERROR;
    const auto size = static_cast<std::streamoff>(end);
    if (size < 0) return ReadFileStatus::IO_ERROR;
    if (static_cast<uint64_t>(size) > maxSize) {
        return ReadFileStatus::TOO_LARGE;
    }
    file.seekg(0, std::ios::beg);
    if (!file) return ReadFileStatus::IO_ERROR;
    out.resize(static_cast<size_t>(size));
    if (out.empty()) return ReadFileStatus::OK;
    if (!file.read(
            reinterpret_cast<char*>(out.data()),
            static_cast<std::streamsize>(size)
        )) {
        return ReadFileStatus::IO_ERROR;
    }
    return ReadFileStatus::OK;
}

bool writeFile(const std::string& path, const uint8_t* data, uint32_t size) {
    std::ofstream file(pathFromUtf8(path), std::ios::binary | std::ios::trunc);
    if (!file) return false;
    if (size == 0) return true;
    file.write(reinterpret_cast<const char*>(data), size);
    return static_cast<bool>(file);
}

void printJsonReport(const inspection::Result& result,
                     const project_file::LoadReport& report,
                     const char* operation) {
    std::cout << "{";
    std::cout << "\"operation\":\"" << operation << "\",";
    std::cout << "\"fileKind\":\"project\",";
    std::cout << "\"status\":\"" << inspection::statusName(result.status) << "\",";
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

void printTextReport(const inspection::Result& result,
                     const project_file::LoadReport& report,
                     const char* operation) {
    std::cout << operation << ": " << inspection::statusName(result.status)
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

void printReport(const inspection::Result& result,
                 const project_file::LoadReport& report,
                 const Args& args) {
    if (args.json) {
        printJsonReport(result, report, args.command.c_str());
    } else {
        printTextReport(result, report, args.command.c_str());
    }
}

int statusExitCode(inspection::Status status) {
    switch (status) {
        case inspection::Status::CURRENT:
            return 0;
        case inspection::Status::UNSUPPORTED:
            return 2;
        case inspection::Status::FAILED:
        default:
            return 1;
    }
}

void printUsage() {
    std::cerr << "Usage:\n"
              << "  ms-core-file-tool inspect <file.mspj> [--json]\n"
              << "  ms-core-file-tool validate <file.mspj> [--json]\n"
              << "  ms-core-file-tool rewrite <file.mspj> --out <file.mspj> "
                 "[--json]\n"
              << "  ms-core-file-tool inspect-step-graph-preset <file.mssp> [--json]\n"
              << "  ms-core-file-tool validate-step-graph-preset <file.mssp> [--json]\n"
              << "  ms-core-file-tool rename-step-graph-preset <in.mssp> "
                 "--name <semantic-name> --out <staged.mssp> [--json]\n";
}

bool sameOutputPath(const std::string& lhs, const std::string& rhs) {
    std::error_code lhsError;
    std::error_code rhsError;
    const auto lhsPath = std::filesystem::absolute(
        pathFromUtf8(lhs),
        lhsError
    ).lexically_normal();
    const auto rhsPath = std::filesystem::absolute(
        pathFromUtf8(rhs),
        rhsError
    ).lexically_normal();
    if (lhsError || rhsError) return lhs == rhs;
#ifdef _WIN32
    auto lhsText = lhsPath.wstring();
    auto rhsText = rhsPath.wstring();
    const auto lower = [](wchar_t value) {
        return static_cast<wchar_t>(std::towlower(value));
    };
    std::transform(lhsText.begin(), lhsText.end(), lhsText.begin(), lower);
    std::transform(rhsText.begin(), rhsText.end(), rhsText.begin(), lower);
    if (lhsText == rhsText) return true;
#else
    if (lhsPath == rhsPath) return true;
#endif

    // Lexically distinct paths can still name the same existing file through
    // a hard link, symlink, junction, or normalized filesystem alias. Refuse
    // those as output targets before the input is read or any truncating open
    // can occur. Missing destinations are expected and simply compare false.
    std::error_code equivalentError;
    const bool equivalent = std::filesystem::equivalent(
        lhsPath,
        rhsPath,
        equivalentError
    );
    return !equivalentError && equivalent;
}

bool parseArgs(int argc, char** argv, Args& out) {
    if (argc < 3) return false;
    out.command = argv[1];
    out.inputPath = argv[2];
    for (int i = 3; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--json") {
            out.json = true;
        } else if (arg == "--out" && i + 1 < argc) {
            out.outputPath = argv[++i];
        } else if (arg == "--name" && i + 1 < argc) {
            out.semanticName = argv[++i];
        } else {
            return false;
        }
    }
    if (out.command != "inspect" &&
        out.command != "validate" &&
        out.command != "rewrite" &&
        !step_graph_preset_tool::isStepGraphPresetCommand(out.command)) {
        return false;
    }
    const bool renamePreset = out.command == "rename-step-graph-preset";
    if ((out.command == "rewrite" || renamePreset) && out.outputPath.empty()) return false;
    if (out.command != "rewrite" && !renamePreset && !out.outputPath.empty()) return false;
    const bool writesOutput = out.command == "rewrite" || renamePreset;
    if (writesOutput && sameOutputPath(out.inputPath, out.outputPath)) return false;
    if (renamePreset && out.semanticName.empty()) return false;
    if (!renamePreset && !out.semanticName.empty()) return false;
    return true;
}

int allocationFailureExit() {
    std::cerr << "Memory allocation failed\n";
    return 70;
}

int runMain(int argc, char** argv) try {
    Args args;
    if (!parseArgs(argc, argv, args)) {
        printUsage();
        return 64;
    }

    std::vector<uint8_t> input;
    const uint32_t inputSizeLimit = inputSizeLimitForCommand(args.command);
    const ReadFileStatus readStatus = readFile(
        args.inputPath,
        inputSizeLimit,
        input
    );
    if (readStatus == ReadFileStatus::TOO_LARGE) {
        std::cerr << "Input file exceeds command limit of "
                  << inputSizeLimit << " bytes: " << args.inputPath << "\n";
        return 66;
    }
    if (readStatus != ReadFileStatus::OK) {
        std::cerr << "Failed to read input file: " << args.inputPath << "\n";
        return 66;
    }

    if (step_graph_preset_tool::isStepGraphPresetCommand(args.command)) {
        std::vector<uint8_t> output;
        const int exitCode = step_graph_preset_tool::runStepGraphPresetCommand(
            args.command,
            input,
            args.semanticName,
            args.json,
            args.command == "rename-step-graph-preset" ? &output : nullptr
        );
        if (exitCode == 0 && args.command == "rename-step-graph-preset") {
            if (output.empty() ||
                !writeFile(
                    args.outputPath,
                    output.data(),
                    static_cast<uint32_t>(output.size())
                )) {
                std::cerr << "Failed to write staged output file: "
                          << args.outputPath << "\n";
                return 73;
            }
        }
        return exitCode;
    }

    project_file::LoadReport report{};
    inspection::Result result{};
    if (args.command == "rewrite") {
        std::vector<uint8_t> output(core::persistence::PROJECT_FILE_MAX_SIZE);
        result = inspection::rewriteProjectBytes(
            input.data(),
            static_cast<uint32_t>(input.size()),
            output.data(),
            static_cast<uint32_t>(output.size()),
            &report
        );
        if (result.bytesWritten > 0) {
            if (!writeFile(args.outputPath, output.data(), result.bytesWritten)) {
                std::cerr << "Failed to write output file: " << args.outputPath << "\n";
                return 73;
            }
        }
    } else {
        result = inspection::inspectProjectBytes(
            input.data(),
            static_cast<uint32_t>(input.size()),
            &report
        );
    }

    printReport(result, report, args);
    if (args.command == "validate" && result.status != inspection::Status::CURRENT) {
        return statusExitCode(result.status);
    }
    return statusExitCode(result.status);
} catch (const std::bad_alloc&) {
    return allocationFailureExit();
}

#ifdef _WIN32
std::string utf8FromWide(const wchar_t* text) {
    if (text == nullptr) return {};
    const int size = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        text,
        -1,
        nullptr,
        0,
        nullptr,
        nullptr
    );
    if (size <= 0) return {};
    std::string result(static_cast<size_t>(size), '\0');
    if (WideCharToMultiByte(
            CP_UTF8,
            WC_ERR_INVALID_CHARS,
            text,
            -1,
            result.data(),
            size,
            nullptr,
            nullptr
        ) <= 0) {
        return {};
    }
    result.resize(static_cast<size_t>(size - 1));
    return result;
}
#endif

}  // namespace

#ifdef _WIN32
int wmain(int argc, wchar_t** wideArgv) try {
    SetConsoleOutputCP(CP_UTF8);
    std::vector<std::string> storage;
    storage.reserve(static_cast<size_t>(argc));
    std::vector<char*> argv;
    argv.reserve(static_cast<size_t>(argc));
    for (int i = 0; i < argc; ++i) {
        storage.push_back(utf8FromWide(wideArgv[i]));
    }
    for (auto& argument : storage) argv.push_back(argument.data());
    return runMain(argc, argv.data());
} catch (const std::bad_alloc&) {
    return allocationFailureExit();
}
#else
int main(int argc, char** argv) {
    return runMain(argc, argv);
}
#endif
