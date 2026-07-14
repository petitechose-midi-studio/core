#include "StepGraphPresetTool.hpp"

#include <algorithm>
#include <cstring>
#include <iomanip>
#include <limits>
#include <iostream>
#include <sstream>
#include <utility>

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
        case Status::RESOURCE_EXHAUSTED:
            return "resource_exhausted";
        default:
            return "unknown";
    }
}

const char* defaultScalePolicyName(
    sequencer::SequencerStepGraphPreset::ScalePolicy policy
) {
    return policy == sequencer::SequencerStepGraphPreset::ScalePolicy::SCALE_RELATIVE
        ? "scale_relative"
        : "chromatic";
}

const char* scalePolicyName(const sequencer::SequencerStepGraphPreset& preset) {
    return preset.mixedPitchPolicy
        ? "mixed"
        : defaultScalePolicyName(preset.scalePolicy);
}

const char* compatibilityName(
    sequencer::SequencerGraphAssetStatus status,
    const sequencer::SequencerStepGraphPreset& preset
) {
    if (status == sequencer::SequencerGraphAssetStatus::UNSUPPORTED_VERSION) {
        return "unsupported_version";
    }
    if (status != sequencer::SequencerGraphAssetStatus::OK) return "blocked_invalid";
    if (preset.metadataDefaulted) return "warning_legacy_defaulted";
    return preset.mixedPitchPolicy ? "ready_mixed" : "ready";
}

void printJsonString(const char* text) {
    std::cout << '"';
    if (text != nullptr) {
        const auto* cursor = reinterpret_cast<const unsigned char*>(text);
        while (*cursor != 0) {
            const unsigned char byte = *cursor++;
            switch (byte) {
                case '"': std::cout << "\\\""; break;
                case '\\': std::cout << "\\\\"; break;
                case '\b': std::cout << "\\b"; break;
                case '\f': std::cout << "\\f"; break;
                case '\n': std::cout << "\\n"; break;
                case '\r': std::cout << "\\r"; break;
                case '\t': std::cout << "\\t"; break;
                default:
                    if (byte < 0x20U) {
                        std::ostringstream escaped;
                        escaped << "\\u" << std::hex << std::uppercase
                                << std::setw(4) << std::setfill('0')
                                << static_cast<unsigned>(byte);
                        std::cout << escaped.str();
                    } else {
                        std::cout << static_cast<char>(byte);
                    }
                    break;
            }
        }
    }
    std::cout << '"';
}

bool reportHas(const sequencer::SequencerGraphAssetReport& report, uint16_t flag) {
    return (report.flags & flag) != 0;
}

void printJsonReport(
    const std::string& command,
    const sequencer::SequencerGraphAssetStatus status,
    const sequencer::SequencerGraphAssetReport& report,
    const sequencer::SequencerStepGraphPreset& preset,
    uint32_t bytesWritten
) {
    std::cout << "{";
    std::cout << "\"operation\":";
    printJsonString(command.c_str());
    std::cout << ",";
    std::cout << "\"fileKind\":\"step_graph_preset\",";
    std::cout << "\"status\":\"" << statusName(status) << "\",";
    std::cout << "\"compatibility\":\"" << compatibilityName(status, preset) << "\",";
    std::cout << "\"formatVersion\":" << static_cast<uint32_t>(preset.formatVersion) << ",";
    std::cout << "\"technicalId\":";
    printJsonString(preset.technicalId);
    std::cout << ",\"semanticName\":";
    printJsonString(preset.semanticName);
    std::cout << ",";
    std::cout << "\"metadataDefaulted\":"
              << (preset.metadataDefaulted ? "true" : "false") << ",";
    std::cout << "\"mixedPitchPolicy\":"
              << (preset.mixedPitchPolicy ? "true" : "false") << ",";
    std::cout << "\"scalePolicy\":\"" << scalePolicyName(preset) << "\",";
    std::cout << "\"defaultScalePolicy\":\""
              << defaultScalePolicyName(preset.scalePolicy) << "\",";
    std::cout << "\"sourceScale\":{";
    std::cout << "\"root\":" << static_cast<uint32_t>(preset.sourceScale.root) << ",";
    std::cout << "\"type\":"
              << static_cast<uint32_t>(preset.sourceScale.type) << ",";
    std::cout << "\"mode\":"
              << static_cast<uint32_t>(preset.sourceScale.mode) << "},";
    std::cout << "\"rootContext\":" << (preset.rootContext ? "true" : "false") << ",";
    std::cout << "\"rootValues\":" << (preset.rootValuesValid ? "true" : "false") << ",";
    std::cout << "\"stepNodeCount\":" << report.stepNodeCount << ",";
    std::cout << "\"sequenceCount\":" << static_cast<uint32_t>(report.sequenceCount) << ",";
    std::cout << "\"cycleSetCount\":" << static_cast<uint32_t>(report.cycleSetCount) << ",";
    std::cout << "\"bytesWritten\":" << bytesWritten << ",";
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
                      : "false")
              << ",";
    std::cout << "\"mixedPitchPolicy\":"
              << (reportHas(
                      report,
                      sequencer::SEQUENCER_GRAPH_ASSET_REPORT_PITCH_POLICY_MIXED
                  ) ? "true" : "false");
    std::cout << "}}\n";
}

void printTextReport(
    const std::string& command,
    const sequencer::SequencerGraphAssetStatus status,
    const sequencer::SequencerGraphAssetReport& report,
    const sequencer::SequencerStepGraphPreset& preset,
    uint32_t bytesWritten
) {
    std::cout << command << ": " << statusName(status)
              << " kind=step_graph_preset"
              << " rootContext=" << (preset.rootContext ? "true" : "false")
              << " formatVersion=" << static_cast<uint32_t>(preset.formatVersion)
              << " technicalId=\"" << preset.technicalId << "\""
              << " semanticName=\"" << preset.semanticName << "\""
              << " compatibility=" << compatibilityName(status, preset)
              << " scalePolicy=" << scalePolicyName(preset)
              << " defaultScalePolicy=" << defaultScalePolicyName(preset.scalePolicy)
              << " metadataDefaulted=" << (preset.metadataDefaulted ? "true" : "false")
              << " mixedPitchPolicy=" << (preset.mixedPitchPolicy ? "true" : "false")
              << " rootValues=" << (preset.rootValuesValid ? "true" : "false")
              << " stepNodes=" << report.stepNodeCount
              << " sequences=" << static_cast<uint32_t>(report.sequenceCount)
              << " cycleSets=" << static_cast<uint32_t>(report.cycleSetCount)
              << " bytesWritten=" << bytesWritten
              << "\n";
}

int statusExitCode(sequencer::SequencerGraphAssetStatus status) {
    return status == sequencer::SequencerGraphAssetStatus::OK ? 0 : 1;
}

}  // namespace

bool isStepGraphPresetCommand(const std::string& command) {
    return command == "inspect-step-graph-preset" ||
           command == "validate-step-graph-preset" ||
           command == "rename-step-graph-preset";
}

int runStepGraphPresetCommand(
    const std::string& command,
    const std::vector<uint8_t>& input,
    const std::string& semanticName,
    bool json,
    std::vector<uint8_t>* output
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

    uint32_t bytesWritten = 0;
    if (status == sequencer::SequencerGraphAssetStatus::OK &&
        command == "rename-step-graph-preset") {
        if (output == nullptr || preset.metadataDefaulted ||
            !sequencer::validStepGraphPresetSemanticName(semanticName.c_str())) {
            status = preset.metadataDefaulted
                ? sequencer::SequencerGraphAssetStatus::UNSUPPORTED_VERSION
                : sequencer::SequencerGraphAssetStatus::INVALID_ARGUMENT;
            report.status = status;
        } else {
            sequencer::SequencerStepGraphPreset renamed = preset;
            if (!sequencer::setStepGraphPresetMetadata(
                    renamed,
                    preset.technicalId,
                    semanticName.c_str(),
                    preset.scalePolicy,
                    preset.sourceScale
                )) {
                status = sequencer::SequencerGraphAssetStatus::INVALID_ARGUMENT;
                report.status = status;
            } else {
                output->assign(
                    sequencer::STEP_GRAPH_PRESET_MAX_ENCODED_SIZE,
                    0
                );
                const auto encoded = sequencer::encodeStepGraphPreset(
                    renamed,
                    output->data(),
                    static_cast<uint16_t>(output->size())
                );
                status = encoded.status;
                report.status = status;
                if (encoded.ok()) {
                    output->resize(encoded.bytesWritten);
                    constexpr size_t semanticOffset =
                        sequencer::STEP_GRAPH_PRESET_V1_HEADER_SIZE + 4U +
                        sequencer::SequencerStepGraphPreset::TECHNICAL_ID_SIZE;
                    constexpr size_t semanticEnd = semanticOffset +
                        sequencer::SequencerStepGraphPreset::SEMANTIC_NAME_SIZE;
                    const bool sameShape = output->size() == input.size();
                    bool unchangedOutsideName = sameShape;
                    if (sameShape) {
                        for (size_t i = 0; i < input.size(); ++i) {
                            if (i >= semanticOffset && i < semanticEnd) continue;
                            if ((*output)[i] != input[i]) {
                                unchangedOutsideName = false;
                                break;
                            }
                        }
                    }

                    sequencer::SequencerStepGraphPreset verified{};
                    sequencer::SequencerGraphAssetReport verifiedReport{};
                    const bool verifiedOk = unchangedOutsideName &&
                        sequencer::decodeStepGraphPreset(
                            output->data(),
                            static_cast<uint16_t>(output->size()),
                            verified,
                            &verifiedReport
                        ) &&
                        std::strcmp(verified.technicalId, preset.technicalId) == 0 &&
                        std::strcmp(verified.semanticName, semanticName.c_str()) == 0 &&
                        verified.scalePolicy == preset.scalePolicy &&
                        verified.mixedPitchPolicy == preset.mixedPitchPolicy;
                    if (!verifiedOk) {
                        output->clear();
                        status = sequencer::SequencerGraphAssetStatus::INVALID_FORMAT;
                        report.status = status;
                    } else {
                        preset = std::move(verified);
                        report = verifiedReport;
                        bytesWritten = encoded.bytesWritten;
                    }
                } else {
                    output->clear();
                }
            }
        }
    }

    if (json) {
        printJsonReport(command, status, report, preset, bytesWritten);
    } else {
        printTextReport(command, status, report, preset, bytesWritten);
    }
    return statusExitCode(status);
}

}  // namespace core::tools::ms_core_file_tool
