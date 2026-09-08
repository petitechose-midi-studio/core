#include "state/modulation/ProjectControlHistory.hpp"

#include <cassert>
#include <cstring>

namespace core::state::modulation {
namespace {

uint32_t loadWord(const uint8_t* bytes) {
    // memcpy permits word-sized access without alignment/aliasing casts.
    uint32_t word;
    std::memcpy(&word, bytes, sizeof(word));
    return word;
}

FLASHMEM uint64_t controlHash(const ProjectControlDomainState& state) {
    uint64_t hash = 14695981039346656037ULL;
    const auto* bytes = reinterpret_cast<const uint8_t*>(&state);
    for (size_t index = 0U; index < sizeof(state); ++index) {
        hash = (hash ^ bytes[index]) * 1099511628211ULL;
    }
    return hash;
}

FLASHMEM void xorControl(ProjectControlDomainState& target,
                         const ProjectControlDomainState& source, uint16_t wordCount) {
    static_assert(sizeof(target) % sizeof(uint32_t) == 0U);
    auto* output = reinterpret_cast<uint8_t*>(&target);
    const auto* input = reinterpret_cast<const uint8_t*>(&source);
    for (size_t index = 0U; index < size_t(wordCount) * sizeof(uint32_t); index += sizeof(uint32_t)) {
        const uint32_t word = loadWord(output + index) ^ loadWord(input + index);
        std::memcpy(output + index, &word, sizeof(word));
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
    static_assert(sizeof(other) / sizeof(uint32_t) <= UINT16_MAX);
    size_t wordCount = sizeof(other) / sizeof(uint32_t);
    const auto* stored = reinterpret_cast<const uint8_t*>(data_.get());
    const auto* compared = reinterpret_cast<const uint8_t*>(&other);
    while (wordCount != 0U &&
        loadWord(stored + (wordCount - 1U) * sizeof(uint32_t)) ==
        loadWord(compared + (wordCount - 1U) * sizeof(uint32_t))) --wordCount;
    word_count_ = static_cast<uint16_t>(wordCount);
    if (word_count_ == 0U) data_.reset();
    else xorControl(*data_, other, word_count_);
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
    if (data_) xorControl(live, *data_, word_count_);
}

}  // namespace core::state::modulation
