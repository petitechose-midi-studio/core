#pragma once

#include <lvgl.h>

#include <oc/api/ButtonAPI.hpp>
#include <oc/api/EncoderAPI.hpp>
#include <oc/state/ExclusiveVisibilityStack.hpp>

#include "app/ExtmemAllocator.hpp"
#include "app/OverlayTypes.hpp"
#include "handler/project/ProjectHandler.hpp"
#include "handler/project/ProjectLifecycleDomainServices.hpp"
#include "handler/sequencer/SequencerHistoryDomainServices.hpp"
#include "handler/settings/SequencerSettingsDomainServices.hpp"
#include "state/MidiSyncState.hpp"
#include "state/MacroEditState.hpp"
#include "state/MacroState.hpp"
#include "state/project/ProjectNavigationState.hpp"
#include "state/project/ProjectTrackDomainServices.hpp"
#include "state/project/ProjectSettingsHistory.hpp"
#include "state/macro/MacroPagesState.hpp"
#include "state/macro/MacroUiState.hpp"
#include "state/StatusBarState.hpp"

#if defined(MS_UX_RECORDER)
#include "context/standalone/ux/StandaloneUxSurfaces.hpp"
#endif

namespace core::context::standalone {

class ProjectFeatureModule {
public:
    struct StateRefs {
        oc::state::ExclusiveVisibilityStack<core::ui::OverlayType>& overlays;
        oc::state::Signal<core::ui::ViewType, 8>& activeView;
        core::state::project::ProjectNavigationState& navigation;
        core::state::project::ProjectTrackState& projectTracks;
        core::state::project::ProjectTrackDomainServices trackDomain;
        core::state::StatusBarState& statusBar;
        core::state::MidiSyncState& midiSync;
        core::state::macro::MacroPagesState& pages;
        core::state::macro::MacroUiState& macroUi;
        core::state::MacroState& macros;
        core::state::MacroEditState& macroEdit;
        oc::state::Signal<uint32_t>& configRevision;
        core::state::macro::MacroHistoryService& macroHistory;
        core::state::project::ProjectSettingsHistoryService& settingsHistory;
        core::state::StructureClipboardState& clipboard;
        core::handler::SequencerHistoryDomainServices history;
        core::handler::ProjectLifecycleDomainServices lifecycle;
    };

    ProjectFeatureModule(StateRefs stateRefs,
                         core::handler::SequencerSettingsDomainServices sequencerSettings,
                         core::handler::MacroEditDomainServices macroEditServices,
                         oc::api::EncoderAPI& encoders,
                         oc::api::ButtonAPI& buttons,
                         lv_obj_t* projectViewElement
#if defined(MS_UX_RECORDER)
                         ,
                         core::validation::ux::SemanticUxSurfaceRegistry* uxRegistry
#endif
    );
    ~ProjectFeatureModule();

    ProjectFeatureModule(const ProjectFeatureModule&) = delete;
    ProjectFeatureModule& operator=(const ProjectFeatureModule&) = delete;

    [[nodiscard]] bool valid() const { return static_cast<bool>(handler_); }
    void syncFocusedEncoder() const;
    void update(uint32_t nowMs);

private:
#if defined(MS_UX_RECORDER)
    core::context::standalone::ux::ProjectModulatorsUxSurface
        modulators_ux_surface_;
#endif
    core::app::ExtmemUniquePtr<core::handler::ProjectHandler> handler_;
};

}  // namespace core::context::standalone
