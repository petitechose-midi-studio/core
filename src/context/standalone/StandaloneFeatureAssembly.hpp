#pragma once

#include <cstdint>
#include <memory>

#include "app/ExtmemAllocator.hpp"
#include <lvgl.h>

#include <oc/type/Ids.hpp>

#include "app/OverlayTypes.hpp"

namespace core::state {
struct CoreState;
}

namespace core::persistence {
class ProductFileService;
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
class OverlayPresentationRegistry;
class ProjectFeatureModule;
class SequencerFeatureModule;
class SettingsFeatureModule;

}  // namespace core::context::standalone

namespace core::validation::ux {
class SemanticUxSurfaceRegistry;
}

namespace core::context::standalone {

/**
 * Composes feature modules after UI and overlay assemblies exist.
 *
 * This class only wires feature modules and forwards external MIDI/encoder-sync
 * events to them. It does not own CoreState or drive the sequencer runtime tick.
 */
class StandaloneFeatureAssembly {
public:
    StandaloneFeatureAssembly(core::state::CoreState& state,
                              core::persistence::ProductFileService& productFiles,
                              oc::context::OverlayManager<core::ui::OverlayType>& overlays,
                              OverlayPresentationRegistry& overlayPresentations,
                              oc::api::EncoderAPI& encoders,
                              oc::api::ButtonAPI& buttons,
                              oc::api::MidiAPI& midi,
                              lv_obj_t* overlayRoot,
                              lv_obj_t* macroViewElement,
                              lv_obj_t* sequencerViewElement,
                              lv_obj_t* projectViewElement,
                              core::ui::ContextSoftkeyBar& contextSoftkeyBar,
                              core::ui::TransportBar& transportBar,
                              oc::type::ScopeID macroViewScope,
                              oc::type::ScopeID sequencerViewScope,
                              oc::type::ScopeID deviceSettingsViewScope
#if defined(MS_UX_RECORDER)
                              ,
                              core::validation::ux::SemanticUxSurfaceRegistry* uxRegistry
#endif
    );
    ~StandaloneFeatureAssembly();

    StandaloneFeatureAssembly(const StandaloneFeatureAssembly&) = delete;
    StandaloneFeatureAssembly& operator=(const StandaloneFeatureAssembly&) = delete;

    [[nodiscard]] bool valid() const { return valid_; }
    void onMacroCC(uint8_t channel, uint8_t cc, uint8_t value) const;
    void onMacroNoteIn() const;
    void update(uint32_t nowMs) const;
    void resetSequencerEncoderSync() const;
    void syncSequencerEncodersNow() const;
    void syncProjectEncoderNow() const;

private:
    core::app::ExtmemUniquePtr<core::context::standalone::MacroFeatureModule> macro_feature_;
    core::app::ExtmemUniquePtr<core::context::standalone::SequencerFeatureModule> sequencer_feature_;
    core::app::ExtmemUniquePtr<core::context::standalone::ProjectFeatureModule> project_feature_;
    core::app::ExtmemUniquePtr<core::context::standalone::SettingsFeatureModule> settings_feature_;
    bool valid_ = false;
};

}  // namespace core::context::standalone
