#pragma once

#include <array>
#include <cstdint>

#include "state/macro/MacroUiState.hpp"
#include "state/macro/MacroWorkflow.hpp"

namespace core::state {
struct CoreState;
}

namespace core::handler {

class MacroDomainServices {
public:
    explicit MacroDomainServices(core::state::CoreState& state);
    static MacroDomainServices fromCoreState(core::state::CoreState& state);

    float runtimeValue(uint8_t index) const;
    void setRuntimeValue(uint8_t index, float value) const;
    const core::state::macro::MacroConfig& activeConfig(uint8_t index) const;
    bool setConfig(uint8_t index, uint8_t channel, uint8_t cc) const;
    bool setTrackConfigs(
        const std::array<core::state::macro::MacroConfig, core::state::macro::MACRO_COUNT>& configs
    ) const;
    bool setConfigCc(uint8_t index, uint8_t cc) const;
    bool setTrackChannel(uint8_t channel) const;
    void switchToPage(uint8_t pageIndex) const;
    void switchToTrack(uint8_t trackIndex) const;
    uint8_t activeTrack() const;
    uint8_t activeTrackChannel() const;
    bool isActivePageEnabled() const;
    void togglePageEnabled(uint8_t pageIndex) const;
    bool deleteActivePage() const;
    bool deleteActiveTrack() const;
    bool deleteSelectedPages(uint16_t selectedMask) const;
    bool deleteSelectedTracks(uint16_t selectedMask) const;
    bool duplicateSelectedPages(uint16_t selectedMask) const;
    bool duplicateSelectedTracks(uint16_t selectedMask) const;
    bool erasePage(uint8_t pageIndex) const;
    bool eraseTrack(uint8_t trackIndex) const;
    bool pastePage(uint8_t pageIndex, const core::state::macro::MacroPageData& pageData) const;
    bool pasteTrack(uint8_t trackIndex, const core::state::macro::MacroTrackData& trackData) const;
    bool createNextPage() const;
    bool createTrack(uint8_t trackIndex) const;
    uint16_t pageEnabledMask() const;
    uint16_t trackEnabledMask() const;

    void pulseCcIn() const;
    void pulseCcOut() const;
    void pulseNoteIn() const;

private:
    void syncPreviewState_() const;

    core::state::CoreState* state_ = nullptr;
};

}  // namespace core::handler
