#include <array>
#include <cassert>
#include <cstring>
#include "protocol/filesystem/UnifiedFileSystemRpc.hpp"
using namespace core::protocol::filesystem::unified;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    Frame frame; frame.requestId = 42;
    if (decode(data, size, frame)) {
        std::array<uint8_t, HEADER + MAX_BODY> encoded{};
        assert(encode(frame, encoded.data(), encoded.size()) == size);
        assert(std::memcmp(data, encoded.data(), size) == 0);
    } else assert(frame.requestId == 42);
    return 0;
}
