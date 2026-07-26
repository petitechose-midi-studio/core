#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

#include <oc/interface/IStorage.hpp>

namespace test_support {

/** Persistent-write storage whose next selected mutation can be power-cut. */
class CuttableStorage final : public oc::interface::IStorage {
public:
    explicit CuttableStorage(size_t capacity)
        : data_(capacity, 0xFF) {}

    explicit CuttableStorage(const std::vector<uint8_t>& image)
        : data_(image) {}

    oc::type::Result<void> init() override {
        initialized_ = true;
        cut_ = false;
        return oc::type::Result<void>::ok();
    }

    bool available() const override { return initialized_ && !cut_; }

    size_t read(uint32_t address, uint8_t* buffer, size_t size) override {
        if (!available() || buffer == nullptr || address > data_.size() ||
            size > data_.size() - address) {
            return 0;
        }
        std::memcpy(buffer, data_.data() + address, size);
        return size;
    }

    size_t write(uint32_t address, const uint8_t* buffer, size_t size) override {
        if (!available() || buffer == nullptr || address > data_.size() ||
            size > data_.size() - address) {
            return 0;
        }
        if (cutThisMutation_()) {
            const size_t partial = size / 2U;
            if (partial > 0U) {
                std::memcpy(data_.data() + address, buffer, partial);
            }
            return partial;
        }
        std::memcpy(data_.data() + address, buffer, size);
        return size;
    }

    bool commit() override {
        if (!available()) return false;
        if (cutThisMutation_()) return false;
        return true;
    }

    bool erase(uint32_t address, size_t size) override {
        if (!available() || address > data_.size() ||
            size > data_.size() - address) {
            return false;
        }
        if (cutThisMutation_()) {
            const size_t partial = size / 2U;
            std::fill_n(data_.begin() + address, partial, 0xFF);
            return false;
        }
        std::fill_n(data_.begin() + address, size, 0xFF);
        return true;
    }

    size_t capacity() const override { return data_.size(); }
    bool isDirty() const override { return false; }

    void cutAfterMutation(size_t index) {
        cutAfter_ = index;
        mutationCount_ = 0;
        cut_ = false;
    }

    void runWithoutCut() {
        cutAfter_ = std::numeric_limits<size_t>::max();
        mutationCount_ = 0;
        cut_ = false;
    }

    void reboot() {
        initialized_ = true;
        cut_ = false;
        cutAfter_ = std::numeric_limits<size_t>::max();
        mutationCount_ = 0;
    }

    [[nodiscard]] size_t mutationCount() const { return mutationCount_; }
    [[nodiscard]] const std::vector<uint8_t>& bytes() const { return data_; }

private:
    bool cutThisMutation_() {
        const bool cutNow = mutationCount_ == cutAfter_;
        ++mutationCount_;
        if (cutNow) cut_ = true;
        return cutNow;
    }

    std::vector<uint8_t> data_;
    size_t cutAfter_ = std::numeric_limits<size_t>::max();
    size_t mutationCount_ = 0;
    bool initialized_ = false;
    bool cut_ = false;
};

}  // namespace test_support
