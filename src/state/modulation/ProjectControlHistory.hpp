#pragma once

#include "app/ExtmemAllocator.hpp"
#include "state/modulation/ProjectControlDomainState.hpp"

namespace core::state::modulation {
struct ProjectControlState;

/** One reserved domain exchanges ownership with live on publication and replay. */
class ProjectControlHistory {
public:
    [[nodiscard]] bool prepare(const ProjectControlDomainState& before);
    ProjectControlDomainState* candidate() { return ready_ ? nullptr : data_.get(); }

    /** Seal a live edit, or a detached candidate while live still holds Before. */
    [[nodiscard]] bool captureAfter(const ProjectControlDomainState& after);
    [[nodiscard]] bool sealCandidate(const ProjectControlDomainState& before);
    [[nodiscard]] bool matches(const ProjectControlDomainState& live, bool after) const;
    /** Caller validates the expected state before its no-fail commit boundary. */
    void apply(ProjectControlState& live) const;

    bool ready() const { return ready_; }
    bool hasStorage() const { return data_ != nullptr; }
    bool changed() const { return ready_ && hasStorage(); }

private:
    bool seal_(const ProjectControlDomainState& other, uint64_t afterHash);
    // Applying a const history exchanges the retained side, without changing its command.
    mutable core::app::ExtmemUniquePtr<ProjectControlDomainState> data_{};
    uint64_t before_hash_ = 0U;
    uint64_t after_hash_ = 0U;
    bool ready_ = false;
};

}  // namespace core::state::modulation
