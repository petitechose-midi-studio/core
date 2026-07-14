#pragma once

#include <cstdint>

#include "state/MacroState.hpp"
#include "state/StructureClipboardState.hpp"
#include "state/macro/MacroPagesState.hpp"
#include "state/macro/MacroUiState.hpp"

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
    using MarkProjectMutatedFn = void (*)(void* context);

    struct StateRefs {
        core::state::macro::MacroPagesState& pages;
        core::state::macro::MacroUiState* macroUi = nullptr;
        core::state::StructureClipboardState* clipboard = nullptr;
        core::state::MacroState* macros = nullptr;
    };

    struct Operations {
        void* context = nullptr;
        SetConfigFn setConfig = nullptr;
        SwitchToPageFn switchToPage = nullptr;
        MarkProjectMutatedFn markProjectMutated = nullptr;
    };

    MacroEditDomainServices(StateRefs state, Operations operations);
    static MacroEditDomainServices fromCoreState(core::state::CoreState& state);

    const core::state::macro::MacroConfig& activeConfig(uint8_t index) const;
    bool isMacroSlotActive(uint8_t index) const;
    bool setConfig(uint8_t index, uint8_t channel, uint8_t cc) const;
    void switchToPage(uint8_t pageIndex) const;
    core::state::macro::MacroAutomationSlotAddress automationAddress(uint8_t index) const;
    const core::state::macro::MacroAutomationSlotState* automationSlot(uint8_t index) const;
    bool automationClipboardAvailable() const;
    bool automationActiveFor(uint8_t index) const;
    bool manualOverrideActiveFor(uint8_t index) const;
    void setManualOverride(uint8_t index, bool active) const;
    bool clearAutomation(uint8_t index) const;
    bool removeAutomation(uint8_t index) const;
    bool copyAutomation(uint8_t index) const;
    bool pasteAutomation(uint8_t index) const;
    bool setAutomationDurationBeats(uint8_t index, float durationBeats) const;
    bool setAutomationWindowOffsetBeats(uint8_t index, float offsetBeats) const;

private:
    core::state::macro::MacroPagesState* pages_ = nullptr;
    core::state::macro::MacroUiState* macro_ui_ = nullptr;
    core::state::StructureClipboardState* clipboard_ = nullptr;
    core::state::MacroState* macros_ = nullptr;
    Operations operations_{};
};

}  // namespace core::handler
