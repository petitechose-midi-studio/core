#pragma once

#include <array>
#include <cstdint>

#include "state/modulation/ProjectControlDomainState.hpp"

namespace core::state::modulation {

/**
 * One exact logical destination mapping used by structural clipboard Paste.
 *
 * A whole-Track entry maps every Page at the same index. A Page entry maps one
 * source Page to one destination Page. Macro indices are always preserved.
 */
struct ProjectControlStructureTransferEntry {
    uint8_t sourceTrack = PROJECT_MODULATION_TRACK_COUNT;
    uint8_t targetTrack = PROJECT_MODULATION_TRACK_COUNT;
    uint8_t sourcePage = PROJECT_MODULATION_PAGE_COUNT;
    uint8_t targetPage = PROJECT_MODULATION_PAGE_COUNT;
    bool wholeTrack = false;
};

struct ProjectControlStructureTransferPlan {
    static constexpr uint8_t MAX_ENTRIES =
        PROJECT_MODULATION_TRACK_COUNT;

    uint8_t count = 0U;
    std::array<
        ProjectControlStructureTransferEntry,
        MAX_ENTRIES
    > entries{};

    [[nodiscard]] bool valid() const;
};

/**
 * Replaces all mapped destination scopes in a caller-owned cold domain.
 *
 * Automation and every modulation edge are copied. A source used exclusively
 * by the copied scope is cloned; a source also used elsewhere remains linked
 * by stable Project ID. The live domain is never touched on failure.
 */
[[nodiscard]] bool replaceProjectControlStructureInDomain(
    ProjectControlDomainState& target,
    const ProjectControlDomainState& source,
    const ProjectControlStructureTransferPlan& plan
);

}  // namespace core::state::modulation
