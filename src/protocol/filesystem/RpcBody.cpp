#include "RpcBody.hpp"
#include <cstring>
#include <config/PlatformCompat.hpp>

namespace core::protocol::filesystem::unified {

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

FLASHMEM ByteReader::ByteReader(const uint8_t* data, size_t size) : data_(data), remaining_(size) {}

FLASHMEM bool ByteReader::readU8(uint8_t& value) {
    if (remaining_ < 1) return false;
    value = *data_++;
    --remaining_;
    return true;
}

FLASHMEM bool ByteReader::readBool(bool& value) {
    uint8_t raw = 0;
    if (!readU8(raw) || raw > 1U) return false;
    value = raw == 1U;
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

} // namespace core::protocol::filesystem::unified
