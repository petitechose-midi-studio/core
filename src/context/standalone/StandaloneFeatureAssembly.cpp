#include "context/standalone/StandaloneFeatureAssembly.hpp"
#include <oc/api/ButtonAPI.hpp>
#include <oc/api/EncoderAPI.hpp>
#include <oc/api/MidiAPI.hpp>
#include <oc/context/OverlayManager.hpp>

#include <config/PlatformCompat.hpp>
#include "context/standalone/MacroFeatureModule.hpp"
#include "context/standalone/SequencerFeatureModule.hpp"
#include "context/standalone/SettingsFeatureModule.hpp"
#include "handler/common/SharedTrackDomainServices.hpp"
#include "handler/macro/MacroEditDomainServices.hpp"
#include "handler/macro/MacroPerformanceDomainServices.hpp"
#include "handler/macro/MacroStructureDomainServices.hpp"
#include "handler/settings/DataManagerDomainServices.hpp"
#include "handler/settings/GlobalSettingsDomainServices.hpp"
#include "state/CoreState.hpp"
#include "ui/transportbar/ContextSoftkeyBar.hpp"
#include "ui/transportbar/TransportBar.hpp"

namespace core::context::standalone {

FLASHMEM StandaloneFeatureAssembly::StandaloneFeatureAssembly(
    core::state::CoreState& state,
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
    oc::type::ScopeID sequencerViewScope
) {
    OC_LOG_DEBUG("StandaloneFeatureAssembly: macro_feature");
    macro_feature_ = core::app::makeExtmemUnique<core::context::standalone::MacroFeatureModule>(
        core::context::standalone::MacroFeatureModule::StateRefs{
            state.activeView,
            state.macroEdit,
            state.pages,
            state.macroUi,
            state.trackNavigation,
            state.sharedTrackActive,
            state.structureNavigationFocus,
            state.structureClipboard,
            state.configRevision,
        },
        core::handler::MacroEditDomainServices::fromCoreState(state),
        core::handler::MacroPerformanceDomainServices::fromCoreState(state),
        core::handler::MacroStructureDomainServices::fromCoreState(state),
        overlays,
        encoders,
        buttons,
        midi,
        mainZone,
        macroViewElement
    );
    OC_LOG_DEBUG("StandaloneFeatureAssembly: sequencer_feature");
    sequencer_feature_ = core::app::makeExtmemUnique<core::context::standalone::SequencerFeatureModule>(
        core::context::standalone::SequencerFeatureModule::StateRefs{
            state.overlays,
            state.activeView,
            state.structureNavigationFocus,
            state.trackNavigation,
            state.structureClipboard,
            state.sequencer,
            state.sequencerTracks,
        },
        core::handler::SharedTrackDomainServices::fromCoreState(state),
        overlays,
        encoders,
        buttons,
        sequencerViewElement
    );
    OC_LOG_DEBUG("StandaloneFeatureAssembly: settings_feature");
    settings_feature_ = core::app::makeExtmemUnique<core::context::standalone::SettingsFeatureModule>(
        core::context::standalone::SettingsFeatureModule::StateRefs{
            state.globalSettings,
            state.midiSync,
            state.settings,
            state.dataManager,
            state.activeView,
        },
        core::handler::GlobalSettingsDomainServices{
            core::handler::GlobalSettingsDomainServices::StateRefs{
                state.midiSync,
                state.settings,
            }
        },
        core::handler::DataManagerDomainServices::fromCoreState(state),
        overlays,
        encoders,
        buttons,
        mainZone,
        contextSoftkeyBar,
        transportBar,
        core::handler::DataManagerHandler::ViewScopes{
            macroViewScope,
            sequencerViewScope,
        }
    );
    OC_LOG_DEBUG("StandaloneFeatureAssembly: ready");
}

FLASHMEM StandaloneFeatureAssembly::~StandaloneFeatureAssembly() = default;

FLASHMEM void StandaloneFeatureAssembly::onMacroCC(uint8_t channel, uint8_t cc, uint8_t value) const {
    if (macro_feature_) {
        macro_feature_->onCC(channel, cc, value);
    }
}

FLASHMEM void StandaloneFeatureAssembly::onMacroNoteIn() const {
    if (macro_feature_) {
        macro_feature_->onNoteIn();
    }
}

FLASHMEM void StandaloneFeatureAssembly::resetSequencerEncoderSync() const {
    if (sequencer_feature_) {
        sequencer_feature_->resetEncoderSync();
    }
}

FLASHMEM void StandaloneFeatureAssembly::syncSequencerEncodersNow() const {
    if (sequencer_feature_) {
        sequencer_feature_->syncEncodersNow();
    }
}

}  // namespace core::context::standalone
