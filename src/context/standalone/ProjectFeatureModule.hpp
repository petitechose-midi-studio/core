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
#include "state/project/ProjectNavigationState.hpp"
#include "state/sequencer/SequencerState.hpp"
#include "state/sequencer/SequencerTrackBankState.hpp"
#include "state/StatusBarState.hpp"

namespace core::context::standalone {

class ProjectFeatureModule {
public:
    struct StateRefs {
        oc::state::ExclusiveVisibilityStack<core::ui::OverlayType>& overlays;
        core::state::project::ProjectNavigationState& navigation;
        core::state::sequencer::SequencerState& sequencer;
        core::state::sequencer::SequencerTrackBankState& sequencerTracks;
        core::state::StatusBarState& statusBar;
        core::state::MidiSyncState& midiSync;
        core::handler::SequencerHistoryDomainServices history;
        core::handler::ProjectLifecycleDomainServices lifecycle;
    };

    ProjectFeatureModule(StateRefs stateRefs,
                         core::handler::SequencerSettingsDomainServices sequencerSettings,
                         oc::api::EncoderAPI& encoders,
                         oc::api::ButtonAPI& buttons,
                         lv_obj_t* projectViewElement);
    ~ProjectFeatureModule();

    ProjectFeatureModule(const ProjectFeatureModule&) = delete;
    ProjectFeatureModule& operator=(const ProjectFeatureModule&) = delete;

    void syncFocusedEncoder() const;

private:
    core::app::ExtmemUniquePtr<core::handler::ProjectHandler> handler_;
};

}  // namespace core::context::standalone
