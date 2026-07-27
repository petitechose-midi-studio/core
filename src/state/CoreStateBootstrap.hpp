#pragma once

namespace core::state {

struct CoreState;

/**
 * Initializes CoreState once after construction.
 *
 * Bootstrap owns settings/library initialization, overlay signal
 * registration, debug labels, and mutation-coalescing wiring. Runtime mutation
 * paths belong to CoreStateLifecycle and workflow classes.
 */
struct CoreStateBootstrap {
    static void initialize(CoreState& state);

private:
    static void registerOverlaySignals_(CoreState& state);
    static void initializePersistence_(CoreState& state);
    static void setupMutationCoalescing_(CoreState& state);
    static void configureMacroMutationCoalescing_(CoreState& state);
    static void configureSequencerMutationCoalescing_(CoreState& state);
};

}  // namespace core::state
