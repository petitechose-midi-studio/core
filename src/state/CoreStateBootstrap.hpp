#pragma once

namespace core::state {

struct CoreState;

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
