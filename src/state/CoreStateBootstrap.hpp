#pragma once

namespace core::state {

struct CoreState;

/**
 * Initializes CoreState once after construction.
 *
 * Bootstrap owns storage loading, default workspace creation, overlay signal
 * registration, debug labels, and auto-persist wiring. Runtime mutation paths
 * belong to CoreStateLifecycle and workflow classes.
 */
struct CoreStateBootstrap {
    static void initialize(CoreState& state);

private:
    static void registerOverlaySignals_(CoreState& state);
    static void initializePersistence_(CoreState& state);
    static void setupAutoPersist_(CoreState& state);
    static void initializeMacroPersistence_(CoreState& state);
    static void initializeSequencerPersistence_(CoreState& state);
    static void configureMacroAutoPersist_(CoreState& state);
    static void configureSequencerAutoPersist_(CoreState& state);
};

}  // namespace core::state
