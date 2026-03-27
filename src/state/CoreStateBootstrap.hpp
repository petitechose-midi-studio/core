#pragma once

namespace core::state {

struct CoreState;

struct CoreStateBootstrap {
    static void initialize(CoreState& state);

private:
    static void registerOverlaySignals_(CoreState& state);
    static void initializePersistence_(CoreState& state);
    static void setupAutoPersist_(CoreState& state);
};

}  // namespace core::state
