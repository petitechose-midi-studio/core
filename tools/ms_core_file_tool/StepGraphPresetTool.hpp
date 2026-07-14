#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace core::tools::ms_core_file_tool {

bool isStepGraphPresetCommand(const std::string& command);
int runStepGraphPresetCommand(
    const std::string& command,
    const std::vector<uint8_t>& input,
    const std::string& semanticName,
    bool json,
    std::vector<uint8_t>* output = nullptr
);

}  // namespace core::tools::ms_core_file_tool
