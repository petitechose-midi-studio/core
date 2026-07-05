#pragma once

#include <cstdint>

#include <oc/api/ButtonAPI.hpp>
#include <oc/api/EncoderAPI.hpp>
#include <oc/context/OverlayManager.hpp>

#include "app/OverlayTypes.hpp"
#include "handler/macro/MacroEditDomainServices.hpp"
#include "state/MacroEditState.hpp"

namespace core::handler {

class MacroAutomationHandler {
public:
    struct StateRefs {
        core::state::MacroEditState& macroEdit;
    };

    MacroAutomationHandler(
        StateRefs state,
        MacroEditDomainServices services,
        oc::context::OverlayManager<core::ui::OverlayType>& overlays,
        oc::api::EncoderAPI& encoders,
        oc::api::ButtonAPI& buttons,
        oc::type::ScopeID automationScope
    );

    MacroAutomationHandler(const MacroAutomationHandler&) = delete;
    MacroAutomationHandler& operator=(const MacroAutomationHandler&) = delete;

private:
    void setupBindings();

    bool active() const;
    uint8_t macroIndex() const;
    void moveFocus(float delta);
    void editFocusedValue(float normalized);
    void backToMacroEdit();
    void clearAutomation();
    void removeAutomation();
    void copyAutomation();
    void pasteAutomation();

    core::state::MacroEditState& macro_edit_;
    MacroEditDomainServices services_;
    oc::context::OverlayManager<core::ui::OverlayType>& overlays_;
    oc::api::EncoderAPI& encoders_;
    oc::api::ButtonAPI& buttons_;
    oc::type::ScopeID automation_scope_ = 0;
    bool ignore_next_bottom_left_release_ = false;
    bool ignore_next_bottom_right_release_ = false;
};

}  // namespace core::handler
