#pragma once

#include <array>
#include <cstdint>

#include <ms/ui/widget/VirtualListKeyValueOverlay.hpp>
#include <ms/ui/widget/VirtualListSelectorOverlay.hpp>

#include <oc/state/Signal.hpp>

#include "state/MacroEditState.hpp"
#include "state/MacroState.hpp"
#include "state/macro/MacroUiState.hpp"
#include "state/macro/MacroPagesState.hpp"

namespace core::context::standalone::macro_overlay_presenter {

/**
 * Pure render-data builders for macro overlay presenters.
 *
 * Static selector labels and edit rows are prepared here; widget rendering and
 * macro mutations stay outside the formatter namespace.
 */
struct Source {
    core::state::MacroEditState& macroEdit;
    core::state::macro::MacroPagesState& pages;
    core::state::macro::MacroUiState& macroUi;
    oc::state::Signal<uint32_t>& configRevision;
};

struct StaticItems {
    std::array<std::array<char, 4>, 16> channelLabels{};
    std::array<const char*, 16> channelItems{};
    std::array<std::array<char, 4>, 128> ccLabels{};
    std::array<const char*, 128> ccItems{};
    std::array<std::array<char, 16>, core::state::MACRO_COUNT> macroLabels{};
    std::array<const char*, core::state::MACRO_COUNT> macroItems{};
};

struct EditRenderData {
    std::array<std::array<char, 12>, 3> valueBuffers{};
    std::array<ms::ui::KeyValueRow, 3> rows{};
    std::array<char, 16> title{};
    std::array<char, 16> meta{};
    uint32_t dataRevision = 0;
    int selectedIndex = 0;
};

struct AutomationRenderData {
    std::array<std::array<char, 16>, 4> valueBuffers{};
    std::array<ms::ui::KeyValueRow, 4> rows{};
    std::array<char, 16> title{};
    std::array<char, 16> meta{};
    uint32_t dataRevision = 0;
    int selectedIndex = 0;
    bool visible = false;
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
EditRenderData buildEditRenderData(Source& source);
AutomationRenderData buildAutomationRenderData(const Source& source);
SelectorRenderData buildEditSelectorRenderData(const Source& source, const StaticItems& items);
SelectorRenderData buildPageSelectorRenderData(const Source& source);
SelectorRenderData buildTargetSelectorRenderData(const Source& source, const StaticItems& items);

}  // namespace core::context::standalone::macro_overlay_presenter
