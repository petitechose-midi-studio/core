#pragma once

#include <memory>

#include <lvgl.h>

#include <oc/api/ButtonAPI.hpp>
#include <oc/api/EncoderAPI.hpp>
#include <oc/context/OverlayManager.hpp>

#include "state/CoreState.hpp"
#include "ui/OverlayTypes.hpp"

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
class SequencerStepEditHandler;
class SequencerStepHandler;
}  // namespace core::handler

namespace core::context::standalone {

class SequencerFeatureModule {
public:
    SequencerFeatureModule(core::state::CoreState& state,
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
    std::unique_ptr<core::context::standalone::SequencerEncoderSyncCoordinator> encoder_sync_;
    std::unique_ptr<ms::ui::VirtualListKeyValueOverlay> step_edit_overlay_;
    std::unique_ptr<core::context::standalone::SequencerOverlayPresenter> presenter_;
    std::unique_ptr<core::handler::SequencerStepHandler> step_handler_;
    std::unique_ptr<core::handler::SequencerPatternQuickControlsHandler> quick_controls_handler_;
    std::unique_ptr<core::handler::SequencerStepEditHandler> step_edit_handler_;
    std::unique_ptr<core::handler::SequencerPropertySelectorHandler> property_selector_handler_;
    std::unique_ptr<core::handler::SequencerMacroPropertyHandler> macro_property_handler_;
};

}  // namespace core::context::standalone
