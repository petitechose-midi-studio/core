#pragma once

#include <oc/state/Signal.hpp>
#include <oc/state/SignalString.hpp>

namespace state {

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

}  // namespace state
