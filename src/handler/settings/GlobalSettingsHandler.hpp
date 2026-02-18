#pragma once

#include <lvgl.h>

#include <oc/api/ButtonAPI.hpp>
#include <oc/api/EncoderAPI.hpp>
#include <oc/context/OverlayManager.hpp>

#include "state/CoreState.hpp"
#include "ui/OverlayTypes.hpp"

namespace core::handler {

class GlobalSettingsHandler {
public:
    GlobalSettingsHandler(core::state::CoreState& state,
                          oc::context::OverlayManager<core::ui::OverlayType>& overlays,
                          oc::api::EncoderAPI& encoders,
                          oc::api::ButtonAPI& buttons,
                          lv_obj_t* settingsOverlayScope,
                          lv_obj_t* selectorOverlayScope);

    ~GlobalSettingsHandler() = default;

    GlobalSettingsHandler(const GlobalSettingsHandler&) = delete;
    GlobalSettingsHandler& operator=(const GlobalSettingsHandler&) = delete;

private:
    void setupBindings();

    void openSettings();
    void closeSettings();

    void moveFocus(float delta);
    void openValueSelector();

    void navigateSelector(float delta);
    void applySelectorAndClose();
    void closeSelectorCancel();

    int currentChoiceIndexForRow_(uint8_t row) const;
    void applyChoiceForRow_(uint8_t row, int choiceIndex);
    void persistRow_(uint8_t row);

    core::state::CoreState& state_;
    oc::context::OverlayManager<core::ui::OverlayType>& overlays_;
    oc::api::EncoderAPI& encoders_;
    oc::api::ButtonAPI& buttons_;
    lv_obj_t* settings_overlay_scope_ = nullptr;
    lv_obj_t* selector_overlay_scope_ = nullptr;

    bool ignore_open_release_ = false;

    static constexpr uint8_t ROW_COUNT = 4;
    static constexpr uint32_t SETTINGS_LONG_PRESS_MS = 2000;
};

}  // namespace core::handler
