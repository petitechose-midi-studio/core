#include "state/modulation/ProjectControlHistory.hpp"

#include <cassert>
#include <cstring>

namespace core::state::modulation {
namespace {

FLASHMEM uint64_t controlHash(const ProjectControlDomainState& state) {
    uint64_t hash = 14695981039346656037ULL;
    const auto* bytes = reinterpret_cast<const uint8_t*>(&state);
    for (size_t index = 0U; index < sizeof(state); ++index) {
        hash = (hash ^ bytes[index]) * 1099511628211ULL;
    }
    return hash;
}

FLASHMEM void xorControl(ProjectControlDomainState& target,
                         const ProjectControlDomainState& source) {
    static_assert(sizeof(target) % sizeof(uint32_t) == 0U);
    auto* output = reinterpret_cast<uint8_t*>(&target);
    const auto* input = reinterpret_cast<const uint8_t*>(&source);
    for (size_t index = 0U; index < sizeof(target); index += sizeof(uint32_t)) {
        // memcpy permits word-sized access without alignment/aliasing casts.
        uint32_t left, right;
        std::memcpy(&left, output + index, sizeof(left));
        std::memcpy(&right, input + index, sizeof(right));
        left ^= right;
        std::memcpy(output + index, &left, sizeof(left));
    }
}

}  // namespace

FLASHMEM bool ProjectControlHistory::prepare(const ProjectControlDomainState& before) {
    auto candidate = core::app::makeExtmemUniqueCopy(before);
    if (!candidate) return false;
    data_ = std::move(candidate);
    before_hash_ = controlHash(before);
    ready_ = false;
    return true;
}

FLASHMEM bool ProjectControlHistory::seal_(
    const ProjectControlDomainState& other, uint64_t afterHash
) {
    after_hash_ = afterHash;
    if (std::memcmp(data_.get(), &other, sizeof(other)) == 0) data_.reset();
    else xorControl(*data_, other);
    ready_ = true;
    return true;
}

FLASHMEM bool ProjectControlHistory::captureAfter(const ProjectControlDomainState& after) {
    return candidate() != nullptr && seal_(after, controlHash(after));
}

FLASHMEM bool ProjectControlHistory::sealCandidate(const ProjectControlDomainState& before) {
    return candidate() != nullptr && before_hash_ == controlHash(before) &&
        seal_(before, controlHash(*data_));
}

FLASHMEM bool ProjectControlHistory::matches(
    const ProjectControlDomainState& live, bool after
) const {
    return ready_ && controlHash(live) == (after ? after_hash_ : before_hash_);
}

FLASHMEM void ProjectControlHistory::apply(ProjectControlDomainState& live) const {
    assert(ready_);
    // The buffer is read only as bytes after sealing, never as authored fields.
    if (data_) xorControl(live, *data_);
}

}  // namespace core::state::modulation
