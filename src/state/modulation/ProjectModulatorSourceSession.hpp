#pragma once

#include <cstdint>
#include <type_traits>

#include "state/modulation/ProjectControlState.hpp"
#include "state/modulation/ProjectModulationDomainOps.hpp"

namespace core::state::modulation {

enum class ProjectModulatorSourceSessionCapability : uint8_t {
    EDIT_SOURCE = 0x01U,
    EDIT_TRIGGER = 0x02U,
    EDIT_DEPTH = 0x04U,
    MANAGE_ROUTING = 0x08U,
    MANAGE_SOURCE = 0x10U,
    APPLY_CANCEL = 0x20U,
};

/**
 * Stack-only, fail-closed view of one source editing session.
 *
 * The descriptor contains permissions, not musical state.  Audition source,
 * edge and rollback data remain owned by ProjectControlState and MacroHistory.
 */
struct ProjectModulatorSourceSessionDescriptor {
    ModulatorId sourceId{};
    ModulationBindingId bindingId{};
    ProjectModulatorSourceSessionMode mode =
        ProjectModulatorSourceSessionMode::DURABLE_PROJECT;
    uint8_t capabilityMask = 0U;
    bool coherent = false;
    uint8_t reserved = 0U;

    [[nodiscard]] constexpr bool valid() const { return coherent; }

    [[nodiscard]] constexpr bool audition() const {
        return coherent &&
            (mode == ProjectModulatorSourceSessionMode::AUDITION_NEW ||
             mode == ProjectModulatorSourceSessionMode::AUDITION_EXISTING);
    }

    [[nodiscard]] constexpr bool newAudition() const {
        return coherent &&
            mode == ProjectModulatorSourceSessionMode::AUDITION_NEW;
    }

    [[nodiscard]] constexpr bool existingAudition() const {
        return coherent &&
            mode == ProjectModulatorSourceSessionMode::AUDITION_EXISTING;
    }

    [[nodiscard]] constexpr bool allows(
        ProjectModulatorSourceSessionCapability capability
    ) const {
        return coherent &&
            (capabilityMask & static_cast<uint8_t>(capability)) != 0U;
    }
};

/**
 * Resolves permissions for the requested source.
 *
 * An active audition never falls back to durable permissions: any unknown
 * mode, zero generation, missing object or source/edge/destination mismatch
 * returns a descriptor with no capabilities.  This resolver checks the local
 * projection only; MacroHistory verifies the generation against its reserved
 * transaction at Apply/Cancel and at global persistence/history boundaries.
 */
[[nodiscard]] inline ProjectModulatorSourceSessionDescriptor
resolveProjectModulatorSourceSession(
    const ProjectControlState& control,
    ModulatorId requestedSource
) {
    ProjectModulatorSourceSessionDescriptor out{};
    out.sourceId = requestedSource;

    const auto& graph = control.authored.modulation;
    const auto* source = findProjectModulator(graph, requestedSource);
    const auto& audition = control.audition;
    if (!audition.active()) {
        if (audition.mode !=
                ProjectModulatorSourceSessionMode::DURABLE_PROJECT ||
            source == nullptr) {
            return out;
        }
        out.capabilityMask =
            static_cast<uint8_t>(
                ProjectModulatorSourceSessionCapability::EDIT_SOURCE
            ) |
            static_cast<uint8_t>(
                ProjectModulatorSourceSessionCapability::EDIT_TRIGGER
            ) |
            static_cast<uint8_t>(
                ProjectModulatorSourceSessionCapability::MANAGE_ROUTING
            ) |
            static_cast<uint8_t>(
                ProjectModulatorSourceSessionCapability::MANAGE_SOURCE
            );
        out.coherent = true;
        return out;
    }

    out.mode = audition.mode;
    out.sourceId = audition.sourceId;
    out.bindingId = audition.bindingId;
    if (requestedSource != audition.sourceId || source == nullptr ||
        audition.generation == 0U || !valid(audition.bindingId)) {
        return out;
    }
    const auto* binding = findProjectModulationBinding(
        graph,
        audition.bindingId
    );
    if (binding == nullptr || binding->sourceId != audition.sourceId ||
        binding->destination != audition.destination) {
        return out;
    }

    out.capabilityMask =
        static_cast<uint8_t>(
            ProjectModulatorSourceSessionCapability::EDIT_DEPTH
        ) |
        static_cast<uint8_t>(
            ProjectModulatorSourceSessionCapability::APPLY_CANCEL
        );
    if (audition.mode == ProjectModulatorSourceSessionMode::AUDITION_NEW) {
        out.capabilityMask |=
            static_cast<uint8_t>(
                ProjectModulatorSourceSessionCapability::EDIT_SOURCE
            ) |
            static_cast<uint8_t>(
                ProjectModulatorSourceSessionCapability::EDIT_TRIGGER
            );
    } else if (audition.mode !=
               ProjectModulatorSourceSessionMode::AUDITION_EXISTING) {
        out.capabilityMask = 0U;
        return out;
    }
    out.coherent = true;
    return out;
}

static_assert(sizeof(ProjectModulatorSourceSessionDescriptor) == 12U);
static_assert(
    std::is_trivially_copyable_v<ProjectModulatorSourceSessionDescriptor>
);

}  // namespace core::state::modulation
