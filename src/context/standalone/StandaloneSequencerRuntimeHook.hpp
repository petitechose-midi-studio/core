#pragma once

#include <cstdint>

#include <oc/app/OpenControlApp.hpp>

#include "app/PhaseRetainingDeadline.hpp"
#include "config/App.hpp"
#include "config/TimeCompat.hpp"
#include "config/Timing.hpp"
#include "context/standalone/StandaloneSequencerRuntimeGate.hpp"

namespace core::context::standalone {

template <typename RuntimeHandle>
bool registerStandaloneSequencerRuntimeHook(oc::app::OpenControlApp& app,
                                             RuntimeHandle& runtime) {
    using RuntimeDeadline = core::app::PhaseRetainingDeadline<
        Config::Timing::SEQUENCER_REALTIME_PERIOD_US
    >;

    return app.registerPreContextUpdateHook([
        &app,
        &runtime,
        wasStandaloneActive = false,
        runtimeDeadline = RuntimeDeadline{}
    ]() mutable {
        if (!runtime) return;

        const bool isStandaloneActive =
            app.contexts().activeId() == static_cast<uint8_t>(Config::ContextID::STANDALONE);
#ifdef ARDUINO
        const bool updateDue = !isStandaloneActive ||
            runtimeDeadline.consumeIfDue(core::time_compat::micros());
#else
        constexpr bool updateDue = true;
#endif
        const auto runtimeDecision =
            decideStandaloneSequencerRuntimeAction(
                isStandaloneActive,
                wasStandaloneActive,
                updateDue
            );
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
