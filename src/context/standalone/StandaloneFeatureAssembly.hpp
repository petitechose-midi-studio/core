#pragma once

#include <cstdint>
#include <memory>

#include "app/ExtmemAllocator.hpp"
#include <lvgl.h>

#include <oc/type/Ids.hpp>

#include "ui/OverlayTypes.hpp"

namespace core::state {
struct CoreState;
}

namespace core::ui {
class ContextSoftkeyBar;
class TransportBar;
}  // namespace core::ui

namespace oc::api {
class ButtonAPI;
class EncoderAPI;
class MidiAPI;
}  // namespace oc::api

namespace oc::context {
template <typename T>
class OverlayManager;
}  // namespace oc::context

namespace core::context::standalone {

class MacroFeatureModule;
class SequencerFeatureModule;
class SettingsFeatureModule;

class StandaloneFeatureAssembly {
public:
    StandaloneFeatureAssembly(core::state::CoreState& state,
                              oc::context::OverlayManager<core::ui::OverlayType>& overlays,
                              oc::api::EncoderAPI& encoders,
                              oc::api::ButtonAPI& buttons,
                              oc::api::MidiAPI& midi,
                              lv_obj_t* mainZone,
                              lv_obj_t* macroViewElement,
                              lv_obj_t* sequencerViewElement,
                              core::ui::ContextSoftkeyBar& contextSoftkeyBar,
                              core::ui::TransportBar& transportBar,
                              oc::type::ScopeID macroViewScope,
                              oc::type::ScopeID sequencerViewScope);
    ~StandaloneFeatureAssembly();

    StandaloneFeatureAssembly(const StandaloneFeatureAssembly&) = delete;
    StandaloneFeatureAssembly& operator=(const StandaloneFeatureAssembly&) = delete;

    void onMacroCC(uint8_t channel, uint8_t cc, uint8_t value) const;
    void onMacroNoteIn() const;
    void resetSequencerEncoderSync() const;
    void syncSequencerEncodersNow() const;

private:
    core::app::ExtmemUniquePtr<core::context::standalone::MacroFeatureModule> macro_feature_;
    core::app::ExtmemUniquePtr<core::context::standalone::SequencerFeatureModule> sequencer_feature_;
    core::app::ExtmemUniquePtr<core::context::standalone::SettingsFeatureModule> settings_feature_;
};

}  // namespace core::context::standalone
