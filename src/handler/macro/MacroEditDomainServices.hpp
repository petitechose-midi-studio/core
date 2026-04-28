#pragma once

#include <cstdint>

#include "state/macro/MacroPagesState.hpp"

namespace core::state {
struct CoreState;
}

namespace core::handler {

/**
 * Macro edit domain service boundary.
 *
 * Macro edit handlers use this service to read/apply active macro config and
 * switch pages through focused state refs and typed operations.
 */
class MacroEditDomainServices {
public:
    using SetConfigFn = bool (*)(void* context, uint8_t index, uint8_t channel, uint8_t cc);
    using SwitchToPageFn = void (*)(void* context, uint8_t pageIndex);

    struct StateRefs {
        core::state::macro::MacroPagesState& pages;
    };

    struct Operations {
        void* context = nullptr;
        SetConfigFn setConfig = nullptr;
        SwitchToPageFn switchToPage = nullptr;
    };

    MacroEditDomainServices(StateRefs state, Operations operations);
    static MacroEditDomainServices fromCoreState(core::state::CoreState& state);

    const core::state::macro::MacroConfig& activeConfig(uint8_t index) const;
    bool setConfig(uint8_t index, uint8_t channel, uint8_t cc) const;
    void switchToPage(uint8_t pageIndex) const;

private:
    core::state::macro::MacroPagesState* pages_ = nullptr;
    Operations operations_{};
};

}  // namespace core::handler
