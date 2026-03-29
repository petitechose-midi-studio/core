#pragma once

/**
 * @file MacroValueHandler.hpp
 * @brief Handles encoder input for macro controls
 *
 * Binds encoders to macro state and sends MIDI CC output.
 * Uses page configuration for CC/channel mapping.
 */

#include <array>
#include <cstdint>

#include <oc/api/EncoderAPI.hpp>
#include <oc/api/MidiAPI.hpp>

#include <config/InputIDs.hpp>
#include "handler/macro/MacroDomainServices.hpp"

namespace core::handler {

/**
 * @brief Encoder input handler for standalone macros
 *
 * Handles encoder turns → updates state → sends MIDI CC.
 * Uses page configuration for CC/channel mapping.
 * Bindings are scoped to the provided LVGL element.
 */
class MacroValueHandler {
public:
    MacroValueHandler(MacroDomainServices services,
                      oc::api::EncoderAPI& encoders,
                      oc::api::MidiAPI& midi,
                      oc::type::ScopeID scopeId);

    ~MacroValueHandler() = default;

    MacroValueHandler(const MacroValueHandler&) = delete;
    MacroValueHandler& operator=(const MacroValueHandler&) = delete;

private:
    void setupBindings();
    void handleValueChange(uint8_t index, float value);

    MacroDomainServices services_;
    oc::api::EncoderAPI& encoders_;
    oc::api::MidiAPI& midi_;
    oc::type::ScopeID scope_id_ = 0;
};

}  // namespace core::handler
