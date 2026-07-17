#pragma once

#include <array>
#include <cstdint>
#include <type_traits>

#include "state/modulation/ProjectControlDomainState.hpp"
#include "state/modulation/ProjectControlRuntime.hpp"

namespace core::state::modulation {

/**
 * Transient projection of one destination-first Modulator audition.
 *
 * This lives beside the EXTMEM Project-control domain so controller UI can
 * identify the provisional objects without adding normal RAM state. Musical
 * rollback facts remain in the reserved history delta; none of this is
 * persisted.
 */
struct ProjectModulatorAuditionState {
    ModulatorId sourceId{};
    ModulationBindingId bindingId{};
    ModulationDestination destination{};
    uint32_t generation = 0;
    bool active = false;
    bool sourceCreated = false;
    std::array<uint8_t, 2> reserved{};
};

inline constexpr uint8_t PROJECT_MODULATION_FOCUS_CACHE_CAPACITY = 8;

/** Transient LRU focus keyed by logical Macro destination. */
struct ProjectModulationFocusEntry {
    ModulationDestination destination{};
    ModulationBindingId bindingId{};
    uint16_t stamp = 0;
    bool active = false;
    uint8_t reserved = 0;
};

struct ProjectModulationFocusState {
    std::array<
        ProjectModulationFocusEntry,
        PROJECT_MODULATION_FOCUS_CACHE_CAPACITY
    > entries{};
    uint16_t clock = 0;
    uint16_t reserved = 0;
};

/**
 * Complete live Project-control owner.
 *
 * MacroPagesState embeds this object in its existing EXTMEM allocation. The
 * authored domain replaces the legacy MacroAutomationBankState; the compiled
 * plan and runtime state are derived facts, never a second writable musical
 * authority. Per-frame scratch is bounded to the 128-source value array and
 * one drained trigger frame; both stay in the enclosing EXTMEM owner.
 */
struct ProjectControlState {
    ProjectControlDomainState authored{};
    ProjectModulationRuntimePlan plan{};
    ProjectControlRuntimeState runtime{};
    std::array<float, PROJECT_MODULATOR_CAPACITY> sourceScratch{};
    ProjectModulationTriggerFrame triggerScratch{};
    ProjectModulatorAuditionState audition{};
    ProjectModulationFocusState focus{};
    uint32_t authoredRevision = 1;
    uint32_t compiledRevision = 0;
    uint32_t runtimeContextHash = 0;
    uint32_t reserved = 0;

    ProjectControlState();

    void clear();

    void markAuthoredMutation() {
        ++authoredRevision;
        if (authoredRevision == 0U) authoredRevision = 1U;
    }
};

static_assert(sizeof(ProjectModulatorAuditionState) == 20U);
static_assert(sizeof(ProjectModulationFocusEntry) == 12U);
static_assert(sizeof(ProjectModulationFocusState) == 100U);
static_assert(sizeof(ProjectControlState) == 183760U);
static_assert(std::is_trivially_copyable_v<ProjectControlState>);

}  // namespace core::state::modulation
