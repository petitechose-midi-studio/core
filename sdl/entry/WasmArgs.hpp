#pragma once

#include <string>

#include "Args.hpp"

namespace ms::wasm {

struct MidiArgs {
    std::string in;
    std::string out;
};

inline MidiArgs parse_midi_args(int argc, char** argv) {
    MidiArgs a;
    if (const char* v = ms::args::value(argc, argv, "--midi-in")) {
        a.in = v;
    }
    if (const char* v = ms::args::value(argc, argv, "--midi-out")) {
        a.out = v;
    }
    return a;
}

} // namespace ms::wasm
