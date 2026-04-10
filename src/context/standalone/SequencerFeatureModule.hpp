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
#include "state/sequencer/SequencerState.hpp"
#include "state/sequencer/SequencerTrackBankState.hpp"
#include "ui/OverlayTypes.hpp"
#include "ui/ViewTypes.hpp"

namespace ms::ui {
class VirtualListKeyValueOverlay;
}

namespace core::context::standalone {
class SequencerEncoderSyncCoordinator;
class SequencerOverlayPresenter;
}  // namespace core::context::standalone

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
        oc::state::ExclusiveVisibilityStack<core::ui::OverlayType>& overlays;
        oc::state::Signal<core::ui::ViewType, 8>& activeView;
    oc::state::Signal<
        core::state::StructureNavigationFocus,
        core::state::kStructureNavigationFocusMaxSubscribers>& structureNavigationFocus;
        core::state::StructureClipboardState& structureClipboard;
        core::state::sequencer::SequencerState& sequencer;
        core::state::sequencer::SequencerTrackBankState& sequencerTracks;
    };

    SequencerFeatureModule(StateRefs stateRefs,
                           oc::context::OverlayManager<core::ui::OverlayType>& overlays,
                           oc::api::EncoderAPI& encoders,
                           oc::api::ButtonAPI& buttons,
                           lv_obj_t* sequencerViewScope);
    ~SequencerFeatureModule();

    SequencerFeatureModule(const SequencerFeatureModule&) = delete;
    SequencerFeatureModule& operator=(const SequencerFeatureModule&) = delete;

    void resetEncoderSync();
    void syncEncodersNow();

private:
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
