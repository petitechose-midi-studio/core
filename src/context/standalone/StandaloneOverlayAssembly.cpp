#include "context/standalone/StandaloneOverlayAssembly.hpp"

#include <cstdio>

#include <config/PlatformCompat.hpp>
#include <oc/api/ButtonAPI.hpp>
#include <oc/context/OverlayManager.hpp>

#include <config/App.hpp>
#include <oc/ui/lvgl/Scope.hpp>
#include "context/standalone/OverlayPresentationRegistry.hpp"
#include "state/CoreState.hpp"
#include "state/ViewSelectorItems.hpp"

namespace core::context::standalone {

FLASHMEM StandaloneOverlayAssembly::StandaloneOverlayAssembly(
    core::state::CoreState& state,
    oc::api::ButtonAPI& buttons,
    lv_obj_t* overlayRoot,
    ActiveViewScopeProvider activeViewScopeProvider
)
    : core_state_(state) {
    presentation_registry_ =
        core::app::makeExtmemUnique<OverlayPresentationRegistry>(overlayRoot);
    if (!presentation_registry_ || !presentation_registry_->valid()) return;
    if (!createOverlayController(buttons, std::move(activeViewScopeProvider))) return;
    if (!createViewSelectorOverlay(overlayRoot)) return;
    valid_ = true;
}

FLASHMEM StandaloneOverlayAssembly::~StandaloneOverlayAssembly() = default;

FLASHMEM oc::context::OverlayManager<core::ui::OverlayType>&
StandaloneOverlayAssembly::controller() const {
    return *overlay_controller_;
}

FLASHMEM OverlayPresentationRegistry&
StandaloneOverlayAssembly::presentationRegistry() const {
    return *presentation_registry_;
}

FLASHMEM lv_obj_t* StandaloneOverlayAssembly::viewSelectorElement() const {
    return view_selector_ ? view_selector_->getElement() : nullptr;
}

FLASHMEM oc::type::ScopeID StandaloneOverlayAssembly::viewSelectorScope() const {
    return view_selector_scope_;
}

FLASHMEM void StandaloneOverlayAssembly::renderViewSelector(int selectedIndex, bool visible) {
    if (!valid_ || !view_selector_) return;
    if (!visible) {
        view_selector_->hide();
        return;
    }

    const auto* undoEntry = core_state_.projectHistory.peekUndo();
    const auto* redoEntry = core_state_.projectHistory.peekRedo();
    char undoLabel[48]{};
    char redoLabel[48]{};
    std::snprintf(
        undoLabel,
        sizeof(undoLabel),
        "C  Undo %s",
        undoEntry
            ? core::state::project::ProjectHistoryCoordinator::actionLabel(*undoEntry)
            : "-"
    );
    std::snprintf(
        redoLabel,
        sizeof(redoLabel),
        "B  Redo %s",
        redoEntry
            ? core::state::project::ProjectHistoryCoordinator::actionLabel(*redoEntry)
            : "-"
    );

    const uint32_t historyRevision = core_state_.projectHistory.revision.get();
    const bool wasVisible = !lv_obj_has_flag(
        view_selector_->getElement(),
        LV_OBJ_FLAG_HIDDEN
    );
    if (wasVisible && historyRevision != view_selector_history_revision_) {
        // A history action only changes the header. Re-show once so LVGL
        // redraws the complete transparent overlay instead of leaving the
        // unchanged list outside a partial refresh region.
        view_selector_->hide();
    }
    view_selector_->show();
    view_selector_history_revision_ = historyRevision;
    view_selector_->render({
        .title = undoLabel,
        .meta = redoLabel,
        .rows = view_selector_rows_.data(),
        .rowCount = core::state::VIEW_SELECTOR_ITEM_COUNT,
        .selectedIndex = selectedIndex,
        .dataRevision = 1,
        .headerLayout = ms::ui::MenuListHeaderLayout::Stacked,
    });
}

FLASHMEM bool StandaloneOverlayAssembly::createOverlayController(
    oc::api::ButtonAPI& buttons,
    ActiveViewScopeProvider activeViewScopeProvider
) {
    overlay_controller_ = core::app::makeExtmemUnique<oc::context::OverlayManager<core::ui::OverlayType>>(
        core_state_.overlays,
        buttons
    );
    if (!overlay_controller_) return false;
    overlay_controller_->setPresentationCallback(
        presentation_registry_.get(),
        &OverlayPresentationRegistry::onPresentationChanged
    );
    overlay_controller_->setActiveViewProvider(std::move(activeViewScopeProvider));
    return true;
}

FLASHMEM bool StandaloneOverlayAssembly::createViewSelectorOverlay(lv_obj_t* overlayRoot) {
    view_selector_ = core::app::makeExtmemUnique<ms::ui::MenuListView>(overlayRoot);
    if (!view_selector_ || !view_selector_->getElement()) return false;
    view_selector_->hide();
    for (int i = 0;
         i < core::state::VIEW_SELECTOR_ITEM_COUNT &&
         i < static_cast<int>(view_selector_rows_.size());
         ++i) {
        const auto item = core::state::viewSelectorItemAt(i);
        view_selector_rows_[static_cast<std::size_t>(i)] = ms::ui::MenuRow{
            .label = core::state::viewSelectorItemLabel(item),
            .value = core::state::viewSelectorItemDescription(item),
            .kind = ms::ui::MenuRowKind::Folder,
            .enabled = true,
            .valueAutoScroll = true,
            .valueRole = ms::ui::MenuRowValueRole::Description,
        };
    }
    view_selector_scope_ = oc::ui::lvgl::scopeID(view_selector_->getElement());
    return registerOverlaySurface(
        *overlay_controller_,
        *presentation_registry_,
        core::ui::OverlayType::VIEW_SELECTOR,
        view_selector_->getElement(),
        static_cast<oc::type::ButtonID>(Config::ButtonID::LEFT_TOP)
    );
}

}  // namespace core::context::standalone
