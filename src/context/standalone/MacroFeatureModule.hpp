#pragma once

#include <memory>

#include <lvgl.h>

#include <oc/api/ButtonAPI.hpp>
#include <oc/api/EncoderAPI.hpp>
#include <oc/api/MidiAPI.hpp>
#include <oc/context/OverlayManager.hpp>
#include <oc/state/Signal.hpp>

#include "app/ExtmemAllocator.hpp"
#include "handler/macro/MacroEditDomainServices.hpp"
#include "handler/macro/MacroPerformanceDomainServices.hpp"
#include "handler/macro/MacroStructureDomainServices.hpp"
#include "state/MacroEditState.hpp"
#include "state/StructureClipboardState.hpp"
#include "state/TrackNavigationState.hpp"
#include "state/macro/MacroPagesState.hpp"
#include "state/macro/MacroUiState.hpp"
#include "app/OverlayTypes.hpp"
#include "app/ViewTypes.hpp"

namespace ms::ui {
class VirtualListKeyValueOverlay;
class VirtualListSelectorOverlay;
}

namespace core::context::standalone {

class MacroOverlayPresenter;

}  // namespace core::context::standalone

namespace core::handler {
class MacroEditHandler;
class MacroMidiHandler;
class MacroPerformanceHandler;
class MacroValueHandler;
}  // namespace core::handler

namespace core::context::standalone {

class MacroFeatureModule {
public:
    struct StateRefs {
        oc::state::Signal<core::ui::ViewType, 8>& activeView;
        core::state::MacroEditState& macroEdit;
        core::state::macro::MacroPagesState& pages;
        core::state::macro::MacroUiState& macroUi;
        core::state::TrackNavigationState& trackNavigation;
        oc::state::Signal<uint8_t, 8>& sharedTrackActive;
        oc::state::Signal<
            core::state::StructureNavigationFocus,
            core::state::kStructureNavigationFocusMaxSubscribers>& structureNavigationFocus;
        core::state::StructureClipboardState& structureClipboard;
        oc::state::Signal<uint32_t>& configRevision;
    };

    MacroFeatureModule(StateRefs stateRefs,
                       core::handler::MacroEditDomainServices editServices,
                       core::handler::MacroPerformanceDomainServices performanceServices,
                       core::handler::MacroStructureDomainServices structureServices,
                       oc::context::OverlayManager<core::ui::OverlayType>& overlays,
                       oc::api::EncoderAPI& encoders,
                       oc::api::ButtonAPI& buttons,
                       oc::api::MidiAPI& midi,
                       lv_obj_t* mainZone,
                       lv_obj_t* macroViewScope);
    ~MacroFeatureModule();

    MacroFeatureModule(const MacroFeatureModule&) = delete;
    MacroFeatureModule& operator=(const MacroFeatureModule&) = delete;

    void onCC(uint8_t channel, uint8_t cc, uint8_t value);
    void onNoteIn();

private:
    core::app::ExtmemUniquePtr<ms::ui::VirtualListKeyValueOverlay> edit_overlay_;
    core::app::ExtmemUniquePtr<ms::ui::VirtualListSelectorOverlay> edit_selector_overlay_;
    core::app::ExtmemUniquePtr<ms::ui::VirtualListSelectorOverlay> page_selector_overlay_;
    core::app::ExtmemUniquePtr<ms::ui::VirtualListSelectorOverlay> target_selector_overlay_;
    core::app::ExtmemUniquePtr<core::context::standalone::MacroOverlayPresenter> presenter_;
    std::unique_ptr<core::handler::MacroValueHandler> value_handler_;
    std::unique_ptr<core::handler::MacroMidiHandler> midi_handler_;
    std::unique_ptr<core::handler::MacroPerformanceHandler> performance_handler_;
    std::unique_ptr<core::handler::MacroEditHandler> edit_handler_;
};

}  // namespace core::context::standalone
