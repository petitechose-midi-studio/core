#include "integration/UxScriptParser.hpp"

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace sdl::integration {

namespace {

std::string trim(std::string text) {
    const auto first = std::find_if_not(text.begin(), text.end(), [](unsigned char c) {
        return std::isspace(c) != 0;
    });
    const auto last = std::find_if_not(text.rbegin(), text.rend(), [](unsigned char c) {
        return std::isspace(c) != 0;
    }).base();
    if (first >= last) return {};
    return std::string(first, last);
}

std::string stripComment(const std::string& line) {
    const size_t hash = line.find('#');
    const size_t slashes = line.find("//");
    size_t end = std::string::npos;
    if (hash != std::string::npos) end = hash;
    if (slashes != std::string::npos) end = std::min(end, slashes);
    return trim(line.substr(0, end));
}

std::string upper(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    return value;
}

bool parseUint(const std::string& text, uint32_t& out) {
    const char* first = text.data();
    const char* last = text.data() + text.size();
    auto result = std::from_chars(first, last, out);
    return result.ec == std::errc{} && result.ptr == last;
}

bool parseFloat(const std::string& text, float& out) {
    char* end = nullptr;
    errno = 0;
    const float value = std::strtof(text.c_str(), &end);
    if (errno != 0 || end == text.c_str() || *end != '\0') return false;
    out = value;
    return true;
}

bool scopeFromName(const std::string& name, ::sdl::ScreenshotScope& out) {
    const std::string scope = upper(name);
    if (scope == "SCREEN") {
        out = ::sdl::ScreenshotScope::Screen;
        return true;
    }
    if (scope == "CONTROLLER") {
        out = ::sdl::ScreenshotScope::Controller;
        return true;
    }
    return false;
}

}  // namespace

std::string uxActionName(UxActionKind kind) {
    switch (kind) {
        case UxActionKind::Button: return "button";
        case UxActionKind::Encoder: return "encoder";
        case UxActionKind::EncoderValue: return "encoder_value";
        case UxActionKind::Capture: return "capture";
        case UxActionKind::Scenario: return "scenario";
        case UxActionKind::Tick:
        default: return "tick";
    }
}

std::string uxScopeName(::sdl::ScreenshotScope scope) {
    return scope == ::sdl::ScreenshotScope::Screen ? "screen" : "controller";
}

bool UxScriptParser::load(const char* path, std::vector<UxAction>& actions) {
    error_.clear();
    actions.clear();

    std::ifstream input(path);
    if (!input) {
        error_ = "unable to open UX script";
        return false;
    }

    uint32_t order = 0;
    uint32_t lineNumber = 0;
    std::string line;
    while (std::getline(input, line)) {
        ++lineNumber;
        if (!parseLine(line, lineNumber, order, actions)) return false;
    }

    std::stable_sort(actions.begin(), actions.end(), [](const UxAction& lhs, const UxAction& rhs) {
        if (lhs.dueMs != rhs.dueMs) return lhs.dueMs < rhs.dueMs;
        return lhs.order < rhs.order;
    });
    return true;
}

bool UxScriptParser::parseLine(const std::string& raw,
                               uint32_t lineNumber,
                               uint32_t& order,
                               std::vector<UxAction>& actions) {
    const std::string line = stripComment(raw);
    if (line.empty()) return true;

    std::istringstream stream(line);
    std::string dueToken;
    std::string command;
    stream >> dueToken >> command;

    uint32_t dueMs = 0;
    if (!parseUint(dueToken, dueMs)) {
        error_ = "line " + std::to_string(lineNumber) + ": expected millisecond timestamp";
        return false;
    }

    command = upper(command);
    if (command == "BUTTON") {
        std::string id;
        std::string state;
        stream >> id >> state;
        state = upper(state);
        if (id.empty() || (state != "DOWN" && state != "UP" && state != "PRESS" && state != "RELEASE")) {
            error_ = "line " + std::to_string(lineNumber) + ": expected `button <id> down|up`";
            return false;
        }
        actions.push_back({dueMs, order++, lineNumber, UxActionKind::Button, id,
                           (state == "DOWN" || state == "PRESS") ? "down" : "up"});
        return true;
    }

    if (command == "TAP") {
        std::string id;
        std::string durationToken;
        stream >> id >> durationToken;
        uint32_t durationMs = 60;
        if (!durationToken.empty() && !parseUint(durationToken, durationMs)) {
            error_ = "line " + std::to_string(lineNumber) + ": tap duration must be an integer";
            return false;
        }
        actions.push_back({dueMs, order++, lineNumber, UxActionKind::Button, id, "down"});
        actions.push_back({dueMs + durationMs, order++, lineNumber, UxActionKind::Button, id, "up"});
        return true;
    }

    if (command == "ENCODER") {
        std::string id;
        std::string delta;
        stream >> id >> delta;
        float parsedDelta = 0.0f;
        if (id.empty() || !parseFloat(delta, parsedDelta)) {
            error_ = "line " + std::to_string(lineNumber) + ": expected `encoder <id> <delta>`";
            return false;
        }
        actions.push_back({dueMs, order++, lineNumber, UxActionKind::Encoder, id, delta, parsedDelta});
        return true;
    }

    if (command == "ENCODER_VALUE") {
        std::string id;
        std::string value;
        stream >> id >> value;
        float parsedValue = 0.0f;
        if (id.empty() || !parseFloat(value, parsedValue)) {
            error_ = "line " + std::to_string(lineNumber) +
                ": expected `encoder_value <id> <value>`";
            return false;
        }
        actions.push_back({dueMs, order++, lineNumber, UxActionKind::EncoderValue,
                           id, value, parsedValue});
        return true;
    }

    if (command == "CAPTURE") {
        std::string scope;
        std::string name;
        stream >> scope >> name;
        ::sdl::ScreenshotScope captureScope = ::sdl::ScreenshotScope::Screen;
        if (!scopeFromName(scope, captureScope)) {
            error_ = "line " + std::to_string(lineNumber) + ": expected `capture screen|controller <name>`";
            return false;
        }
        actions.push_back({dueMs, order++, lineNumber, UxActionKind::Capture, name, "", 0.0f, captureScope});
        return true;
    }

    if (command == "SCENARIO") {
        std::string name;
        stream >> name;
        if (name.empty()) {
            error_ = "line " + std::to_string(lineNumber) + ": expected `scenario <name>`";
            return false;
        }
        actions.push_back({dueMs, order++, lineNumber, UxActionKind::Scenario, name});
        return true;
    }

    if (command == "TICK" || command == "WAIT") {
        actions.push_back({dueMs, order++, lineNumber, UxActionKind::Tick});
        return true;
    }

    error_ = "line " + std::to_string(lineNumber) + ": unknown command `" + command + "`";
    return false;
}

}  // namespace sdl::integration
