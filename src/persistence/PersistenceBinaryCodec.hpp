#pragma once

#include <cstdint>
#include <cstring>

namespace core::persistence::binary_codec {

class Writer {
public:
    Writer(uint8_t* data, uint32_t capacity)
        : data_(data), capacity_(capacity) {
        if (data_ == nullptr) ok_ = false;
    }

    bool ok() const { return ok_; }
    uint32_t offset() const { return offset_; }
    uint32_t remaining() const {
        return (offset_ <= capacity_) ? static_cast<uint32_t>(capacity_ - offset_) : 0U;
    }
    uint8_t* current() {
        return ok_ && offset_ <= capacity_ ? data_ + offset_ : nullptr;
    }

    bool writeU8(uint8_t value) { return writeByte_(value); }
    bool writeI8(int8_t value) { return writeByte_(static_cast<uint8_t>(value)); }

    bool writeU16(uint16_t value) {
        return writeByte_(static_cast<uint8_t>(value & 0xFFU)) &&
               writeByte_(static_cast<uint8_t>((value >> 8U) & 0xFFU));
    }

    bool writeI16(int16_t value) {
        return writeU16(static_cast<uint16_t>(value));
    }

    bool writeU32(uint32_t value) {
        return writeByte_(static_cast<uint8_t>(value & 0xFFU)) &&
               writeByte_(static_cast<uint8_t>((value >> 8U) & 0xFFU)) &&
               writeByte_(static_cast<uint8_t>((value >> 16U) & 0xFFU)) &&
               writeByte_(static_cast<uint8_t>((value >> 24U) & 0xFFU));
    }

    bool writeU64(uint64_t value) {
        for (uint8_t i = 0; i < 8; ++i) {
            if (!writeByte_(static_cast<uint8_t>((value >> (i * 8U)) & 0xFFU))) {
                return false;
            }
        }
        return true;
    }

    bool writeFloat32(float value) {
        static_assert(sizeof(float) == sizeof(uint32_t), "float must be 32-bit");
        uint32_t raw = 0;
        std::memcpy(&raw, &value, sizeof(raw));
        return writeU32(raw);
    }

    bool writeBytes(const void* source, uint32_t size) {
        if (!ok_) return false;
        if (size == 0) return true;
        if (source == nullptr || size > remaining()) {
            ok_ = false;
            return false;
        }
        std::memcpy(data_ + offset_, source, size);
        offset_ += size;
        return true;
    }

    bool writeZeroes(uint32_t size) {
        if (!ok_) return false;
        if (size > remaining()) {
            ok_ = false;
            return false;
        }
        std::memset(data_ + offset_, 0, size);
        offset_ += size;
        return true;
    }

    bool reserveBytes(uint32_t size, uint8_t*& out) {
        out = nullptr;
        if (!ok_ || size > remaining()) {
            ok_ = false;
            return false;
        }
        out = data_ + offset_;
        offset_ += size;
        return true;
    }

    bool patchU16(uint32_t offset, uint16_t value) {
        if (!ok_ || offset > capacity_ || capacity_ - offset < sizeof(uint16_t)) {
            ok_ = false;
            return false;
        }
        data_[offset] = static_cast<uint8_t>(value & 0xFFU);
        data_[offset + 1U] = static_cast<uint8_t>((value >> 8U) & 0xFFU);
        return true;
    }

private:
    bool writeByte_(uint8_t value) {
        if (!ok_ || offset_ >= capacity_) {
            ok_ = false;
            return false;
        }
        data_[offset_++] = value;
        return true;
    }

    uint8_t* data_ = nullptr;
    uint32_t capacity_ = 0;
    uint32_t offset_ = 0;
    bool ok_ = true;
};

class Reader {
public:
    Reader(const uint8_t* data, uint32_t size)
        : data_(data), size_(size) {
        if (data_ == nullptr) ok_ = false;
    }

    bool ok() const { return ok_; }
    uint32_t offset() const { return offset_; }
    uint32_t remaining() const {
        return (offset_ <= size_) ? static_cast<uint32_t>(size_ - offset_) : 0U;
    }
    const uint8_t* current() const {
        return ok_ && offset_ <= size_ ? data_ + offset_ : nullptr;
    }

    bool readU8(uint8_t& out) { return readByte_(out); }

    bool readI8(int8_t& out) {
        uint8_t raw = 0;
        if (!readByte_(raw)) return false;
        out = static_cast<int8_t>(raw);
        return true;
    }

    bool readU16(uint16_t& out) {
        uint8_t lo = 0;
        uint8_t hi = 0;
        if (!readByte_(lo) || !readByte_(hi)) return false;
        out = static_cast<uint16_t>(lo | static_cast<uint16_t>(hi << 8U));
        return true;
    }

    bool readI16(int16_t& out) {
        uint16_t raw = 0;
        if (!readU16(raw)) return false;
        out = static_cast<int16_t>(raw);
        return true;
    }

    bool readU32(uint32_t& out) {
        uint8_t b0 = 0;
        uint8_t b1 = 0;
        uint8_t b2 = 0;
        uint8_t b3 = 0;
        if (!readByte_(b0) || !readByte_(b1) || !readByte_(b2) || !readByte_(b3)) {
            return false;
        }
        out = static_cast<uint32_t>(b0) |
              (static_cast<uint32_t>(b1) << 8U) |
              (static_cast<uint32_t>(b2) << 16U) |
              (static_cast<uint32_t>(b3) << 24U);
        return true;
    }

    bool readU64(uint64_t& out) {
        out = 0;
        for (uint8_t i = 0; i < 8; ++i) {
            uint8_t byte = 0;
            if (!readByte_(byte)) return false;
            out |= static_cast<uint64_t>(byte) << (i * 8U);
        }
        return true;
    }

    bool readFloat32(float& out) {
        static_assert(sizeof(float) == sizeof(uint32_t), "float must be 32-bit");
        uint32_t raw = 0;
        if (!readU32(raw)) return false;
        std::memcpy(&out, &raw, sizeof(out));
        return true;
    }

    bool readBytes(void* target, uint32_t size) {
        if (!ok_) return false;
        if (size == 0) return true;
        if (target == nullptr || size > remaining()) {
            ok_ = false;
            return false;
        }
        std::memcpy(target, data_ + offset_, size);
        offset_ += size;
        return true;
    }

    bool skip(uint32_t size) {
        if (!ok_ || size > remaining()) {
            ok_ = false;
            return false;
        }
        offset_ += size;
        return true;
    }

private:
    bool readByte_(uint8_t& out) {
        if (!ok_ || offset_ >= size_) {
            ok_ = false;
            return false;
        }
        out = data_[offset_++];
        return true;
    }

    const uint8_t* data_ = nullptr;
    uint32_t size_ = 0;
    uint32_t offset_ = 0;
    bool ok_ = true;
};

}  // namespace core::persistence::binary_codec
