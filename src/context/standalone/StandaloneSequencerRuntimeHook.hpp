#pragma once

#include <cstdint>

#include <oc/app/OpenControlApp.hpp>

#include "config/App.hpp"
#include "context/standalone/StandaloneSequencerRuntimeGate.hpp"

namespace core::context::standalone {

template <typename RuntimeHandle>
bool registerStandaloneSequencerRuntimeHook(oc::app::OpenControlApp& app,
                                            RuntimeHandle& runtime) {
    return app.registerPreContextUpdateHook([&app, &runtime, wasStandaloneActive = false]() mutable {
        if (!runtime) return;

        const bool isStandaloneActive =
            app.contexts().activeId() == static_cast<uint8_t>(Config::ContextID::STANDALONE);
        const auto runtimeDecision =
            decideStandaloneSequencerRuntimeAction(isStandaloneActive, wasStandaloneActive);
        wasStandaloneActive = runtimeDecision.nextWasStandaloneActive;

        switch (runtimeDecision.action) {
            case StandaloneSequencerRuntimeAction::UPDATE:
                runtime->update();
                return;
            case StandaloneSequencerRuntimeAction::STOP:
                runtime->stop();
                return;
            case StandaloneSequencerRuntimeAction::NONE:
            default:
                return;
        }
    });
}

}  // namespace core::context::standalone
