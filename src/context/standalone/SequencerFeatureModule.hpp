#pragma once

#include <memory>

#include <lvgl.h>

#include <oc/api/ButtonAPI.hpp>
#include <oc/api/EncoderAPI.hpp>
#include <oc/context/OverlayManager.hpp>
#include <oc/state/ExclusiveVisibilityStack.hpp>
#include <oc/state/Signal.hpp>

#include "app/ExtmemAllocator.hpp"
#include "state/StructureClipboardState.hpp"
#include "state/TrackNavigationState.hpp"
#include "state/sequencer/SequencerState.hpp"
#include "state/sequencer/SequencerTrackBankState.hpp"
#include "app/OverlayTypes.hpp"
#include "app/ViewTypes.hpp"

namespace ms::ui {
class VirtualListKeyValueOverlay;
}

namespace oc::api {
class MidiAPI;
}

namespace oc::interface {
class IEventBus;
}

namespace core::state {
struct CoreState;
}

namespace core::context::standalone {
class SequencerEncoderSyncCoordinator;
class SequencerOverlayPresenter;
}  // namespace core::context::standalone

namespace core::sequencer {
class SequencerRuntimeService;
}

namespace core::handler {
class SequencerMacroPropertyHandler;
class SequencerPatternQuickControlsHandler;
class SequencerPropertySelectorHandler;
class SequencerRangeActionHandler;
class SequencerStepEditHandler;
class SequencerStepHandler;
}  // namespace core::handler

namespace core::context::standalone {

class SequencerFeatureModule {
public:
    struct StateRefs {
        core::state::CoreState& coreState;
        oc::state::ExclusiveVisibilityStack<core::ui::OverlayType>& overlays;
        oc::state::Signal<core::ui::ViewType, 8>& activeView;
        oc::state::Signal<
            core::state::StructureNavigationFocus,
            core::state::kStructureNavigationFocusMaxSubscribers>& structureNavigationFocus;
        core::state::TrackNavigationState& trackNavigation;
        core::state::StructureClipboardState& structureClipboard;
        core::state::sequencer::SequencerState& sequencer;
        core::state::sequencer::SequencerTrackBankState& sequencerTracks;
    };

    SequencerFeatureModule(StateRefs stateRefs,
                           oc::context::OverlayManager<core::ui::OverlayType>& overlays,
                           oc::api::EncoderAPI& encoders,
                           oc::api::ButtonAPI& buttons,
                           oc::api::MidiAPI& midi,
                           oc::interface::IEventBus& eventBus,
                           lv_obj_t* sequencerViewScope);
    ~SequencerFeatureModule();

    SequencerFeatureModule(const SequencerFeatureModule&) = delete;
    SequencerFeatureModule& operator=(const SequencerFeatureModule&) = delete;

    void resetEncoderSync();
    void syncEncodersNow();
    void update();

private:
    std::unique_ptr<core::sequencer::SequencerRuntimeService> runtime_;
    core::app::ExtmemUniquePtr<core::context::standalone::SequencerEncoderSyncCoordinator>
        encoder_sync_;
    core::app::ExtmemUniquePtr<ms::ui::VirtualListKeyValueOverlay> step_edit_overlay_;
    core::app::ExtmemUniquePtr<core::context::standalone::SequencerOverlayPresenter> presenter_;
    std::unique_ptr<core::handler::SequencerStepHandler> step_handler_;
    std::unique_ptr<core::handler::SequencerRangeActionHandler> range_action_handler_;
    std::unique_ptr<core::handler::SequencerPatternQuickControlsHandler> quick_controls_handler_;
    std::unique_ptr<core::handler::SequencerStepEditHandler> step_edit_handler_;
    std::unique_ptr<core::handler::SequencerPropertySelectorHandler> property_selector_handler_;
    std::unique_ptr<core::handler::SequencerMacroPropertyHandler> macro_property_handler_;
};

}  // namespace core::context::standalone
