#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

#include <oc/interface/IStorage.hpp>

namespace test_support {

class MemoryStorage : public oc::interface::IStorage {
public:
    enum class FaultMode {
        NONE,
        COMMIT_FAIL,
        SHORT_WRITE,
    };

    explicit MemoryStorage(size_t capacity = 512 * 1024)
        : data_(capacity, 0xFF) {}

    oc::type::Result<void> init() override {
        initialized_ = true;
        return oc::type::Result<void>::ok();
    }

    bool available() const override { return initialized_ && !forceUnavailable_; }

    size_t read(uint32_t address, uint8_t* buffer, size_t size) override {
        if (!buffer || address >= data_.size()) return 0;
        const size_t n = clampedSize(address, size);
        std::memcpy(buffer, data_.data() + address, n);
        return n;
    }

    size_t write(uint32_t address, const uint8_t* buffer, size_t size) override {
        if (!buffer || address >= data_.size()) return 0;

        size_t n = clampedSize(address, size);
        if (faultMode_ == FaultMode::SHORT_WRITE && n > 0) {
            --n;
        }
        if (n > 0) {
            std::memcpy(data_.data() + address, buffer, n);
            dirty_ = true;
        }
        return n;
    }

    bool commit() override {
        if (faultMode_ == FaultMode::COMMIT_FAIL) {
            return false;
        }
        dirty_ = false;
        ++commitCount;
        return true;
    }

    bool erase(uint32_t address, size_t size) override {
        if (address >= data_.size()) return false;
        const size_t n = clampedSize(address, size);
        std::memset(data_.data() + address, 0xFF, n);
        dirty_ = true;
        return true;
    }

    size_t capacity() const override { return data_.size(); }
    bool isDirty() const override { return dirty_; }

    void setFaultMode(FaultMode mode) { faultMode_ = mode; }
    void setAvailable(bool available) { forceUnavailable_ = !available; }

    int commitCount = 0;

private:
    size_t clampedSize(uint32_t address, size_t size) const {
        return std::min(size, data_.size() - static_cast<size_t>(address));
    }

    bool initialized_ = false;
    bool forceUnavailable_ = false;
    bool dirty_ = false;
    FaultMode faultMode_ = FaultMode::NONE;
    std::vector<uint8_t> data_;
};

}  // namespace test_support
