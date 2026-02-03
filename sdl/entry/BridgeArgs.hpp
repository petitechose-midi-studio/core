#pragma once

#include <string>

#include "Args.hpp"

namespace ms::bridge {

inline int udp_port(int argc, char** argv, int default_port) {
    return ms::args::int_value(argc, argv, "--bridge-udp-port", default_port);
}

inline std::string ws_url(int argc, char** argv, const std::string& default_url) {
    if (const char* v = ms::args::value(argc, argv, "--bridge-ws-url")) {
        return std::string(v);
    }
    return default_url;
}

} // namespace ms::bridge
