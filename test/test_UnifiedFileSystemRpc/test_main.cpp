#include <array>
#include <cassert>
#include <cstring>
#include <iostream>
#include <vector>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif
#include "protocol/filesystem/UnifiedFileSystemRpc.hpp"
#include "protocol/filesystem/FileSystemRpc.hpp"

using namespace core::protocol::filesystem::unified;

int main(int argc, char**) {
    // Stream oracle for the cross-language corpus: exact accepted bytes round-trip.
    if (argc > 1) {
#ifdef _WIN32
        _setmode(_fileno(stdin), _O_BINARY);
        _setmode(_fileno(stdout), _O_BINARY);
#endif
        uint32_t size = 0;
        while (std::cin.read(reinterpret_cast<char*>(&size), sizeof(size))) {
            assert(size <= HEADER + MAX_BODY + 1);
            std::vector<uint8_t> bytes(size);
            std::cin.read(reinterpret_cast<char*>(bytes.data()), size);
            Frame f;
            const uint8_t accepted = decode(bytes.data(), bytes.size(), f);
            if (accepted) {
                std::vector<uint8_t> result(bytes.size());
                assert(encode(f, result.data(), result.size()) == bytes.size());
                assert(result == bytes);
            }
            std::cout.write(reinterpret_cast<const char*>(&accepted), 1);
        }
        return 0;
    }
    constexpr std::array<uint8_t, 26> golden = {0xfc, 2, 6, 0, 0x34, 0x12, 0, 0,
        4, 3, 2, 1, 0, 0, 0, 0, 0x10, 0x27, 0, 0, 2, 0, 0, 0, 7, 0};
    Frame frame;
    assert(decode(golden.data(), golden.size(), frame));
    assert(frame.operation == Operation::UploadCommit && frame.requestId == 0x1234);
    assert(frame.body == golden.data() + HEADER && frame.bodySize == 2);
    std::array<uint8_t, 26> encoded{};
    assert(encode(frame, encoded.data(), encoded.size()) == golden.size());
    assert(encoded == golden);
    for (size_t size = 0; size < golden.size(); ++size) {
        Frame unchanged; unchanged.requestId = 42;
        assert(!decode(golden.data(), size, unchanged));
        assert(unchanged.requestId == 42);
        encoded.fill(0xa5);
        assert(encode(frame, encoded.data(), size) == 0);
        for (auto byte : encoded) assert(byte == 0xa5);
    }
    assert(!decode(nullptr, 100, frame));
    frame.body = nullptr;
    assert(!valid(frame));
    std::vector<uint8_t> body(MAX_BODY, 0x5a), wire(HEADER + MAX_BODY);
    frame = {Operation::Read, State::Complete, 1, Error::None, 0, 0, 0, body.data(), body.size()};
    assert(encode(frame, wire.data(), wire.size()) == wire.size());
    assert(decode(wire.data(), wire.size(), frame));
    wire.push_back(0); assert(!decode(wire.data(), wire.size(), frame));
    frame.bodySize = MAX_BODY + 1; assert(!valid(frame));
    std::array<uint8_t, 128> reference{};
    const size_t oldCommit = core::protocol::filesystem::FileSystemRpcCodec::encodeWriteCommitRequest(0x1234, 7, reference.data(), reference.size());
    assert(oldCommit > 0);
    const size_t oldJobHeader = 5 + std::strlen(core::protocol::filesystem::FILESYSTEM_JOB_RPC_REQUEST_NAME)
        + core::protocol::filesystem::FILESYSTEM_JOB_RPC_REQUEST_HEADER_BYTES;
    std::cout << "Commit request bytes: reference=" << oldJobHeader + oldCommit << " unified=" << golden.size() << '\n';
    std::cout << "Unified filesystem codec: golden, truncation, failure atomicity and bounds passed\n";
}
