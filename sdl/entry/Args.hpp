#pragma once

#include <cstdlib>
#include <cstring>

namespace ms::args {

inline const char* value(int argc, char** argv, const char* key) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (::strcmp(argv[i], key) == 0) {
            return argv[i + 1];
        }
    }
    return nullptr;
}

inline int int_value(int argc, char** argv, const char* key, int default_value) {
    if (const char* v = value(argc, argv, key)) {
        const int n = std::atoi(v);
        if (n > 0 && n <= 65535) {
            return n;
        }
    }
    return default_value;
}

} // namespace ms::args
