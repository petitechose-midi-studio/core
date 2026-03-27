#pragma once

#include <memory>

#include <lvgl.h>

#include <oc/api/ButtonAPI.hpp>
#include <oc/api/EncoderAPI.hpp>
#include <oc/api/MidiAPI.hpp>
#include <oc/context/OverlayManager.hpp>

#include "state/CoreState.hpp"
#include "ui/OverlayTypes.hpp"

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
class MacroValueHandler;
}  // namespace core::handler

namespace core::context::standalone {

class MacroFeatureModule {
public:
    MacroFeatureModule(core::state::CoreState& state,
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
    std::unique_ptr<ms::ui::VirtualListKeyValueOverlay> edit_overlay_;
    std::unique_ptr<ms::ui::VirtualListSelectorOverlay> edit_selector_overlay_;
    std::unique_ptr<ms::ui::VirtualListSelectorOverlay> page_selector_overlay_;
    std::unique_ptr<ms::ui::VirtualListSelectorOverlay> target_selector_overlay_;
    std::unique_ptr<core::context::standalone::MacroOverlayPresenter> presenter_;
    std::unique_ptr<core::handler::MacroValueHandler> value_handler_;
    std::unique_ptr<core::handler::MacroMidiHandler> midi_handler_;
    std::unique_ptr<core::handler::MacroEditHandler> edit_handler_;
};

}  // namespace core::context::standalone
