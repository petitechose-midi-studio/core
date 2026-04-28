#pragma once

#include <array>
#include <cstddef>

#include <oc/api/ButtonAPI.hpp>
#include <oc/api/EncoderAPI.hpp>
#include <oc/context/OverlayManager.hpp>
#include <oc/state/Signal.hpp>

#include "handler/settings/DataManagerDomainServices.hpp"
#include "state/DataManagerCatalog.hpp"
#include "state/DataManagerState.hpp"
#include "app/OverlayTypes.hpp"
#include "app/ViewTypes.hpp"

namespace core::state {
struct DataManagerCommandExecutionResult;
}

namespace core::handler {

/**
 * Binds Data Manager modal flow to buttons and encoders.
 *
 * The handler owns opening, dialog navigation, confirmation, slot picking, and
 * feedback. Persistence and command execution are delegated to
 * DataManagerDomainServices.
 */
class DataManagerHandler {
public:
    static constexpr std::size_t VIEW_SCOPE_COUNT = static_cast<std::size_t>(core::ui::ViewType::COUNT);
    using ViewScopes = std::array<oc::type::ScopeID, VIEW_SCOPE_COUNT>;
    struct StateRefs {
        core::state::DataManagerState& dataManager;
        oc::state::Signal<core::ui::ViewType, 8>& activeView;
    };

    DataManagerHandler(StateRefs state,
                       DataManagerDomainServices services,
                       oc::context::OverlayManager<core::ui::OverlayType>& overlays,
                       oc::api::EncoderAPI& encoders,
                       oc::api::ButtonAPI& buttons,
                       ViewScopes viewScopes,
                       oc::type::ScopeID managerOverlayScope,
                       oc::type::ScopeID dialogOverlayScope);

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

    core::state::DataManagerState& data_manager_;
    oc::state::Signal<core::ui::ViewType, 8>& active_view_;
    DataManagerDomainServices services_;
    oc::context::OverlayManager<core::ui::OverlayType>& overlays_;
    oc::api::EncoderAPI& encoders_;
    oc::api::ButtonAPI& buttons_;
    ViewScopes view_scopes_{};
    oc::type::ScopeID manager_overlay_scope_ = 0;
    oc::type::ScopeID dialog_overlay_scope_ = 0;

    bool ignore_open_release_ = false;

    static constexpr uint32_t OPEN_LONG_PRESS_MS = 2000;
};

}  // namespace core::handler
