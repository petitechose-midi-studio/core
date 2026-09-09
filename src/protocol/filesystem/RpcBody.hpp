#pragma once
#include <cstddef>
#include <cstdint>

namespace core::protocol::filesystem::unified {

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

private:
    const uint8_t* data_ = nullptr;
    size_t remaining_ = 0;
};

} // namespace core::protocol::filesystem::unified
