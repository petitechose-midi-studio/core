#pragma once

#include <cstddef>
#include <cstdint>

#include <oc/interface/IFileSystem.hpp>
#include <oc/type/Result.hpp>

#include "protocol/filesystem/FileSystemRpc.hpp"

namespace core::protocol::filesystem::internal {

class ByteWriter {
public:
    ByteWriter(uint8_t* data, size_t size);

    bool writeU8(uint8_t value);
    bool writeBool(bool value);
    bool writeU16(uint16_t value);
    bool writeU32(uint32_t value);
    bool writeBytes(const uint8_t* data, size_t size);
    bool writeString(const char* value, size_t maxLength);

    size_t position() const;
    size_t remaining() const;
    uint8_t* cursor();
    bool advance(size_t size);
    bool patchU8(size_t offset, uint8_t value);

private:
    uint8_t* data_ = nullptr;
    size_t size_ = 0;
    size_t offset_ = 0;
};

class ByteReader {
public:
    ByteReader(const uint8_t* data, size_t size);

    bool readU8(uint8_t& value);
    bool readBool(bool& value);
    bool readU16(uint16_t& value);
    bool readU32(uint32_t& value);
    bool readBytes(const uint8_t*& data, size_t size);
    bool readString(char* out, size_t outSize, size_t maxLength);

    size_t remaining() const;
    const uint8_t* current() const;

private:
    const uint8_t* data_ = nullptr;
    size_t remaining_ = 0;
};

FileSystemRpcStatus mapError(oc::type::Error error);
FileSystemRpcFileType mapFileType(oc::interface::FileType type);
oc::type::Result<size_t> bufferTooSmall();
bool writeFrameHeader(ByteWriter& writer, FileSystemRpcMessageId messageId, uint16_t requestId);
size_t encodeStatusOnly(FileSystemRpcMessageId messageId,
                        uint16_t requestId,
                        FileSystemRpcStatus status,
                        uint8_t* out,
                        size_t outSize);
size_t encodeWriteResponse(FileSystemRpcMessageId messageId,
                           uint16_t requestId,
                           FileSystemRpcStatus status,
                           uint16_t sessionId,
                           uint16_t bytesWritten,
                           uint8_t* out,
                           size_t outSize);
oc::type::Result<void> readPath(ByteReader& reader, char* path, size_t pathSize);
bool isProtocolReservedPath(
    core::persistence::ProductFileService& files,
    const char* productPath
);

}  // namespace core::protocol::filesystem::internal
