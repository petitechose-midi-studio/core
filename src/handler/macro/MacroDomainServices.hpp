#pragma once

#include <cstdint>

#include "state/macro/MacroPagesState.hpp"

namespace core::state {
struct CoreState;
}

namespace core::handler {

class MacroDomainServices {
public:
    explicit MacroDomainServices(core::state::CoreState& state);

    float runtimeValue(uint8_t index) const;
    void setRuntimeValue(uint8_t index, float value) const;
    const core::state::macro::MacroConfig& activeConfig(uint8_t index) const;
    bool setConfig(uint8_t index, uint8_t channel, uint8_t cc) const;
    void switchToPage(uint8_t pageIndex) const;

    void pulseCcIn() const;
    void pulseCcOut() const;
    void pulseNoteIn() const;

private:
    core::state::CoreState* state_ = nullptr;
};

}  // namespace core::handler
