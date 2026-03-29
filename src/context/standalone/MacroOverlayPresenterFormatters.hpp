#pragma once

#include <array>
#include <cstdint>

#include <ms/ui/widget/VirtualListKeyValueOverlay.hpp>
#include <ms/ui/widget/VirtualListSelectorOverlay.hpp>

#include "state/MacroState.hpp"

namespace core::state {
struct CoreState;
}

namespace core::context::standalone::macro_overlay_presenter {

struct StaticItems {
    std::array<std::array<char, 4>, 16> channelLabels{};
    std::array<const char*, 16> channelItems{};
    std::array<std::array<char, 4>, 128> ccLabels{};
    std::array<const char*, 128> ccItems{};
    std::array<std::array<char, 16>, core::state::MACRO_COUNT> macroLabels{};
    std::array<const char*, core::state::MACRO_COUNT> macroItems{};
};

struct EditRenderData {
    std::array<std::array<char, 8>, 2> valueBuffers{};
    std::array<ms::ui::KeyValueRow, 2> rows{};
    std::array<char, 16> title{};
    std::array<char, 16> meta{};
    uint32_t dataRevision = 0;
    int selectedIndex = 0;
};

struct SelectorRenderData {
    const char* title = "";
    const char* meta = "";
    const char* const* items = nullptr;
    int itemCount = 0;
    int selectedIndex = 0;
    uint32_t dataRevision = 0;
    bool visible = false;
};

void initializeStaticItems(StaticItems& items);
EditRenderData buildEditRenderData(core::state::CoreState& state);
SelectorRenderData buildEditSelectorRenderData(const core::state::CoreState& state, const StaticItems& items);
SelectorRenderData buildPageSelectorRenderData(const core::state::CoreState& state);
SelectorRenderData buildTargetSelectorRenderData(const core::state::CoreState& state, const StaticItems& items);

}  // namespace core::context::standalone::macro_overlay_presenter
