#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace core::tools::ms_core_file_tool {

bool isStepGraphPresetCommand(const std::string& command);
int runStepGraphPresetCommand(
    const std::string& command,
    const std::vector<uint8_t>& input,
    bool json
);

}  // namespace core::tools::ms_core_file_tool
