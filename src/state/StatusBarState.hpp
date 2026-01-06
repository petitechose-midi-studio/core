#pragma once

/**
 * @file StatusBarState.hpp
 * @brief Status bar reactive state
 */

#include <oc/state/Signal.hpp>
#include <oc/state/SignalString.hpp>

namespace core::state {

using oc::state::Signal;
using oc::state::SignalLabel;

/**
 * @brief State for TopBar and TransportBar
 */
struct StatusBarState {
    // TopBar
    SignalLabel pageName;

    // TransportBar - MIDI Note indicators
    Signal<bool> noteInActive{false};
    Signal<bool> noteOutActive{false};

    // TransportBar - MIDI CC indicators
    Signal<bool> ccInActive{false};
    Signal<bool> ccOutActive{false};

    // TransportBar - Transport
    Signal<bool> playing{false};
    Signal<float> tempo{120.0f};

    // TransportBar - Beat
    Signal<bool> beatPulse{false};

    StatusBarState() {
        pageName.set("Page 1");
    }
};

}  // namespace core::state
