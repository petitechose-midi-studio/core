#pragma once

namespace core::state {

struct CoreState;

/**
 * Centralizes CoreState lifecycle side effects.
 *
 * Runtime code calls CoreState's public methods; this helper keeps factory
 * reset and transient UI reset behavior in one implementation boundary.
 */
struct CoreStateLifecycle {
    static void update(CoreState& state);
    static void flush(CoreState& state);
    static void flushProjectMutationCoalescing(CoreState& state);
    static void factoryReset(CoreState& state);
    static void resetStandaloneTransientUi(CoreState& state);
    static void resetMusicalProject(CoreState& state);

private:
    static void updateMutationCoalescers_(CoreState& state);
    static void flushMutationCoalescers_(CoreState& state);
    static void persistFactoryDefaults_(CoreState& state);
    static void resetMacroDomain_(CoreState& state);
    static void resetSequencerDomain_(CoreState& state);
    static void resetUiState_(CoreState& state);
};

}  // namespace core::state
