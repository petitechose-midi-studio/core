#pragma once

#include <array>
#include <cstdint>

#include <ms/ui/widget/VirtualListKeyValueOverlay.hpp>
#include <ms/ui/widget/VirtualListSelectorOverlay.hpp>

#include "state/SequencerSettingsState.hpp"
#include "state/sequencer/SequencerTrackBankState.hpp"

namespace core::context::standalone::sequencer_settings_presenter {

struct Source {
    core::state::SequencerSettingsState& sequencerSettings;
    core::state::sequencer::SequencerTrackBankState& trackBank;
};

struct OverlayRenderData {
    std::array<ms::ui::KeyValueRow, 3> rows{};
    uint32_t dataRevision = 0;
    int selectedIndex = 0;
};

struct SelectorRenderData {
    const char* title = "";
    const char* meta = "PROJECT";
    const char* const* items = nullptr;
    int itemCount = 0;
    int selectedIndex = 0;
    uint32_t dataRevision = 0;
    bool visible = false;
};

OverlayRenderData buildOverlayRenderData(const Source& source);
SelectorRenderData buildSelectorRenderData(const Source& source);

}  // namespace core::context::standalone::sequencer_settings_presenter
