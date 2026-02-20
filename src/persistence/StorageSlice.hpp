#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>

#include <oc/interface/IStorage.hpp>

namespace core::persistence {

class StorageSlice : public oc::interface::IStorage {
public:
    StorageSlice(oc::interface::IStorage& parent,
                 uint32_t baseAddress,
                 size_t sliceCapacity)
        : parent_(parent)
        , base_address_(baseAddress)
        , capacity_(sliceCapacity) {}

    oc::type::Result<void> init() override {
        return parent_.available()
                   ? oc::type::Result<void>::ok()
                   : oc::type::Result<void>::err({oc::type::ErrorCode::HARDWARE_INIT_FAILED,
                                                  "Parent storage unavailable"});
    }

    bool available() const override {
        return parent_.available();
    }

    size_t read(uint32_t address, uint8_t* buffer, size_t size) override {
        if (!buffer || address >= capacity_) return 0;
        const size_t bounded = std::min(size, capacity_ - static_cast<size_t>(address));
        return parent_.read(base_address_ + address, buffer, bounded);
    }

    size_t write(uint32_t address, const uint8_t* buffer, size_t size) override {
        if (!buffer || address >= capacity_) return 0;
        const size_t bounded = std::min(size, capacity_ - static_cast<size_t>(address));
        return parent_.write(base_address_ + address, buffer, bounded);
    }

    bool commit() override {
        return parent_.commit();
    }

    bool erase(uint32_t address, size_t size) override {
        if (address >= capacity_) return false;
        const size_t bounded = std::min(size, capacity_ - static_cast<size_t>(address));
        return parent_.erase(base_address_ + address, bounded);
    }

    size_t capacity() const override {
        return capacity_;
    }

    bool isDirty() const override {
        return parent_.isDirty();
    }

private:
    oc::interface::IStorage& parent_;
    uint32_t base_address_ = 0;
    size_t capacity_ = 0;
};

}  // namespace core::persistence
