#include "DataManagerHandler.hpp"

#include <algorithm>

#include <oc/log/Log.hpp>
#include <oc/ui/lvgl/Scope.hpp>
#include <oc/util/Index.hpp>

#include <config/InputIDs.hpp>

namespace core::handler {

using oc::ui::lvgl::scope;
using oc::util::wrapIndex;
DataManagerHandler::DataManagerHandler(core::state::CoreState& state,
                                       oc::context::OverlayManager<core::ui::OverlayType>& overlays,
                                       oc::api::EncoderAPI& encoders,
                                       oc::api::ButtonAPI& buttons,
                                       DataManagerHandler::ViewScopes viewScopes,
                                       lv_obj_t* managerOverlayScope,
                                       lv_obj_t* setLoadSelectorScope)
    : state_(state)
    , overlays_(overlays)
    , encoders_(encoders)
    , buttons_(buttons)
    , view_scopes_(viewScopes)
    , manager_overlay_scope_(managerOverlayScope)
    , set_load_selector_scope_(setLoadSelectorScope) {
    setupBindings();
}

void DataManagerHandler::setupBindings() {
    const auto navButton = static_cast<oc::type::ButtonID>(Config::ButtonID::NAV);
    const auto leftTopButton = static_cast<oc::type::ButtonID>(Config::ButtonID::LEFT_TOP);
    const auto navEncoder = static_cast<oc::type::EncoderID>(Config::EncoderID::NAV);
    const auto optEncoder = static_cast<oc::type::EncoderID>(Config::EncoderID::OPT);

    lv_obj_t* lastBoundScope = nullptr;
    for (auto* viewScope : view_scopes_) {
        if (!viewScope || viewScope == lastBoundScope) continue;

        buttons_.button(navButton)
            .longPress(OPEN_LONG_PRESS_MS)
            .scope(scope(viewScope))
            .when([this]() {
                return overlays_.current() == core::ui::OverlayType::NONE;
            })
            .then([this]() { openManager(); });

        lastBoundScope = viewScope;
    }

    encoders_.encoder(navEncoder)
        .turn()
        .scope(scope(manager_overlay_scope_))
        .then([this](float delta) { moveFocus(delta); });

    encoders_.encoder(optEncoder)
        .turn()
        .scope(scope(manager_overlay_scope_))
        .then([this](float normalized) { editFocusedValue(normalized); });

    buttons_.button(navButton)
        .release()
        .scope(scope(manager_overlay_scope_))
        .then([this]() { executeFocusedAction(); });

    buttons_.button(leftTopButton)
        .release()
        .scope(scope(manager_overlay_scope_))
        .then([this]() { closeManager(); });

    encoders_.encoder(navEncoder)
        .turn()
        .scope(scope(set_load_selector_scope_))
        .then([this](float delta) { navigateSetLoadModeSelector_(delta); });

    buttons_.button(navButton)
        .release()
        .scope(scope(set_load_selector_scope_))
        .then([this]() { applySetLoadModeAndExecute_(); });

    buttons_.button(leftTopButton)
        .release()
        .scope(scope(set_load_selector_scope_))
        .then([this]() { closeSetLoadModeSelector_(); });

    OC_LOG_DEBUG("[DataManagerHandler] Bindings setup complete");
}

void DataManagerHandler::openManager() {
    auto& dm = state_.dataManager;
    dm.reset();
    clampSlotToDomain_();

    ignore_open_release_ = true;
    overlays_.show(core::ui::OverlayType::DATA_MANAGER, false);
}

void DataManagerHandler::closeManager() {
    if (ignore_open_release_) {
        ignore_open_release_ = false;
        return;
    }

    while (true) {
        const auto current = overlays_.current();
        if (current == core::ui::OverlayType::DATA_MANAGER_SET_LOAD_MODE_SELECTOR) {
            overlays_.hide();
            continue;
        }
        break;
    }

    if (overlays_.current() == core::ui::OverlayType::DATA_MANAGER) {
        overlays_.hide();
    }

    state_.dataManager.reset();
}

void DataManagerHandler::moveFocus(float delta) {
    if (delta == 0.0f) return;
    const int step = (delta > 0.0f) ? 1 : -1;

    auto& dm = state_.dataManager;
    const int count = static_cast<int>(dm.rowCount());
    const int current = static_cast<int>(dm.focusedRow.get());
    const int next = wrapIndex(current + step, count);
    dm.focusedRow.set(static_cast<uint8_t>(next));
}

void DataManagerHandler::editFocusedValue(float normalized) {
    auto& dm = state_.dataManager;
    const uint8_t row = dm.focusedRow.get();
    const float clamped = std::clamp(normalized, 0.0f, 1.0f);

    if (row == 0) {
        const int idx = static_cast<int>(clamped * 2.0f + 0.5f);
        const auto domain = static_cast<core::state::DataManagerDomain>(std::clamp(idx, 0, 2));
        dm.domain.set(domain);
        clampSlotToDomain_();
        if (!dm.isSetLoadModeRowVisible() && dm.focusedRow.get() > 2U) {
            dm.focusedRow.set(2);
        }
        return;
    }

    if (row == 1) {
        const int idx = static_cast<int>(clamped * 2.0f + 0.5f);
        dm.action.set(static_cast<core::state::DataManagerAction>(std::clamp(idx, 0, 2)));
        if (!dm.isSetLoadModeRowVisible() && dm.focusedRow.get() > 2U) {
            dm.focusedRow.set(2);
        }
        return;
    }

    if (row == 2) {
        const uint8_t count = slotCountForDomain_(dm.domain.get());
        if (count == 0U) {
            dm.slotIndex.set(0);
            return;
        }
        const int idx = static_cast<int>(clamped * static_cast<float>(count - 1U) + 0.5f);
        dm.slotIndex.set(static_cast<uint8_t>(std::clamp(idx, 0, static_cast<int>(count - 1U))));
        return;
    }

    if (row == 3 && dm.isSetLoadModeRowVisible()) {
        const int idx = (clamped >= 0.5f) ? 1 : 0;
        dm.setLoadMode.set((idx == 0) ? core::state::DataManagerSetLoadMode::REPLACE
                                       : core::state::DataManagerSetLoadMode::MERGE);
    }
}

void DataManagerHandler::executeFocusedAction() {
    if (ignore_open_release_) {
        ignore_open_release_ = false;
        return;
    }

    auto& dm = state_.dataManager;
    const auto domain = dm.domain.get();
    const auto action = dm.action.get();
    const uint8_t slot = dm.slotIndex.get();

    if (domain == core::state::DataManagerDomain::SEQ_SET_LIBRARY &&
        action == core::state::DataManagerAction::LOAD &&
        !dm.setLoadSelector.visible.get()) {
        openSetLoadModeSelector_();
        return;
    }

    switch (domain) {
        case core::state::DataManagerDomain::MACRO_LIBRARY:
            switch (action) {
                case core::state::DataManagerAction::SAVE:
                    state_.saveMacroLibrarySlot(slot);
                    break;
                case core::state::DataManagerAction::LOAD:
                    state_.loadMacroLibrarySlot(slot);
                    break;
                case core::state::DataManagerAction::ERASE:
                    state_.eraseMacroLibrarySlot(slot);
                    break;
            }
            break;

        case core::state::DataManagerDomain::SEQ_PATTERN_LIBRARY:
            switch (action) {
                case core::state::DataManagerAction::SAVE:
                    state_.saveSequencerPatternSlot(slot);
                    break;
                case core::state::DataManagerAction::LOAD:
                    state_.loadSequencerPatternSlot(slot);
                    break;
                case core::state::DataManagerAction::ERASE:
                    state_.eraseSequencerPatternSlot(slot);
                    break;
            }
            break;

        case core::state::DataManagerDomain::SEQ_SET_LIBRARY:
            switch (action) {
                case core::state::DataManagerAction::SAVE:
                    state_.saveSequencerSetSlot(slot);
                    break;
                case core::state::DataManagerAction::LOAD: {
                    const bool merge = dm.setLoadMode.get() == core::state::DataManagerSetLoadMode::MERGE;
                    state_.loadSequencerSetSlot(slot, merge);
                    break;
                }
                case core::state::DataManagerAction::ERASE:
                    state_.eraseSequencerSetSlot(slot);
                    break;
            }
            break;
    }
}

void DataManagerHandler::openSetLoadModeSelector_() {
    auto& selector = state_.dataManager.setLoadSelector;
    selector.reset();
    selector.selectedIndex.set(
        state_.dataManager.setLoadMode.get() == core::state::DataManagerSetLoadMode::REPLACE ? 0 : 1
    );
    overlays_.show(core::ui::OverlayType::DATA_MANAGER_SET_LOAD_MODE_SELECTOR, true);
}

void DataManagerHandler::navigateSetLoadModeSelector_(float delta) {
    if (delta == 0.0f) return;

    auto& selector = state_.dataManager.setLoadSelector;
    const int step = (delta > 0.0f) ? 1 : -1;
    const int next = wrapIndex(selector.selectedIndex.get() + step, 2);
    selector.selectedIndex.set(next);
}

void DataManagerHandler::applySetLoadModeAndExecute_() {
    auto& dm = state_.dataManager;
    auto& selector = dm.setLoadSelector;

    const int choice = std::clamp(selector.selectedIndex.get(), 0, 1);
    dm.setLoadMode.set((choice == 0) ? core::state::DataManagerSetLoadMode::REPLACE
                                     : core::state::DataManagerSetLoadMode::MERGE);

    overlays_.hide();
    selector.reset();

    executeFocusedAction();
}

void DataManagerHandler::closeSetLoadModeSelector_() {
    overlays_.hide();
    state_.dataManager.setLoadSelector.reset();
}

uint8_t DataManagerHandler::slotCountForDomain_(core::state::DataManagerDomain domain) const {
    switch (domain) {
        case core::state::DataManagerDomain::MACRO_LIBRARY:
            return core::persistence::MacroPersistence::LIBRARY_SLOT_COUNT;
        case core::state::DataManagerDomain::SEQ_PATTERN_LIBRARY:
            return core::persistence::SequencerPersistence::PATTERN_LIBRARY_SLOT_COUNT;
        case core::state::DataManagerDomain::SEQ_SET_LIBRARY:
            return core::persistence::SequencerPersistence::SET_LIBRARY_SLOT_COUNT;
    }

    return 0;
}

void DataManagerHandler::clampSlotToDomain_() {
    auto& dm = state_.dataManager;
    const uint8_t count = slotCountForDomain_(dm.domain.get());
    if (count == 0U) {
        dm.slotIndex.set(0);
        return;
    }

    const uint8_t maxIndex = static_cast<uint8_t>(count - 1U);
    if (dm.slotIndex.get() > maxIndex) {
        dm.slotIndex.set(maxIndex);
    }
}

}  // namespace core::handler
