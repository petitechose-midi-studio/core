#pragma once

#include <array>
#include <cstdint>

#include <ms/ui/widget/VirtualListKeyValueOverlay.hpp>
#include <ms/ui/widget/VirtualListSelectorOverlay.hpp>

#include <oc/state/Signal.hpp>

#include "state/MacroEditState.hpp"
#include "state/MacroState.hpp"
#include "state/StatusBarState.hpp"
#include "state/macro/MacroUiState.hpp"
#include "state/macro/MacroPagesState.hpp"
#include "state/project/ProjectTrackState.hpp"
#include "state/StructureClipboardState.hpp"
#include "state/shared/MidiCcDestinationResolver.hpp"
#include "ui/strip/ContextActionStrip.hpp"
#include "ui/macro/MacroEditorPreviewModel.hpp"

namespace core::sequencer {
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
    const core::state::project::ProjectTrackState& projectTracks;
    core::state::macro::MacroUiState& macroUi;
    oc::state::Signal<uint32_t>& configRevision;
    core::state::StructureClipboardState* clipboard = nullptr;
    const core::sequencer::MidiCcGlobalFrameCoordinator*
        midiCcCoordinator = nullptr;
    const core::state::StatusBarState* statusBar = nullptr;
};

struct StaticItems {
    std::array<std::array<char, 4>, 128> ccLabels{};
    std::array<const char*, 128> ccItems{};
};

struct EditRenderData {
    std::array<std::array<char, 32>, 4> valueBuffers{};
    std::array<ms::ui::KeyValueRow, 4> rows{};
    std::array<char, 24> title{};
    std::array<char, 24> meta{};
    std::array<char, 24> interactionLabel{};
    std::array<char, 32> interactionValue{};
    const char* interactionIcon = nullptr;
    uint32_t interactionColor = 0;
    bool interactionOverlayVisible = false;
    uint32_t dataRevision = 0;
    uint32_t previewRevision = UINT32_MAX;
    uint32_t previewSessionOpenedAtMs = UINT32_MAX;
    float frozenTimelineTempoBpm = 120.0f;
    int selectedIndex = 0;
    int rowCount = 3;
    core::ui::MacroEditorPreviewModel preview{};
    core::ui::MacroEditorLiveValue live{};
};

struct AutomationRenderData {
    std::array<std::array<char, 32>, 7> valueBuffers{};
    std::array<ms::ui::KeyValueRow, 7> rows{};
    ms::ui::KeyValueRowProvider rowProvider = nullptr;
    void* rowProviderContext = nullptr;
    std::array<char, 24> title{};
    std::array<char, 24> meta{};
    uint32_t dataRevision = 0;
    int selectedIndex = 0;
    int rowCount = 0;
    bool visible = false;
    // A sparkline descriptor may outlive the formatter call. Keep its sampler
    // context in the presenter-owned render data instead of pointing at a
    // stack-local preview.
    core::ui::MacroEditorPreviewModel modulationPreview{};
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
core::ui::MacroEditorLiveValue buildEditLiveValue(const Source& source);
void buildEditRenderData(Source& source, EditRenderData& data);
EditRenderData buildEditRenderData(Source& source);
void buildAutomationRenderData(const Source& source, AutomationRenderData& data);
SelectorRenderData buildEditSelectorRenderData(const Source& source, const StaticItems& items);
core::ui::ContextActionStripProps buildEditActionStripProps(const Source& source);
core::ui::ContextActionStripProps buildDetailActionStripProps(const Source& source);

}  // namespace core::context::standalone::macro_overlay_presenter
