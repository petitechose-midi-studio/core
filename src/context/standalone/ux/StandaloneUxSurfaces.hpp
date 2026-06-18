#pragma once

#if defined(MS_UX_RECORDER)

#include <cstdint>

#include <oc/state/Signal.hpp>

#include "app/ViewTypes.hpp"
#include "context/standalone/MacroOverlayPresenterFormatters.hpp"
#include "state/StructureSelectionState.hpp"
#include "validation/ux/SemanticUxSurface.hpp"

namespace core::state {
struct DeviceSettingsState;
struct MacroEditState;
struct MacroState;
struct DataManagerState;
struct MidiSyncState;
struct StatusBarState;
struct StructureClipboardState;
struct TrackNavigationState;
struct ViewSelectorState;
namespace macro {
struct MacroPagesState;
struct MacroUiState;
}
namespace sequencer {
struct SequencerState;
struct SequencerTrackBankState;
}
}

namespace core::validation::ux {
struct StructureUxTraceState;
}

namespace core::context::standalone::ux {

namespace priority {
constexpr uint8_t DEVICE_SETTINGS = 10;
constexpr uint8_t VIEW_SELECTOR = 15;
constexpr uint8_t TRANSPORT = 20;
constexpr uint8_t SEQUENCER_STEP_EDIT = 25;
constexpr uint8_t SEQUENCER_PROPERTY_SELECTOR = 30;
constexpr uint8_t SEQUENCER_QUICK_CONTROLS = 35;
constexpr uint8_t SEQUENCER_STRUCTURE = 40;
constexpr uint8_t SEQUENCER_STEP_GRID = 50;
constexpr uint8_t MACRO_STRUCTURE = 52;
constexpr uint8_t MACRO_PERFORMANCE = 54;
constexpr uint8_t MACRO_EDIT = 55;
constexpr uint8_t MACRO_VALUE = 60;
constexpr uint8_t DATA_MANAGER = 70;
}  // namespace priority

class ViewSelectorUxSurface final : public core::validation::ux::SemanticUxSurface {
public:
    ViewSelectorUxSurface(oc::state::Signal<core::ui::ViewType, 8>& activeView,
                          core::state::ViewSelectorState& viewSelector);

    bool captureSemanticUxContext(
        const oc::core::input::InputBindingTraceEvent& event,
        core::validation::ux::SemanticUxContext& out
    ) const override;

private:
    oc::state::Signal<core::ui::ViewType, 8>& active_view_;
    core::state::ViewSelectorState& view_selector_;
};

class DeviceSettingsUxSurface final : public core::validation::ux::SemanticUxSurface {
public:
    DeviceSettingsUxSurface(core::state::DeviceSettingsState& deviceSettings,
                            core::state::MidiSyncState& midiSync);

    bool captureSemanticUxContext(
        const oc::core::input::InputBindingTraceEvent& event,
        core::validation::ux::SemanticUxContext& out
    ) const override;

private:
    core::state::DeviceSettingsState& device_settings_;
    core::state::MidiSyncState& midi_sync_;
};

class TransportUxSurface final : public core::validation::ux::SemanticUxSurface {
public:
    explicit TransportUxSurface(core::state::StatusBarState& statusBar);

    bool captureSemanticUxContext(
        const oc::core::input::InputBindingTraceEvent& event,
        core::validation::ux::SemanticUxContext& out
    ) const override;

private:
    core::state::StatusBarState& status_bar_;
};

class SequencerPropertySelectorUxSurface final : public core::validation::ux::SemanticUxSurface {
public:
    SequencerPropertySelectorUxSurface(
        oc::state::Signal<core::ui::ViewType, 8>& activeView,
        core::state::sequencer::SequencerState& sequencer
    );

    bool captureSemanticUxContext(
        const oc::core::input::InputBindingTraceEvent& event,
        core::validation::ux::SemanticUxContext& out
    ) const override;

private:
    oc::state::Signal<core::ui::ViewType, 8>& active_view_;
    core::state::sequencer::SequencerState& sequencer_;
};

class SequencerStepGridUxSurface final : public core::validation::ux::SemanticUxSurface {
public:
    SequencerStepGridUxSurface(
        oc::state::Signal<core::ui::ViewType, 8>& activeView,
        oc::state::Signal<
            core::state::StructureNavigationFocus,
            core::state::kStructureNavigationFocusMaxSubscribers>& navigationFocus,
        core::state::TrackNavigationState& trackNavigation,
        core::state::sequencer::SequencerState& sequencer,
        core::state::sequencer::SequencerTrackBankState& tracks
    );

    bool captureSemanticUxContext(
        const oc::core::input::InputBindingTraceEvent& event,
        core::validation::ux::SemanticUxContext& out
    ) const override;

private:
    oc::state::Signal<core::ui::ViewType, 8>& active_view_;
    oc::state::Signal<
        core::state::StructureNavigationFocus,
        core::state::kStructureNavigationFocusMaxSubscribers>& navigation_focus_;
    core::state::TrackNavigationState& track_navigation_;
    core::state::sequencer::SequencerState& sequencer_;
    core::state::sequencer::SequencerTrackBankState& tracks_;
};

class SequencerStepEditUxSurface final : public core::validation::ux::SemanticUxSurface {
public:
    SequencerStepEditUxSurface(
        oc::state::Signal<core::ui::ViewType, 8>& activeView,
        oc::state::Signal<
            core::state::StructureNavigationFocus,
            core::state::kStructureNavigationFocusMaxSubscribers>& navigationFocus,
        core::state::TrackNavigationState& trackNavigation,
        core::state::sequencer::SequencerState& sequencer,
        core::state::sequencer::SequencerTrackBankState& tracks
    );

    bool captureSemanticUxContext(
        const oc::core::input::InputBindingTraceEvent& event,
        core::validation::ux::SemanticUxContext& out
    ) const override;

private:
    oc::state::Signal<core::ui::ViewType, 8>& active_view_;
    oc::state::Signal<
        core::state::StructureNavigationFocus,
        core::state::kStructureNavigationFocusMaxSubscribers>& navigation_focus_;
    core::state::TrackNavigationState& track_navigation_;
    core::state::sequencer::SequencerState& sequencer_;
    core::state::sequencer::SequencerTrackBankState& tracks_;
};

class SequencerQuickControlsUxSurface final : public core::validation::ux::SemanticUxSurface {
public:
    SequencerQuickControlsUxSurface(
        oc::state::Signal<core::ui::ViewType, 8>& activeView,
        core::state::sequencer::SequencerState& sequencer
    );

    bool captureSemanticUxContext(
        const oc::core::input::InputBindingTraceEvent& event,
        core::validation::ux::SemanticUxContext& out
    ) const override;

private:
    oc::state::Signal<core::ui::ViewType, 8>& active_view_;
    core::state::sequencer::SequencerState& sequencer_;
};

class SequencerStructureUxSurface final : public core::validation::ux::SemanticUxSurface {
public:
    SequencerStructureUxSurface(
        oc::state::Signal<core::ui::ViewType, 8>& activeView,
        oc::state::Signal<
            core::state::StructureNavigationFocus,
            core::state::kStructureNavigationFocusMaxSubscribers>& navigationFocus,
        core::state::TrackNavigationState& trackNavigation,
        core::state::StructureClipboardState& structureClipboard,
        core::state::sequencer::SequencerState& sequencer,
        core::state::sequencer::SequencerTrackBankState& tracks,
        const core::validation::ux::StructureUxTraceState* traceState
    );

    bool captureSemanticUxContext(
        const oc::core::input::InputBindingTraceEvent& event,
        core::validation::ux::SemanticUxContext& out
    ) const override;

private:
    oc::state::Signal<core::ui::ViewType, 8>& active_view_;
    oc::state::Signal<
        core::state::StructureNavigationFocus,
        core::state::kStructureNavigationFocusMaxSubscribers>& navigation_focus_;
    core::state::TrackNavigationState& track_navigation_;
    core::state::StructureClipboardState& structure_clipboard_;
    core::state::sequencer::SequencerState& sequencer_;
    core::state::sequencer::SequencerTrackBankState& tracks_;
    const core::validation::ux::StructureUxTraceState* trace_state_ = nullptr;
};

class MacroValueUxSurface final : public core::validation::ux::SemanticUxSurface {
public:
    MacroValueUxSurface(oc::state::Signal<core::ui::ViewType, 8>& activeView,
                        core::state::MacroState& macros,
                        core::state::macro::MacroPagesState& pages,
                        core::state::macro::MacroUiState& macroUi,
                        core::state::MacroEditState& macroEdit);

    bool captureSemanticUxContext(
        const oc::core::input::InputBindingTraceEvent& event,
        core::validation::ux::SemanticUxContext& out
    ) const override;

private:
    oc::state::Signal<core::ui::ViewType, 8>& active_view_;
    core::state::MacroState& macros_;
    core::state::macro::MacroPagesState& pages_;
    core::state::macro::MacroUiState& macro_ui_;
    core::state::MacroEditState& macro_edit_;
};

class MacroPerformanceUxSurface final : public core::validation::ux::SemanticUxSurface {
public:
    MacroPerformanceUxSurface(oc::state::Signal<core::ui::ViewType, 8>& activeView,
                              core::state::macro::MacroUiState& macroUi,
                              core::state::MacroEditState& macroEdit);

    bool captureSemanticUxContext(
        const oc::core::input::InputBindingTraceEvent& event,
        core::validation::ux::SemanticUxContext& out
    ) const override;

private:
    oc::state::Signal<core::ui::ViewType, 8>& active_view_;
    core::state::macro::MacroUiState& macro_ui_;
    core::state::MacroEditState& macro_edit_;
};

class MacroStructureUxSurface final : public core::validation::ux::SemanticUxSurface {
public:
    MacroStructureUxSurface(
        oc::state::Signal<core::ui::ViewType, 8>& activeView,
        oc::state::Signal<
            core::state::StructureNavigationFocus,
            core::state::kStructureNavigationFocusMaxSubscribers>& navigationFocus,
        core::state::TrackNavigationState& trackNavigation,
        core::state::StructureClipboardState& structureClipboard,
        core::state::macro::MacroUiState& macroUi,
        core::state::macro::MacroPagesState& pages,
        core::state::MacroEditState& macroEdit,
        const core::validation::ux::StructureUxTraceState* traceState
    );

    bool captureSemanticUxContext(
        const oc::core::input::InputBindingTraceEvent& event,
        core::validation::ux::SemanticUxContext& out
    ) const override;

private:
    oc::state::Signal<core::ui::ViewType, 8>& active_view_;
    oc::state::Signal<
        core::state::StructureNavigationFocus,
        core::state::kStructureNavigationFocusMaxSubscribers>& navigation_focus_;
    core::state::TrackNavigationState& track_navigation_;
    core::state::StructureClipboardState& structure_clipboard_;
    core::state::macro::MacroUiState& macro_ui_;
    core::state::macro::MacroPagesState& pages_;
    core::state::MacroEditState& macro_edit_;
    const core::validation::ux::StructureUxTraceState* trace_state_ = nullptr;
};

class MacroEditUxSurface final : public core::validation::ux::SemanticUxSurface {
public:
    MacroEditUxSurface(oc::state::Signal<core::ui::ViewType, 8>& activeView,
                       core::state::MacroEditState& macroEdit,
                       core::state::macro::MacroPagesState& pages,
                       oc::state::Signal<uint32_t>& configRevision);

    bool captureSemanticUxContext(
        const oc::core::input::InputBindingTraceEvent& event,
        core::validation::ux::SemanticUxContext& out
    ) const override;

private:
    oc::state::Signal<core::ui::ViewType, 8>& active_view_;
    core::state::MacroEditState& macro_edit_;
    core::state::macro::MacroPagesState& pages_;
    oc::state::Signal<uint32_t>& config_revision_;
    core::context::standalone::macro_overlay_presenter::StaticItems static_items_;
};

class DataManagerUxSurface final : public core::validation::ux::SemanticUxSurface {
public:
    explicit DataManagerUxSurface(core::state::DataManagerState& dataManager);

    bool captureSemanticUxContext(
        const oc::core::input::InputBindingTraceEvent& event,
        core::validation::ux::SemanticUxContext& out
    ) const override;

private:
    core::state::DataManagerState& data_manager_;
};

}  // namespace core::context::standalone::ux

#endif
