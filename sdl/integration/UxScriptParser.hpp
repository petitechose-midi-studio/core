#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "SdlEnvironment.hpp"

namespace sdl::integration {

enum class UxActionKind {
    Button,
    Encoder,
    EncoderValue,
    Capture,
    Scenario,
    Tick,
};

struct UxAction {
    uint32_t dueMs = 0;
    uint32_t order = 0;
    uint32_t line = 0;
    UxActionKind kind = UxActionKind::Tick;
    std::string id;
    std::string value;
    float amount = 0.0f;
    ::sdl::ScreenshotScope scope = ::sdl::ScreenshotScope::Screen;
};

class UxScriptParser {
public:
    bool load(const char* path, std::vector<UxAction>& actions);
    const std::string& error() const { return error_; }

private:
    bool parseLine(const std::string& raw,
                   uint32_t lineNumber,
                   uint32_t& order,
                   std::vector<UxAction>& actions);

    std::string error_;
};

std::string uxActionName(UxActionKind kind);
std::string uxScopeName(::sdl::ScreenshotScope scope);

}  // namespace sdl::integration
