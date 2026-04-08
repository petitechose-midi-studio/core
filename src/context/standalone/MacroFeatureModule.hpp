#pragma once

#include <memory>

#include <lvgl.h>

#include <oc/api/ButtonAPI.hpp>
#include <oc/api/EncoderAPI.hpp>
#include <oc/api/MidiAPI.hpp>
#include <oc/context/OverlayManager.hpp>
#include <oc/state/Signal.hpp>

#include "app/ExtmemAllocator.hpp"
#include "handler/macro/MacroDomainServices.hpp"
#include "state/MacroEditState.hpp"
#include "state/macro/MacroPagesState.hpp"
#include "state/macro/MacroUiState.hpp"
#include "ui/OverlayTypes.hpp"
#include "ui/ViewTypes.hpp"

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
        oc::state::Signal<uint32_t>& configRevision;
    };

    MacroFeatureModule(StateRefs stateRefs,
                       core::handler::MacroDomainServices services,
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
