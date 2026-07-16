#pragma once

#include <array>
#include <cstdint>
#include <type_traits>

#include "state/modulation/ProjectControlDomainState.hpp"
#include "state/modulation/ProjectControlRuntime.hpp"

namespace core::state::modulation {

/**
 * Complete live Project-control owner.
 *
 * MacroPagesState embeds this object in its existing EXTMEM allocation. The
 * authored domain replaces the legacy MacroAutomationBankState; the compiled
 * plan and runtime state are derived facts, never a second writable musical
 * authority. The sole per-frame scratch is the 128-source value array.
 */
struct ProjectControlState {
    ProjectControlDomainState authored{};
    ProjectModulationRuntimePlan plan{};
    ProjectControlRuntimeState runtime{};
    std::array<float, PROJECT_MODULATOR_CAPACITY> sourceScratch{};
    uint32_t authoredRevision = 1;
    uint32_t compiledRevision = 0;
    uint32_t runtimeContextHash = 0;
    uint32_t reserved = 0;

    void clear() {
        authored.clear();
        plan = {};
        runtime = {};
        sourceScratch.fill(0.0f);
        authoredRevision = 1;
        compiledRevision = 0;
        runtimeContextHash = 0;
        reserved = 0;
    }

    void markAuthoredMutation() {
        ++authoredRevision;
        if (authoredRevision == 0U) authoredRevision = 1U;
    }
};

static_assert(sizeof(ProjectControlState) == 181076U);
static_assert(std::is_trivially_copyable_v<ProjectControlState>);

}  // namespace core::state::modulation
