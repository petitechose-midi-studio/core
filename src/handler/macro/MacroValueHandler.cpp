#include "MacroValueHandler.hpp"

#include <algorithm>

#include <oc/log/Log.hpp>

#include <config/PlatformCompat.hpp>
#include <config/TimeCompat.hpp>
#include "midi/MidiUtils.hpp"

namespace core::handler {

namespace {

struct MacroValueProfiling {
    uint32_t window_start_ms = 0;
    uint32_t call_count = 0;
    uint32_t total_us = 0;
    uint32_t max_us = 0;

    void record(uint32_t elapsed_us) {
        const uint32_t now = core::time_compat::millis();
        if (window_start_ms == 0) {
            window_start_ms = now;
        }

        call_count += 1;
        total_us += elapsed_us;
        max_us = std::max(max_us, elapsed_us);

        if ((now - window_start_ms) < 500) return;

        const uint32_t avg_us = call_count > 0 ? (total_us / call_count) : 0;
        if (max_us >= 1000 || avg_us >= 500) {
            OC_LOG_INFO("[Perf][MacroValue] calls={} avg={}us max={}us",
                        call_count,
                        avg_us,
                        max_us);
        }

        window_start_ms = now;
        call_count = 0;
        total_us = 0;
        max_us = 0;
    }
};

MacroValueProfiling g_macro_value_profiling;

}  // namespace

MacroValueHandler::MacroValueHandler(MacroDomainServices services,
                                     oc::api::EncoderAPI& encoders,
                                     oc::api::MidiAPI& midi,
                                     oc::type::ScopeID scopeId)
    : services_(services)
    , encoders_(encoders)
    , midi_(midi)
    , scope_id_(scopeId) {
    setupBindings();
}

FLASHMEM void MacroValueHandler::setupBindings() {
    for (uint8_t i = 0; i < core::state::macro::MACRO_COUNT; ++i) {
        encoders_.encoder(Config::MACRO_ENCODERS[i])
            .turn()
            .scope(scope_id_)
            .then([this, i](float value) { handleValueChange(i, value); });
    }
}

void MacroValueHandler::handleValueChange(uint8_t index, float value) {
    const uint32_t start_us = core::time_compat::micros();
    const float clamped = std::clamp(value, 0.0f, 1.0f);
    const uint8_t cc_value = core::midi::toCC(clamped);
    const float quantized = core::midi::fromCC(cc_value);

    if (std::abs(services_.runtimeValue(index) - quantized) < 0.0005f) {
        g_macro_value_profiling.record(core::time_compat::micros() - start_us);
        return;
    }

    // Update state (triggers UI update, marks dirty for persistence)
    services_.setRuntimeValue(index, quantized);

    // Send MIDI CC
    const auto& config = services_.activeConfig(index);
    midi_.sendCC(config.channel, config.cc, cc_value);

    // Signal CC MIDI OUT activity
    services_.pulseCcOut();

    g_macro_value_profiling.record(core::time_compat::micros() - start_us);
}

}  // namespace core::handler
