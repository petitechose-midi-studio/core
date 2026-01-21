#pragma once

#include <oc/interface/IStorage.hpp>
#include <oc/type/Result.hpp>
#include <vector>
#include <cstring>

namespace desktop {

/**
 * @brief In-memory storage backend for desktop testing
 *
 * Data is lost on exit. For persistent desktop storage,
 * implement a FileStorageBackend instead.
 */
class MemoryStorage : public oc::interface::IStorage {
public:
    explicit MemoryStorage(size_t capacity = 4096)
        : data_(capacity, 0xFF) {}

    oc::type::Result<void> init() override { return oc::type::Result<void>::ok(); }

    bool available() const override { return true; }

    size_t read(uint32_t address, uint8_t* buffer, size_t size) override {
        if (address + size > data_.size()) return 0;
        std::memcpy(buffer, data_.data() + address, size);
        return size;
    }

    size_t write(uint32_t address, const uint8_t* buffer, size_t size) override {
        if (address + size > data_.size()) return 0;
        std::memcpy(data_.data() + address, buffer, size);
        return size;
    }

    bool commit() override { return true; }

    bool erase(uint32_t address, size_t size) override {
        if (address + size > data_.size()) return false;
        std::memset(data_.data() + address, 0xFF, size);
        return true;
    }

    size_t capacity() const override { return data_.size(); }

private:
    std::vector<uint8_t> data_;
};

} // namespace desktop
