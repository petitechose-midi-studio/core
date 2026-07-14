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
#include "state/StructureClipboardState.hpp"
#include "state/shared/MidiCcDestinationResolver.hpp"
#include "ui/strip/ContextActionStrip.hpp"
#include "ui/macro/MacroEditorPreviewModel.hpp"

namespace core::handler {
class MidiCcGlobalFrameCoordinator;
}

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
    core::state::StructureClipboardState* clipboard = nullptr;
    const core::handler::MidiCcGlobalFrameCoordinator* midiCcCoordinator = nullptr;
};

struct StaticItems {
    std::array<std::array<char, 4>, 128> ccLabels{};
    std::array<const char*, 128> ccItems{};
    std::array<std::array<char, 16>, core::state::MACRO_COUNT> macroLabels{};
    std::array<const char*, core::state::MACRO_COUNT> macroItems{};
};

struct EditRenderData {
    std::array<std::array<char, 32>, 4> valueBuffers{};
    std::array<ms::ui::KeyValueRow, 4> rows{};
    std::array<char, 24> title{};
    std::array<char, 24> meta{};
    uint32_t dataRevision = 0;
    uint32_t previewRevision = UINT32_MAX;
    int selectedIndex = 0;
    int rowCount = 3;
    core::ui::MacroEditorPreviewModel preview{};
};

struct AutomationRenderData {
    std::array<std::array<char, 32>, 7> valueBuffers{};
    std::array<ms::ui::KeyValueRow, 7> rows{};
    std::array<char, 24> title{};
    std::array<char, 24> meta{};
    uint32_t dataRevision = 0;
    int selectedIndex = 0;
    int rowCount = 0;
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
void buildEditRenderData(Source& source, EditRenderData& data);
EditRenderData buildEditRenderData(Source& source);
AutomationRenderData buildAutomationRenderData(const Source& source);
SelectorRenderData buildEditSelectorRenderData(const Source& source, const StaticItems& items);
SelectorRenderData buildPageSelectorRenderData(const Source& source);
SelectorRenderData buildTargetSelectorRenderData(const Source& source, const StaticItems& items);
core::ui::ContextActionStripProps buildEditActionStripProps(const Source& source);
core::ui::ContextActionStripProps buildDetailActionStripProps(const Source& source);

}  // namespace core::context::standalone::macro_overlay_presenter
