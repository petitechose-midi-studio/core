#include "StepGraphPresetTool.hpp"

#include <limits>
#include <iostream>

#include "state/sequencer/SequencerGraphAssetCodec.hpp"

namespace core::tools::ms_core_file_tool {

namespace {

namespace sequencer = core::state::sequencer;

const char* statusName(sequencer::SequencerGraphAssetStatus status) {
    using Status = sequencer::SequencerGraphAssetStatus;
    switch (status) {
        case Status::OK:
            return "ok";
        case Status::INVALID_ARGUMENT:
            return "invalid_argument";
        case Status::INVALID_FORMAT:
            return "invalid_format";
        case Status::UNSUPPORTED_VERSION:
            return "unsupported_version";
        case Status::INCOMPATIBLE_TARGET:
            return "incompatible_target";
        case Status::GRAPH_LIMIT_REACHED:
            return "graph_limit_reached";
        case Status::BUFFER_TOO_SMALL:
            return "buffer_too_small";
        default:
            return "unknown";
    }
}

bool reportHas(const sequencer::SequencerGraphAssetReport& report, uint16_t flag) {
    return (report.flags & flag) != 0;
}

void printJsonReport(
    const std::string& command,
    const sequencer::SequencerGraphAssetStatus status,
    const sequencer::SequencerGraphAssetReport& report,
    const sequencer::SequencerStepGraphPreset& preset
) {
    std::cout << "{";
    std::cout << "\"operation\":\"" << command << "\",";
    std::cout << "\"fileKind\":\"step_graph_preset\",";
    std::cout << "\"status\":\"" << statusName(status) << "\",";
    std::cout << "\"rootContext\":" << (preset.rootContext ? "true" : "false") << ",";
    std::cout << "\"rootValues\":" << (preset.rootValuesValid ? "true" : "false") << ",";
    std::cout << "\"stepNodeCount\":" << report.stepNodeCount << ",";
    std::cout << "\"sequenceCount\":" << static_cast<uint32_t>(report.sequenceCount) << ",";
    std::cout << "\"cycleSetCount\":" << static_cast<uint32_t>(report.cycleSetCount) << ",";
    std::cout << "\"flags\":{";
    std::cout << "\"rootValues\":"
              << (reportHas(report, sequencer::SEQUENCER_GRAPH_ASSET_REPORT_ROOT_VALUES)
                      ? "true"
                      : "false")
              << ",";
    std::cout << "\"graphPayload\":"
              << (reportHas(report, sequencer::SEQUENCER_GRAPH_ASSET_REPORT_GRAPH_PAYLOAD)
                      ? "true"
                      : "false")
              << ",";
    std::cout << "\"overwrite\":"
              << (reportHas(report, sequencer::SEQUENCER_GRAPH_ASSET_REPORT_OVERWRITE)
                      ? "true"
                      : "false");
    std::cout << "}}\n";
}

void printTextReport(
    const std::string& command,
    const sequencer::SequencerGraphAssetStatus status,
    const sequencer::SequencerGraphAssetReport& report,
    const sequencer::SequencerStepGraphPreset& preset
) {
    std::cout << command << ": " << statusName(status)
              << " kind=step_graph_preset"
              << " rootContext=" << (preset.rootContext ? "true" : "false")
              << " rootValues=" << (preset.rootValuesValid ? "true" : "false")
              << " stepNodes=" << report.stepNodeCount
              << " sequences=" << static_cast<uint32_t>(report.sequenceCount)
              << " cycleSets=" << static_cast<uint32_t>(report.cycleSetCount)
              << "\n";
}

int statusExitCode(sequencer::SequencerGraphAssetStatus status) {
    return status == sequencer::SequencerGraphAssetStatus::OK ? 0 : 1;
}

}  // namespace

bool isStepGraphPresetCommand(const std::string& command) {
    return command == "inspect-step-graph-preset" ||
           command == "validate-step-graph-preset";
}

int runStepGraphPresetCommand(
    const std::string& command,
    const std::vector<uint8_t>& input,
    bool json
) {
    sequencer::SequencerStepGraphPreset preset{};
    sequencer::SequencerGraphAssetReport report{};
    sequencer::SequencerGraphAssetStatus status = sequencer::SequencerGraphAssetStatus::OK;
    if (input.size() > std::numeric_limits<uint16_t>::max()) {
        status = sequencer::SequencerGraphAssetStatus::INVALID_ARGUMENT;
        report.status = status;
    } else if (!sequencer::decodeStepGraphPreset(
            input.data(),
            static_cast<uint16_t>(input.size()),
            preset,
            &report
        )) {
        status = report.status;
    }

    if (json) {
        printJsonReport(command, status, report, preset);
    } else {
        printTextReport(command, status, report, preset);
    }
    return statusExitCode(status);
}

}  // namespace core::tools::ms_core_file_tool
