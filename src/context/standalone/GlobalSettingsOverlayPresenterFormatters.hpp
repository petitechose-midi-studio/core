#pragma once

#include <array>
#include <cstdint>

#include <ms/ui/widget/VirtualListKeyValueOverlay.hpp>
#include <ms/ui/widget/VirtualListSelectorOverlay.hpp>

#include "state/GlobalSettingsState.hpp"
#include "state/MidiSyncState.hpp"

namespace core::context::standalone::global_settings_presenter {

/**
 * Pure render-data builders for global settings overlays.
 *
 * Formatting logic lives here so presenters can stay focused on signal binding
 * and widget rendering.
 */
struct Source {
    core::state::GlobalSettingsState& globalSettings;
    core::state::MidiSyncState& midiSync;
};

struct OverlayRenderData {
    std::array<std::array<char, 16>, 2> valueBuffers{};
    std::array<ms::ui::KeyValueRow, 4> rows{};
    std::array<char, 24> meta{};
    uint32_t dataRevision = 0;
    int selectedIndex = 0;
};

struct SelectorRenderData {
    const char* title = "";
    const char* meta = "GLOBAL";
    const char* const* items = nullptr;
    int itemCount = 0;
    int selectedIndex = 0;
    uint32_t dataRevision = 0;
    bool visible = false;
};

OverlayRenderData buildOverlayRenderData(const Source& source);
SelectorRenderData buildSelectorRenderData(const Source& source);

}  // namespace core::context::standalone::global_settings_presenter
