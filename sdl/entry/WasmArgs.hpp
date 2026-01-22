#pragma once

#include <cstring>
#include <string>

namespace ms::wasm {

struct MidiArgs {
    std::string in;
    std::string out;
};

inline const char* arg_value(int argc, char** argv, const char* key) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (::strcmp(argv[i], key) == 0) {
            return argv[i + 1];
        }
    }
    return nullptr;
}

inline MidiArgs parse_midi_args(int argc, char** argv) {
    MidiArgs a;
    if (const char* v = arg_value(argc, argv, "--midi-in")) {
        a.in = v;
    }
    if (const char* v = arg_value(argc, argv, "--midi-out")) {
        a.out = v;
    }
    return a;
}

} // namespace ms::wasm
