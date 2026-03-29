#pragma once

#include <array>
#include <cstddef>

#include <oc/api/ButtonAPI.hpp>
#include <oc/api/EncoderAPI.hpp>
#include <oc/context/OverlayManager.hpp>

#include "state/DataManagerCatalog.hpp"
#include "state/DataManagerState.hpp"
#include "ui/OverlayTypes.hpp"
#include "ui/ViewTypes.hpp"

namespace core::state {
struct CoreState;
struct DataManagerCommandExecutionResult;
}

namespace core::handler {

class DataManagerHandler {
public:
    static constexpr std::size_t VIEW_SCOPE_COUNT = static_cast<std::size_t>(core::ui::ViewType::COUNT);
    using ViewScopes = std::array<oc::type::ScopeID, VIEW_SCOPE_COUNT>;
    struct StateRefs {
        core::state::DataManagerState& dataManager;
        oc::state::Signal<core::ui::ViewType>& activeView;
    };
    class Services {
    public:
        explicit Services(core::state::CoreState& state);

        uint8_t slotCount(core::state::DataManagerCommand command) const;
        bool slotOccupied(core::state::DataManagerCommand command, uint8_t slotIndex) const;
        core::state::DataManagerCommandExecutionResult execute(
            core::state::DataManagerCommand command,
            uint8_t slotIndex,
            core::state::DataManagerSetLoadMode setLoadMode
        ) const;
        void setShortcut(core::state::DataManagerContext context,
                         bool leftButton,
                         core::state::DataManagerCommand command) const;

    private:
        core::state::CoreState* state_ = nullptr;
    };

    DataManagerHandler(StateRefs state,
                       Services services,
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
    oc::state::Signal<core::ui::ViewType>& active_view_;
    Services services_;
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
