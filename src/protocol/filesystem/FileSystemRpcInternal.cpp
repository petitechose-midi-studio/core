#include "protocol/filesystem/FileSystemRpcInternal.hpp"

#include <cstring>

#include <config/PlatformCompat.hpp>

namespace core::protocol::filesystem::internal {

using oc::interface::FileType;
using oc::type::Error;
using oc::type::ErrorCode;
using oc::type::Result;

constexpr const char* kErrorContextBufferTooSmall = "filesystem rpc buffer too small";

FLASHMEM ByteWriter::ByteWriter(uint8_t* data, size_t size) : data_(data), size_(size) {}

FLASHMEM bool ByteWriter::writeU8(uint8_t value) {
    if (remaining() < 1) return false;
    data_[offset_++] = value;
    return true;
}

FLASHMEM bool ByteWriter::writeBool(bool value) {
    return writeU8(value ? 1 : 0);
}

FLASHMEM bool ByteWriter::writeU16(uint16_t value) {
    if (remaining() < 2) return false;
    data_[offset_++] = static_cast<uint8_t>(value & 0xFF);
    data_[offset_++] = static_cast<uint8_t>((value >> 8) & 0xFF);
    return true;
}

FLASHMEM bool ByteWriter::writeU32(uint32_t value) {
    if (remaining() < 4) return false;
    data_[offset_++] = static_cast<uint8_t>(value & 0xFF);
    data_[offset_++] = static_cast<uint8_t>((value >> 8) & 0xFF);
    data_[offset_++] = static_cast<uint8_t>((value >> 16) & 0xFF);
    data_[offset_++] = static_cast<uint8_t>((value >> 24) & 0xFF);
    return true;
}

FLASHMEM bool ByteWriter::writeBytes(const uint8_t* data, size_t size) {
    if (!data && size > 0) return false;
    if (remaining() < size) return false;
    if (size > 0) {
        std::memcpy(data_ + offset_, data, size);
    }
    offset_ += size;
    return true;
}

FLASHMEM bool ByteWriter::writeString(const char* value, size_t maxLength) {
    if (!value) return false;
    const size_t length = std::strlen(value);
    if (length > maxLength || length > UINT8_MAX) return false;
    return writeU8(static_cast<uint8_t>(length)) &&
           writeBytes(reinterpret_cast<const uint8_t*>(value), length);
}

FLASHMEM size_t ByteWriter::position() const {
    return offset_;
}

FLASHMEM size_t ByteWriter::remaining() const {
    return offset_ <= size_ ? size_ - offset_ : 0;
}

FLASHMEM uint8_t* ByteWriter::cursor() {
    return data_ + offset_;
}

FLASHMEM bool ByteWriter::advance(size_t size) {
    if (remaining() < size) return false;
    offset_ += size;
    return true;
}

FLASHMEM bool ByteWriter::patchU8(size_t offset, uint8_t value) {
    if (offset >= size_) return false;
    data_[offset] = value;
    return true;
}

FLASHMEM ByteReader::ByteReader(const uint8_t* data, size_t size) : data_(data), remaining_(size) {}

FLASHMEM bool ByteReader::readU8(uint8_t& value) {
    if (remaining_ < 1) return false;
    value = *data_++;
    --remaining_;
    return true;
}

FLASHMEM bool ByteReader::readBool(bool& value) {
    uint8_t raw = 0;
    if (!readU8(raw)) return false;
    value = raw != 0;
    return true;
}

FLASHMEM bool ByteReader::readU16(uint16_t& value) {
    if (remaining_ < 2) return false;
    value = static_cast<uint16_t>(data_[0]) |
            static_cast<uint16_t>(static_cast<uint16_t>(data_[1]) << 8);
    data_ += 2;
    remaining_ -= 2;
    return true;
}

FLASHMEM bool ByteReader::readU32(uint32_t& value) {
    if (remaining_ < 4) return false;
    value = static_cast<uint32_t>(data_[0]) |
            (static_cast<uint32_t>(data_[1]) << 8) |
            (static_cast<uint32_t>(data_[2]) << 16) |
            (static_cast<uint32_t>(data_[3]) << 24);
    data_ += 4;
    remaining_ -= 4;
    return true;
}

FLASHMEM bool ByteReader::readBytes(const uint8_t*& data, size_t size) {
    if (remaining_ < size) return false;
    data = data_;
    data_ += size;
    remaining_ -= size;
    return true;
}

FLASHMEM bool ByteReader::readString(char* out, size_t outSize, size_t maxLength) {
    if (!out || outSize == 0) return false;
    uint8_t length = 0;
    if (!readU8(length)) return false;
    if (length > maxLength || static_cast<size_t>(length) + 1 > outSize) return false;
    if (remaining_ < length) return false;
    if (length > 0) {
        std::memcpy(out, data_, length);
    }
    out[length] = '\0';
    data_ += length;
    remaining_ -= length;
    return true;
}

FLASHMEM size_t ByteReader::remaining() const {
    return remaining_;
}

FLASHMEM const uint8_t* ByteReader::current() const {
    return data_;
}

FLASHMEM FileSystemRpcStatus mapError(Error error) {
    switch (error.code) {
        case ErrorCode::OK:
            return FileSystemRpcStatus::OK;
        case ErrorCode::INVALID_ARGUMENT:
            return FileSystemRpcStatus::INVALID_ARGUMENT;
        case ErrorCode::RESOURCE_NOT_FOUND:
            return FileSystemRpcStatus::NOT_FOUND;
        case ErrorCode::HARDWARE_BUSY:
            return FileSystemRpcStatus::BUSY;
        case ErrorCode::RESOURCE_EXHAUSTED:
            return FileSystemRpcStatus::TOO_LARGE;
        case ErrorCode::INVALID_STATE:
            return FileSystemRpcStatus::INVALID_STATE;
        case ErrorCode::HARDWARE_NOT_FOUND:
        case ErrorCode::HARDWARE_INIT_FAILED:
        case ErrorCode::HARDWARE_TIMEOUT:
        case ErrorCode::STORAGE_READ_FAILED:
        case ErrorCode::STORAGE_WRITE_FAILED:
        case ErrorCode::STORAGE_CORRUPT:
            return FileSystemRpcStatus::STORAGE_ERROR;
        default:
            return FileSystemRpcStatus::INVALID_STATE;
    }
}

FLASHMEM FileSystemRpcFileType mapFileType(FileType type) {
    switch (type) {
        case FileType::FILE:
            return FileSystemRpcFileType::FILE;
        case FileType::DIRECTORY:
            return FileSystemRpcFileType::DIRECTORY;
        case FileType::OTHER:
            return FileSystemRpcFileType::OTHER;
        case FileType::MISSING:
        default:
            return FileSystemRpcFileType::MISSING;
    }
}

FLASHMEM Result<size_t> bufferTooSmall() {
    return Result<size_t>::err({ErrorCode::RESOURCE_EXHAUSTED, kErrorContextBufferTooSmall});
}

FLASHMEM bool writeFrameHeader(ByteWriter& writer,
                               FileSystemRpcMessageId messageId,
                               uint16_t requestId) {
    const char* name = FileSystemRpcCodec::messageName(messageId);
    return writer.writeU8(static_cast<uint8_t>(messageId)) &&
           writer.writeString(name, UINT8_MAX) &&
           writer.writeU8(FILESYSTEM_RPC_SCHEMA) &&
           writer.writeU16(requestId);
}

FLASHMEM size_t encodeStatusOnly(FileSystemRpcMessageId messageId,
                                 uint16_t requestId,
                                 FileSystemRpcStatus status,
                                 uint8_t* out,
                                 size_t outSize) {
    ByteWriter writer(out, outSize);
    if (!writeFrameHeader(writer, messageId, requestId) ||
        !writer.writeU8(static_cast<uint8_t>(status))) {
        return 0;
    }
    return writer.position();
}

FLASHMEM size_t encodeWriteResponse(FileSystemRpcMessageId messageId,
                                    uint16_t requestId,
                                    FileSystemRpcStatus status,
                                    uint16_t sessionId,
                                    uint16_t bytesWritten,
                                    uint8_t* out,
                                    size_t outSize) {
    ByteWriter writer(out, outSize);
    if (!writeFrameHeader(writer, messageId, requestId) ||
        !writer.writeU8(static_cast<uint8_t>(status)) ||
        !writer.writeU16(sessionId) ||
        !writer.writeU16(bytesWritten)) {
        return 0;
    }
    return writer.position();
}

FLASHMEM Result<void> readPath(ByteReader& reader, char* path, size_t pathSize) {
    if (!reader.readString(path, pathSize, oc::interface::FILESYSTEM_MAX_PATH_LENGTH)) {
        return Result<void>::err({ErrorCode::INVALID_ARGUMENT, "invalid filesystem rpc path"});
    }
    return Result<void>::ok();
}

}  // namespace core::protocol::filesystem::internal
