#pragma once

#include <oc/app/OpenControlApp.hpp>

#include "SdlEnvironment.hpp"
#include "state/CoreState.hpp"

namespace sdl::integration {

bool applyCaptureScenario(core::state::CoreState& state, const char* scenario);

void tickFrames(::sdl::SdlEnvironment& env,
                oc::app::OpenControlApp& app,
                core::state::CoreState& state,
                int frames);

::sdl::ScreenshotScope captureScopeFromArg(const char* value);

}  // namespace sdl::integration
