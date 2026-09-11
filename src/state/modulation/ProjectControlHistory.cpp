#include "state/modulation/ProjectControlHistory.hpp"

#include <cassert>
#include <cstring>
#include "state/modulation/ProjectControlState.hpp"

namespace core::state::modulation {
namespace {

FLASHMEM uint64_t controlHash(const ProjectControlDomainState& state) {
    constexpr uint64_t prime = 1099511628211ULL;
    static_assert(sizeof(state) % sizeof(uint32_t) == 0U);
    uint64_t hash = 14695981039346656037ULL;
    const auto* bytes = reinterpret_cast<const uint8_t*>(&state);
    for (size_t offset = 0U; offset < sizeof(state); offset += sizeof(uint32_t)) {
        uint32_t word;
        std::memcpy(&word, bytes + offset, sizeof(word));
        if (word == 0U) {
            // Four zero-byte FNV-1a steps, with exactly the same 64-bit result.
            hash *= prime * prime * prime * prime;
        } else {
            const auto* wordBytes = reinterpret_cast<const uint8_t*>(&word);
            hash = (hash ^ wordBytes[0]) * prime;
            hash = (hash ^ wordBytes[1]) * prime;
            hash = (hash ^ wordBytes[2]) * prime;
            hash = (hash ^ wordBytes[3]) * prime;
        }
    }
    return hash;
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

FLASHMEM bool ProjectControlHistory::sealCandidate(const ProjectControlDomainState& before) {
    if (candidate() == nullptr || before_hash_ != controlHash(before)) return false;
    after_hash_ = controlHash(*data_);
    if (std::memcmp(data_.get(), &before, sizeof(before)) == 0) data_.reset();
    ready_ = true;
    return true;
}

FLASHMEM bool ProjectControlHistory::matches(
    const ProjectControlDomainState& live, bool after
) const {
    return ready_ && controlHash(live) == (after ? after_hash_ : before_hash_);
}

FLASHMEM void ProjectControlHistory::apply(ProjectControlState& live) const {
    assert(ready_);
    if (data_) data_.swap(live.authored_);
}

}  // namespace core::state::modulation
