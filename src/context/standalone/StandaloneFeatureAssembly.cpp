#include "context/standalone/StandaloneFeatureAssembly.hpp"
#include <oc/api/ButtonAPI.hpp>
#include <oc/api/EncoderAPI.hpp>
#include <oc/api/MidiAPI.hpp>
#include <oc/context/OverlayManager.hpp>

#include <config/PlatformCompat.hpp>
#include "context/standalone/MacroFeatureModule.hpp"
#include "context/standalone/OverlayPresentationRegistry.hpp"
#include "context/standalone/ProjectFeatureModule.hpp"
#include "context/standalone/SequencerFeatureModule.hpp"
#include "context/standalone/SettingsFeatureModule.hpp"
#if OC_ENABLE_STATS
#include "diagnostics/MemoryFootprintReporter.hpp"
#endif
#include "handler/common/SharedTrackDomainServices.hpp"
#include "handler/macro/MacroEditDomainServices.hpp"
#include "handler/macro/MacroPerformanceDomainServices.hpp"
#include "handler/macro/MacroStructureDomainServices.hpp"
#include "handler/sequencer/SequencerHistoryDomainServices.hpp"
#include "handler/sequencer/SequencerChordPresetDomainServices.hpp"
#include "handler/sequencer/SequencerStepPresetDomainServices.hpp"
#include "handler/settings/DeviceSettingsDomainServices.hpp"
#include "handler/project/ProjectScaleSettingsDomainServices.hpp"
#include "persistence/ProductDirectoryCatalog.hpp"
#include "persistence/ProductFileService.hpp"
#include "state/CoreState.hpp"
#include "ui/transportbar/ContextSoftkeyBar.hpp"
#include "ui/transportbar/TransportBar.hpp"

namespace core::context::standalone {

FLASHMEM StandaloneFeatureAssembly::StandaloneFeatureAssembly(
    core::state::CoreState& state,
    core::persistence::ProductFileService& productFiles,
    core::persistence::ProductDirectoryCatalog& productCatalog,
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
) {
    macro_feature_ = core::app::makeExtmemUnique<core::context::standalone::MacroFeatureModule>(
        core::context::standalone::MacroFeatureModule::StateRefs{
            state.overlays,
            state.activeView,
            state.projectNavigation,
            state.macros,
            state.macroEdit,
            state.pages,
            state.macroUi,
            state.projectTracks,
            state.trackNavigation,
            state.sharedTrackActive,
            state.structureNavigationFocus,
            state.structureClipboard,
            state.configRevision,
            state.statusBar,
            state.macroHistory,
            &state.macroRuntimeOwnerRevision,
            state.midiCcCoordinator,
        },
        core::handler::MacroEditDomainServices::fromCoreState(state),
        core::handler::MacroPerformanceDomainServices::fromCoreState(state),
        core::handler::MacroStructureDomainServices::fromCoreState(state),
        overlays,
        overlayPresentations,
        encoders,
        buttons,
        overlayRoot,
        macroViewElement
#if defined(MS_UX_RECORDER)
        ,
        uxRegistry
#endif
    );
    if (!macro_feature_ || !macro_feature_->valid()) return;
#if OC_ENABLE_STATS
    core::diagnostics::logMemoryFootprint("standalone-feature-macro");
#endif
    sequencer_feature_ = core::app::makeExtmemUnique<core::context::standalone::SequencerFeatureModule>(
        core::context::standalone::SequencerFeatureModule::StateRefs{
            state.overlays,
            state.activeView,
            state.structureNavigationFocus,
            state.trackNavigation,
            state.projectNavigation,
            state.projectTrackEditor,
            state.projectTracks,
            state.sharedTrackActive,
            state.sharedTrackEnabledMask,
            state.structureClipboard,
            state.patternPitchSettings,
            state.sequencer,
            state.sequencerTracks,
            core::handler::SequencerHistoryDomainServices::fromCoreState(state),
            &state.sequencerTrackActivations,
            &state.statusBar,
            &state.pages,
            state.midiCcCoordinator,
        },
        core::handler::SharedTrackDomainServices::fromCoreState(state),
        core::state::project::ProjectTrackDomainServices::fromCoreState(state),
        core::handler::SequencerStepPresetDomainServices::fromCoreState(
            state,
            productFiles,
            productCatalog
        ),
        core::handler::SequencerChordPresetDomainServices::fromCoreState(
            state,
            productFiles,
            productCatalog
        ),
        overlays,
        overlayPresentations,
        encoders,
        buttons,
        midi,
        overlayRoot,
        sequencerViewElement
#if defined(MS_UX_RECORDER)
        ,
        uxRegistry
#endif
    );
    if (!sequencer_feature_ || !sequencer_feature_->valid()) return;
    if (auto* trackEditor = sequencer_feature_->trackEditorHandler()) {
        macro_feature_->attachTrackEditor(*trackEditor);
    }
#if OC_ENABLE_STATS
    core::diagnostics::logMemoryFootprint("standalone-feature-sequencer");
#endif
    const core::handler::DeviceSettingsDomainServices deviceSettingsServices{
        core::handler::DeviceSettingsDomainServices::StateRefs{
            state.midiSync,
            state.midiNoteDisplay,
            state.deviceSettingsStore,
        }
    };
    project_feature_ =
        core::app::makeExtmemUnique<core::context::standalone::ProjectFeatureModule>(
            core::context::standalone::ProjectFeatureModule::StateRefs{
                state.overlays,
                state.activeView,
                state.projectNavigation,
                state.projectTracks,
                core::state::project::ProjectTrackDomainServices::fromCoreState(
                    state
                ),
                state.statusBar,
                state.pages,
                state.macroUi,
                state.macros,
                state.macroEdit,
                state.configRevision,
                state.macroHistory,
                state.projectSettingsHistory,
                state.structureClipboard,
                core::handler::SequencerHistoryDomainServices::fromCoreState(state),
                core::handler::ProjectLifecycleDomainServices::fromCoreState(
                    state,
                    productFiles,
                    productCatalog
                ),
            },
            deviceSettingsServices,
            core::handler::ProjectScaleSettingsDomainServices{
                core::handler::ProjectScaleSettingsDomainServices::StateRefs{
                    state.sequencerTracks,
                }
            },
            core::handler::MacroEditDomainServices::fromCoreState(state),
            encoders,
            buttons,
            projectViewElement
#if defined(MS_UX_RECORDER)
            ,
            uxRegistry
#endif
        );
    if (!project_feature_ || !project_feature_->valid()) return;
#if OC_ENABLE_STATS
    core::diagnostics::logMemoryFootprint("standalone-feature-project");
#endif
    settings_feature_ = core::app::makeExtmemUnique<core::context::standalone::SettingsFeatureModule>(
        core::context::standalone::SettingsFeatureModule::StateRefs{
            state.deviceSettings,
            state.midiSync,
            state.midiNoteDisplay,
        },
        deviceSettingsServices,
        overlays,
        overlayPresentations,
        encoders,
        buttons,
        overlayRoot,
        deviceSettingsViewScope
#if defined(MS_UX_RECORDER)
        ,
        uxRegistry
#endif
    );
    if (!settings_feature_ || !settings_feature_->valid()) return;
#if OC_ENABLE_STATS
    core::diagnostics::logMemoryFootprint("standalone-feature-settings");
#endif
    valid_ = true;
}

FLASHMEM StandaloneFeatureAssembly::~StandaloneFeatureAssembly() = default;

void StandaloneFeatureAssembly::onMacroCC(uint8_t channel, uint8_t cc, uint8_t value) const {
    if (macro_feature_) {
        macro_feature_->onCC(channel, cc, value);
    }
}

void StandaloneFeatureAssembly::onMacroNoteIn() const {
    if (macro_feature_) {
        macro_feature_->onNoteIn();
    }
}

void StandaloneFeatureAssembly::update(uint32_t nowMs) const {
    if (macro_feature_) {
        macro_feature_->update(nowMs);
    }
    if (sequencer_feature_) {
        sequencer_feature_->update(nowMs);
    }
    if (project_feature_) {
        project_feature_->update(nowMs);
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

FLASHMEM void StandaloneFeatureAssembly::syncProjectEncoderNow() const {
    if (project_feature_) {
        project_feature_->syncFocusedEncoder();
    }
}

}  // namespace core::context::standalone
