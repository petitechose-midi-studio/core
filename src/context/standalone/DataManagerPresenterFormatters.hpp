#pragma once

#include <array>
#include <cstdint>

#include <ms/ui/widget/VirtualListKeyValueOverlay.hpp>
#include <ms/ui/widget/VirtualListSelectorOverlay.hpp>

#include "state/DataManagerCatalog.hpp"

namespace core::state {
struct CoreState;
}

namespace core::context::standalone::data_manager_presenter {

struct OverlayRenderData {
    std::array<ms::ui::KeyValueRow, 2> rows{};
    uint32_t dataRevision = 0;
    const char* title = "";
    const char* meta = "";
    int selectedIndex = 0;
};

struct DialogRenderData {
    std::array<std::array<char, 8>, 32> slotLabels{};
    std::array<const char*, 32> slotItems{};
    std::array<const char*, core::state::DATA_MANAGER_MAX_COMMANDS_PER_CONTEXT> commandItems{};
    uint32_t dataRevision = 0;
    const char* title = "";
    const char* meta = "";
    const char* const* items = nullptr;
    int itemCount = 0;
    int selectedIndex = 0;
    bool visible = false;
};

struct SoftkeyRenderData {
    std::array<char, 24> leftLabel{};
    std::array<char, 24> rightLabel{};
    bool visible = false;
};

OverlayRenderData buildOverlayRenderData(const core::state::CoreState& state);
DialogRenderData buildDialogRenderData(const core::state::CoreState& state);
SoftkeyRenderData buildSoftkeyRenderData(const core::state::CoreState& state);

}  // namespace core::context::standalone::data_manager_presenter
