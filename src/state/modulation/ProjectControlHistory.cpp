#include "state/modulation/ProjectControlHistory.hpp"

#include <cassert>
#include <cstring>
#include "state/modulation/ProjectControlState.hpp"

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
