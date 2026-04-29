#pragma once

#include <fstream>
#include <string>

#include <oc/core/input/InputBindingTrace.hpp>

namespace sdl::integration {

class InputBindingTraceWriter {
public:
    bool open(const char* path);
    void write(const oc::core::input::InputBindingTraceEvent& event);

    bool isOpen() const { return stream_.is_open(); }
    const std::string& error() const { return error_; }

private:
    std::ofstream stream_;
    std::string error_;
};

}  // namespace sdl::integration
