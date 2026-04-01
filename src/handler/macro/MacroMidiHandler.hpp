#pragma once

/**
 * @file MacroMidiHandler.hpp
 * @brief Handles incoming MIDI CC for macro controls
 *
 * Receives MIDI CC and updates macro state + encoder positions.
 * Uses page configuration for CC/channel mapping.
 */

#include <cstdint>

#include <oc/api/EncoderAPI.hpp>
#include <oc/state/Signal.hpp>

#include <config/InputIDs.hpp>
#include "handler/macro/MacroDomainServices.hpp"
#include "ui/ViewTypes.hpp"

namespace core::handler {

/**
 * @brief MIDI input handler for standalone macros
 *
 * Handles incoming MIDI CC → updates state → syncs encoder position.
 * Uses page configuration for CC/channel matching.
 */
class MacroMidiHandler {
public:
    struct StateRefs {
        oc::state::Signal<core::ui::ViewType, 8>& activeView;
    };

    MacroMidiHandler(StateRefs state,
                     MacroDomainServices services,
                     oc::api::EncoderAPI& encoders);

    ~MacroMidiHandler() = default;

    MacroMidiHandler(const MacroMidiHandler&) = delete;
    MacroMidiHandler& operator=(const MacroMidiHandler&) = delete;

    // Event handlers (subscribe via EventBus)
    void onCC(uint8_t channel, uint8_t cc, uint8_t value);
    void onNoteIn();

private:
    void handleIncomingCC(uint8_t channel, uint8_t cc, uint8_t value);

    /// Find macro index for given CC/channel (-1 if not found)
    int8_t findMacroForCC(uint8_t channel, uint8_t cc) const;

    oc::state::Signal<core::ui::ViewType, 8>& active_view_;
    MacroDomainServices services_;
    oc::api::EncoderAPI& encoders_;
};

}  // namespace core::handler
