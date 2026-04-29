#pragma once

#include <functional>
#include <string>

#include <oc/app/OpenControlApp.hpp>

#include "SdlEnvironment.hpp"
#include "state/CoreState.hpp"

namespace sdl::integration {

struct UxRunOptions {
    const char* scriptPath = nullptr;
    const char* outputDir = nullptr;
};

class UxScenarioRunner {
public:
    using ScenarioApplier = std::function<bool(const char*)>;

    bool run(const UxRunOptions& options,
             sdl::SdlEnvironment& env,
             oc::app::OpenControlApp& app,
             core::state::CoreState& state,
             ScenarioApplier scenarioApplier);

    const std::string& error() const { return error_; }

private:
    std::string error_;
};

}  // namespace sdl::integration
