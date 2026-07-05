#pragma once

#include <array>
#include <cstdint>

#include <oc/api/MidiAPI.hpp>

#include "handler/macro/MacroPerformanceDomainServices.hpp"
#include "state/StatusBarState.hpp"
#include "state/macro/MacroAutomationState.hpp"
#include "state/macro/MacroPagesState.hpp"
#include "state/macro/MacroUiState.hpp"

namespace core::handler {

class MacroAutomationPlaybackService {
public:
    struct StateRefs {
        core::state::macro::MacroPagesState& pages;
        core::state::macro::MacroUiState& macroUi;
        core::state::StatusBarState& statusBar;
    };

    MacroAutomationPlaybackService(StateRefs state,
                                   MacroPerformanceDomainServices services,
                                   oc::api::MidiAPI& midi);

    void update(uint32_t nowMs);
    void reset();

private:
    void updatePlaybackBeat_(uint32_t nowMs);
    void invalidateSentCache_();

    core::state::macro::MacroPagesState& pages_;
    core::state::macro::MacroUiState& macro_ui_;
    core::state::StatusBarState& status_bar_;
    MacroPerformanceDomainServices services_;
    oc::api::MidiAPI& midi_;

    bool was_playing_ = false;
    uint32_t last_update_ms_ = 0;
    uint32_t next_due_ms_ = 0;
    float playback_beat_ = 0.0f;
    uint8_t cached_track_ = 0xFF;
    uint8_t cached_page_ = 0xFF;
    std::array<uint8_t, core::state::macro::MACRO_COUNT> sent_cc_values_{};
};

}  // namespace core::handler
