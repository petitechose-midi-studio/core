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
    using StateTick = std::function<void()>;
    using CaptureObserver = std::function<void(const char*)>;
    using ScenarioObserver = std::function<void()>;

    bool run(const UxRunOptions& options,
             sdl::SdlEnvironment& env,
             oc::app::OpenControlApp& app,
             core::state::CoreState& state,
             ScenarioApplier scenarioApplier,
             StateTick stateTick = {},
             CaptureObserver captureObserver = {},
             ScenarioObserver scenarioObserver = {});

    const std::string& error() const { return error_; }

private:
    std::string error_;
};

}  // namespace sdl::integration
