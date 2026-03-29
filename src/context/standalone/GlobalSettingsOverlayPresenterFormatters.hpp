#pragma once

#include <array>
#include <cstdint>

#include <ms/ui/widget/VirtualListKeyValueOverlay.hpp>
#include <ms/ui/widget/VirtualListSelectorOverlay.hpp>

namespace core::state {
struct CoreState;
}

namespace core::context::standalone::global_settings_presenter {

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

OverlayRenderData buildOverlayRenderData(const core::state::CoreState& state);
SelectorRenderData buildSelectorRenderData(const core::state::CoreState& state);

}  // namespace core::context::standalone::global_settings_presenter
