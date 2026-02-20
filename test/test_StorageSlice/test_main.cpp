#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

#include <oc/interface/IStorage.hpp>

#include "../../src/persistence/StorageSlice.hpp"

namespace {

class MemoryStorage : public oc::interface::IStorage {
public:
    explicit MemoryStorage(size_t capacity = 1024)
        : data_(capacity, 0xFF) {}

    oc::type::Result<void> init() override {
        initialized_ = true;
        return oc::type::Result<void>::ok();
    }

    bool available() const override { return initialized_; }

    size_t read(uint32_t address, uint8_t* buffer, size_t size) override {
        if (!buffer || address >= data_.size()) return 0;
        const size_t n = std::min(size, data_.size() - static_cast<size_t>(address));
        std::memcpy(buffer, data_.data() + address, n);
        return n;
    }

    size_t write(uint32_t address, const uint8_t* buffer, size_t size) override {
        if (!buffer || address >= data_.size()) return 0;
        const size_t n = std::min(size, data_.size() - static_cast<size_t>(address));
        std::memcpy(data_.data() + address, buffer, n);
        return n;
    }

    bool commit() override {
        ++commitCount;
        return true;
    }

    bool erase(uint32_t address, size_t size) override {
        if (address >= data_.size()) return false;
        const size_t n = std::min(size, data_.size() - static_cast<size_t>(address));
        std::memset(data_.data() + address, 0xFF, n);
        return true;
    }

    size_t capacity() const override { return data_.size(); }

    int commitCount = 0;

private:
    bool initialized_ = false;
    std::vector<uint8_t> data_;
};

void test_slice_maps_reads_and_writes() {
    MemoryStorage parent(512);
    parent.init();

    core::persistence::StorageSlice slice(parent, 100, 64);
    assert(slice.init());

    const uint8_t src[] = {1, 2, 3, 4};
    const size_t written = slice.write(10, src, sizeof(src));
    assert(written == sizeof(src));

    uint8_t parentRead[4] = {0};
    const size_t parentBytes = parent.read(110, parentRead, sizeof(parentRead));
    assert(parentBytes == sizeof(parentRead));
    assert(std::memcmp(parentRead, src, sizeof(src)) == 0);

    uint8_t dst[4] = {0};
    const size_t read = slice.read(10, dst, sizeof(dst));
    assert(read == sizeof(dst));
    assert(std::memcmp(dst, src, sizeof(src)) == 0);

    std::cout << "[PASS] test_slice_maps_reads_and_writes\n";
}

void test_slice_bounds_and_commit_passthrough() {
    MemoryStorage parent(256);
    parent.init();

    core::persistence::StorageSlice slice(parent, 32, 16);
    assert(slice.init());

    const uint8_t src[] = {9, 8, 7, 6, 5};
    const size_t written = slice.write(14, src, sizeof(src));
    assert(written == 2);

    assert(slice.commit());
    assert(parent.commitCount == 1);

    assert(slice.erase(8, 16));

    uint8_t check[8] = {};
    parent.read(40, check, sizeof(check));
    for (uint8_t value : check) {
        assert(value == 0xFF);
    }

    std::cout << "[PASS] test_slice_bounds_and_commit_passthrough\n";
}

}  // namespace

int main() {
    std::cout << "==============================================\n";
    std::cout << "StorageSlice tests\n";
    std::cout << "==============================================\n\n";

    test_slice_maps_reads_and_writes();
    test_slice_bounds_and_commit_passthrough();

    std::cout << "\n==============================================\n";
    std::cout << "All tests passed\n";
    std::cout << "==============================================\n";
    return 0;
}
