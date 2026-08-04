#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <utility>

#include "persistence/ProductFileService.hpp"

namespace core::test {

class ProductFileTestMutation {
public:
    explicit ProductFileTestMutation(
        core::persistence::ProductFileService& files,
        core::persistence::ProductMutationOwner owner =
            core::persistence::ProductMutationOwner::FILESYSTEM_RPC
    ) : files_(files) {
        auto acquired = files_.acquireMutation(owner);
        assert(acquired);
        lease_ = std::move(acquired.value());
    }

    ~ProductFileTestMutation() {
        if (lease_.valid()) (void)files_.releaseMutation(lease_);
    }

    ProductFileTestMutation(const ProductFileTestMutation&) = delete;
    ProductFileTestMutation& operator=(const ProductFileTestMutation&) = delete;

    const core::persistence::ProductMutationLease& lease() const { return lease_; }

    oc::type::Result<void> release() {
        if (!lease_.valid()) return oc::type::Result<void>::ok();
        return files_.releaseMutation(lease_);
    }

private:
    core::persistence::ProductFileService& files_;
    core::persistence::ProductMutationLease lease_{};
};

inline oc::type::Result<size_t> writeProductFileFixture(
    core::persistence::ProductFileService& files,
    const char* path,
    uint32_t offset,
    const uint8_t* data,
    size_t size
) {
    ProductFileTestMutation mutation(files);
    auto result = files.write(mutation.lease(), path, offset, data, size);
    auto released = mutation.release();
    if (result && !released) {
        return oc::type::Result<size_t>::err(released.error());
    }
    return result;
}

inline oc::type::Result<void> flushProductFileFixture(
    core::persistence::ProductFileService& files,
    const char* path
) {
    ProductFileTestMutation mutation(files);
    auto result = files.flush(mutation.lease(), path);
    auto released = mutation.release();
    if (result && !released) return released;
    return result;
}

inline oc::type::Result<void> renameProductFileFixture(
    core::persistence::ProductFileService& files,
    const char* from,
    const char* to
) {
    ProductFileTestMutation mutation(files);
    auto result = files.rename(mutation.lease(), from, to);
    auto released = mutation.release();
    if (result && !released) return released;
    return result;
}

}  // namespace core::test
