#pragma once

#include <cstdint>

#include "state/macro/MacroAutomationState.hpp"
#include "state/modulation/ProjectControlDomainState.hpp"

namespace core::persistence::project_control_migration {

enum class Status : uint8_t {
    OK = 0,
    INVALID_LEGACY_BANK,
    CAPACITY_EXCEEDED,
    SCRATCH_ALLOCATION_FAILED,
    INVALID_MIGRATED_DOMAIN,
};

struct Result {
    Status status = Status::INVALID_LEGACY_BANK;
    uint16_t automationCount = 0;
    uint16_t sourceCount = 0;
    uint16_t bindingCount = 0;
    uint16_t pointCount = 0;

    [[nodiscard]] bool migrated() const { return status == Status::OK; }
};

/**
 * Lifts the combined MAUT v1.4/v1.5 bank into current absolute Automation and
 * root Recorded Shape domains. `out` is published only after full validation.
 */
[[nodiscard]] Result liftLegacyMacroAutomationBank(
    const core::state::macro::MacroAutomationBankState& legacy,
    core::state::modulation::ProjectControlDomainState& out
);

/** Allocation-free variant for a caller-owned unpublished temporary domain. */
[[nodiscard]] Result liftLegacyMacroAutomationBankIntoPending(
    const core::state::macro::MacroAutomationBankState& legacy,
    core::state::modulation::ProjectControlDomainState& pending
);

}  // namespace core::persistence::project_control_migration
