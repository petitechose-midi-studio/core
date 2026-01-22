#pragma once

#include <oc/hal/midi/LibreMidiTransport.hpp>

#include <string>

namespace ms::midi {

struct Defaults {
    std::string input_pattern;
    std::string output_pattern;
    bool use_virtual_ports = false;
};

inline Defaults native_defaults() {
#if defined(__linux__)
    return Defaults{.input_pattern = "VirMIDI", .output_pattern = "VirMIDI", .use_virtual_ports = false};
#elif defined(__APPLE__)
    // macOS: CoreMIDI supports virtual ports.
    return Defaults{.input_pattern = "MIDI Studio IN", .output_pattern = "MIDI Studio OUT", .use_virtual_ports = true};
#else
    // Windows: user-created loopMIDI ports.
    return Defaults{.input_pattern = "loopMIDI", .output_pattern = "loopMIDI", .use_virtual_ports = false};
#endif
}

inline oc::hal::midi::LibreMidiConfig make_native_config(
    const std::string& app_name,
    const std::string& input_pattern_override = {},
    const std::string& output_pattern_override = {}) {
    const auto d = native_defaults();
    return oc::hal::midi::LibreMidiConfig{
        .appName = app_name,
        .inputPortName = input_pattern_override.empty() ? d.input_pattern : input_pattern_override,
        .outputPortName = output_pattern_override.empty() ? d.output_pattern : output_pattern_override,
        .useVirtualPorts = d.use_virtual_ports,
    };
}

inline oc::hal::midi::LibreMidiConfig make_wasm_config(
    const std::string& app_name,
    const std::string& input_pattern,
    const std::string& output_pattern) {
    return oc::hal::midi::LibreMidiConfig{
        .appName = app_name,
        .inputPortName = input_pattern,
        .outputPortName = output_pattern,
        .useVirtualPorts = false,
    };
}

} // namespace ms::midi
