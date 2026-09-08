#pragma once

#include <algorithm>
#include <array>
#include <cstring>
#include <oc/interface/IFileSystem.hpp>
#include <oc/interface/IStorage.hpp>
#include "persistence/DeviceSettingsStorageLayout.hpp"

namespace core::validation::benchmark {

// The existing vector-backed MemoryStorage does not compile on the Teensy
// toolchain's size_t ABI. This exact-size array also keeps benchmark settings
// off RAM2's dynamic heap; only these eleven bytes survive within a boot.
class BenchSettingsStorage final : public oc::interface::IStorage {
public:
    BenchSettingsStorage() { bytes_.fill(0xFF); }
    oc::type::Result<void> init() override { return oc::type::Result<void>::ok(); }
    bool available() const override { return true; }
    size_t capacity() const override { return bytes_.size(); }
    size_t read(uint32_t address, uint8_t* buffer, size_t size) override {
        if (!buffer || address >= capacity()) return 0;
        const size_t count = std::min<size_t>(size, capacity() - address);
        std::memcpy(buffer, bytes_.data() + address, count);
        return count;
    }
    size_t write(uint32_t address, const uint8_t* data, size_t size) override {
        if (!data || address >= capacity()) return 0;
        const size_t count = std::min<size_t>(size, capacity() - address);
        std::memcpy(bytes_.data() + address, data, count);
        return count;
    }
    bool erase(uint32_t address, size_t size) override {
        if (address >= capacity()) return false;
        std::memset(bytes_.data() + address, 0xFF,
                    std::min<size_t>(size, capacity() - address));
        return true;
    }
    bool commit() override { return true; }

private:
    std::array<uint8_t, core::persistence::device_settings::layout::STORAGE_END> bytes_;
};

// No delegate and no hardware handle: even an accidental persistence call
// cannot touch the user's SD card in the benchmark image.
class UnavailableFileSystem final : public oc::interface::IFileSystem {
public:
    oc::type::Result<void> init() override { return unavailable<void>(); }
    bool available() const override { return false; }
    oc::type::Result<oc::interface::FileInfo> stat(const char*) override {
        return unavailable<oc::interface::FileInfo>();
    }
    oc::type::Result<void> list(const char*, oc::interface::DirectoryEntryVisitor,
                               void*) override { return unavailable<void>(); }
    oc::type::Result<void> createDirectory(const char*) override { return unavailable<void>(); }
    oc::type::Result<void> remove(const char*, oc::interface::RemoveMode) override {
        return unavailable<void>();
    }
    oc::type::Result<void> rename(const char*, const char*) override { return unavailable<void>(); }
    oc::type::Result<size_t> read(const char*, uint32_t, uint8_t*, size_t) override {
        return unavailable<size_t>();
    }
    oc::type::Result<size_t> write(const char*, uint32_t, const uint8_t*, size_t) override {
        return unavailable<size_t>();
    }
    oc::type::Result<void> flush(const char*) override { return unavailable<void>(); }
    oc::type::Result<void> beginWrite(const char*, uint32_t) override { return unavailable<void>(); }
    oc::type::Result<size_t> appendWrite(const uint8_t*, size_t) override { return unavailable<size_t>(); }
    oc::type::Result<void> finishWrite() override { return unavailable<void>(); }
    void abortWrite() override {}

private:
    template<typename T>
    static oc::type::Result<T> unavailable() {
        return oc::type::Result<T>::err({
            oc::type::ErrorCode::HARDWARE_NOT_FOUND, "benchmark SD disabled"
        });
    }
};

}  // namespace core::validation::benchmark
