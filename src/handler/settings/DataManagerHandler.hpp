#pragma once

#include <array>
#include <cstddef>

#include <lvgl.h>

#include <oc/api/ButtonAPI.hpp>
#include <oc/api/EncoderAPI.hpp>
#include <oc/context/OverlayManager.hpp>

#include "state/DataManagerCatalog.hpp"
#include "ui/OverlayTypes.hpp"
#include "ui/ViewTypes.hpp"

namespace core::state {
struct CoreState;
}

namespace core::handler {

class DataManagerHandler {
public:
    static constexpr std::size_t VIEW_SCOPE_COUNT = static_cast<std::size_t>(core::ui::ViewType::COUNT);
    using ViewScopes = std::array<lv_obj_t*, VIEW_SCOPE_COUNT>;

    DataManagerHandler(core::state::CoreState& state,
                       oc::context::OverlayManager<core::ui::OverlayType>& overlays,
                       oc::api::EncoderAPI& encoders,
                       oc::api::ButtonAPI& buttons,
                       ViewScopes viewScopes,
                       lv_obj_t* managerOverlayScope,
                       lv_obj_t* dialogOverlayScope);

    ~DataManagerHandler() = default;

    DataManagerHandler(const DataManagerHandler&) = delete;
    DataManagerHandler& operator=(const DataManagerHandler&) = delete;

private:
    void setupBindings();

    void openManager();
    void closeManager();
    void moveFocus(float delta);

    void openShortcutAssignmentDialog_();
    void openCommandPaletteDialog_();
    void runShortcut_(bool leftButton);

    void navigateDialog_(float delta);
    void applyDialogSelection_();
    void applyShortcutAssignmentSelection_();
    void applyCommandPaletteSelection_();
    void applySlotPickerSelection_();
    void applySetLoadModeSelection_();
    void applyConfirmSelection_();
    void closeDialog_();
    void showDialog_(core::state::DataManagerDialogMode mode, int selectedIndex, uint8_t editingShortcutRow = 0);
    int dialogChoiceCount_(core::state::DataManagerDialogMode mode) const;

    void startCommandFlow_(core::state::DataManagerCommand command);
    void openSlotPickerForPendingCommand_();
    void openSetLoadModeDialog_();
    void openConfirmDialog_();
    void executePendingCommand_();

    core::state::DataManagerContext contextForActiveView_() const;

    void setFeedback_(const char* message);

    core::state::CoreState& state_;
    oc::context::OverlayManager<core::ui::OverlayType>& overlays_;
    oc::api::EncoderAPI& encoders_;
    oc::api::ButtonAPI& buttons_;
    ViewScopes view_scopes_{};
    lv_obj_t* manager_overlay_scope_ = nullptr;
    lv_obj_t* dialog_overlay_scope_ = nullptr;

    bool ignore_open_release_ = false;

    static constexpr uint32_t OPEN_LONG_PRESS_MS = 2000;
};

}  // namespace core::handler
